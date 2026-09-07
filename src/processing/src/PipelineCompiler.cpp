#include <lumora/processing/PipelineCompiler.hpp>
#include <algorithm>
#include <array>
#include <cmath>
namespace lumora::processing {
std::string_view stageName(StageId id) noexcept {
    switch (id) {
    case StageId::Normalize: return "Sensor normalization";
    case StageId::WindowLevel: return "Window and level";
    case StageId::BrightnessContrast: return "Brightness and contrast";
    case StageId::Gamma: return "Gamma";
    case StageId::Clahe: return "Local contrast";
    case StageId::Denoise: return "Denoise";
    case StageId::Sharpen: return "Sharpen";
    case StageId::Invert: return "Invert";
    }
    return "Unknown stage";
}

PipelineDefinition defaultPipeline() {
    return {1, 1, 0, {
        {StageId::Normalize, true, NormalizationParameters{}},
        {StageId::WindowLevel, true, WindowLevelParameters{}},
        {StageId::BrightnessContrast, false, BrightnessContrastParameters{}},
        {StageId::Gamma, false, GammaParameters{}},
        {StageId::Clahe, false, ClaheParameters{}},
        {StageId::Denoise, false, DenoiseParameters{}},
        {StageId::Sharpen, false, SharpenParameters{}},
        {StageId::Invert, false, InvertParameters{}}}};
}
std::vector<StageTraits> stageRegistry() {
    std::vector<StageTraits> registry;
    for (int i = 0; i < 8; ++i) registry.push_back({static_cast<StageId>(i),
        i == 0 ? ImageDomain::SensorNative : ImageDomain::CanonicalU16, ImageDomain::CanonicalU16});
    return registry;
}
namespace {
using Violations = std::vector<PipelineViolation>;
void add(Violations& errors, std::optional<std::size_t> index, std::string code, std::string detail) {
    errors.push_back({index, std::move(code), std::move(detail)});
}
std::optional<std::size_t> stagePosition(StageId id) {
    switch (id) {
    case StageId::Normalize: return 0;
    case StageId::WindowLevel: return 1;
    case StageId::BrightnessContrast: return 2;
    case StageId::Gamma: return 3;
    case StageId::Clahe: return 4;
    case StageId::Denoise: return 5;
    case StageId::Sharpen: return 6;
    case StageId::Invert: return 7;
    }
    return std::nullopt;
}
void validateParameters(const StageParameters& parameters, std::size_t index, Violations& errors) {
    const auto bound = [&](double value, double low, double high, const char* name) {
        if (!std::isfinite(value) || value < low || value > high) {
            add(errors, index, std::string("invalid_") + name,
                std::string(name) + " must be finite and within [" + std::to_string(low) + ", " + std::to_string(high) + "].");
        }
    };
    std::visit([&](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, WindowLevelParameters>) {
            bound(p.window, 1, 65535, "window"); bound(p.level, 0, 65535, "level");
        } else if constexpr (std::is_same_v<T, BrightnessContrastParameters>) {
            bound(p.brightness, -1, 1, "brightness"); bound(p.contrast, 0, 4, "contrast");
        } else if constexpr (std::is_same_v<T, GammaParameters>) {
            bound(p.gamma, 0.1, 5, "gamma");
        } else if constexpr (std::is_same_v<T, ClaheParameters>) {
            bound(p.clipLimit, 0.1, 40, "clip_limit");
            bound(p.tileGridSize, 2, 32, "tile_grid_size");
        } else if constexpr (std::is_same_v<T, DenoiseParameters>) {
            if (p.mode != DenoiseMode::Gaussian && p.mode != DenoiseMode::Median)
                add(errors, index, "invalid_denoise_mode", "Denoise mode must be Gaussian or Median.");
            if (p.kernelSize != 3 && p.kernelSize != 5 && p.kernelSize != 7)
                add(errors, index, "invalid_kernel_size", "Denoise kernel must be 3, 5, or 7.");
        } else if constexpr (std::is_same_v<T, SharpenParameters>) {
            bound(p.amount, 0, 5, "amount"); bound(p.radius, 0.5, 5, "radius"); bound(p.threshold, 0, 65535, "threshold");
        }
    }, parameters);
}
bool knownDomain(ImageDomain domain) {
    return domain == ImageDomain::SensorNative || domain == ImageDomain::CanonicalU16;
}
} // namespace

core::Result<CompiledPipeline, PipelineValidationError> PipelineCompiler::compile(
    const PipelineDefinition& definition) const {
    using CompileResult = core::Result<CompiledPipeline, PipelineValidationError>;
    Violations errors;
    if (definition.schemaVersion != 1) add(errors, {}, "unsupported_schema_version", "Only schema version 1 is supported.");
    if (definition.orderVersion != 1) add(errors, {}, "unsupported_order_version", "Only order version 1 is supported.");
    if (std::none_of(definition.stages.begin(), definition.stages.end(), [](const auto& s) { return s.id == StageId::Normalize; }))
        add(errors, {}, "missing_normalization", "Normalization is mandatory.");
    for (const auto& traits : registry_) {
        if (!stagePosition(traits.id)) add(errors, {}, "unknown_registry_stage", "Registry contains an unknown stage ID.");
    }
    std::array<bool, 8> seen{};
    std::optional<std::size_t> previousPosition;
    ImageDomain currentDomain = ImageDomain::SensorNative;
    std::vector<CompiledStage> stages;
    std::optional<std::size_t> lastEnabledIndex;
    for (std::size_t i = 0; i < definition.stages.size(); ++i) {
        const auto& stage = definition.stages[i];
        const auto position = stagePosition(stage.id);
        if (!position) {
            add(errors, i, "unknown_stage", "The stage ID is not supported.");
            validateParameters(stage.parameters, i, errors);
            continue;
        }
        if (seen[*position]) add(errors, i, stage.id == StageId::Normalize ? "duplicate_normalization" : "duplicate_stage", "Stage IDs must be unique.");
        seen[*position] = true;
        if (previousPosition && *position < *previousPosition) add(errors, i, "invalid_stage_order", "Stages must follow canonical order.");
        previousPosition = position;
        if (stage.id == StageId::Normalize) {
            if (i != 0) add(errors, i, "normalization_not_first", "Normalization must be first.");
            if (!stage.enabled) add(errors, i, "normalization_disabled", "Normalization must be enabled.");
        }
        if (stage.parameters.index() != *position) add(errors, i, "parameter_type_mismatch", "Parameter variant does not match the stage ID.");
        validateParameters(stage.parameters, i, errors);
        const auto count = std::count_if(registry_.begin(), registry_.end(), [&](const auto& t) { return t.id == stage.id; });
        if (count != 1) {
            add(errors, i, count == 0 ? "missing_stage_traits" : "duplicate_stage_traits", "Each defined stage requires exactly one registry entry.");
            continue;
        }
        const auto& traits = *std::find_if(registry_.begin(), registry_.end(), [&](const auto& t) { return t.id == stage.id; });
        if (!knownDomain(traits.inputDomain) || !knownDomain(traits.outputDomain))
            add(errors, i, "unknown_image_domain", "Stage traits contain an unknown image domain.");
        if (traits.backend != ExecutionBackend::Cpu) add(errors, i, "unsupported_backend", "Only CPU execution is supported.");
        if (stage.enabled) {
            if (traits.inputDomain != currentDomain) add(errors, i, "image_domain_mismatch", "Enabled stage input does not match the preceding output domain.");
            currentDomain = traits.outputDomain;
            stages.push_back({stage, traits});
            lastEnabledIndex = i;
        }
    }
    if (!stages.empty() && currentDomain != ImageDomain::CanonicalU16)
        add(errors, lastEnabledIndex, "invalid_output_domain", "Pipeline output must be canonical U16.");
    // Pipeline-wide errors precede per-stage errors. Preserve detection order
    // within each stage, including output validation discovered after the loop.
    std::stable_sort(errors.begin(), errors.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.stageIndex < rhs.stageIndex;
    });
    if (!errors.empty()) return CompileResult::failure({errors.front().code, std::move(errors)});
    return CompileResult::success(CompiledPipeline(definition, std::move(stages)));
}
} // namespace lumora::processing
