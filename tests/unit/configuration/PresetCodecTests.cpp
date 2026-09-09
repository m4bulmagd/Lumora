#include <lumora/configuration/ConfigurationCodec.hpp>
#include <lumora/configuration/PresetCodec.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace lumora::configuration {
namespace {

// Literal wire values deliberately do not use the production encoder/defaults.
QJsonObject standardJson() {
    return QJsonDocument::fromJson(R"json({
        "schemaVersion":1,"orderVersion":1,"stages":[
        {"id":"normalize","enabled":true,"parameters":{}},
        {"id":"window_level","enabled":true,"parameters":{"window":65535,"level":32767.5}},
        {"id":"brightness_contrast","enabled":true,"parameters":{"brightness":0,"contrast":1}},
        {"id":"gamma","enabled":true,"parameters":{"gamma":1}},
        {"id":"clahe","enabled":true,"parameters":{"clipLimit":2,"tileGridSize":8}},
        {"id":"denoise","enabled":true,"parameters":{"mode":"gaussian","kernelSize":3,"sigma":0}},
        {"id":"sharpen","enabled":true,"parameters":{"amount":1,"radius":1,"threshold":0}},
        {"id":"invert","enabled":false,"parameters":{}}]})json").object();
}

QJsonObject stateJson() {
    return {{"schemaVersion", 1}, {"selectedId", "standard"},
        {"activePipeline", standardJson()}, {"customPresets", QJsonArray{}}, {"legacy", QJsonObject{}}};
}

QJsonObject recipeJson(QString id = QStringLiteral("user-one")) {
    return {{"schemaVersion", 1}, {"revision", "1"}, {"id", std::move(id)},
        {"name", "Saved recipe"}, {"description", "A saved processing definition."},
        {"builtIn", false}, {"pipeline", standardJson()}};
}

void changeStage(QJsonObject& pipeline, int index, const std::function<void(QJsonObject&)>& change) {
    auto stages = pipeline.value("stages").toArray();
    auto stage = stages[index].toObject();
    change(stage);
    stages[index] = stage;
    pipeline.insert("stages", stages);
}

QJsonObject configurationJson(int version, const QJsonObject& presets) {
    return {{"schemaVersion", version}, {"application", QJsonObject{{"theme", "dark"}}},
        {"cameraProfiles", QJsonObject{{"camera", "retained"}}},
        {"processing", QJsonObject{{"opaque", true}}}, {"presets", presets},
        {"capture", QJsonObject{{"path", "captures"}}}, {"ui", QJsonObject{{"sidebar", true}}},
        {"startup", QJsonValue{}}};
}

class CurrentDirectoryGuard final {
public:
    ~CurrentDirectoryGuard() { (void)QDir::setCurrent(previous_); }
private:
    QString previous_{QDir::currentPath()};
};

TEST(PresetCodec, ShippedResourceLoadsOutsideCheckoutWithExactRecipeSemantics) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    CurrentDirectoryGuard restoreDirectory;
    ASSERT_TRUE(QDir::setCurrent(directory.path()));

    auto loaded = PresetCodec::loadDefaultRepository();

    ASSERT_TRUE(loaded.hasValue()) << loaded.error().diagnosticDetail;
    const auto presets = loaded.value().list();
    ASSERT_EQ(presets.size(), 5U);
    const std::vector<std::string> ids{"original", "standard", "high-contrast", "soft-detail", "custom"};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        SCOPED_TRACE(ids[index]);
        EXPECT_EQ(presets[index].id.value, ids[index]);
        EXPECT_EQ(presets[index].builtIn, index != 4U);
        EXPECT_EQ(presets[index].schemaVersion, 1U);
        EXPECT_EQ(presets[index].revision, 1U);
        EXPECT_FALSE(presets[index].name.empty());
        ASSERT_EQ(presets[index].pipeline.stages.size(), 8U);
    }
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(presets[0].pipeline, processing::defaultPipeline()));
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(presets[1].pipeline, processing::standardPipeline()));
    EXPECT_DOUBLE_EQ(std::get<processing::BrightnessContrastParameters>(presets[2].pipeline.stages[2].parameters).contrast, 1.25);
    EXPECT_DOUBLE_EQ(std::get<processing::ClaheParameters>(presets[2].pipeline.stages[4].parameters).clipLimit, 3.0);
    EXPECT_DOUBLE_EQ(std::get<processing::ClaheParameters>(presets[3].pipeline.stages[4].parameters).clipLimit, 1.5);
    EXPECT_DOUBLE_EQ(std::get<processing::SharpenParameters>(presets[3].pipeline.stages[6].parameters).amount, 0.5);
    auto highContrast = processing::standardPipeline();
    std::get<processing::BrightnessContrastParameters>(highContrast.stages[2].parameters).contrast = 1.25;
    std::get<processing::ClaheParameters>(highContrast.stages[4].parameters).clipLimit = 3.0;
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(presets[2].pipeline, highContrast));
    auto softDetail = processing::standardPipeline();
    std::get<processing::ClaheParameters>(softDetail.stages[4].parameters).clipLimit = 1.5;
    std::get<processing::SharpenParameters>(softDetail.stages[6].parameters).amount = 0.5;
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(presets[3].pipeline, softDetail));
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(presets[4].pipeline, processing::defaultPipeline()));
    EXPECT_EQ(loaded.value().snapshot().selectedId.value, "original");
}

TEST(PresetCodec, LiteralStandardDecodesWithoutWarningsAndEncodesExactWireShape) {
    const auto decoded = PresetCodec::decode(stateJson());
    ASSERT_TRUE(decoded.hasValue()) << decoded.error().diagnosticDetail;
    EXPECT_TRUE(decoded.value().issues.empty());
    EXPECT_EQ(decoded.value().state.selectedId.value, "standard");
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(decoded.value().state.activePipeline, processing::standardPipeline()));
    const auto encoded = PresetCodec::encode(decoded.value().state);
    ASSERT_TRUE(encoded.hasValue());
    EXPECT_EQ(encoded.value(), stateJson());
}

TEST(PresetCodec, EveryParameterAndMedianModeSurviveTheTypedBoundary) {
    auto pipeline = standardJson();
    const std::vector<QJsonObject> parameters{{}, {{"window", 32100.25}, {"level", 1234.5}},
        {{"brightness", 0.55}, {"contrast", 1.2}}, {{"gamma", 1.7}}, {{"clipLimit", 3.5}, {"tileGridSize", 16}},
        {{"mode", "median"}, {"kernelSize", 5}, {"sigma", 0}}, {{"amount", 1.25}, {"radius", 2.0}, {"threshold", 3.5}}, {}};
    for (int index = 0; index < 8; ++index) {
        changeStage(pipeline, index, [&](auto& stage) { stage.insert("parameters", parameters[static_cast<std::size_t>(index)]); });
    }
    auto input = stateJson(); input.insert("selectedId", "custom"); input.insert("activePipeline", pipeline);
    const auto decoded = PresetCodec::decode(input);
    ASSERT_TRUE(decoded.hasValue()); ASSERT_TRUE(decoded.value().issues.empty());
    const auto& stages = decoded.value().state.activePipeline.stages;
    ASSERT_EQ(stages.size(), 8U);
    EXPECT_DOUBLE_EQ(std::get<processing::WindowLevelParameters>(stages[1].parameters).window, 32100.25);
    EXPECT_DOUBLE_EQ(std::get<processing::BrightnessContrastParameters>(stages[2].parameters).brightness, 0.55);
    EXPECT_DOUBLE_EQ(std::get<processing::GammaParameters>(stages[3].parameters).gamma, 1.7);
    EXPECT_EQ(std::get<processing::ClaheParameters>(stages[4].parameters).tileGridSize, 16U);
    EXPECT_EQ(std::get<processing::DenoiseParameters>(stages[5].parameters).mode, processing::DenoiseMode::Median);
    EXPECT_DOUBLE_EQ(std::get<processing::SharpenParameters>(stages[6].parameters).threshold, 3.5);
    const auto encoded = PresetCodec::encode(decoded.value().state);
    ASSERT_TRUE(encoded.hasValue()); EXPECT_EQ(encoded.value().value("activePipeline").toObject(), pipeline);
}

TEST(PresetCodec, GaussianSigmaRetainsItsNondefaultFiniteValue) {
    auto pipeline = standardJson();
    changeStage(pipeline, 5, [](auto& stage) {
        stage.insert("parameters", QJsonObject{{"mode", "gaussian"}, {"kernelSize", 7}, {"sigma", 2.25}});
    });
    auto input = stateJson(); input.insert("selectedId", "custom"); input.insert("activePipeline", pipeline);
    const auto decoded = PresetCodec::decode(input);
    ASSERT_TRUE(decoded.hasValue()); ASSERT_TRUE(decoded.value().issues.empty());
    const auto& value = std::get<processing::DenoiseParameters>(decoded.value().state.activePipeline.stages[5].parameters);
    EXPECT_EQ(value.mode, processing::DenoiseMode::Gaussian);
    EXPECT_EQ(value.kernelSize, 7U); EXPECT_DOUBLE_EQ(value.sigma, 2.25);
    const auto encoded = PresetCodec::encode(decoded.value().state);
    ASSERT_TRUE(encoded.hasValue()); EXPECT_EQ(encoded.value().value("activePipeline").toObject(), pipeline);
}

TEST(PresetCodec, MaximumRecipeRevisionAndUnicodeAreLosslessWhileRuntimeRevisionIsOmitted) {
    auto recipe = recipeJson(QString::fromUtf8("وصفة-α"));
    recipe.insert("name", QString::fromUtf8("تفاصيل 🔬"));
    recipe.insert("revision", "18446744073709551615");
    auto input = stateJson(); input.insert("customPresets", QJsonArray{recipe});
    input.insert("selectedId", recipe.value("id"));
    auto decoded = PresetCodec::decode(input);
    ASSERT_TRUE(decoded.hasValue()); ASSERT_TRUE(decoded.value().issues.empty());
    ASSERT_EQ(decoded.value().state.customPresets.size(), 1U);
    EXPECT_EQ(decoded.value().state.customPresets[0].revision, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(decoded.value().state.activePipeline.version.configurationRevision, 0U);
    decoded.value().state.activePipeline.version.configurationRevision = 123U;
    decoded.value().state.customPresets[0].pipeline.version.configurationRevision = 456U;
    const auto encoded = PresetCodec::encode(decoded.value().state);
    ASSERT_TRUE(encoded.hasValue()); EXPECT_EQ(encoded.value(), input);
}

TEST(PresetCodec, FirstValidDuplicateWinsAndIssuesRetainOriginalArrayIndices) {
    auto invalid = recipeJson(); invalid.insert("revision", "0");
    auto first = recipeJson(); first.insert("name", "First valid");
    auto duplicate = recipeJson(); duplicate.insert("name", "Later duplicate");
    auto input = stateJson(); input.insert("customPresets", QJsonArray{invalid, first, duplicate, recipeJson("user-two")});
    input.insert("selectedId", "user-one");
    const auto decoded = PresetCodec::decode(input);
    ASSERT_TRUE(decoded.hasValue());
    ASSERT_EQ(decoded.value().state.customPresets.size(), 2U);
    EXPECT_EQ(decoded.value().state.customPresets[0].name, "First valid");
    EXPECT_EQ(decoded.value().state.customPresets[1].id.value, "user-two");
    EXPECT_EQ(decoded.value().state.selectedId.value, "user-one");
    ASSERT_EQ(decoded.value().issues.size(), 2U);
    EXPECT_EQ(decoded.value().issues[0].entryIndex, 0U);
    EXPECT_EQ(decoded.value().issues[1].entryIndex, 2U);
    ASSERT_TRUE(decoded.value().issues[1].presetId.has_value());
    EXPECT_EQ(decoded.value().issues[1].presetId->value, "user-one");
}

TEST(PresetCodec, InvalidSelectionRecoversToCustomWithoutReclassifyingActiveValues) {
    for (const auto& selection : std::vector<QJsonValue>{QJsonValue::Undefined, 42, "missing", "original"}) {
        auto input = stateJson(); input.insert("selectedId", selection);
        const auto decoded = PresetCodec::decode(input);
        ASSERT_TRUE(decoded.hasValue()); EXPECT_FALSE(decoded.value().issues.empty());
        EXPECT_EQ(decoded.value().state.selectedId.value, "custom");
        EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(decoded.value().state.activePipeline, processing::standardPipeline()));
    }
}

TEST(PresetCodec, InvalidActivePipelineRecoversToOriginalAndRetainsValidRecipes) {
    for (const auto& active : std::vector<QJsonValue>{QJsonValue::Undefined, 42, QJsonObject{}}) {
        auto input = stateJson(); input.insert("activePipeline", active); input.insert("customPresets", QJsonArray{recipeJson()});
        const auto decoded = PresetCodec::decode(input);
        ASSERT_TRUE(decoded.hasValue()); EXPECT_FALSE(decoded.value().issues.empty());
        EXPECT_EQ(decoded.value().state.selectedId.value, "original");
        EXPECT_EQ(decoded.value().state.customPresets.size(), 1U);
        EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(decoded.value().state.activePipeline, processing::defaultPipeline()));
    }
}

TEST(PresetCodec, MissingWrongTypedAndFutureRequiredEnvelopesAreWholeFailures) {
    for (const char* field : {"schemaVersion", "customPresets", "legacy"}) {
        auto missing = stateJson(); missing.remove(QLatin1String(field));
        EXPECT_FALSE(PresetCodec::decode(missing).hasValue()) << field;
        auto wrong = stateJson(); wrong.insert(QLatin1String(field), true);
        EXPECT_FALSE(PresetCodec::decode(wrong).hasValue()) << field;
    }
    auto future = stateJson(); future.insert("schemaVersion", 2);
    EXPECT_FALSE(PresetCodec::decode(future).hasValue());
}

TEST(PresetCodec, InvalidRecipeVersionsFlagsAndRevisionSpellingsAreSkipped) {
    std::vector<QJsonObject> invalid;
    for (const auto& revision : std::vector<QJsonValue>{0, 1, "0", "01", "+1", "-1", "1.0", "18446744073709551616"}) {
        auto entry = recipeJson(); entry.insert("revision", revision); invalid.push_back(entry);
    }
    auto reserved = recipeJson("standard"); invalid.push_back(reserved);
    auto builtIn = recipeJson(); builtIn.insert("builtIn", true); invalid.push_back(builtIn);
    auto future = recipeJson(); future.insert("schemaVersion", 2); invalid.push_back(future);
    auto extra = recipeJson(); extra.insert("unexpected", true); invalid.push_back(extra);
    for (const auto& entry : invalid) {
        auto input = stateJson(); input.insert("customPresets", QJsonArray{entry, recipeJson("survivor")});
        const auto decoded = PresetCodec::decode(input);
        ASSERT_TRUE(decoded.hasValue()); ASSERT_EQ(decoded.value().state.customPresets.size(), 1U);
        EXPECT_EQ(decoded.value().state.customPresets[0].id.value, "survivor");
        ASSERT_EQ(decoded.value().issues.size(), 1U); EXPECT_EQ(decoded.value().issues[0].entryIndex, 0U);
    }
}

TEST(PresetCodec, UnknownMissingReorderedAndInvalidDisabledStageDataIsRejected) {
    std::vector<QJsonObject> invalid;
    auto missing = standardJson(); auto stages = missing.value("stages").toArray(); stages.removeAt(7); missing.insert("stages", stages); invalid.push_back(missing);
    auto reordered = standardJson(); stages = reordered.value("stages").toArray(); const auto saved = stages.at(2); stages[2] = stages.at(3); stages[3] = saved; reordered.insert("stages", stages); invalid.push_back(reordered);
    auto runtime = standardJson(); runtime.insert("configurationRevision", "1"); invalid.push_back(runtime);
    auto unknown = standardJson(); changeStage(unknown, 3, [](auto& stage) { stage.insert("id", "future_stage"); }); invalid.push_back(unknown);
    auto extra = standardJson(); changeStage(extra, 3, [](auto& stage) { stage.insert("future", true); }); invalid.push_back(extra);
    auto disabled = standardJson(); changeStage(disabled, 3, [](auto& stage) { stage.insert("enabled", false); stage.insert("parameters", QJsonObject{{"gamma", 0}}); }); invalid.push_back(disabled);
    auto parameter = standardJson(); changeStage(parameter, 3, [](auto& stage) { stage.insert("parameters", QJsonObject{{"gamma", 1}, {"future", 1}}); }); invalid.push_back(parameter);
    auto boolean = standardJson(); changeStage(boolean, 3, [](auto& stage) { stage.insert("enabled", 1); }); invalid.push_back(boolean);
    for (const auto& pipeline : invalid) {
        auto entry = recipeJson(); entry.insert("pipeline", pipeline);
        auto input = stateJson(); input.insert("customPresets", QJsonArray{entry});
        const auto decoded = PresetCodec::decode(input);
        ASSERT_TRUE(decoded.hasValue()); EXPECT_TRUE(decoded.value().state.customPresets.empty());
        ASSERT_EQ(decoded.value().issues.size(), 1U); EXPECT_EQ(decoded.value().issues[0].entryIndex, 0U);
    }
}

TEST(PresetCodec, RejectsLossyUnicodeInBothDirections) {
    auto entry = recipeJson(); entry.insert("name", QString(1, QChar(static_cast<char16_t>(0xD800))));
    auto input = stateJson(); input.insert("customPresets", QJsonArray{entry});
    const auto decoded = PresetCodec::decode(input);
    ASSERT_TRUE(decoded.hasValue()); EXPECT_TRUE(decoded.value().state.customPresets.empty());
    ASSERT_EQ(decoded.value().issues.size(), 1U);
    application::PresetState state;
    application::Preset recipe; recipe.id = {"user-one"}; recipe.name = std::string{"\xC3\x28", 2};
    state.customPresets.push_back(recipe);
    EXPECT_FALSE(PresetCodec::encode(state).hasValue());
}

TEST(PresetCodec, EncodeRejectsInvalidEntriesAndNonfiniteActiveValuesWithoutDroppingThem) {
    application::PresetState state;
    application::Preset invalid; invalid.id = {"reserved"}; invalid.name = "Invalid revision"; invalid.revision = 0;
    state.customPresets.push_back(invalid);
    EXPECT_FALSE(PresetCodec::encode(state).hasValue());
    state.customPresets.clear(); state.selectedId = {"custom"};
    std::get<processing::GammaParameters>(state.activePipeline.stages[3].parameters).gamma = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(PresetCodec::encode(state).hasValue());
}

TEST(PresetCodec, SchemaOneAndTwoMigrationPreserveLegacyAndNormalizeToTheSameSchemaThree) {
    const QJsonObject legacy{{"selected", "Standard"}, {"uninterpreted", QJsonArray{1, "keep"}}};
    auto first = configurationJson(1, legacy); first.remove("startup");
    const auto second = configurationJson(2, legacy);
    const auto one = ConfigurationCodec::decode(QJsonDocument(first).toJson());
    const auto two = ConfigurationCodec::decode(QJsonDocument(second).toJson());
    ASSERT_TRUE(one.hasValue()); ASSERT_TRUE(two.hasValue());
    EXPECT_EQ(one.value().schemaVersion, 3); EXPECT_EQ(two.value().schemaVersion, 3);
    EXPECT_FALSE(one.value().usedDefaults); EXPECT_TRUE(one.value().loadWarning.has_value());
    const auto encodedOne = ConfigurationCodec::encode(one.value());
    const auto encodedTwo = ConfigurationCodec::encode(two.value());
    ASSERT_TRUE(encodedOne.hasValue()); ASSERT_TRUE(encodedTwo.hasValue()); EXPECT_EQ(encodedOne.value(), encodedTwo.value());
    const auto normalized = QJsonDocument::fromJson(encodedOne.value()).object();
    EXPECT_EQ(normalized.value("schemaVersion").toInt(), 3);
    EXPECT_EQ(normalized.value("presets").toObject().value("legacy").toObject(), legacy);
    EXPECT_EQ(normalized.value("presets").toObject().value("selectedId"), "original");
    for (const char* section : {"application", "cameraProfiles", "processing", "capture", "ui"}) EXPECT_EQ(normalized.value(QLatin1String(section)), first.value(QLatin1String(section)));
    const auto again = ConfigurationCodec::decode(encodedOne.value()); ASSERT_TRUE(again.hasValue());
    const auto reencoded = ConfigurationCodec::encode(again.value()); ASSERT_TRUE(reencoded.hasValue()); EXPECT_EQ(reencoded.value(), encodedOne.value());
}

TEST(PresetCodec, RecoveredSchemaThreeHasUsableLoadWarningWithoutSerializingIssues) {
    auto presets = stateJson(); presets.insert("selectedId", "missing");
    const auto decoded = ConfigurationCodec::decode(QJsonDocument(configurationJson(3, presets)).toJson());
    ASSERT_TRUE(decoded.hasValue()); EXPECT_FALSE(decoded.value().usedDefaults);
    EXPECT_TRUE(decoded.value().loadWarning.has_value());
    EXPECT_FALSE(decoded.value().preservedInvalidFile.has_value());
    const auto encoded = ConfigurationCodec::encode(decoded.value()); ASSERT_TRUE(encoded.hasValue());
    const auto root = QJsonDocument::fromJson(encoded.value()).object();
    EXPECT_EQ(root.value("presets").toObject().value("selectedId"), "custom");
    EXPECT_FALSE(root.contains("presetIssues")); EXPECT_FALSE(root.value("presets").toObject().contains("issues"));
}

}  // namespace
}  // namespace lumora::configuration
