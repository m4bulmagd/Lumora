#include <lumora/configuration/PresetCodec.hpp>

#include "PipelineDefinitionCodec.hpp"

#include <lumora/processing/ProcessingDefaults.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QResource>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

// Q_INIT_RESOURCE must be referenced outside a C++ namespace so static-library
// consumers pull in the RCC object without depending on CWD or a UI target.
static void initializePresetResource() {
    Q_INIT_RESOURCE(default_presets);
}

namespace lumora::configuration {
namespace {

core::Error presetError(std::string code, std::string detail, bool recoverable = true) {
    return {core::ErrorCategory::Configuration, std::move(code),
        "Saved preset settings are invalid.", std::move(detail), recoverable};
}

core::Error resourceError(std::string detail) {
    return {core::ErrorCategory::Configuration, "preset_resource_invalid",
        "The installed default presets could not be loaded.", std::move(detail), false};
}

bool fields(const QJsonObject& object, std::initializer_list<const char*> names) {
    if (object.size() != static_cast<qsizetype>(names.size())) return false;
    for (const auto* name : names) if (!object.contains(QLatin1String(name))) return false;
    return true;
}

core::Result<std::string> readString(const QJsonValue& value) {
    if (!value.isString()) return core::Result<std::string>::failure(presetError(
        "preset_invalid_string", "A preset string is absent or has the wrong JSON type."));
    const auto text = value.toString();
    const auto bytes = text.toUtf8();
    if (QString::fromUtf8(bytes) != text) return core::Result<std::string>::failure(presetError(
        "preset_invalid_unicode", "A preset string contains unpaired UTF-16 surrogates."));
    return core::Result<std::string>::success(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

core::Result<QString> writeString(const std::string& value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        return core::Result<QString>::failure(presetError("preset_invalid_string", "A preset string is too large."));
    }
    const auto text = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    const auto bytes = text.toUtf8();
    if (std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())) != value) {
        return core::Result<QString>::failure(presetError("preset_invalid_unicode", "A preset string is not lossless UTF-8."));
    }
    return core::Result<QString>::success(text);
}

core::Result<application::Preset> decodeRecipe(const QJsonValue& value, bool resource) {
    const auto object = value.toObject();
    if (!value.isObject() || !fields(object, {"schemaVersion", "revision", "id", "name", "description", "builtIn", "pipeline"})
        || !object.value("schemaVersion").isDouble() || object.value("schemaVersion").toDouble() != 1
        || !object.value("builtIn").isBool() || (!resource && object.value("builtIn").toBool())
        || !object.value("pipeline").isObject()) {
        return core::Result<application::Preset>::failure(presetError("preset_invalid_recipe_json", "Recipe fields, version or builtIn flag are invalid."));
    }
    auto id = readString(object.value("id"));
    auto name = readString(object.value("name"));
    auto description = readString(object.value("description"));
    auto revision = readString(object.value("revision"));
    for (const auto* parsed : {&id, &name, &description, &revision}) {
        if (!parsed->hasValue()) return core::Result<application::Preset>::failure(parsed->error());
    }
    const auto& text = revision.value();
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || text.front() < '1' || text.front() > '9'
        || error != std::errc{} || end != text.data() + text.size() || number == 0) {
        return core::Result<application::Preset>::failure(presetError("preset_invalid_revision", "Recipe revision must be a canonical positive decimal uint64 string."));
    }
    auto pipeline = detail::PipelineDefinitionCodec::decode(object.value("pipeline").toObject());
    if (!pipeline.hasValue()) return core::Result<application::Preset>::failure(pipeline.error());
    application::Preset preset{{std::move(id).value()}, std::move(name).value(), std::move(description).value(),
        object.value("builtIn").toBool(), 1U, number, std::move(pipeline).value()};
    if (!resource) {
        const auto valid = application::validateCustomPreset(preset);
        if (!valid.hasValue()) return core::Result<application::Preset>::failure(valid.error());
    }
    return core::Result<application::Preset>::success(std::move(preset));
}

core::Result<QJsonObject> encodeRecipe(const application::Preset& preset) {
    const auto valid = application::validateCustomPreset(preset);
    if (!valid.hasValue()) return core::Result<QJsonObject>::failure(valid.error());
    const auto id = writeString(preset.id.value);
    const auto name = writeString(preset.name);
    const auto description = writeString(preset.description);
    for (const auto* parsed : {&id, &name, &description}) {
        if (!parsed->hasValue()) return core::Result<QJsonObject>::failure(parsed->error());
    }
    const auto pipeline = detail::PipelineDefinitionCodec::encode(preset.pipeline);
    if (!pipeline.hasValue()) return core::Result<QJsonObject>::failure(pipeline.error());
    return core::Result<QJsonObject>::success({{"schemaVersion", 1}, {"revision", QString::number(static_cast<qulonglong>(preset.revision))},
        {"id", id.value()}, {"name", name.value()}, {"description", description.value()}, {"builtIn", false}, {"pipeline", pipeline.value()}});
}

bool envelope(const QJsonObject& object) {
    if (!object.value("schemaVersion").isDouble() || object.value("schemaVersion").toDouble() != 1
        || !object.value("customPresets").isArray() || !object.value("legacy").isObject()) return false;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QLatin1String("schemaVersion") && it.key() != QLatin1String("selectedId")
            && it.key() != QLatin1String("activePipeline") && it.key() != QLatin1String("customPresets")
            && it.key() != QLatin1String("legacy")) return false;
    }
    return true;
}

}  // namespace

core::Result<application::PresetRepository> PresetCodec::loadDefaultRepository() {
    static const bool initialized = [] { initializePresetResource(); return true; }();
    (void)initialized;
    QFile file(QStringLiteral(":/lumora/configuration/default-presets.json"));
    if (!file.open(QIODevice::ReadOnly)) return core::Result<application::PresetRepository>::failure(resourceError("The embedded default-presets.json resource is missing or unreadable."));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return core::Result<application::PresetRepository>::failure(resourceError("The embedded preset JSON could not be parsed."));
    }
    const auto root = document.object();
    if (!fields(root, {"schemaVersion", "presets"}) || !root.value("schemaVersion").isDouble()
        || root.value("schemaVersion").toDouble() != 1 || !root.value("presets").isArray()) {
        return core::Result<application::PresetRepository>::failure(resourceError("The embedded preset envelope has an unsupported shape or version."));
    }
    std::vector<application::Preset> recipes;
    for (const auto& value : root.value("presets").toArray()) {
        auto recipe = decodeRecipe(value, true);
        if (!recipe.hasValue()) return core::Result<application::PresetRepository>::failure(resourceError(recipe.error().diagnosticDetail));
        recipes.push_back(std::move(recipe).value());
    }
    auto repository = application::PresetRepository::create(std::move(recipes));
    if (!repository.hasValue()) return core::Result<application::PresetRepository>::failure(resourceError(repository.error().diagnosticDetail));
    return repository;
}

core::Result<DecodedPresetState> PresetCodec::decode(const QJsonObject& object) {
    auto repository = loadDefaultRepository();
    if (!repository.hasValue()) return core::Result<DecodedPresetState>::failure(repository.error());
    if (!envelope(object)) return core::Result<DecodedPresetState>::failure(presetError(
        "preset_invalid_envelope", "Preset state requires schemaVersion1, customPresets array and legacy object, with no unknown fields."));
    DecodedPresetState decoded{repository.value().snapshot(), object.value("legacy").toObject(), {}};
    const auto entries = object.value("customPresets").toArray();
    for (qsizetype index = 0; index < entries.size(); ++index) {
        auto recipe = decodeRecipe(entries[index], false);
        std::optional<application::PresetId> id;
        auto parsedId = readString(entries[index].toObject().value("id"));
        if (parsedId.hasValue()) id = application::PresetId{std::move(parsedId).value()};
        if (!recipe.hasValue()) {
            decoded.issues.push_back({static_cast<std::size_t>(index), std::move(id), recipe.error()});
            continue;
        }
        const auto duplicate = std::find_if(decoded.state.customPresets.begin(), decoded.state.customPresets.end(),
            [&](const auto& saved) { return saved.id == recipe.value().id; });
        if (duplicate != decoded.state.customPresets.end()) {
            decoded.issues.push_back({static_cast<std::size_t>(index), recipe.value().id,
                presetError("preset_duplicate_id", "A later valid entry repeats an earlier valid preset ID.")});
            continue;
        }
        decoded.state.customPresets.push_back(std::move(recipe).value());
    }
    const auto activeValue = object.value("activePipeline");
    auto active = activeValue.isObject() ? detail::PipelineDefinitionCodec::decode(activeValue.toObject())
        : core::Result<processing::PipelineDefinition>::failure(presetError("preset_invalid_active_pipeline", "The active pipeline is missing or is not an object."));
    if (!active.hasValue()) {
        decoded.issues.push_back({{}, {}, active.error()});
        // Resource-validated initial state is Original; surviving recipes remain.
    } else {
        decoded.state.activePipeline = std::move(active).value();
        auto selected = readString(object.value("selectedId"));
        bool matches = false;
        if (selected.hasValue()) {
            const application::PresetId id{selected.value()};
            if (id.value == "custom") matches = true;
            else {
                const auto saved = std::find_if(decoded.state.customPresets.begin(), decoded.state.customPresets.end(),
                    [&](const auto& recipe) { return recipe.id == id; });
                if (saved != decoded.state.customPresets.end()) matches = processing::semanticallyEqualPipelineDefinitions(saved->pipeline, decoded.state.activePipeline);
                else {
                    const auto shipped = repository.value().find(id);
                    matches = shipped.hasValue() && processing::semanticallyEqualPipelineDefinitions(shipped.value().pipeline, decoded.state.activePipeline);
                }
            }
            if (matches) decoded.state.selectedId = id;
        }
        if (!matches) {
            decoded.state.selectedId = {"custom"};
            decoded.issues.push_back({{}, selected.hasValue() ? std::optional<application::PresetId>{application::PresetId{selected.value()}} : std::nullopt,
                presetError("preset_selection_recovered", "Invalid or mismatched selection was recovered to Custom, preserving active values.")});
        }
    }
    const auto restored = repository.value().restore(decoded.state);
    if (!restored.hasValue()) return core::Result<DecodedPresetState>::failure(restored.error());
    return core::Result<DecodedPresetState>::success(std::move(decoded));
}

core::Result<QJsonObject> PresetCodec::encode(const application::PresetState& state, const QJsonObject& legacy) {
    auto repository = loadDefaultRepository();
    if (!repository.hasValue()) return core::Result<QJsonObject>::failure(repository.error());
    const auto restored = repository.value().restore(state);
    if (!restored.hasValue()) return core::Result<QJsonObject>::failure(restored.error());
    const auto selected = writeString(state.selectedId.value);
    if (!selected.hasValue()) return core::Result<QJsonObject>::failure(selected.error());
    const auto active = detail::PipelineDefinitionCodec::encode(state.activePipeline);
    if (!active.hasValue()) return core::Result<QJsonObject>::failure(active.error());
    QJsonArray recipes;
    for (const auto& recipe : state.customPresets) {
        const auto encoded = encodeRecipe(recipe);
        if (!encoded.hasValue()) return core::Result<QJsonObject>::failure(encoded.error());
        recipes.append(encoded.value());
    }
    return core::Result<QJsonObject>::success({{"schemaVersion", 1}, {"selectedId", selected.value()},
        {"activePipeline", active.value()}, {"customPresets", recipes}, {"legacy", legacy}});
}

}  // namespace lumora::configuration
