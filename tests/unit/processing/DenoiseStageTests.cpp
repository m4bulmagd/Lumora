#include <lumora/processing/DenoiseStage.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {

using namespace lumora;
using processing::DenoiseMode;
using processing::DenoiseParameters;
using processing::DenoiseStage;
using processing::ImageDomain;
using processing::ImageView;
using processing::MutableImageView;

[[nodiscard]] core::ImageLayout layout(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t stride = 0U,
    core::StorageType storage = core::StorageType::UInt16,
    std::size_t payload = 0U) {
    const auto bytesPerPixel = storage == core::StorageType::UInt16 ? 2U : 1U;
    if (stride == 0U) stride = static_cast<std::size_t>(width) * bytesPerPixel;
    if (payload == 0U) payload = stride * static_cast<std::size_t>(height);
    return core::ImageLayout::create(width, height, stride, storage, payload).value();
}

[[nodiscard]] core::SourcePixelFormat canonicalFormat(
    std::uint8_t validBits = 16U,
    std::uint16_t sampleMaximum = 65535U) {
    return {"CanonicalU16", 0U, validBits, sampleMaximum,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt16};
}

[[nodiscard]] std::unique_ptr<DenoiseStage> createStage(
    std::uint32_t width,
    std::uint32_t height,
    DenoiseParameters parameters) {
    auto result = DenoiseStage::create(parameters, layout(width, height));
    EXPECT_TRUE(result.hasValue())
        << (result.hasValue() ? "" : result.error().code);
    return result.hasValue() ? std::move(result).value() : nullptr;
}

[[nodiscard]] std::vector<std::uint16_t> runTight(
    DenoiseStage& stage,
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint16_t> input,
    const core::SourcePixelFormat& format = canonicalFormat()) {
    const auto imageLayout = layout(width, height);
    std::vector<std::byte> source(input.size_bytes());
    std::memcpy(source.data(), input.data(), input.size_bytes());
    const auto sourceBefore = source;
    std::vector<std::byte> destination(input.size_bytes(), std::byte{0xA5});
    const auto sourceView = ImageView::create(
        imageLayout, source, ImageDomain::CanonicalU16).value();
    const auto destinationView = MutableImageView::create(
        imageLayout, destination, ImageDomain::CanonicalU16).value();
    const auto result = stage.process(sourceView, destinationView, format);
    EXPECT_TRUE(result.hasValue())
        << (result.hasValue() ? "" : result.error().code);
    EXPECT_EQ(source, sourceBefore);
    std::vector<std::uint16_t> output(input.size());
    if (result.hasValue()) std::memcpy(output.data(), destination.data(), destination.size());
    return output;
}

TEST(DenoiseStage, GaussianThreeUsesIndependentImpulseOracle) {
    constexpr std::uint32_t width = 5U;
    constexpr std::uint32_t height = 5U;
    std::array<std::uint16_t, width * height> input{};
    input[2U * width + 2U] = 256U;
    constexpr std::array<std::uint16_t, width * height> expected{
        0U, 0U, 0U, 0U, 0U,
        0U, 16U, 32U, 16U, 0U,
        0U, 32U, 64U, 32U, 0U,
        0U, 16U, 32U, 16U, 0U,
        0U, 0U, 0U, 0U, 0U};
    auto stage = createStage(width, height, {DenoiseMode::Gaussian, 3U, 0.0});
    ASSERT_NE(stage, nullptr);

    EXPECT_EQ(runTight(*stage, width, height, input),
        (std::vector<std::uint16_t>(expected.begin(), expected.end())));
}

TEST(DenoiseStage, GaussianUsesReflect101AndExplicitSigma) {
    constexpr std::array<std::uint16_t, 3> borderInput{0U, 100U, 400U};
    constexpr std::array<std::uint16_t, 3> borderExpected{50U, 150U, 250U};
    auto automatic = createStage(3U, 1U, {DenoiseMode::Gaussian, 3U, 0.0});
    ASSERT_NE(automatic, nullptr);
    EXPECT_EQ(runTight(*automatic, 3U, 1U, borderInput),
        (std::vector<std::uint16_t>(borderExpected.begin(), borderExpected.end())));

    constexpr std::array<std::uint16_t, 7> sigmaInput{0U, 0U, 0U, 600U, 0U, 0U, 0U};
    constexpr std::array<std::uint16_t, 7> sigmaExpected{0U, 0U, 100U, 400U, 100U, 0U, 0U};
    const auto sigma = 1.0 / std::sqrt(2.0 * std::log(4.0));
    auto explicitSigma = createStage(7U, 1U, {DenoiseMode::Gaussian, 3U, sigma});
    ASSERT_NE(explicitSigma, nullptr);
    EXPECT_EQ(runTight(*explicitSigma, 7U, 1U, sigmaInput),
        (std::vector<std::uint16_t>(sigmaExpected.begin(), sigmaExpected.end())));
}

TEST(DenoiseStage, GaussianPreservesConstantsAndSingletonAxes) {
    for (const auto parameters : {
             DenoiseParameters{DenoiseMode::Gaussian, 3U, 0.0},
             DenoiseParameters{DenoiseMode::Gaussian, 5U, 5.0},
             DenoiseParameters{DenoiseMode::Gaussian, 7U, 1.25}}) {
        for (const auto value : {std::uint16_t{0U}, std::uint16_t{1001U},
                 std::uint16_t{65535U}}) {
            constexpr std::uint32_t width = 1U;
            constexpr std::uint32_t height = 3U;
            const std::array input{value, value, value};
            auto stage = createStage(width, height, parameters);
            ASSERT_NE(stage, nullptr);
            EXPECT_EQ(runTight(*stage, width, height, input),
                (std::vector<std::uint16_t>(input.begin(), input.end())));
        }
    }
}

TEST(DenoiseStage, MedianRemovesHighAndLowImpulsesForBothKernels) {
    constexpr std::uint32_t side = 7U;
    for (const auto kernel : {3U, 5U}) {
        for (const auto impulse : {std::uint16_t{0U}, std::uint16_t{65535U}}) {
            std::array<std::uint16_t, side * side> input{};
            input.fill(1000U);
            input[3U * side + 3U] = impulse;
            auto stage = createStage(side, side, {DenoiseMode::Median, kernel, 0.0});
            ASSERT_NE(stage, nullptr);
            const auto output = runTight(*stage, side, side, input);
            EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](std::uint16_t value) {
                return value == 1000U;
            }));
        }
    }
    std::array<std::uint16_t, 9> highConstant{};
    highConstant.fill(40001U);
    auto highStage = createStage(3U, 3U, {DenoiseMode::Median, 5U, 0.0});
    ASSERT_NE(highStage, nullptr);
    EXPECT_EQ(runTight(*highStage, 3U, 3U, highConstant),
        (std::vector<std::uint16_t>(highConstant.begin(), highConstant.end())));
}

TEST(DenoiseStage, MedianReflectsRepeatedlyAcrossTinyRows) {
    constexpr std::array<std::uint16_t, 3> three{0U, 100U, 400U};
    constexpr std::array<std::uint16_t, 3> expectedThree{100U, 100U, 100U};
    auto kernelThree = createStage(3U, 1U, {DenoiseMode::Median, 3U, 0.0});
    ASSERT_NE(kernelThree, nullptr);
    EXPECT_EQ(runTight(*kernelThree, 3U, 1U, three),
        (std::vector<std::uint16_t>(expectedThree.begin(), expectedThree.end())));

    constexpr std::array<std::uint16_t, 2> two{10U, 100U};
    constexpr std::array<std::uint16_t, 2> expectedKernelThree{100U, 10U};
    constexpr std::array<std::uint16_t, 2> expectedKernelFive{10U, 100U};
    auto tinyThree = createStage(2U, 1U, {DenoiseMode::Median, 3U, 0.0});
    auto tinyFive = createStage(2U, 1U, {DenoiseMode::Median, 5U, 0.0});
    ASSERT_NE(tinyThree, nullptr);
    ASSERT_NE(tinyFive, nullptr);
    EXPECT_EQ(runTight(*tinyThree, 2U, 1U, two),
        (std::vector<std::uint16_t>(expectedKernelThree.begin(), expectedKernelThree.end())));
    EXPECT_EQ(runTight(*tinyFive, 2U, 1U, two),
        (std::vector<std::uint16_t>(expectedKernelFive.begin(), expectedKernelFive.end())));
}

TEST(DenoiseStage, FactoryValidatesModeKernelSigmaStorageScratchAndBudget) {
    const auto imageLayout = layout(3U, 2U);
    for (const auto parameters : {
             DenoiseParameters{DenoiseMode::Gaussian, 3U, 0.0},
             DenoiseParameters{DenoiseMode::Gaussian, 5U, 5.0},
             DenoiseParameters{DenoiseMode::Gaussian, 7U, 0.5},
             DenoiseParameters{DenoiseMode::Median, 3U, 0.0},
             DenoiseParameters{DenoiseMode::Median, 5U, 0.0}}) {
        EXPECT_TRUE(DenoiseStage::create(parameters, imageLayout).hasValue());
    }
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto sigma : {-0.0001, 5.0001, infinity, -infinity, nan}) {
        const auto result = DenoiseStage::create(
            {DenoiseMode::Gaussian, 3U, sigma}, imageLayout);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "denoise_invalid_sigma");
    }
    for (const auto kernel : {0U, 1U, 2U, 4U, 6U, 8U, 9U}) {
        EXPECT_FALSE(DenoiseStage::create(
            {DenoiseMode::Gaussian, kernel, 0.0}, imageLayout).hasValue());
    }
    EXPECT_FALSE(DenoiseStage::create(
        {DenoiseMode::Median, 7U, 0.0}, imageLayout).hasValue());
    EXPECT_FALSE(DenoiseStage::create(
        {DenoiseMode::Median, 3U, 0.1}, imageLayout).hasValue());
    EXPECT_FALSE(DenoiseStage::create(
        {static_cast<DenoiseMode>(99), 3U, 0.0}, imageLayout).hasValue());
    EXPECT_FALSE(DenoiseStage::create({DenoiseMode::Gaussian, 3U, 0.0},
        layout(3U, 2U, 0U, core::StorageType::UInt8)).hasValue());

    const auto gaussianBytes = DenoiseStage::requiredScratchBytes(
        {DenoiseMode::Gaussian, 3U, 0.0}, imageLayout);
    const auto medianBytes = DenoiseStage::requiredScratchBytes(
        {DenoiseMode::Median, 5U, 0.0}, imageLayout);
    ASSERT_TRUE(gaussianBytes.hasValue());
    ASSERT_TRUE(medianBytes.hasValue());
    EXPECT_EQ(gaussianBytes.value(), 6U * sizeof(double) + 3U * sizeof(double));
    EXPECT_EQ(medianBytes.value(), 0U);
    const auto budgetResult = DenoiseStage::create(
        {DenoiseMode::Gaussian, 3U, 0.0}, imageLayout, gaussianBytes.value() - 1U);
    ASSERT_FALSE(budgetResult.hasValue());
    EXPECT_EQ(budgetResult.error().code, "denoise_scratch_budget_exceeded");

    if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
        constexpr auto width = std::numeric_limits<std::uint32_t>::max();
        constexpr auto height = std::uint32_t{1073741823U};
        const auto stride = static_cast<std::size_t>(width) * 2U;
        const auto huge = layout(width, height, stride, core::StorageType::UInt16,
            stride * static_cast<std::size_t>(height));
        const auto overflow = DenoiseStage::requiredScratchBytes(
            {DenoiseMode::Gaussian, 3U, 0.0}, huge);
        ASSERT_FALSE(overflow.hasValue());
        EXPECT_EQ(overflow.error().code, "denoise_scratch_size_overflow");
    }
}

TEST(DenoiseStage, ReportsOwnedScratchAndCanonicalTraits) {
    auto gaussian = createStage(3U, 2U, {DenoiseMode::Gaussian, 3U, 0.0});
    auto median = createStage(3U, 2U, {DenoiseMode::Median, 5U, 0.0});
    ASSERT_NE(gaussian, nullptr);
    ASSERT_NE(median, nullptr);
    EXPECT_EQ(gaussian->scratchBytes(), 72U);
    EXPECT_EQ(median->scratchBytes(), 0U);
    EXPECT_EQ(gaussian->id(), processing::StageId::Denoise);
    EXPECT_EQ(gaussian->traits().scratchImages, 4U);
    EXPECT_EQ(gaussian->traits().inputDomain, ImageDomain::CanonicalU16);
    const auto registry = processing::stageRegistry();
    const auto entry = std::find_if(registry.begin(), registry.end(), [](const auto& traits) {
        return traits.id == processing::StageId::Denoise;
    });
    ASSERT_NE(entry, registry.end());
    EXPECT_EQ(entry->scratchImages, 4U);
}

TEST(DenoiseStage, SupportsOddStridesUnalignedStartsReuseAndProvenance) {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 3U;
    constexpr std::size_t sourceStride = 11U;
    constexpr std::size_t destinationStride = 13U;
    constexpr std::array<std::uint16_t, width * height> first{
        0U, 100U, 400U, 900U, 1001U, 2000U, 3000U, 4000U,
        65535U, 500U, 600U, 700U};
    constexpr std::array<std::uint16_t, width * height> second{
        9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U, 0U, 1U, 2U};
    auto stage = createStage(width, height, {DenoiseMode::Gaussian, 3U, 0.0});
    ASSERT_NE(stage, nullptr);
    const auto expected = runTight(*stage, width, height, first);
    const auto different = runTight(*stage, width, height, second);
    EXPECT_NE(expected, different);

    std::array<std::byte, 1U + sourceStride * height + 3U> source{};
    std::array<std::byte, 1U + destinationStride * height + 3U> destination{};
    source.fill(std::byte{0xC3});
    destination.fill(std::byte{0x5A});
    for (std::size_t y = 0U; y < height; ++y) {
        std::memcpy(source.data() + 1U + y * sourceStride,
            first.data() + y * width, width * sizeof(std::uint16_t));
    }
    const auto sourceBefore = source;
    const auto destinationBefore = destination;
    const auto sourceView = ImageView::create(layout(width, height, sourceStride),
        std::span(source).subspan(1U), ImageDomain::CanonicalU16).value();
    const auto destinationView = MutableImageView::create(
        layout(width, height, destinationStride), std::span(destination).subspan(1U),
        ImageDomain::CanonicalU16).value();
    const auto result = stage->process(
        sourceView, destinationView, canonicalFormat(10U, 1023U));
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(source, sourceBefore);
    for (std::size_t y = 0U; y < height; ++y) {
        std::array<std::uint16_t, width> row{};
        std::memcpy(row.data(), destination.data() + 1U + y * destinationStride,
            width * sizeof(std::uint16_t));
        EXPECT_TRUE(std::equal(row.begin(), row.end(), expected.begin()
            + static_cast<std::ptrdiff_t>(y * width)));
    }
    for (std::size_t i = 0U; i < destination.size(); ++i) {
        const auto relative = i == 0U ? destinationStride : (i - 1U) % destinationStride;
        if (i == 0U || relative >= width * 2U || i >= 1U + destinationStride * height) {
            EXPECT_EQ(destination[i], destinationBefore[i]) << i;
        }
    }
    EXPECT_EQ(runTight(*stage, width, height, first, canonicalFormat(12U, 4095U)), expected);
}

TEST(DenoiseStage, RejectsInvalidViewsAndAllPayloadOverlapBeforeWriting) {
    auto stage = createStage(2U, 1U, {DenoiseMode::Median, 3U, 0.0});
    ASSERT_NE(stage, nullptr);
    std::array<std::byte, 32> source{};
    std::array<std::byte, 32> destination{};
    destination.fill(std::byte{0x6D});
    const auto before = destination;
    const auto goodLayout = layout(2U, 1U, 6U, core::StorageType::UInt16, 6U);
    const auto goodSource = ImageView::create(
        goodLayout, source, ImageDomain::CanonicalU16).value();
    const auto goodDestination = MutableImageView::create(
        goodLayout, destination, ImageDomain::CanonicalU16).value();

    const auto expectRejected = [&](const ImageView& sourceView,
                                    MutableImageView destinationView) {
        const auto result = stage->process(sourceView, destinationView, canonicalFormat());
        EXPECT_FALSE(result.hasValue());
        EXPECT_EQ(destination, before);
    };
    expectRejected(ImageView::create(
        layout(2U, 1U, 2U, core::StorageType::UInt8, 2U), source,
        ImageDomain::SensorNative).value(), goodDestination);
    expectRejected(goodSource, MutableImageView::create(
        layout(2U, 1U, 2U, core::StorageType::UInt8, 2U), destination,
        ImageDomain::SensorNative).value());
    expectRejected(ImageView::create(
        goodLayout, source, ImageDomain::SensorNative).value(), goodDestination);
    expectRejected(goodSource, MutableImageView::create(
        goodLayout, destination, ImageDomain::SensorNative).value());
    expectRejected(ImageView::create(
        layout(3U, 1U), source, ImageDomain::CanonicalU16).value(), goodDestination);
    const auto largerLayout = layout(3U, 1U);
    expectRejected(ImageView::create(
        largerLayout, source, ImageDomain::CanonicalU16).value(),
        MutableImageView::create(
            largerLayout, destination, ImageDomain::CanonicalU16).value());

    std::array<std::byte, 16> shared{};
    shared.fill(std::byte{0x4B});
    const auto sharedBefore = shared;
    const auto overlappingSource = ImageView::create(goodLayout,
        std::span(shared).subspan(0U, 6U), ImageDomain::CanonicalU16).value();
    for (const auto offset : {0U, 2U, 4U}) {
        const auto overlappingDestination = MutableImageView::create(goodLayout,
            std::span(shared).subspan(offset, 6U), ImageDomain::CanonicalU16).value();
        EXPECT_FALSE(stage->process(overlappingSource, overlappingDestination,
            canonicalFormat()).hasValue());
        EXPECT_EQ(shared, sharedBefore);
    }
}

}  // namespace
