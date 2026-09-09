#include <lumora/application/PresetRepository.hpp>

#include <lumora/processing/PipelineCompiler.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace lumora::application {
namespace {

constexpr std::array<std::string_view, 5> reservedIds{
    "original", "standard", "high-contrast", "soft-detail", "custom"};
constexpr std::array<std::string_view, 4> namedRecipeIds{
    "original", "standard", "high-contrast", "soft-detail"};

[[nodiscard]] core::Error presetError(std::string code, std::string detail) {
    return {core::ErrorCategory::Configuration, std::move(code),
        "Preset operation failed.", std::move(detail), false};
}

[[nodiscard]] core::Result<void> failure(std::string code, std::string detail) {
    return core::Result<void>::failure(
        presetError(std::move(code), std::move(detail)));
}

[[nodiscard]] bool isBlank(std::string_view value) {
    return value.empty() || std::all_of(value.begin(), value.end(), [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] bool isReserved(const PresetId& id) {
    return std::find(reservedIds.begin(), reservedIds.end(), id.value)
        != reservedIds.end();
}

[[nodiscard]] bool samePipeline(const processing::PipelineDefinition& left,
    const processing::PipelineDefinition& right) noexcept {
    return processing::semanticallyEqualPipelineDefinitions(left, right);
}

[[nodiscard]] const Preset* findPreset(
    const std::vector<Preset>& presets, const PresetId& id) {
    const auto found = std::find_if(presets.begin(), presets.end(), [&](const Preset& preset) {
        return preset.id == id;
    });
    return found == presets.end() ? nullptr : &*found;
}

[[nodiscard]] core::Result<void> validatePresetEnvelope(const Preset& preset) {
    if (isBlank(preset.id.value)) {
        return failure("preset_id_empty", "Preset IDs must contain a non-whitespace character.");
    }
    if (isBlank(preset.name)) {
        return failure("preset_name_empty", "Preset names must contain a non-whitespace character.");
    }
    if (preset.schemaVersion != 1U) {
        return failure("preset_schema_version_unsupported",
            "Only preset schema version 1 is supported.");
    }
    if (preset.revision == 0U) {
        return failure("preset_revision_invalid", "Preset revisions must be positive.");
    }
    return validatePresetPipeline(preset.pipeline);
}

[[nodiscard]] core::Result<void> validateShipped(
    const std::vector<Preset>& shipped) {
    if (shipped.size() != reservedIds.size()) {
        return failure("shipped_preset_set_invalid",
            "The shipped set must contain exactly the five reserved preset IDs.");
    }
    for (const auto& preset : shipped) {
        const auto envelope = validatePresetEnvelope(preset);
        if (!envelope.hasValue()) return envelope;
    }
    for (const auto id : reservedIds) {
        const auto count = std::count_if(
            shipped.begin(), shipped.end(), [&](const Preset& preset) {
                return preset.id.value == id;
            });
        if (count != 1) {
            return failure("shipped_preset_set_invalid",
                "Every reserved preset ID must occur exactly once.");
        }
    }

    const auto* original = findPreset(shipped, PresetId{"original"});
    const auto* standard = findPreset(shipped, PresetId{"standard"});
    const auto* highContrast = findPreset(shipped, PresetId{"high-contrast"});
    const auto* softDetail = findPreset(shipped, PresetId{"soft-detail"});
    const auto* custom = findPreset(shipped, PresetId{"custom"});
    if (!original->builtIn || !standard->builtIn || !highContrast->builtIn
        || !softDetail->builtIn || custom->builtIn) {
        return failure("shipped_preset_flags_invalid",
            "Four named recipes must be built in and Custom must be editable.");
    }
    if (!samePipeline(original->pipeline, processing::defaultPipeline())
        || !samePipeline(standard->pipeline, processing::standardPipeline())
        || !samePipeline(custom->pipeline, processing::defaultPipeline())) {
        return failure("shipped_preset_recipe_invalid",
            "A shipped recipe does not match its approved complete definition.");
    }
    return core::Result<void>::success();
}

[[nodiscard]] const Preset* selectedRecipe(const std::vector<Preset>& shipped,
    const PresetState& state) {
    if (const auto* preset = findPreset(shipped, state.selectedId)) return preset;
    return findPreset(state.customPresets, state.selectedId);
}

[[nodiscard]] core::Result<void> validateState(
    const std::vector<Preset>& shipped, const PresetState& state) {
    const auto active = validatePresetPipeline(state.activePipeline);
    if (!active.hasValue()) return active;

    for (std::size_t index = 0U; index < state.customPresets.size(); ++index) {
        const auto valid = validateCustomPreset(state.customPresets[index]);
        if (!valid.hasValue()) return valid;
        const auto current = state.customPresets.begin()
            + static_cast<std::ptrdiff_t>(index);
        const auto duplicate = std::find_if(
            state.customPresets.begin(), current, [&](const Preset& preset) {
                return preset.id == state.customPresets[index].id;
            });
        if (duplicate != current) {
            return failure("custom_preset_id_duplicate",
                "Saved custom preset IDs must be unique.");
        }
    }

    if (state.selectedId == PresetId{"custom"}) {
        return core::Result<void>::success();
    }
    const auto* selected = selectedRecipe(shipped, state);
    if (selected == nullptr) {
        return failure("selected_preset_not_found",
            "The selected preset ID must identify a shipped or saved recipe.");
    }
    if (!samePipeline(selected->pipeline, state.activePipeline)) {
        return failure("selected_preset_pipeline_mismatch",
            "The active pipeline must match the explicitly selected recipe.");
    }
    return core::Result<void>::success();
}

void swapPipeline(processing::PipelineDefinition& left,
    processing::PipelineDefinition& right) noexcept {
    std::swap(left.version, right.version);
    left.stages.swap(right.stages);
}

void swapState(PresetState& left, PresetState& right) noexcept {
    left.customPresets.swap(right.customPresets);
    left.selectedId.value.swap(right.selectedId.value);
    swapPipeline(left.activePipeline, right.activePipeline);
}

}  // namespace

core::Result<void> validatePresetPipeline(
    const processing::PipelineDefinition& definition) {
    constexpr std::array expectedOrder{
        processing::StageId::Normalize,
        processing::StageId::WindowLevel,
        processing::StageId::BrightnessContrast,
        processing::StageId::Gamma,
        processing::StageId::Clahe,
        processing::StageId::Denoise,
        processing::StageId::Sharpen,
        processing::StageId::Invert,
    };
    if (definition.stages.size() != expectedOrder.size()) {
        return failure("preset_pipeline_incomplete",
            "Preset pipelines must contain all eight canonical stages.");
    }
    for (std::size_t index = 0U; index < expectedOrder.size(); ++index) {
        if (definition.stages[index].id != expectedOrder[index]) {
            return failure("preset_pipeline_order_invalid",
                "Preset pipelines must use the complete canonical stage order.");
        }
    }

    const processing::PipelineCompiler compiler(processing::stageRegistry());
    const auto compiled = compiler.compile(definition);
    if (!compiled.hasValue()) {
        std::string detail = "Pipeline validation failed.";
        for (const auto& violation : compiled.error().violations) {
            detail += " [";
            if (violation.stageIndex.has_value()) {
                detail += "stage " + std::to_string(*violation.stageIndex) + ", ";
            }
            detail += violation.code + ": " + violation.detail + ']';
        }
        return failure("preset_pipeline_invalid",
            std::move(detail));
    }
    return core::Result<void>::success();
}

core::Result<void> validateCustomPreset(const Preset& preset) {
    const auto envelope = validatePresetEnvelope(preset);
    if (!envelope.hasValue()) return envelope;
    if (preset.builtIn) {
        return failure("custom_preset_built_in",
            "Saved custom recipes cannot be marked as built in.");
    }
    if (isReserved(preset.id)) {
        return failure("custom_preset_id_reserved",
            "Saved custom recipes cannot use a reserved preset ID.");
    }
    return core::Result<void>::success();
}

PresetRepository::PresetRepository(
    std::vector<Preset> shipped, PresetState state)
    : shipped_(std::move(shipped)), state_(std::move(state)) {}

core::Result<PresetRepository> PresetRepository::create(
    std::vector<Preset> shipped, PresetState state) {
    const auto validShipped = validateShipped(shipped);
    if (!validShipped.hasValue()) {
        return core::Result<PresetRepository>::failure(validShipped.error());
    }
    const auto validState = validateState(shipped, state);
    if (!validState.hasValue()) {
        return core::Result<PresetRepository>::failure(validState.error());
    }
    return core::Result<PresetRepository>::success(
        PresetRepository(std::move(shipped), std::move(state)));
}

std::vector<Preset> PresetRepository::list() const {
    auto result = shipped_;
    const auto custom = std::find_if(result.begin(), result.end(), [](const Preset& preset) {
        return preset.id == PresetId{"custom"};
    });
    if (custom != result.end()) custom->pipeline = state_.activePipeline;
    result.insert(result.end(), state_.customPresets.begin(), state_.customPresets.end());
    return result;
}

core::Result<Preset> PresetRepository::find(const PresetId& id) const {
    if (id == PresetId{"custom"}) {
        auto preset = *findPreset(shipped_, id);
        preset.pipeline = state_.activePipeline;
        return core::Result<Preset>::success(std::move(preset));
    }
    if (const auto* preset = findPreset(shipped_, id)) {
        return core::Result<Preset>::success(*preset);
    }
    if (const auto* preset = findPreset(state_.customPresets, id)) {
        return core::Result<Preset>::success(*preset);
    }
    return core::Result<Preset>::failure(
        presetError("preset_not_found", "No preset has the requested ID."));
}

core::Result<processing::PipelineDefinition> PresetRepository::apply(const PresetId& id) {
    const auto preset = find(id);
    if (!preset.hasValue()) {
        return core::Result<processing::PipelineDefinition>::failure(preset.error());
    }

    PresetState candidate = state_;
    candidate.selectedId = id;
    if (id != PresetId{"custom"}) candidate.activePipeline = preset.value().pipeline;
    processing::PipelineDefinition returned = candidate.activePipeline;
    swapState(state_, candidate);
    return core::Result<processing::PipelineDefinition>::success(std::move(returned));
}

core::Result<void> PresetRepository::edit(
    processing::PipelineDefinition definition) {
    const auto valid = validatePresetPipeline(definition);
    if (!valid.hasValue()) return valid;

    PresetState candidate = state_;
    candidate.selectedId = PresetId{"custom"};
    candidate.activePipeline = std::move(definition);
    swapState(state_, candidate);
    return core::Result<void>::success();
}

core::Result<PresetId> PresetRepository::classify(
    const processing::PipelineDefinition& definition) const {
    const auto valid = validatePresetPipeline(definition);
    if (!valid.hasValue()) {
        return core::Result<PresetId>::failure(valid.error());
    }
    for (const auto id : namedRecipeIds) {
        const auto* preset = findPreset(shipped_, PresetId{std::string(id)});
        if (samePipeline(preset->pipeline, definition)) {
            return core::Result<PresetId>::success(PresetId{std::string(id)});
        }
    }
    for (const auto& preset : state_.customPresets) {
        if (samePipeline(preset.pipeline, definition)) {
            return core::Result<PresetId>::success(preset.id);
        }
    }
    return core::Result<PresetId>::success(PresetId{"custom"});
}

core::Result<void> PresetRepository::saveCustom(
    PresetId id, std::string name, std::string description) {
    if (isReserved(id)) {
        return failure("custom_preset_id_reserved",
            "Saved custom recipes cannot use a reserved preset ID.");
    }

    const auto* existing = findPreset(state_.customPresets, id);
    if (existing != nullptr
        && existing->revision == std::numeric_limits<std::uint64_t>::max()) {
        return failure("custom_preset_revision_overflow",
            "The saved custom preset revision cannot be incremented.");
    }
    const auto revision = existing == nullptr ? 1U : existing->revision + 1U;
    Preset replacement{std::move(id), std::move(name), std::move(description),
        false, 1U, revision, state_.activePipeline};
    const auto valid = validateCustomPreset(replacement);
    if (!valid.hasValue()) return valid;
    PresetId savedId = replacement.id;

    PresetState candidate = state_;
    const auto found = std::find_if(candidate.customPresets.begin(),
        candidate.customPresets.end(), [&](const Preset& preset) {
            return preset.id == replacement.id;
        });
    if (found == candidate.customPresets.end()) {
        candidate.customPresets.push_back(std::move(replacement));
    } else {
        *found = std::move(replacement);
    }
    candidate.selectedId = savedId;
    swapState(state_, candidate);
    return core::Result<void>::success();
}

core::Result<void> PresetRepository::deleteCustom(const PresetId& id) {
    if (isReserved(id)) {
        return failure("custom_preset_id_reserved",
            "Reserved preset IDs cannot be deleted.");
    }
    if (findPreset(state_.customPresets, id) == nullptr) {
        return failure("preset_not_found", "No saved custom preset has the requested ID.");
    }

    PresetState candidate = state_;
    const auto found = std::find_if(candidate.customPresets.begin(),
        candidate.customPresets.end(), [&](const Preset& preset) {
            return preset.id == id;
        });
    candidate.customPresets.erase(found);
    if (candidate.selectedId == id) candidate.selectedId = PresetId{"custom"};
    swapState(state_, candidate);
    return core::Result<void>::success();
}

core::Result<void> PresetRepository::restore(PresetState state) {
    const auto valid = validateState(shipped_, state);
    if (!valid.hasValue()) return valid;
    swapState(state_, state);
    return core::Result<void>::success();
}

PresetState PresetRepository::snapshot() const { return state_; }

}  // namespace lumora::application
