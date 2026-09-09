#include "PipelineDefinitionCodec.hpp"

#include <lumora/application/PresetRepository.hpp>

#include <QJsonArray>

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

namespace lumora::configuration::detail {
namespace {

using namespace processing;
constexpr std::array<const char*, 8> stageNames{
    "normalize", "window_level", "brightness_contrast", "gamma", "clahe", "denoise", "sharpen", "invert"};

core::Error invalidPipeline(std::string detail) {
    return {core::ErrorCategory::Configuration, "preset_invalid_pipeline_json",
        "A saved processing definition is invalid.", std::move(detail), true};
}

bool fields(const QJsonObject& object, std::initializer_list<const char*> names) {
    if (object.size() != static_cast<qsizetype>(names.size())) return false;
    for (const auto* name : names) if (!object.contains(QLatin1String(name))) return false;
    return true;
}

core::Result<StageParameters> parameters(std::size_t stage, const QJsonObject& object) {
    bool valid = true;
    const auto number = [&](const char* key) {
        const auto value = object.value(QLatin1String(key));
        if (!value.isDouble() || !std::isfinite(value.toDouble())) { valid = false; return 0.0; }
        return value.toDouble();
    };
    const auto integer = [&](const char* key) {
        const double value = number(key);
        if (value < 0 || value > std::numeric_limits<std::uint32_t>::max() || std::floor(value) != value) {
            valid = false; return std::uint32_t{0};
        }
        return static_cast<std::uint32_t>(value);
    };
    StageParameters parsed;
    switch (stage) {
    case 0: valid = fields(object, {}); parsed = NormalizationParameters{}; break;
    case 1:
        valid = fields(object, {"window", "level"});
        parsed = WindowLevelParameters{number("window"), number("level")}; break;
    case 2:
        valid = fields(object, {"brightness", "contrast"});
        parsed = BrightnessContrastParameters{number("brightness"), number("contrast")}; break;
    case 3:
        valid = fields(object, {"gamma"}); parsed = GammaParameters{number("gamma")}; break;
    case 4:
        valid = fields(object, {"clipLimit", "tileGridSize"});
        parsed = ClaheParameters{number("clipLimit"), integer("tileGridSize")}; break;
    case 5: {
        valid = fields(object, {"mode", "kernelSize", "sigma"});
        const auto mode = object.value("mode");
        if (!mode.isString() || (mode != QLatin1String("gaussian") && mode != QLatin1String("median"))) valid = false;
        parsed = DenoiseParameters{mode == QLatin1String("median") ? DenoiseMode::Median : DenoiseMode::Gaussian,
            integer("kernelSize"), number("sigma")}; break;
    }
    case 6:
        valid = fields(object, {"amount", "radius", "threshold"});
        parsed = SharpenParameters{number("amount"), number("radius"), number("threshold")}; break;
    case 7: valid = fields(object, {}); parsed = InvertParameters{}; break;
    default: valid = false; break;
    }
    if (!valid) return core::Result<StageParameters>::failure(invalidPipeline("Stage parameters have missing, unknown or wrongly typed fields."));
    return core::Result<StageParameters>::success(std::move(parsed));
}

QJsonObject encodeParameters(const StageDefinition& stage) {
    switch (stage.id) {
    case StageId::Normalize: case StageId::Invert: return {};
    case StageId::WindowLevel: {
        const auto& p = std::get<WindowLevelParameters>(stage.parameters);
        return {{"window", p.window}, {"level", p.level}};
    }
    case StageId::BrightnessContrast: {
        const auto& p = std::get<BrightnessContrastParameters>(stage.parameters);
        return {{"brightness", p.brightness}, {"contrast", p.contrast}};
    }
    case StageId::Gamma: return {{"gamma", std::get<GammaParameters>(stage.parameters).gamma}};
    case StageId::Clahe: {
        const auto& p = std::get<ClaheParameters>(stage.parameters);
        return {{"clipLimit", p.clipLimit}, {"tileGridSize", static_cast<qint64>(p.tileGridSize)}};
    }
    case StageId::Denoise: {
        const auto& p = std::get<DenoiseParameters>(stage.parameters);
        return {{"mode", p.mode == DenoiseMode::Gaussian ? "gaussian" : "median"},
            {"kernelSize", static_cast<qint64>(p.kernelSize)}, {"sigma", p.sigma}};
    }
    case StageId::Sharpen: {
        const auto& p = std::get<SharpenParameters>(stage.parameters);
        return {{"amount", p.amount}, {"radius", p.radius}, {"threshold", p.threshold}};
    }
    }
    return {}; // The domain validator rejects unknown stage IDs before this call.
}

}  // namespace

core::Result<PipelineDefinition> PipelineDefinitionCodec::decode(const QJsonObject& object) {
    if (!fields(object, {"schemaVersion", "orderVersion", "stages"})
        || !object.value("schemaVersion").isDouble() || object.value("schemaVersion").toDouble() != 1
        || !object.value("orderVersion").isDouble() || object.value("orderVersion").toDouble() != 1
        || !object.value("stages").isArray()) {
        return core::Result<PipelineDefinition>::failure(invalidPipeline("Pipeline requires schemaVersion1, orderVersion1 and only the ordered stages array."));
    }
    const auto stages = object.value("stages").toArray();
    if (stages.size() != static_cast<qsizetype>(stageNames.size())) {
        return core::Result<PipelineDefinition>::failure(invalidPipeline("A preset requires all eight canonical stages."));
    }
    PipelineDefinition pipeline{{1, 1, 0}, {}};
    for (std::size_t index = 0; index < stageNames.size(); ++index) {
        const auto value = stages[static_cast<qsizetype>(index)];
        const auto stage = value.toObject();
        if (!value.isObject() || !fields(stage, {"id", "enabled", "parameters"})
            || stage.value("id") != QLatin1String(stageNames[index])
            || !stage.value("enabled").isBool() || !stage.value("parameters").isObject()) {
            return core::Result<PipelineDefinition>::failure(invalidPipeline("Stage fields or canonical order are invalid at index " + std::to_string(index) + "."));
        }
        auto parsed = parameters(index, stage.value("parameters").toObject());
        if (!parsed.hasValue()) return core::Result<PipelineDefinition>::failure(parsed.error());
        pipeline.stages.push_back({static_cast<StageId>(index), stage.value("enabled").toBool(), std::move(parsed).value()});
    }
    const auto validated = application::validatePresetPipeline(pipeline);
    if (!validated.hasValue()) return core::Result<PipelineDefinition>::failure(validated.error());
    return core::Result<PipelineDefinition>::success(std::move(pipeline));
}

core::Result<QJsonObject> PipelineDefinitionCodec::encode(const PipelineDefinition& pipeline) {
    const auto validated = application::validatePresetPipeline(pipeline);
    if (!validated.hasValue()) return core::Result<QJsonObject>::failure(validated.error());
    QJsonArray stages;
    for (std::size_t index = 0; index < pipeline.stages.size(); ++index) {
        const auto& stage = pipeline.stages[index];
        stages.append(QJsonObject{{"id", QLatin1String(stageNames[index])}, {"enabled", stage.enabled},
            {"parameters", encodeParameters(stage)}});
    }
    return core::Result<QJsonObject>::success({{"schemaVersion", 1}, {"orderVersion", 1}, {"stages", stages}});
}

}  // namespace lumora::configuration::detail
