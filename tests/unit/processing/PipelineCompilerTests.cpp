#include <lumora/processing/PipelineCompiler.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <functional>
#include <limits>
#include <algorithm>

namespace {
using namespace lumora::processing;

TEST(PipelineCompiler, RejectsDuplicateMandatoryNormalization) {
    auto definition = defaultPipeline();
    definition.stages.insert(definition.stages.begin(), definition.stages.front());
    const auto result = PipelineCompiler(stageRegistry()).compile(definition);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "duplicate_normalization");
}

TEST(PipelineCompiler, CompiledCopyRetainsDisabledConfigurationAndOwnsRegistry) {
    auto d = defaultPipeline();
    d.version.configurationRevision = 17;
    d.stages[1].enabled = false;
    auto registry = stageRegistry();
    PipelineCompiler compiler(registry);
    registry.clear();
    auto result = compiler.compile(d);
    ASSERT_TRUE(result.hasValue());
    d.stages.clear();
    EXPECT_EQ(result.value().definition().version.configurationRevision, 17U);
    EXPECT_EQ(result.value().definition().stages.size(), 8U);
    ASSERT_EQ(result.value().enabledStages().size(), 1U);
    EXPECT_EQ(result.value().enabledStages()[0].traits.inputDomain, ImageDomain::SensorNative);
}

TEST(PipelineCompiler, AcceptsEveryCanonicalSubsetOfOptionalStages) {
    for (unsigned mask = 0; mask < 128; ++mask) {
        auto d = defaultPipeline();
        for (std::size_t i = 7; i > 0; --i) {
            if ((mask & (1U << (i - 1))) == 0) d.stages.erase(d.stages.begin() + static_cast<std::ptrdiff_t>(i));
        }
        ASSERT_TRUE(PipelineCompiler(stageRegistry()).compile(d).hasValue()) << mask;
    }
}

TEST(PipelineCompiler, RejectsEveryPairReorderingEvenWhenDisabled) {
    for (std::size_t i = 0; i < 8; ++i) for (std::size_t j = i + 1; j < 8; ++j) {
        auto d = defaultPipeline();
        std::swap(d.stages[i], d.stages[j]);
        EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue()) << i << "," << j;
    }
}

TEST(PipelineCompiler, RejectsMissingOrDisabledNormalization) {
    auto d = defaultPipeline();
    d.stages.erase(d.stages.begin());
    EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
    d = defaultPipeline(); d.stages[0].enabled = false;
    EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
    d.stages.clear();
    EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
}

TEST(PipelineCompiler, RejectsDuplicateIdsAndWrongVariantsAtEveryPosition) {
    for (std::size_t i = 0; i < 8; ++i) {
        auto d = defaultPipeline();
        d.stages.insert(d.stages.begin() + static_cast<std::ptrdiff_t>(i), d.stages[i]);
        EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
        d = defaultPipeline(); d.stages[i].parameters = d.stages[(i + 1) % 8].parameters;
        EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
    }
}

TEST(PipelineCompiler, RejectsUnsupportedVersionsButAcceptsAllRevisionValues) {
    for (auto revision : {std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max()}) {
        auto d = defaultPipeline(); d.version.configurationRevision = revision;
        EXPECT_TRUE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
    }
    for (auto version : {0U, 2U, std::numeric_limits<std::uint32_t>::max()}) {
        auto d = defaultPipeline(); d.version.schemaVersion = version;
        EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
        d = defaultPipeline(); d.version.orderVersion = version;
        EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
    }
}

TEST(PipelineCompiler, ReturnsEveryViolationInStableStageOrder) {
    auto d = defaultPipeline();
    d.version.schemaVersion = 0;
    d.stages[1].parameters = WindowLevelParameters{0, -1};
    d.stages[2].parameters = BrightnessContrastParameters{-2, 5};
    d.stages[3].parameters = GammaParameters{0};
    const auto r = PipelineCompiler(stageRegistry()).compile(d);
    ASSERT_FALSE(r.hasValue());
    ASSERT_EQ(r.error().violations.size(), 6U);
    EXPECT_EQ(r.error().code, "unsupported_schema_version");
    EXPECT_FALSE(r.error().violations[0].stageIndex.has_value());
    EXPECT_EQ(r.error().violations[1].code, "invalid_window");
    EXPECT_EQ(r.error().violations[2].code, "invalid_level");
    EXPECT_EQ(r.error().violations[3].code, "invalid_brightness");
    EXPECT_EQ(r.error().violations[4].code, "invalid_contrast");
    EXPECT_EQ(r.error().violations[5].code, "invalid_gamma");
    EXPECT_EQ(r.error().violations[5].stageIndex, 3U);
}

TEST(PipelineCompiler, ValidatesFloatingBoundsIncludingDisabledStagesAndNonfiniteValues) {
    struct Bound { std::size_t index; double low; double high; std::function<void(StageParameters&, double)> set; };
    const Bound bounds[]{
        {1, 1, 65535, [](auto& p, double v) { std::get<WindowLevelParameters>(p).window = v; }},
        {1, 0, 65535, [](auto& p, double v) { std::get<WindowLevelParameters>(p).level = v; }},
        {2, -1, 1, [](auto& p, double v) { std::get<BrightnessContrastParameters>(p).brightness = v; }},
        {2, 0, 4, [](auto& p, double v) { std::get<BrightnessContrastParameters>(p).contrast = v; }},
        {3, 0.1, 5, [](auto& p, double v) { std::get<GammaParameters>(p).gamma = v; }},
        {4, 0.1, 40, [](auto& p, double v) { std::get<ClaheParameters>(p).clipLimit = v; }},
        {6, 0, 5, [](auto& p, double v) { std::get<SharpenParameters>(p).amount = v; }},
        {6, 0.5, 5, [](auto& p, double v) { std::get<SharpenParameters>(p).radius = v; }},
        {6, 0, 65535, [](auto& p, double v) { std::get<SharpenParameters>(p).threshold = v; }}
    };
    const double infinity = std::numeric_limits<double>::infinity();
    for (const auto& b : bounds) {
        for (double v : {b.low, b.high}) {
            auto d = defaultPipeline(); b.set(d.stages[b.index].parameters, v);
            EXPECT_TRUE(PipelineCompiler(stageRegistry()).compile(d).hasValue()) << b.index << "=" << v;
        }
        for (double v : {std::nextafter(b.low, -infinity), std::nextafter(b.high, infinity), infinity, -infinity, std::numeric_limits<double>::quiet_NaN()}) {
            auto d = defaultPipeline(); b.set(d.stages[b.index].parameters, v);
            EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue()) << b.index << "=" << v;
        }
    }
}

TEST(PipelineCompiler, ValidatesIntegerBoundsAndDenoiseMode) {
    for (auto tiles : {0U, 1U, 2U, 32U, 33U, std::numeric_limits<std::uint32_t>::max()}) {
        auto d = defaultPipeline(); std::get<ClaheParameters>(d.stages[4].parameters).tileGridSize = tiles;
        EXPECT_EQ(PipelineCompiler(stageRegistry()).compile(d).hasValue(), tiles == 2 || tiles == 32);
    }
    for (auto kernel : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U}) {
        for (auto mode : {DenoiseMode::Gaussian, DenoiseMode::Median}) {
            auto d = defaultPipeline(); d.stages[5].parameters = DenoiseParameters{mode, kernel};
            EXPECT_EQ(PipelineCompiler(stageRegistry()).compile(d).hasValue(), kernel == 3 || kernel == 5 || kernel == 7);
        }
    }
    auto d = defaultPipeline(); std::get<DenoiseParameters>(d.stages[5].parameters).mode = static_cast<DenoiseMode>(99);
    EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
}

TEST(PipelineCompiler, RejectsUnknownStageAndRegistryEntries) {
    auto d = defaultPipeline(); d.stages[1].id = static_cast<StageId>(99);
    EXPECT_FALSE(PipelineCompiler(stageRegistry()).compile(d).hasValue());
    auto registry = stageRegistry(); registry.pop_back();
    EXPECT_FALSE(PipelineCompiler(registry).compile(defaultPipeline()).hasValue());
    registry = stageRegistry(); registry.push_back(registry.front());
    EXPECT_FALSE(PipelineCompiler(registry).compile(defaultPipeline()).hasValue());
    registry = stageRegistry(); registry[0].backend = static_cast<ExecutionBackend>(99);
    EXPECT_FALSE(PipelineCompiler(registry).compile(defaultPipeline()).hasValue());
}

TEST(PipelineCompiler, ReportsUnknownAndDuplicateViolationsForRepeatedUnknownIds) {
    auto definition = defaultPipeline();
    const StageDefinition unknown{static_cast<StageId>(99), false, InvertParameters{}};
    definition.stages.push_back(unknown);
    definition.stages.push_back(unknown);

    const auto result = PipelineCompiler(stageRegistry()).compile(definition);

    ASSERT_FALSE(result.hasValue());
    ASSERT_EQ(result.error().violations.size(), 3U);
    EXPECT_EQ(result.error().violations[0].stageIndex, 8U);
    EXPECT_EQ(result.error().violations[0].code, "unknown_stage");
    EXPECT_EQ(result.error().violations[1].stageIndex, 9U);
    EXPECT_EQ(result.error().violations[1].code, "unknown_stage");
    EXPECT_EQ(result.error().violations[2].stageIndex, 9U);
    EXPECT_EQ(result.error().violations[2].code, "duplicate_stage");
}

TEST(PipelineCompiler, RejectsInvalidDomainsIncludingInitialAndFinalDomain) {
    for (std::size_t i = 0; i < 8; ++i) {
        for (bool input : {false, true}) {
            auto registry = stageRegistry();
            auto& domain = input ? registry[i].inputDomain : registry[i].outputDomain;
            domain = static_cast<ImageDomain>(99);
            EXPECT_FALSE(PipelineCompiler(registry).compile(defaultPipeline()).hasValue());
            registry = stageRegistry();
            auto& flipped = input ? registry[i].inputDomain : registry[i].outputDomain;
            flipped = flipped == ImageDomain::SensorNative ? ImageDomain::CanonicalU16 : ImageDomain::SensorNative;
            auto d = defaultPipeline(); for (auto& s : d.stages) s.enabled = true;
            EXPECT_FALSE(PipelineCompiler(registry).compile(d).hasValue()) << i << input;
        }
    }
}

TEST(PipelineCompiler, RetainsAllEnabledStagesAndTheirMetadataInExecutionOrder) {
    auto d = defaultPipeline();
    for (auto& stage : d.stages) stage.enabled = true;
    auto registry = stageRegistry();
    registry[3].scratchImages = 2;
    const auto result = PipelineCompiler(registry).compile(d);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().enabledStages().size(), 8U);
    EXPECT_EQ(result.value().enabledStages()[3].definition.id, StageId::Gamma);
    EXPECT_EQ(result.value().enabledStages()[3].traits.scratchImages, 2U);
    EXPECT_EQ(result.value().enabledStages()[7].definition.id, StageId::Invert);
}

TEST(PipelineCompiler, AttributesFinalDomainViolationToLastEnabledStageBeforeLaterDisabledErrors) {
    auto d = defaultPipeline();
    d.stages[7].parameters = GammaParameters{0};
    auto registry = stageRegistry();
    registry[1].outputDomain = ImageDomain::SensorNative;
    const auto result = PipelineCompiler(registry).compile(d);
    ASSERT_FALSE(result.hasValue());
    ASSERT_EQ(result.error().violations.size(), 3U);
    EXPECT_EQ(result.error().violations[0].code, "invalid_output_domain");
    EXPECT_EQ(result.error().violations[0].stageIndex, 1U);
    EXPECT_EQ(result.error().violations[1].code, "parameter_type_mismatch");
    EXPECT_EQ(result.error().violations[1].stageIndex, 7U);
    EXPECT_EQ(result.error().violations[2].code, "invalid_gamma");
}
} // namespace
