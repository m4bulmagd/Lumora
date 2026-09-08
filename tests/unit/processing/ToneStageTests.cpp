#include <lumora/processing/ToneStages.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
using lumora::core::BitAlignment;
using lumora::core::ImageLayout;
using lumora::core::SourcePacking;
using lumora::core::SourcePixelFormat;
using lumora::core::StorageType;
using lumora::processing::BrightnessContrastParameters;
using lumora::processing::BrightnessContrastStage;
using lumora::processing::GammaParameters;
using lumora::processing::GammaStage;
using lumora::processing::IProcessingStage;
using lumora::processing::ImageDomain;
using lumora::processing::ImageView;
using lumora::processing::InvertStage;
using lumora::processing::MutableImageView;
using lumora::processing::StageId;

[[nodiscard]] SourcePixelFormat canonicalSourceFormat() {
    return {"CanonicalU16", 0U, 16U, 65535U, SourcePacking::Unpacked,
        BitAlignment::LeastSignificant, StorageType::UInt16};
}

[[nodiscard]] std::vector<std::uint16_t> canonicalDomain() {
    std::vector<std::uint16_t> values(65536U);
    for (std::uint32_t value = 0U; value <= 65535U; ++value) {
        values[value] = static_cast<std::uint16_t>(value);
    }
    return values;
}

[[nodiscard]] std::vector<std::uint16_t> run(
    const IProcessingStage& stage,
    std::span<const std::uint16_t> input) {
    std::vector<std::byte> sourceBytes(input.size_bytes());
    std::vector<std::byte> destinationBytes(input.size_bytes(), std::byte{0xA5});
    std::memcpy(sourceBytes.data(), input.data(), input.size_bytes());
    const auto layout = ImageLayout::create(static_cast<std::uint32_t>(input.size()),
        1U, input.size_bytes(), StorageType::UInt16, input.size_bytes()).value();
    const auto source = ImageView::create(
        layout, sourceBytes, ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(
        layout, destinationBytes, ImageDomain::CanonicalU16).value();
    const auto result = stage.process(source, destination, canonicalSourceFormat());
    if (!result.hasValue()) {
        ADD_FAILURE() << result.error().code << ": " << result.error().diagnosticDetail;
        return {};
    }

    std::vector<std::uint16_t> output(input.size());
    std::memcpy(output.data(), destinationBytes.data(), destinationBytes.size());
    return output;
}

// Independent exact oracle for the dyadic brightness/contrast cases below.
[[nodiscard]] std::uint16_t rationalToneOracle(
    std::uint16_t input,
    std::int64_t brightnessOffset,
    std::int64_t contrastNumerator,
    std::int64_t contrastDenominator) {
    const auto brightened = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(input) + brightnessOffset, 0, 65535);
    const auto denominator = 2 * contrastDenominator;
    const auto numerator = (2 * brightened - 65535) * contrastNumerator
        + 65535 * contrastDenominator;
    if (numerator <= 0) return 0U;
    if (numerator >= 65535 * denominator) return 65535U;
    return static_cast<std::uint16_t>((numerator + denominator / 2) / denominator);
}

[[nodiscard]] std::uint16_t exactGammaHalfOracle(std::uint16_t input) {
    const auto squared = static_cast<std::uint64_t>(input) * input;
    return static_cast<std::uint16_t>((squared + 32767U) / 65535U);
}

[[nodiscard]] std::uint16_t scalarGammaOracle(std::uint16_t input, double gamma) {
    if (input == 0U) return 0U;
    if (input == 65535U) return 65535U;
    const auto normalized = static_cast<double>(input) / 65535.0;
    const auto scaled = std::pow(normalized, 1.0 / gamma) * 65535.0;
    return static_cast<std::uint16_t>(std::floor(scaled + 0.5));
}

TEST(ToneStages, BrightnessThenContrastUsesSequentialSaturationAndSpecifiedRounding) {
    constexpr std::array<std::uint16_t, 6> input{
        0U, 1U, 32767U, 32768U, 60000U, 65535U};
    EXPECT_EQ(run(BrightnessContrastStage({.brightness = 0.5, .contrast = 0.5}), input),
        (std::vector<std::uint16_t>{32768U, 32768U, 49151U, 49151U, 49151U, 49151U}));
    EXPECT_EQ(run(BrightnessContrastStage({.brightness = -0.5, .contrast = 1.0}), input),
        (std::vector<std::uint16_t>{0U, 0U, 0U, 0U, 27232U, 32767U}));
    EXPECT_EQ(run(BrightnessContrastStage({.brightness = 0.0, .contrast = 0.0}), input),
        (std::vector<std::uint16_t>(input.size(), 32768U)));
}

TEST(ToneStages, BrightnessContrastMatchesIndependentRationalOracleForEveryValue) {
    struct Case final {
        BrightnessContrastParameters parameters;
        std::int64_t offset;
        std::int64_t contrastNumerator;
        std::int64_t contrastDenominator;
    };
    constexpr std::array<Case, 10> cases{{
        {{-1.0, 1.0}, -65535, 1, 1},
        {{-0.5, 0.5}, -32768, 1, 2},
        {{0.0, 0.0}, 0, 0, 1},
        {{0.0, 0.5}, 0, 1, 2},
        {{0.0, 1.0}, 0, 1, 1},
        {{0.0, 2.0}, 0, 2, 1},
        {{0.0, 4.0}, 0, 4, 1},
        {{0.5, 0.5}, 32768, 1, 2},
        {{0.5, 2.0}, 32768, 2, 1},
        {{1.0, 4.0}, 65535, 4, 1},
    }};
    const auto input = canonicalDomain();
    for (const auto& testCase : cases) {
        const auto output = run(BrightnessContrastStage(testCase.parameters), input);
        ASSERT_EQ(output.size(), input.size());
        for (std::uint32_t value = 0U; value <= 65535U; ++value) {
            EXPECT_EQ(output[value], rationalToneOracle(static_cast<std::uint16_t>(value),
                testCase.offset, testCase.contrastNumerator,
                testCase.contrastDenominator))
                << "brightness=" << testCase.parameters.brightness
                << ", contrast=" << testCase.parameters.contrast
                << ", input=" << value;
        }
    }
}

TEST(ToneStages, GammaPreservesEndpointsDirectionAndFixedReviewedValues) {
    constexpr std::array<std::uint16_t, 6> input{
        0U, 1U, 16384U, 32768U, 65534U, 65535U};
    EXPECT_EQ(run(GammaStage({.gamma = 0.5}), input),
        (std::vector<std::uint16_t>{0U, 0U, 4096U, 16384U, 65533U, 65535U}));
    EXPECT_EQ(run(GammaStage({.gamma = 2.0}), input),
        (std::vector<std::uint16_t>{0U, 256U, 32768U, 46341U, 65534U, 65535U}));
}

TEST(ToneStages, GammaHalfMatchesIndependentIntegerOracleForEveryValue) {
    const auto input = canonicalDomain();
    const auto output = run(GammaStage({.gamma = 0.5}), input);
    ASSERT_EQ(output.size(), input.size());
    for (std::uint32_t value = 0U; value <= 65535U; ++value) {
        EXPECT_EQ(output[value], exactGammaHalfOracle(static_cast<std::uint16_t>(value)))
            << "input=" << value;
    }
}

TEST(ToneStages, GammaIdentityIsExactForEveryValue) {
    const auto input = canonicalDomain();
    EXPECT_EQ(run(GammaStage({.gamma = 1.0}), input), input);
}

TEST(ToneStages, GammaMatchesScalarReferenceAtBoundariesAndInteriorValues) {
    const auto input = canonicalDomain();
    for (const auto gamma : {0.1, 1.7, 2.0, 5.0}) {
        const auto output = run(GammaStage({.gamma = gamma}), input);
        ASSERT_EQ(output.size(), input.size());
        for (std::uint32_t value = 0U; value <= 65535U; ++value) {
            EXPECT_EQ(output[value], scalarGammaOracle(
                static_cast<std::uint16_t>(value), gamma))
                << "gamma=" << gamma << ", input=" << value;
        }
    }
}

TEST(ToneStages, ConfiguredGammaInstanceReusesOwnedLutAcrossDifferentCalls) {
    const GammaStage stage({.gamma = 0.5});
    constexpr std::array<std::uint16_t, 4> firstInput{0U, 16384U, 32768U, 65535U};
    constexpr std::array<std::uint16_t, 5> secondInput{1U, 2U, 3U, 65534U, 60000U};
    EXPECT_EQ(run(stage, firstInput),
        (std::vector<std::uint16_t>{0U, 4096U, 16384U, 65535U}));
    EXPECT_EQ(run(stage, secondInput),
        (std::vector<std::uint16_t>{0U, 0U, 0U, 65533U, 54932U}));

    const GammaStage differentStage({.gamma = 2.0});
    EXPECT_EQ(run(differentStage, firstInput),
        (std::vector<std::uint16_t>{0U, 32768U, 46341U, 65535U}));
}

TEST(ToneStages, InvertIsExactForEveryCanonicalValue) {
    const auto input = canonicalDomain();
    const auto output = run(InvertStage{}, input);
    ASSERT_EQ(output.size(), input.size());
    for (std::uint32_t value = 0U; value <= 65535U; ++value) {
        EXPECT_EQ(output[value], static_cast<std::uint16_t>(65535U - value))
            << "input=" << value;
    }
}

void expectPaddedUnalignedResult(
    const IProcessingStage& stage,
    const std::array<std::uint16_t, 6>& expected) {
    constexpr std::size_t sourceStride = 9U;
    constexpr std::size_t destinationStride = 11U;
    std::array<std::byte, 1U + sourceStride * 2U> sourceStorage{};
    std::array<std::byte, 1U + destinationStride * 2U> destinationStorage{};
    sourceStorage.fill(std::byte{0xC3});
    destinationStorage.fill(std::byte{0x5A});
    constexpr std::array<std::uint16_t, 6> samples{
        0U, 1000U, 30000U, 40000U, 60000U, 65535U};
    std::memcpy(sourceStorage.data() + 1U, samples.data(), 6U);
    std::memcpy(sourceStorage.data() + 1U + sourceStride, samples.data() + 3U, 6U);
    const auto sourceBefore = sourceStorage;
    const auto destinationBefore = destinationStorage;
    const auto sourceLayout = ImageLayout::create(
        3U, 2U, sourceStride, StorageType::UInt16, sourceStride * 2U).value();
    const auto destinationLayout = ImageLayout::create(
        3U, 2U, destinationStride, StorageType::UInt16, destinationStride * 2U).value();
    const auto source = ImageView::create(sourceLayout,
        std::span(sourceStorage).subspan(1U), ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(destinationLayout,
        std::span(destinationStorage).subspan(1U), ImageDomain::CanonicalU16).value();

    const auto result = stage.process(source, destination, canonicalSourceFormat());

    ASSERT_TRUE(result.hasValue()) << result.error().diagnosticDetail;
    std::array<std::uint16_t, 6> actual{};
    std::memcpy(actual.data(), destinationStorage.data() + 1U, 6U);
    std::memcpy(actual.data() + 3U,
        destinationStorage.data() + 1U + destinationStride, 6U);
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(sourceStorage, sourceBefore);
    for (std::size_t index = 0U; index < destinationStorage.size(); ++index) {
        const auto rowOffset = index == 0U
            ? destinationStride
            : (index - 1U) % destinationStride;
        if (index == 0U || rowOffset >= 6U) {
            EXPECT_EQ(destinationStorage[index], destinationBefore[index]) << index;
        }
    }
}

TEST(ToneStages, EveryStageHandlesDistinctPaddedUnalignedStridesWithoutMutation) {
    expectPaddedUnalignedResult(
        BrightnessContrastStage({.brightness = 0.5, .contrast = 0.5}),
        {32768U, 33268U, 47768U, 49151U, 49151U, 49151U});
    expectPaddedUnalignedResult(GammaStage({.gamma = 0.5}),
        {0U, 15U, 13733U, 24414U, 54932U, 65535U});
    expectPaddedUnalignedResult(InvertStage{},
        {65535U, 64535U, 35535U, 25535U, 5535U, 0U});
}

// Copying payloads or strides for either neutral stage corrupts the destination canaries.
TEST(ToneStages, NeutralBrightnessContrastAndGammaCopyUnalignedRowsWithUnequalStrides) {
    constexpr std::array<std::uint16_t, 6> samples{
        0U, 1000U, 30000U, 40000U, 60000U, 65535U};
    expectPaddedUnalignedResult(
        BrightnessContrastStage({.brightness = 0.0, .contrast = 1.0}), samples);
    expectPaddedUnalignedResult(GammaStage({.gamma = 1.0}), samples);
}

void expectInvalidParameterFailure(
    const IProcessingStage& stage,
    std::string expectedCode) {
    constexpr std::array<std::uint16_t, 2> samples{100U, 200U};
    const auto layout = ImageLayout::create(
        2U, 1U, 4U, StorageType::UInt16, 4U).value();
    const auto source = ImageView::create(layout, std::as_bytes(std::span(samples)),
        ImageDomain::CanonicalU16).value();
    std::array<std::byte, 4> destinationStorage{};
    destinationStorage.fill(std::byte{0x7E});
    const auto before = destinationStorage;
    const auto destination = MutableImageView::create(
        layout, destinationStorage, ImageDomain::CanonicalU16).value();
    const auto result = stage.process(source, destination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, expectedCode);
    EXPECT_EQ(destinationStorage, before);
}

TEST(ToneStages, BrightnessContrastRejectsEveryNonFiniteAndOutOfRangeParameter) {
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto parameters : {
             BrightnessContrastParameters{-1.0000001, 1.0},
             BrightnessContrastParameters{1.0000001, 1.0},
             BrightnessContrastParameters{infinity, 1.0},
             BrightnessContrastParameters{-infinity, 1.0},
             BrightnessContrastParameters{nan, 1.0},
             BrightnessContrastParameters{0.0, -0.0000001},
             BrightnessContrastParameters{0.0, 4.0000001},
             BrightnessContrastParameters{0.0, infinity},
             BrightnessContrastParameters{0.0, -infinity},
             BrightnessContrastParameters{0.0, nan},
         }) {
        expectInvalidParameterFailure(BrightnessContrastStage(parameters),
            "brightness_contrast_invalid_parameters");
    }
}

TEST(ToneStages, GammaRejectsEveryNonFiniteAndOutOfRangeParameter) {
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto gamma : {0.0999999, 5.0000001, infinity, -infinity, nan}) {
        expectInvalidParameterFailure(
            GammaStage({.gamma = gamma}), "gamma_invalid_parameters");
    }
}

void expectCommonViewGuards(
    const IProcessingStage& stage,
    const std::string& prefix) {
    std::array<std::byte, 32> sourceStorage{};
    std::array<std::byte, 16> destinationStorage{};
    destinationStorage.fill(std::byte{0x6B});
    const auto before = destinationStorage;
    const auto u16Layout = ImageLayout::create(
        2U, 2U, 4U, StorageType::UInt16, 8U).value();
    const auto u8Layout = ImageLayout::create(
        2U, 2U, 2U, StorageType::UInt8, 4U).value();
    const auto goodSource = ImageView::create(u16Layout, sourceStorage,
        ImageDomain::CanonicalU16).value();
    const auto goodDestination = MutableImageView::create(u16Layout,
        destinationStorage, ImageDomain::CanonicalU16).value();
    const auto u8Source = ImageView::create(
        u8Layout, sourceStorage, ImageDomain::SensorNative).value();
    auto result = stage.process(u8Source, goodDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, prefix + "_source_storage_mismatch");
    EXPECT_EQ(destinationStorage, before);

    const auto u8Destination = MutableImageView::create(
        u8Layout, destinationStorage, ImageDomain::SensorNative).value();
    result = stage.process(goodSource, u8Destination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, prefix + "_destination_storage_mismatch");
    EXPECT_EQ(destinationStorage, before);

    const auto sensorSource = ImageView::create(
        u16Layout, sourceStorage, ImageDomain::SensorNative).value();
    result = stage.process(sensorSource, goodDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, prefix + "_image_domain_mismatch");
    EXPECT_EQ(destinationStorage, before);

    const auto sensorDestination = MutableImageView::create(
        u16Layout, destinationStorage, ImageDomain::SensorNative).value();
    result = stage.process(goodSource, sensorDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, prefix + "_image_domain_mismatch");
    EXPECT_EQ(destinationStorage, before);

    const auto narrowLayout = ImageLayout::create(
        1U, 2U, 2U, StorageType::UInt16, 4U).value();
    const auto narrowDestination = MutableImageView::create(narrowLayout,
        destinationStorage, ImageDomain::CanonicalU16).value();
    result = stage.process(goodSource, narrowDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, prefix + "_image_extent_mismatch");
    EXPECT_EQ(destinationStorage, before);
}

TEST(ToneStages, EveryStageRejectsStorageDomainAndExtentBeforeWriting) {
    expectCommonViewGuards(BrightnessContrastStage{}, "brightness_contrast");
    expectCommonViewGuards(GammaStage{}, "gamma");
    expectCommonViewGuards(InvertStage{}, "invert");
}

void expectOverlapRejected(
    const IProcessingStage& stage,
    const std::string& prefix,
    std::size_t sourceOffset,
    std::size_t destinationOffset) {
    std::array<std::byte, 48> storage{};
    for (std::size_t index = 0U; index < storage.size(); ++index) {
        storage[index] = static_cast<std::byte>(index);
    }
    const auto before = storage;
    const auto layout = ImageLayout::create(
        2U, 2U, 8U, StorageType::UInt16, 16U).value();
    const auto source = ImageView::create(layout,
        std::span(storage).subspan(sourceOffset), ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(layout,
        std::span(storage).subspan(destinationOffset), ImageDomain::CanonicalU16).value();
    const auto result = stage.process(source, destination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, prefix + "_image_views_overlap");
    EXPECT_EQ(storage, before);
}

void expectEveryOverlapRejected(
    const IProcessingStage& stage,
    const std::string& prefix) {
    expectOverlapRejected(stage, prefix, 8U, 8U);   // exact alias
    expectOverlapRejected(stage, prefix, 8U, 9U);   // destination starts inside source
    expectOverlapRejected(stage, prefix, 9U, 8U);   // source starts inside destination
    expectOverlapRejected(stage, prefix, 8U, 12U);  // only declared padding intersects
    expectOverlapRejected(stage, prefix, 12U, 8U);  // reverse padding intersection
}

TEST(ToneStages, EveryStageRejectsExactPartialAndPaddingOverlapBeforeWriting) {
    expectEveryOverlapRejected(BrightnessContrastStage{}, "brightness_contrast");
    expectEveryOverlapRejected(GammaStage{}, "gamma");
    expectEveryOverlapRejected(InvertStage{}, "invert");
}

TEST(ToneStages, EveryStageReportsCanonicalTraitsAndStableId) {
    const BrightnessContrastStage brightnessContrast;
    const GammaStage gamma;
    const InvertStage invert;
    const std::array<std::pair<const IProcessingStage*, StageId>, 3> stages{{
        {&brightnessContrast, StageId::BrightnessContrast},
        {&gamma, StageId::Gamma},
        {&invert, StageId::Invert},
    }};
    for (const auto& [stage, id] : stages) {
        EXPECT_EQ(stage->id(), id);
        EXPECT_EQ(stage->traits().id, id);
        EXPECT_EQ(stage->traits().inputDomain, ImageDomain::CanonicalU16);
        EXPECT_EQ(stage->traits().outputDomain, ImageDomain::CanonicalU16);
        EXPECT_FALSE(stage->traits().changesDimensions);
        EXPECT_EQ(stage->traits().scratchImages, 0U);
        EXPECT_EQ(stage->traits().historyFrames, 0U);
        EXPECT_FALSE(stage->traits().requiresCalibrationAsset);
    }
}

}  // namespace
