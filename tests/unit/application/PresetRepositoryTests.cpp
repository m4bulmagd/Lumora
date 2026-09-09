#include <lumora/application/PresetRepository.hpp>

#include <lumora/processing/ProcessingDefaults.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using lumora::application::Preset;
using lumora::application::PresetId;
using lumora::application::PresetRepository;
using lumora::application::PresetState;
using lumora::core::ErrorCategory;
using lumora::processing::BrightnessContrastParameters;
using lumora::processing::ClaheParameters;
using lumora::processing::PipelineDefinition;
using lumora::processing::SharpenParameters;

[[nodiscard]] PipelineDefinition highContrastPipeline() {
    auto definition = lumora::processing::standardPipeline();
    std::get<BrightnessContrastParameters>(definition.stages[2].parameters).contrast = 1.25;
    std::get<ClaheParameters>(definition.stages[4].parameters).clipLimit = 3.0;
    return definition;
}

[[nodiscard]] PipelineDefinition softDetailPipeline() {
    auto definition = lumora::processing::standardPipeline();
    std::get<ClaheParameters>(definition.stages[4].parameters).clipLimit = 1.5;
    std::get<SharpenParameters>(definition.stages[6].parameters).amount = 0.5;
    return definition;
}

[[nodiscard]] std::vector<Preset> shippedPresets() {
    return {
        {PresetId{"original"}, "Original", "Original full-range processing.", true,
            1U, 1U, lumora::processing::defaultPipeline()},
        {PresetId{"standard"}, "Standard", "Balanced processing defaults.", true,
            1U, 1U, lumora::processing::standardPipeline()},
        {PresetId{"high-contrast"}, "High Contrast", "Higher local and global contrast.",
            true, 1U, 1U, highContrastPipeline()},
        {PresetId{"soft-detail"}, "Soft Detail", "Gentler local contrast and sharpening.",
            true, 1U, 1U, softDetailPipeline()},
        {PresetId{"custom"}, "Custom", "Current manually edited processing.", false,
            1U, 1U, lumora::processing::defaultPipeline()},
    };
}

[[nodiscard]] PresetRepository makeRepository() {
    auto result = PresetRepository::create(shippedPresets());
    if (!result.hasValue()) {
        throw std::runtime_error(result.error().code + ": " + result.error().diagnosticDetail);
    }
    return std::move(result).value();
}

void expectPipelineExactly(const PipelineDefinition& actual,
    const PipelineDefinition& expected) {
    EXPECT_TRUE(lumora::processing::semanticallyEqualPipelineDefinitions(actual, expected));
    EXPECT_EQ(actual.version.configurationRevision, expected.version.configurationRevision);
}

void expectPresetExactly(const Preset& actual, const Preset& expected) {
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.name, expected.name);
    EXPECT_EQ(actual.description, expected.description);
    EXPECT_EQ(actual.builtIn, expected.builtIn);
    EXPECT_EQ(actual.schemaVersion, expected.schemaVersion);
    EXPECT_EQ(actual.revision, expected.revision);
    expectPipelineExactly(actual.pipeline, expected.pipeline);
}

void expectStateExactly(const PresetState& actual, const PresetState& expected) {
    EXPECT_EQ(actual.selectedId, expected.selectedId);
    expectPipelineExactly(actual.activePipeline, expected.activePipeline);
    ASSERT_EQ(actual.customPresets.size(), expected.customPresets.size());
    for (std::size_t index = 0U; index < expected.customPresets.size(); ++index) {
        expectPresetExactly(actual.customPresets[index], expected.customPresets[index]);
    }
}

template<typename ResultType>
void expectConfigurationFailure(const ResultType& result) {
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, ErrorCategory::Configuration);
}

[[nodiscard]] PipelineDefinition pipelineWithContrast(
    double contrast, std::uint64_t configurationRevision = 0U) {
    auto definition = lumora::processing::standardPipeline();
    definition.version.configurationRevision = configurationRevision;
    std::get<BrightnessContrastParameters>(definition.stages[2].parameters).contrast = contrast;
    return definition;
}

[[nodiscard]] Preset customPreset(
    std::string id, std::string name, PipelineDefinition pipeline, std::uint64_t revision = 1U) {
    return {PresetId{std::move(id)}, std::move(name), "", false,
        1U, revision, std::move(pipeline)};
}

[[nodiscard]] const Preset& presetWithId(
    const std::vector<Preset>& presets, std::string_view id) {
    const auto found = std::find_if(presets.begin(), presets.end(), [&](const Preset& preset) {
        return preset.id.value == id;
    });
    if (found == presets.end()) {
        throw std::runtime_error("missing preset " + std::string(id));
    }
    return *found;
}

// Returning borrowed recipes, applying only the selected ID, or allowing callers
// to mutate repository-owned state breaks this end-to-end repository contract.
TEST(PresetRepository, CreatesFindsAppliesAndReturnsOwnedPresetState) {
    auto repository = makeRepository();

    auto listed = repository.list();
    ASSERT_EQ(listed.size(), 5U);
    EXPECT_TRUE(presetWithId(listed, "original").builtIn);
    EXPECT_TRUE(presetWithId(listed, "standard").builtIn);
    EXPECT_TRUE(presetWithId(listed, "high-contrast").builtIn);
    EXPECT_TRUE(presetWithId(listed, "soft-detail").builtIn);
    EXPECT_FALSE(presetWithId(listed, "custom").builtIn);
    expectPipelineExactly(
        presetWithId(listed, "standard").pipeline,
        lumora::processing::standardPipeline());
    EXPECT_TRUE(lumora::application::validatePresetPipeline(
        presetWithId(listed, "high-contrast").pipeline).hasValue());
    EXPECT_TRUE(lumora::application::validatePresetPipeline(
        presetWithId(listed, "soft-detail").pipeline).hasValue());

    auto foundStandard = repository.find(PresetId{"standard"});
    ASSERT_TRUE(foundStandard.hasValue());
    EXPECT_EQ(foundStandard.value().name, "Standard");
    foundStandard.value().name = "caller mutation";
    std::get<BrightnessContrastParameters>(
        foundStandard.value().pipeline.stages[2].parameters).contrast = 3.5;
    listed.front().name = "list mutation";
    listed.clear();

    auto foundAgain = repository.find(PresetId{"standard"});
    ASSERT_TRUE(foundAgain.hasValue());
    EXPECT_EQ(foundAgain.value().name, "Standard");
    expectPipelineExactly(
        foundAgain.value().pipeline, lumora::processing::standardPipeline());
    EXPECT_EQ(repository.list().size(), 5U);

    auto applied = repository.apply(PresetId{"high-contrast"});
    ASSERT_TRUE(applied.hasValue());
    expectPipelineExactly(applied.value(), highContrastPipeline());
    auto state = repository.snapshot();
    EXPECT_EQ(state.selectedId, PresetId{"high-contrast"});
    expectPipelineExactly(state.activePipeline, highContrastPipeline());

    std::get<ClaheParameters>(applied.value().stages[4].parameters).clipLimit = 39.0;
    state.activePipeline.stages.clear();
    const auto ownedState = repository.snapshot();
    EXPECT_EQ(ownedState.selectedId, PresetId{"high-contrast"});
    expectPipelineExactly(ownedState.activePipeline, highContrastPipeline());

    const auto custom = repository.find(PresetId{"custom"});
    ASSERT_TRUE(custom.hasValue());
    EXPECT_FALSE(custom.value().builtIn);
    expectPipelineExactly(custom.value().pipeline, highContrastPipeline());

    const auto beforeCustomApply = repository.snapshot();
    const auto customApply = repository.apply(PresetId{"custom"});
    ASSERT_TRUE(customApply.hasValue());
    expectPipelineExactly(customApply.value(), beforeCustomApply.activePipeline);
    const auto afterCustomApply = repository.snapshot();
    EXPECT_EQ(afterCustomApply.selectedId, PresetId{"custom"});
    expectPipelineExactly(afterCustomApply.activePipeline,
        beforeCustomApply.activePipeline);
}

// Accepting partial, reordered, mismatched, or invalid disabled stages would let
// persistence retain a definition that cannot represent the complete editor.
TEST(PresetRepository, ValidatesCompletePipelinesAndEveryDisabledStageParameter) {
    EXPECT_TRUE(lumora::application::validatePresetPipeline(
        lumora::processing::defaultPipeline()).hasValue());
    EXPECT_TRUE(lumora::application::validatePresetPipeline(
        lumora::processing::standardPipeline()).hasValue());

    std::vector<PipelineDefinition> invalid;
    auto missingStage = lumora::processing::defaultPipeline();
    missingStage.stages.pop_back();
    invalid.push_back(missingStage);

    auto reordered = lumora::processing::defaultPipeline();
    std::swap(reordered.stages[2], reordered.stages[3]);
    invalid.push_back(reordered);

    auto mismatchedParameters = lumora::processing::defaultPipeline();
    mismatchedParameters.stages[7].parameters =
        lumora::processing::NormalizationParameters{};
    invalid.push_back(mismatchedParameters);

    auto invalidDisabled = lumora::processing::defaultPipeline();
    ASSERT_FALSE(invalidDisabled.stages[4].enabled);
    std::get<ClaheParameters>(invalidDisabled.stages[4].parameters).clipLimit = 0.0;
    invalid.push_back(invalidDisabled);

    auto wrongSchema = lumora::processing::defaultPipeline();
    wrongSchema.version.schemaVersion = 2U;
    invalid.push_back(wrongSchema);

    for (const auto& definition : invalid) {
        expectConfigurationFailure(
            lumora::application::validatePresetPipeline(definition));
    }
}

// Weak shipped validation admits mutable reserved entries or recipes whose IDs
// no longer describe their exact approved values.
TEST(PresetRepository, RejectsMalformedShippedSetsAndCustomPresetEnvelopes) {
    std::vector<std::vector<Preset>> invalidShipped;

    auto missing = shippedPresets();
    missing.pop_back();
    invalidShipped.push_back(missing);

    auto duplicate = shippedPresets();
    duplicate.back().id = PresetId{"standard"};
    invalidShipped.push_back(duplicate);

    auto mutableOriginal = shippedPresets();
    mutableOriginal.front().builtIn = false;
    invalidShipped.push_back(mutableOriginal);

    auto immutableCustom = shippedPresets();
    immutableCustom.back().builtIn = true;
    invalidShipped.push_back(immutableCustom);

    auto wrongOriginal = shippedPresets();
    wrongOriginal[0].pipeline = lumora::processing::standardPipeline();
    invalidShipped.push_back(wrongOriginal);

    auto wrongStandard = shippedPresets();
    wrongStandard[1].pipeline = lumora::processing::defaultPipeline();
    invalidShipped.push_back(wrongStandard);

    auto wrongCustomTemplate = shippedPresets();
    wrongCustomTemplate[4].pipeline = lumora::processing::standardPipeline();
    invalidShipped.push_back(wrongCustomTemplate);

    auto badVersion = shippedPresets();
    badVersion[1].schemaVersion = 2U;
    invalidShipped.push_back(badVersion);

    auto zeroRevision = shippedPresets();
    zeroRevision[2].revision = 0U;
    invalidShipped.push_back(zeroRevision);

    auto blankName = shippedPresets();
    blankName[3].name = " \t ";
    invalidShipped.push_back(blankName);

    for (auto& shipped : invalidShipped) {
        expectConfigurationFailure(PresetRepository::create(std::move(shipped)));
    }

    const auto validCustom = customPreset(
        "user-one", "User One", pipelineWithContrast(1.75));
    EXPECT_TRUE(lumora::application::validateCustomPreset(validCustom).hasValue());

    std::vector<Preset> invalidCustom;
    auto candidate = validCustom;
    candidate.id.value = " \n\t";
    invalidCustom.push_back(candidate);
    candidate = validCustom;
    candidate.name = "  ";
    invalidCustom.push_back(candidate);
    candidate = validCustom;
    candidate.builtIn = true;
    invalidCustom.push_back(candidate);
    candidate = validCustom;
    candidate.schemaVersion = 2U;
    invalidCustom.push_back(candidate);
    candidate = validCustom;
    candidate.revision = 0U;
    invalidCustom.push_back(candidate);
    candidate = validCustom;
    candidate.pipeline.stages.pop_back();
    invalidCustom.push_back(candidate);
    for (const auto& id : {
             "original", "standard", "high-contrast", "soft-detail", "custom"}) {
        candidate = validCustom;
        candidate.id = PresetId{id};
        invalidCustom.push_back(candidate);
    }
    for (const auto& preset : invalidCustom) {
        expectConfigurationFailure(
            lumora::application::validateCustomPreset(preset));
    }
}

// Reclassifying an edit as a built-in loses the user's editing-mode identity;
// rejecting a candidate after mutation corrupts the last valid state.
TEST(PresetRepository, EditsAlwaysSelectCustomAndFailuresPreservePriorState) {
    auto repository = makeRepository();
    ASSERT_TRUE(repository.apply(PresetId{"high-contrast"}).hasValue());

    const auto beforeMissing = repository.snapshot();
    expectConfigurationFailure(repository.apply(PresetId{"missing"}));
    expectStateExactly(repository.snapshot(), beforeMissing);

    auto incomplete = lumora::processing::standardPipeline();
    incomplete.stages.pop_back();
    expectConfigurationFailure(repository.edit(std::move(incomplete)));
    expectStateExactly(repository.snapshot(), beforeMissing);

    auto edited = lumora::processing::standardPipeline();
    edited.version.configurationRevision = 77U;
    ASSERT_TRUE(repository.edit(edited).hasValue());
    const auto afterEdit = repository.snapshot();
    EXPECT_EQ(afterEdit.selectedId, PresetId{"custom"});
    expectPipelineExactly(afterEdit.activePipeline, edited);

    const auto classified = repository.classify(edited);
    ASSERT_TRUE(classified.hasValue());
    EXPECT_EQ(classified.value(), PresetId{"standard"});

    const auto beforeCustomApply = repository.snapshot();
    const auto appliedCustom = repository.apply(PresetId{"custom"});
    ASSERT_TRUE(appliedCustom.hasValue());
    expectPipelineExactly(appliedCustom.value(), edited);
    expectStateExactly(repository.snapshot(), beforeCustomApply);
}

// Classification is a value lookup, while saved/selected identity must remain
// distinct when multiple user entries or a built-in share the same values.
TEST(PresetRepository, ClassifiesByPrecedenceWithoutChangingDuplicateValueIdentity) {
    auto repository = makeRepository();

    auto standardWithRuntimeRevision = lumora::processing::standardPipeline();
    standardWithRuntimeRevision.version.configurationRevision = 91U;
    ASSERT_TRUE(repository.edit(standardWithRuntimeRevision).hasValue());
    ASSERT_TRUE(repository.saveCustom(
        PresetId{"user-standard"}, "User Standard", "").hasValue());

    const auto duplicateValues = pipelineWithContrast(1.75, 12U);
    ASSERT_TRUE(repository.edit(duplicateValues).hasValue());
    ASSERT_TRUE(repository.saveCustom(
        PresetId{"first-copy"}, "First Copy", "first").hasValue());
    ASSERT_TRUE(repository.edit(duplicateValues).hasValue());
    ASSERT_TRUE(repository.saveCustom(
        PresetId{"second-copy"}, "Second Copy", "second").hasValue());

    auto classification = repository.classify(standardWithRuntimeRevision);
    ASSERT_TRUE(classification.hasValue());
    EXPECT_EQ(classification.value(), PresetId{"standard"});
    classification = repository.classify(duplicateValues);
    ASSERT_TRUE(classification.hasValue());
    EXPECT_EQ(classification.value(), PresetId{"first-copy"});
    classification = repository.classify(pipelineWithContrast(1.9));
    ASSERT_TRUE(classification.hasValue());
    EXPECT_EQ(classification.value(), PresetId{"custom"});

    const auto selected = repository.snapshot();
    EXPECT_EQ(selected.selectedId, PresetId{"second-copy"});
    expectPipelineExactly(selected.activePipeline, duplicateValues);

    const auto applied = repository.apply(PresetId{"user-standard"});
    ASSERT_TRUE(applied.hasValue());
    EXPECT_EQ(repository.snapshot().selectedId, PresetId{"user-standard"});
    expectPipelineExactly(applied.value(), standardWithRuntimeRevision);

    auto invalid = duplicateValues;
    invalid.stages.pop_back();
    expectConfigurationFailure(repository.classify(invalid));
}

// Saving must capture current values, update in place, and reject invalid input
// or revision overflow without partially changing selection or recipes.
TEST(PresetRepository, SavesCustomRecipesWithCheckedMonotonicRevisions) {
    auto repository = makeRepository();
    const auto firstValues = pipelineWithContrast(1.75, 15U);
    ASSERT_TRUE(repository.edit(firstValues).hasValue());
    ASSERT_TRUE(repository.saveCustom(
        PresetId{"reader"}, "Reading", "initial").hasValue());

    auto state = repository.snapshot();
    ASSERT_EQ(state.customPresets.size(), 1U);
    EXPECT_EQ(state.selectedId, PresetId{"reader"});
    EXPECT_EQ(state.customPresets[0].revision, 1U);
    EXPECT_EQ(state.customPresets[0].name, "Reading");
    EXPECT_EQ(state.customPresets[0].description, "initial");
    expectPipelineExactly(state.customPresets[0].pipeline, firstValues);

    const auto secondValues = pipelineWithContrast(1.9, 16U);
    ASSERT_TRUE(repository.edit(secondValues).hasValue());
    ASSERT_TRUE(repository.saveCustom(
        PresetId{"reader"}, "Reading Updated", "").hasValue());
    state = repository.snapshot();
    ASSERT_EQ(state.customPresets.size(), 1U);
    EXPECT_EQ(state.customPresets[0].revision, 2U);
    EXPECT_EQ(state.customPresets[0].name, "Reading Updated");
    EXPECT_TRUE(state.customPresets[0].description.empty());
    expectPipelineExactly(state.customPresets[0].pipeline, secondValues);

    for (const auto& id : {
             "original", "standard", "high-contrast", "soft-detail", "custom"}) {
        const auto before = repository.snapshot();
        expectConfigurationFailure(repository.saveCustom(
            PresetId{id}, "Reserved", ""));
        expectStateExactly(repository.snapshot(), before);
    }
    for (const auto& [id, name] : {
             std::pair{"", "Name"}, std::pair{" \t", "Name"},
             std::pair{"new-id", ""}, std::pair{"new-id", " \n"}}) {
        const auto before = repository.snapshot();
        expectConfigurationFailure(repository.saveCustom(
            PresetId{id}, name, ""));
        expectStateExactly(repository.snapshot(), before);
    }

    auto atMaximum = repository.snapshot();
    atMaximum.customPresets[0].revision = std::numeric_limits<std::uint64_t>::max();
    atMaximum.selectedId = PresetId{"reader"};
    atMaximum.activePipeline = atMaximum.customPresets[0].pipeline;
    ASSERT_TRUE(repository.restore(atMaximum).hasValue());
    const auto beforeOverflow = repository.snapshot();
    expectConfigurationFailure(repository.saveCustom(
        PresetId{"reader"}, "Cannot Increment", ""));
    expectStateExactly(repository.snapshot(), beforeOverflow);
}

// Deleting the selected recipe must retain its current image settings while
// moving identity to Custom; deleting another recipe must not disturb either.
TEST(PresetRepository, DeletesCustomRecipesWithoutDiscardingActiveValues) {
    auto repository = makeRepository();
    const auto first = pipelineWithContrast(1.75, 21U);
    const auto second = pipelineWithContrast(1.9, 22U);
    ASSERT_TRUE(repository.edit(first).hasValue());
    ASSERT_TRUE(repository.saveCustom(PresetId{"first"}, "First", "").hasValue());
    ASSERT_TRUE(repository.edit(second).hasValue());
    ASSERT_TRUE(repository.saveCustom(PresetId{"second"}, "Second", "").hasValue());
    ASSERT_TRUE(repository.apply(PresetId{"first"}).hasValue());

    ASSERT_TRUE(repository.deleteCustom(PresetId{"second"}).hasValue());
    auto state = repository.snapshot();
    EXPECT_EQ(state.selectedId, PresetId{"first"});
    expectPipelineExactly(state.activePipeline, first);
    ASSERT_EQ(state.customPresets.size(), 1U);
    EXPECT_EQ(state.customPresets[0].id, PresetId{"first"});

    ASSERT_TRUE(repository.deleteCustom(PresetId{"first"}).hasValue());
    state = repository.snapshot();
    EXPECT_EQ(state.selectedId, PresetId{"custom"});
    expectPipelineExactly(state.activePipeline, first);
    EXPECT_TRUE(state.customPresets.empty());

    const auto beforeMissing = repository.snapshot();
    expectConfigurationFailure(repository.deleteCustom(PresetId{"missing"}));
    expectStateExactly(repository.snapshot(), beforeMissing);
    for (const auto& id : {
             "original", "standard", "high-contrast", "soft-detail", "custom"}) {
        expectConfigurationFailure(repository.deleteCustom(PresetId{id}));
        expectStateExactly(repository.snapshot(), beforeMissing);
    }
}

// Restore must reject the whole candidate on any invalid entry, duplicate ID,
// unknown selection, or selected/value mismatch, while preserving explicit IDs.
TEST(PresetRepository, RestoresStrictSnapshotsAtomicallyAndPreservesExplicitSelection) {
    auto repository = makeRepository();
    const auto duplicateValues = pipelineWithContrast(1.75, 31U);
    PresetState explicitState{
        {customPreset("first-copy", "First Copy", duplicateValues, 4U),
            customPreset("second-copy", "Second Copy", duplicateValues, 7U)},
        PresetId{"second-copy"}, duplicateValues};

    ASSERT_TRUE(repository.restore(explicitState).hasValue());
    expectStateExactly(repository.snapshot(), explicitState);
    const auto classification = repository.classify(duplicateValues);
    ASSERT_TRUE(classification.hasValue());
    EXPECT_EQ(classification.value(), PresetId{"first-copy"});
    EXPECT_EQ(repository.snapshot().selectedId, PresetId{"second-copy"});

    std::vector<PresetState> invalid;
    auto candidate = explicitState;
    candidate.customPresets[1].id = PresetId{"first-copy"};
    invalid.push_back(candidate);
    candidate = explicitState;
    candidate.selectedId = PresetId{"missing"};
    invalid.push_back(candidate);
    candidate = explicitState;
    candidate.activePipeline = pipelineWithContrast(1.9);
    invalid.push_back(candidate);
    candidate = explicitState;
    candidate.customPresets[0].revision = 0U;
    invalid.push_back(candidate);
    candidate = explicitState;
    candidate.activePipeline.stages.pop_back();
    invalid.push_back(candidate);
    candidate = explicitState;
    candidate.selectedId = PresetId{"standard"};
    invalid.push_back(candidate);

    const auto beforeFailures = repository.snapshot();
    for (auto& state : invalid) {
        expectConfigurationFailure(repository.restore(std::move(state)));
        expectStateExactly(repository.snapshot(), beforeFailures);
    }

    auto customState = explicitState;
    customState.selectedId = PresetId{"custom"};
    customState.activePipeline = pipelineWithContrast(1.9, 44U);
    ASSERT_TRUE(repository.restore(customState).hasValue());
    expectStateExactly(repository.snapshot(), customState);

    auto invalidInitialState = explicitState;
    invalidInitialState.selectedId = PresetId{"missing"};
    expectConfigurationFailure(PresetRepository::create(
        shippedPresets(), std::move(invalidInitialState)));
}

}  // namespace
