#include <lumora/processing/SharpenStage.hpp>

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
using processing::ImageDomain;
using processing::ImageView;
using processing::MutableImageView;
using processing::SharpenParameters;
using processing::SharpenStage;

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

[[nodiscard]] std::unique_ptr<SharpenStage> createStage(
    std::uint32_t width,
    std::uint32_t height,
    SharpenParameters parameters) {
    auto result = SharpenStage::create(parameters, layout(width, height));
    EXPECT_TRUE(result.hasValue())
        << (result.hasValue() ? "" : result.error().code);
    return result.hasValue() ? std::move(result).value() : nullptr;
}

[[nodiscard]] std::vector<std::uint16_t> runTight(
    SharpenStage& stage,
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

[[nodiscard]] double rationalRadius() {
    return 1.0 / std::sqrt(2.0 * std::log(2.0));
}

TEST(SharpenStage, UsesIndependentRationalStepOracleOnBothAxes) {
    constexpr std::uint32_t length = 15U;
    std::array<std::uint16_t, length> horizontal{};
    horizontal.fill(10000U);
    for (std::size_t x = 8U; x < horizontal.size(); ++x) horizontal[x] = 11090U;
    auto horizontalStage = createStage(length, 1U, {1.0, rationalRadius(), 0.0});
    ASSERT_NE(horizontalStage, nullptr);
    const auto horizontalOutput = runTight(*horizontalStage, length, 1U, horizontal);
    EXPECT_EQ(horizontalOutput[7], 9711U);
    EXPECT_EQ(horizontalOutput[8], 11379U);
    EXPECT_EQ(horizontalOutput.front(), 10000U);
    EXPECT_EQ(horizontalOutput.back(), 11090U);

    auto verticalStage = createStage(1U, length, {1.0, rationalRadius(), 0.0});
    ASSERT_NE(verticalStage, nullptr);
    const auto verticalOutput = runTight(*verticalStage, 1U, length, horizontal);
    EXPECT_EQ(verticalOutput[7], 9711U);
    EXPECT_EQ(verticalOutput[8], 11379U);
}

TEST(SharpenStage, ThresholdUsesStrictGreaterThanAtExactDetail) {
    constexpr std::uint32_t width = 15U;
    std::array<std::uint16_t, width> input{};
    input.fill(10000U);
    for (std::size_t x = 8U; x < input.size(); ++x) input[x] = 11090U;
    for (const auto& [threshold, left, right] : {
             std::array<double, 3>{288.5, 9711.0, 11379.0},
             std::array<double, 3>{289.0, 10000.0, 11090.0},
             std::array<double, 3>{289.5, 10000.0, 11090.0}}) {
        auto stage = createStage(width, 1U, {1.0, rationalRadius(), threshold});
        ASSERT_NE(stage, nullptr);
        const auto output = runTight(*stage, width, 1U, input);
        EXPECT_EQ(output[7], static_cast<std::uint16_t>(left));
        EXPECT_EQ(output[8], static_cast<std::uint16_t>(right));
    }
}

TEST(SharpenStage, RoundsPositiveHalvesUpAfterApplyingSignedDetail) {
    constexpr std::uint32_t width = 15U;
    std::array<std::uint16_t, width> input{};
    input.fill(10000U);
    for (std::size_t x = 8U; x < input.size(); ++x) input[x] = 10004U;
    auto stage = createStage(width, 1U, {0.5, rationalRadius(), 0.0});
    ASSERT_NE(stage, nullptr);
    const auto output = runTight(*stage, width, 1U, input);
    EXPECT_EQ(output[7], 10000U);
    EXPECT_EQ(output[8], 10005U);
}

TEST(SharpenStage, IdentityPathsAndSaturationPreserveU16Values) {
    constexpr std::array<std::uint16_t, 5> values{0U, 1U, 1001U, 32769U, 65535U};
    for (const auto parameters : {
             SharpenParameters{0.0, 0.5, 0.0},
             SharpenParameters{5.0, 5.0, 65535.0}}) {
        auto stage = createStage(5U, 1U, parameters);
        ASSERT_NE(stage, nullptr);
        EXPECT_EQ(runTight(*stage, 5U, 1U, values),
            (std::vector<std::uint16_t>(values.begin(), values.end())));
    }
    constexpr std::array<std::uint16_t, 9> constant{
        1001U, 1001U, 1001U, 1001U, 1001U,
        1001U, 1001U, 1001U, 1001U};
    auto constantStage = createStage(3U, 3U, {2.0, 1.25, 0.0});
    ASSERT_NE(constantStage, nullptr);
    EXPECT_EQ(runTight(*constantStage, 3U, 3U, constant),
        (std::vector<std::uint16_t>(constant.begin(), constant.end())));

    std::array<std::uint16_t, 15> step{};
    for (std::size_t x = 8U; x < step.size(); ++x) step[x] = 65535U;
    auto saturated = createStage(15U, 1U, {5.0, rationalRadius(), 0.0});
    ASSERT_NE(saturated, nullptr);
    const auto output = runTight(*saturated, 15U, 1U, step);
    EXPECT_EQ(output[7], 0U);
    EXPECT_EQ(output[8], 65535U);
}

TEST(SharpenStage, FactoryValidatesBoundsStorageScratchAndBudget) {
    const auto imageLayout = layout(3U, 2U);
    for (const auto parameters : {
             SharpenParameters{0.0, 0.5, 0.0},
             SharpenParameters{5.0, 5.0, 65535.0}}) {
        EXPECT_TRUE(SharpenStage::create(parameters, imageLayout).hasValue());
    }
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    struct Bound final { double low; double high; std::size_t field; };
    constexpr std::array bounds{
        Bound{0.0, 5.0, 0U}, Bound{0.5, 5.0, 1U}, Bound{0.0, 65535.0, 2U}};
    for (const auto& bound : bounds) {
        for (const auto value : {std::nextafter(bound.low, -infinity),
                 std::nextafter(bound.high, infinity), infinity, -infinity, nan}) {
            auto parameters = SharpenParameters{};
            if (bound.field == 0U) parameters.amount = value;
            if (bound.field == 1U) parameters.radius = value;
            if (bound.field == 2U) parameters.threshold = value;
            EXPECT_FALSE(SharpenStage::create(parameters, imageLayout).hasValue());
        }
    }
    EXPECT_FALSE(SharpenStage::create({},
        layout(3U, 2U, 0U, core::StorageType::UInt8)).hasValue());
    const auto minimum = SharpenStage::requiredScratchBytes({1.0, 0.5, 0.0}, imageLayout);
    const auto maximum = SharpenStage::requiredScratchBytes({1.0, 5.0, 0.0}, imageLayout);
    ASSERT_TRUE(minimum.hasValue());
    ASSERT_TRUE(maximum.hasValue());
    EXPECT_EQ(minimum.value(), 6U * sizeof(double) + 5U * sizeof(double));
    EXPECT_EQ(maximum.value(), 6U * sizeof(double) + 31U * sizeof(double));
    const auto budget = SharpenStage::create(
        {1.0, 0.5, 0.0}, imageLayout, minimum.value() - 1U);
    ASSERT_FALSE(budget.hasValue());
    EXPECT_EQ(budget.error().code, "sharpen_scratch_budget_exceeded");

    const auto square = layout(2048U, 2048U);
    const auto squareRadiusOne = SharpenStage::requiredScratchBytes(
        {1.0, 1.0, 0.0}, square);
    const auto squareRadiusFive = SharpenStage::requiredScratchBytes(
        {1.0, 5.0, 0.0}, square);
    ASSERT_TRUE(squareRadiusOne.hasValue());
    ASSERT_TRUE(squareRadiusFive.hasValue());
    EXPECT_EQ(squareRadiusOne.value(), 114744U);
    EXPECT_EQ(squareRadiusFive.value(), 508152U);

    if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
        constexpr auto width = std::numeric_limits<std::uint32_t>::max();
        constexpr auto height = std::uint32_t{1073741823U};
        const auto stride = static_cast<std::size_t>(width) * 2U;
        const auto huge = layout(width, height, stride, core::StorageType::UInt16,
            stride * static_cast<std::size_t>(height));
        const auto required = SharpenStage::requiredScratchBytes({}, huge);
        ASSERT_TRUE(required.hasValue());
        EXPECT_EQ(required.value(),
            static_cast<std::size_t>(width) * 7U * sizeof(double)
                + 7U * sizeof(double));
    }
}

TEST(SharpenStage, TallImagesUseExactRingScratchBudget) {
    constexpr std::uint32_t width = 17U;
    constexpr std::uint32_t height = 37U;
    constexpr SharpenParameters parameters{1.5, 2.0, 12.5};
    constexpr std::size_t kernelSize = 13U;
    constexpr std::size_t expectedBytes =
        width * kernelSize * sizeof(double) + kernelSize * sizeof(double);
    const auto imageLayout = layout(width, height, width * 2U + 3U);

    const auto required = SharpenStage::requiredScratchBytes(parameters, imageLayout);
    ASSERT_TRUE(required.hasValue());
    EXPECT_EQ(required.value(), expectedBytes);

    auto exact = SharpenStage::create(parameters, imageLayout, expectedBytes);
    ASSERT_TRUE(exact.hasValue());
    EXPECT_EQ(exact.value()->scratchBytes(), expectedBytes);

    const auto oneByteShort = SharpenStage::create(
        parameters, imageLayout, expectedBytes - 1U);
    ASSERT_FALSE(oneByteShort.hasValue());
    EXPECT_EQ(oneByteShort.error().code, "sharpen_scratch_budget_exceeded");
}

TEST(SharpenStage, ReportsOwnedScratchAndCanonicalTraits) {
    auto stage = createStage(3U, 2U, {1.0, 0.5, 0.0});
    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->scratchBytes(), 88U);
    EXPECT_EQ(stage->id(), processing::StageId::Sharpen);
    EXPECT_EQ(stage->traits().scratchImages, 4U);
    EXPECT_EQ(stage->traits().inputDomain, ImageDomain::CanonicalU16);
    const auto registry = processing::stageRegistry();
    const auto entry = std::find_if(registry.begin(), registry.end(), [](const auto& traits) {
        return traits.id == processing::StageId::Sharpen;
    });
    ASSERT_NE(entry, registry.end());
    EXPECT_EQ(entry->scratchImages, 4U);
}

TEST(SharpenStage, SupportsOddStridesUnalignedStartsReuseAndProvenance) {
    constexpr std::uint32_t width = 5U;
    constexpr std::uint32_t height = 2U;
    constexpr std::size_t sourceStride = 13U;
    constexpr std::size_t destinationStride = 15U;
    constexpr std::array<std::uint16_t, width * height> first{
        0U, 100U, 400U, 1000U, 65535U, 2U, 4U, 6U, 8U, 10U};
    constexpr std::array<std::uint16_t, width * height> second{
        10U, 9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U};
    auto stage = createStage(width, height, {1.0, 0.5, 0.0});
    ASSERT_NE(stage, nullptr);
    const auto expected = runTight(*stage, width, height, first);
    EXPECT_NE(runTight(*stage, width, height, second), expected);
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
    ASSERT_TRUE(stage->process(sourceView, destinationView,
        canonicalFormat(10U, 1023U)).hasValue());
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

TEST(SharpenStage, RejectsInvalidViewsAndPayloadOverlapBeforeWriting) {
    auto stage = createStage(2U, 1U, {1.0, 0.5, 0.0});
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
        EXPECT_FALSE(stage->process(sourceView, destinationView, canonicalFormat()).hasValue());
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
    auto amountIdentity = createStage(2U, 1U, {0.0, 0.5, 0.0});
    auto thresholdIdentity = createStage(2U, 1U, {5.0, 0.5, 65535.0});
    ASSERT_NE(amountIdentity, nullptr);
    ASSERT_NE(thresholdIdentity, nullptr);
    const auto fullOverlap = MutableImageView::create(goodLayout,
        std::span(shared).subspan(0U, 6U), ImageDomain::CanonicalU16).value();
    EXPECT_FALSE(amountIdentity->process(
        overlappingSource, fullOverlap, canonicalFormat()).hasValue());
    EXPECT_FALSE(thresholdIdentity->process(
        ImageView::create(goodLayout, source, ImageDomain::SensorNative).value(),
        goodDestination, canonicalFormat()).hasValue());
    EXPECT_EQ(shared, sharedBefore);
    EXPECT_EQ(destination, before);
}

}  // namespace
