#include <lumora/processing/OrientationTransform.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace {
using lumora::core::DisplayStorage;
using lumora::core::ImageLayout;
using lumora::core::Orientation;
using lumora::core::Rotation;
using lumora::core::StorageType;
using lumora::processing::OrientationTransform;

struct OrientationCase final {
    Orientation orientation;
    std::uint32_t outputWidth;
    std::uint32_t outputHeight;
    std::array<std::uint16_t, 6> expected;
};

constexpr std::array<OrientationCase, 16> orientationCases{{
    {{false, false, Rotation::Degrees0}, 2U, 3U, {1U, 2U, 3U, 4U, 5U, 6U}},
    {{false, false, Rotation::Degrees90}, 3U, 2U, {5U, 3U, 1U, 6U, 4U, 2U}},
    {{false, false, Rotation::Degrees180}, 2U, 3U, {6U, 5U, 4U, 3U, 2U, 1U}},
    {{false, false, Rotation::Degrees270}, 3U, 2U, {2U, 4U, 6U, 1U, 3U, 5U}},
    {{true, false, Rotation::Degrees0}, 2U, 3U, {2U, 1U, 4U, 3U, 6U, 5U}},
    {{true, false, Rotation::Degrees90}, 3U, 2U, {6U, 4U, 2U, 5U, 3U, 1U}},
    {{true, false, Rotation::Degrees180}, 2U, 3U, {5U, 6U, 3U, 4U, 1U, 2U}},
    {{true, false, Rotation::Degrees270}, 3U, 2U, {1U, 3U, 5U, 2U, 4U, 6U}},
    {{false, true, Rotation::Degrees0}, 2U, 3U, {5U, 6U, 3U, 4U, 1U, 2U}},
    {{false, true, Rotation::Degrees90}, 3U, 2U, {1U, 3U, 5U, 2U, 4U, 6U}},
    {{false, true, Rotation::Degrees180}, 2U, 3U, {2U, 1U, 4U, 3U, 6U, 5U}},
    {{false, true, Rotation::Degrees270}, 3U, 2U, {6U, 4U, 2U, 5U, 3U, 1U}},
    {{true, true, Rotation::Degrees0}, 2U, 3U, {6U, 5U, 4U, 3U, 2U, 1U}},
    {{true, true, Rotation::Degrees90}, 3U, 2U, {2U, 4U, 6U, 1U, 3U, 5U}},
    {{true, true, Rotation::Degrees180}, 2U, 3U, {1U, 2U, 3U, 4U, 5U, 6U}},
    {{true, true, Rotation::Degrees270}, 3U, 2U, {5U, 3U, 1U, 6U, 4U, 2U}},
}};

void expectEveryOrientationOnPairedDisplays(
    DisplayStorage storage,
    StorageType storageType) {
    constexpr std::array<std::uint16_t, 6> samples{1U, 2U, 3U, 4U, 5U, 6U};
    std::array<std::byte, 12> source{};
    const auto sampleBytes = storage == DisplayStorage::Gray16 ? 2U : 1U;
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        if (sampleBytes == 1U) {
            source[index] = static_cast<std::byte>(samples[index]);
        } else {
            std::memcpy(source.data() + index * sampleBytes,
                &samples[index], sampleBytes);
        }
    }
    const auto sourceLayout = ImageLayout::create(
        2U, 3U, 2U * sampleBytes, storageType, 6U * sampleBytes).value();

    for (const auto& testCase : orientationCases) {
        const auto layoutResult = OrientationTransform::outputLayout(
            sourceLayout, storage, testCase.orientation);
        ASSERT_TRUE(layoutResult.hasValue());
        const auto& destinationLayout = layoutResult.value();
        EXPECT_EQ(destinationLayout.width(), testCase.outputWidth);
        EXPECT_EQ(destinationLayout.height(), testCase.outputHeight);
        std::array<std::byte, 12> originalDestination{};
        std::array<std::byte, 12> enhancedDestination{};

        const auto originalResult = OrientationTransform{}.apply(sourceLayout, storage,
            std::span<const std::byte>(source).first(sourceLayout.payloadBytes()),
            destinationLayout,
            std::span<std::byte>(originalDestination)
                .first(destinationLayout.payloadBytes()),
            testCase.orientation);
        const auto enhancedResult = OrientationTransform{}.apply(sourceLayout, storage,
            std::span<const std::byte>(source).first(sourceLayout.payloadBytes()),
            destinationLayout,
            std::span<std::byte>(enhancedDestination)
                .first(destinationLayout.payloadBytes()),
            testCase.orientation);

        ASSERT_TRUE(originalResult.hasValue())
            << static_cast<int>(testCase.orientation.rotation);
        ASSERT_TRUE(enhancedResult.hasValue())
            << static_cast<int>(testCase.orientation.rotation);
        for (std::size_t index = 0U; index < testCase.expected.size(); ++index) {
            std::uint16_t originalActual = 0U;
            std::uint16_t enhancedActual = 0U;
            std::memcpy(&originalActual,
                originalDestination.data() + index * sampleBytes, sampleBytes);
            std::memcpy(&enhancedActual,
                enhancedDestination.data() + index * sampleBytes, sampleBytes);
            EXPECT_EQ(originalActual, testCase.expected[index])
                << "rotation=" << static_cast<int>(testCase.orientation.rotation)
                << " flipHorizontal=" << testCase.orientation.flipHorizontal
                << " flipVertical=" << testCase.orientation.flipVertical
                << " index=" << index;
            EXPECT_EQ(enhancedActual, testCase.expected[index])
                << "rotation=" << static_cast<int>(testCase.orientation.rotation)
                << " flipHorizontal=" << testCase.orientation.flipHorizontal
                << " flipVertical=" << testCase.orientation.flipVertical
                << " index=" << index;
        }
    }
}

TEST(OrientationTransform, AppliesAllSixteenLiteralMatricesToPairedGray8Displays) {
    expectEveryOrientationOnPairedDisplays(
        DisplayStorage::Gray8, StorageType::UInt8);
}

TEST(OrientationTransform, AppliesAllSixteenLiteralMatricesToPairedGray16Displays) {
    expectEveryOrientationOnPairedDisplays(
        DisplayStorage::Gray16, StorageType::UInt16);
}

TEST(OrientationTransform, ReportsCheckedTightLayoutsAndRejectsInvalidInputs) {
    const auto gray8Source = ImageLayout::create(
        3U, 2U, 5U, StorageType::UInt8, 12U).value();
    const auto gray8Result = OrientationTransform::outputLayout(gray8Source,
        DisplayStorage::Gray8, {false, false, Rotation::Degrees90});
    ASSERT_TRUE(gray8Result.hasValue());
    EXPECT_EQ(gray8Result.value().width(), 2U);
    EXPECT_EQ(gray8Result.value().height(), 3U);
    EXPECT_EQ(gray8Result.value().strideBytes(), 2U);
    EXPECT_EQ(gray8Result.value().requiredBytes(), 6U);
    EXPECT_EQ(gray8Result.value().payloadBytes(), 6U);
    EXPECT_EQ(gray8Result.value().storage(), StorageType::UInt8);

    const auto gray16Source = ImageLayout::create(
        3U, 2U, 7U, StorageType::UInt16, 15U).value();
    const auto gray16Result = OrientationTransform::outputLayout(gray16Source,
        DisplayStorage::Gray16, {true, false, Rotation::Degrees270});
    ASSERT_TRUE(gray16Result.hasValue());
    EXPECT_EQ(gray16Result.value().width(), 2U);
    EXPECT_EQ(gray16Result.value().height(), 3U);
    EXPECT_EQ(gray16Result.value().strideBytes(), 4U);
    EXPECT_EQ(gray16Result.value().requiredBytes(), 12U);
    EXPECT_EQ(gray16Result.value().payloadBytes(), 12U);
    EXPECT_EQ(gray16Result.value().storage(), StorageType::UInt16);

    const auto invalidStorage = OrientationTransform::outputLayout(gray8Source,
        static_cast<DisplayStorage>(255), {false, false, Rotation::Degrees0});
    ASSERT_FALSE(invalidStorage.hasValue());
    EXPECT_EQ(invalidStorage.error().code, "orientation_invalid_display_storage");

    const auto mismatchedStorage = OrientationTransform::outputLayout(gray8Source,
        DisplayStorage::Gray16, {false, false, Rotation::Degrees0});
    ASSERT_FALSE(mismatchedStorage.hasValue());
    EXPECT_EQ(mismatchedStorage.error().code, "orientation_source_storage_mismatch");

    const auto invalidRotation = OrientationTransform::outputLayout(gray8Source,
        DisplayStorage::Gray8,
        {false, false, static_cast<Rotation>(255)});
    ASSERT_FALSE(invalidRotation.hasValue());
    EXPECT_EQ(invalidRotation.error().code, "orientation_invalid_rotation");
}

TEST(OrientationTransform, PreservesPaddingCanariesSourceAndIdentitySamples) {
    std::array<std::byte, 18> sourceBacking{};
    sourceBacking.fill(std::byte{0xD1});
    auto source = std::span(sourceBacking).subspan(1U, 16U);
    constexpr std::array<std::uint8_t, 9> samples{
        11U, 12U, 13U, 21U, 22U, 23U, 31U, 32U, 33U};
    for (std::size_t row = 0U; row < 3U; ++row) {
        std::memcpy(source.data() + row * 5U,
            samples.data() + row * 3U, 3U);
    }
    const auto sourceBefore = sourceBacking;
    const auto sourceLayout = ImageLayout::create(
        3U, 3U, 5U, StorageType::UInt8, source.size()).value();

    std::array<std::byte, 22> destinationBacking{};
    destinationBacking.fill(std::byte{0xA7});
    auto destination = std::span(destinationBacking).subspan(1U, 20U);
    const auto destinationLayout = ImageLayout::create(
        3U, 3U, 6U, StorageType::UInt8, destination.size()).value();

    const auto result = OrientationTransform{}.apply(sourceLayout,
        DisplayStorage::Gray8, source, destinationLayout, destination,
        {false, false, Rotation::Degrees0});

    ASSERT_TRUE(result.hasValue());
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            EXPECT_EQ(destination[row * 6U + column],
                static_cast<std::byte>(samples[row * 3U + column]));
        }
    }
    for (const auto index : {3U, 4U, 5U, 9U, 10U, 11U,
             15U, 16U, 17U, 18U, 19U}) {
        EXPECT_EQ(destination[index], std::byte{0xA7}) << index;
    }
    EXPECT_EQ(sourceBacking, sourceBefore);
    EXPECT_EQ(destinationBacking.front(), std::byte{0xA7});
    EXPECT_EQ(destinationBacking.back(), std::byte{0xA7});
}

TEST(OrientationTransform, CopiesUnalignedGray16AcrossOddPaddedStrides) {
    std::array<std::byte, 18> sourceBacking{};
    sourceBacking.fill(std::byte{0xD3});
    auto source = std::span(sourceBacking).subspan(1U, 15U);
    constexpr std::array<std::uint16_t, 6> samples{
        100U, 200U, 300U, 400U, 500U, 600U};
    for (std::size_t row = 0U; row < 2U; ++row) {
        std::memcpy(source.data() + row * 7U,
            samples.data() + row * 3U, 6U);
    }
    const auto sourceBefore = sourceBacking;
    const auto sourceLayout = ImageLayout::create(
        3U, 2U, 7U, StorageType::UInt16, source.size()).value();

    std::array<std::byte, 19> destinationBacking{};
    destinationBacking.fill(std::byte{0xA7});
    auto destination = std::span(destinationBacking).subspan(1U, 16U);
    const auto destinationLayout = ImageLayout::create(
        2U, 3U, 5U, StorageType::UInt16, destination.size()).value();

    const auto result = OrientationTransform{}.apply(sourceLayout,
        DisplayStorage::Gray16, source, destinationLayout, destination,
        {false, false, Rotation::Degrees90});

    ASSERT_TRUE(result.hasValue());
    constexpr std::array<std::uint16_t, 6> expected{
        400U, 100U, 500U, 200U, 600U, 300U};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 2U; ++column) {
            std::uint16_t actual = 0U;
            std::memcpy(&actual,
                destination.data() + row * 5U + column * 2U, 2U);
            EXPECT_EQ(actual, expected[row * 2U + column]);
        }
        EXPECT_EQ(destination[row * 5U + 4U], std::byte{0xA7});
    }
    EXPECT_EQ(destination[15], std::byte{0xA7});
    EXPECT_EQ(sourceBacking, sourceBefore);
    EXPECT_EQ(destinationBacking.front(), std::byte{0xA7});
    EXPECT_EQ(destinationBacking.back(), std::byte{0xA7});
}

TEST(OrientationTransform, RotatesSingletonAxesWithoutChangingSamples) {
    constexpr std::array<std::byte, 4> column{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto columnLayout = ImageLayout::create(
        1U, 4U, 1U, StorageType::UInt8, column.size()).value();
    const auto rowLayout = OrientationTransform::outputLayout(columnLayout,
        DisplayStorage::Gray8, {false, false, Rotation::Degrees90}).value();
    std::array<std::byte, 4> row{};
    ASSERT_TRUE(OrientationTransform{}.apply(columnLayout, DisplayStorage::Gray8,
        column, rowLayout, row, {false, false, Rotation::Degrees90}).hasValue());
    EXPECT_EQ(row, (std::array<std::byte, 4>{
        std::byte{4}, std::byte{3}, std::byte{2}, std::byte{1}}));

    constexpr std::array<std::byte, 5> wideRow{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const auto wideRowLayout = ImageLayout::create(
        5U, 1U, 5U, StorageType::UInt8, wideRow.size()).value();
    const auto tallLayout = OrientationTransform::outputLayout(wideRowLayout,
        DisplayStorage::Gray8, {false, false, Rotation::Degrees270}).value();
    std::array<std::byte, 5> tall{};
    ASSERT_TRUE(OrientationTransform{}.apply(wideRowLayout, DisplayStorage::Gray8,
        wideRow, tallLayout, tall, {false, false, Rotation::Degrees270}).hasValue());
    EXPECT_EQ(tall, (std::array<std::byte, 5>{
        std::byte{5}, std::byte{4}, std::byte{3}, std::byte{2}, std::byte{1}}));
}

TEST(OrientationTransform, RejectsEnumStorageAndExtentErrorsBeforeWriting) {
    constexpr std::array<std::byte, 6> source{
        std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6}};
    const auto sourceLayout = ImageLayout::create(
        2U, 3U, 2U, StorageType::UInt8, source.size()).value();
    const auto validDestinationLayout = ImageLayout::create(
        2U, 3U, 2U, StorageType::UInt8, 6U).value();
    std::array<std::byte, 12> destination{};

    const auto expectRejected = [&](const auto& result, const char* expectedCode) {
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, expectedCode);
        EXPECT_EQ(destination, (std::array<std::byte, 12>{
            std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C},
            std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C},
            std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C}}));
    };

    destination.fill(std::byte{0x6C});
    expectRejected(OrientationTransform{}.apply(sourceLayout,
        static_cast<DisplayStorage>(255), source, validDestinationLayout,
        destination, {false, false, Rotation::Degrees0}),
        "orientation_invalid_display_storage");

    destination.fill(std::byte{0x6C});
    expectRejected(OrientationTransform{}.apply(sourceLayout, DisplayStorage::Gray8,
        source, validDestinationLayout, destination,
        {false, false, static_cast<Rotation>(255)}),
        "orientation_invalid_rotation");

    const auto gray16SourceLayout = ImageLayout::create(
        2U, 3U, 4U, StorageType::UInt16, 12U).value();
    constexpr std::array<std::byte, 12> gray16Source{};
    destination.fill(std::byte{0x6C});
    expectRejected(OrientationTransform{}.apply(gray16SourceLayout,
        DisplayStorage::Gray8, gray16Source, validDestinationLayout, destination,
        {false, false, Rotation::Degrees0}),
        "orientation_source_storage_mismatch");

    const auto gray16DestinationLayout = ImageLayout::create(
        2U, 3U, 4U, StorageType::UInt16, 12U).value();
    destination.fill(std::byte{0x6C});
    expectRejected(OrientationTransform{}.apply(sourceLayout, DisplayStorage::Gray8,
        source, gray16DestinationLayout, destination,
        {false, false, Rotation::Degrees0}),
        "orientation_destination_storage_mismatch");

    const auto wrongExtentLayout = ImageLayout::create(
        3U, 2U, 3U, StorageType::UInt8, 6U).value();
    destination.fill(std::byte{0x6C});
    expectRejected(OrientationTransform{}.apply(sourceLayout, DisplayStorage::Gray8,
        source, wrongExtentLayout, destination,
        {false, false, Rotation::Degrees0}),
        "orientation_destination_extent_mismatch");
}

TEST(OrientationTransform, RejectsIncompleteDeclaredPayloadsBeforeWriting) {
    std::array<std::byte, 12> source{};
    source.fill(std::byte{0x31});
    const auto sourceLayout = ImageLayout::create(
        2U, 3U, 3U, StorageType::UInt8, 9U).value();
    const auto destinationLayout = ImageLayout::create(
        3U, 2U, 5U, StorageType::UInt8, 10U).value();
    std::array<std::byte, 12> destination{};
    destination.fill(std::byte{0x6C});
    const auto destinationBefore = destination;

    const auto shortSource = OrientationTransform{}.apply(sourceLayout,
        DisplayStorage::Gray8, std::span(source).first(8U), destinationLayout,
        destination, {false, false, Rotation::Degrees90});
    ASSERT_FALSE(shortSource.hasValue());
    EXPECT_EQ(shortSource.error().code, "orientation_source_span_too_small");
    EXPECT_EQ(destination, destinationBefore);

    const auto shortDestination = OrientationTransform{}.apply(sourceLayout,
        DisplayStorage::Gray8, std::span(source).first(9U), destinationLayout,
        std::span(destination).first(9U),
        {false, false, Rotation::Degrees90});
    ASSERT_FALSE(shortDestination.hasValue());
    EXPECT_EQ(shortDestination.error().code,
        "orientation_destination_span_too_small");
    EXPECT_EQ(destination, destinationBefore);
}

TEST(OrientationTransform, RejectsEveryCompletePayloadOverlapShape) {
    const auto expectOverlapRejected = [](
                                           std::size_t sourceOffset,
                                           std::size_t sourceStride,
                                           std::size_t sourcePayload,
                                           std::size_t destinationOffset,
                                           std::size_t destinationStride,
                                           std::size_t destinationPayload) {
        std::array<std::byte, 24> backing{};
        backing.fill(std::byte{0x4D});
        backing[sourceOffset] = std::byte{1};
        backing[sourceOffset + 1U] = std::byte{2};
        const auto before = backing;
        const auto sourceLayout = ImageLayout::create(2U, 1U,
            sourceStride, StorageType::UInt8, sourcePayload).value();
        const auto destinationLayout = ImageLayout::create(2U, 1U,
            destinationStride, StorageType::UInt8, destinationPayload).value();

        const auto result = OrientationTransform{}.apply(sourceLayout,
            DisplayStorage::Gray8, std::span(backing).subspan(sourceOffset),
            destinationLayout, std::span(backing).subspan(destinationOffset),
            {false, false, Rotation::Degrees0});

        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "orientation_views_overlap");
        EXPECT_EQ(backing, before);
    };

    expectOverlapRejected(8U, 4U, 4U, 8U, 4U, 4U);   // identical
    expectOverlapRejected(8U, 2U, 6U, 9U, 2U, 2U);   // destination contained
    expectOverlapRejected(8U, 2U, 2U, 6U, 2U, 6U);   // source contained
    expectOverlapRejected(8U, 4U, 4U, 6U, 2U, 4U);   // source head
    expectOverlapRejected(8U, 4U, 4U, 10U, 2U, 4U);  // source tail
    expectOverlapRejected(8U, 4U, 4U, 10U, 2U, 2U);  // source padding only
}

TEST(OrientationTransform, BoundsAliasingChecksToDeclaredPayloads) {
    std::array<std::byte, 12> backing{};
    backing.fill(std::byte{0x4D});
    backing[4] = std::byte{7};
    backing[5] = std::byte{9};
    const auto sourceLayout = ImageLayout::create(
        2U, 1U, 2U, StorageType::UInt8, 2U).value();
    const auto destinationLayout = ImageLayout::create(
        2U, 1U, 2U, StorageType::UInt8, 2U).value();

    const auto result = OrientationTransform{}.apply(sourceLayout,
        DisplayStorage::Gray8, std::span(backing).subspan(4U, 8U),
        destinationLayout, std::span(backing).subspan(6U, 6U),
        {false, false, Rotation::Degrees0});

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(backing[4], std::byte{7});
    EXPECT_EQ(backing[5], std::byte{9});
    EXPECT_EQ(backing[6], std::byte{7});
    EXPECT_EQ(backing[7], std::byte{9});
    for (const auto index : {0U, 1U, 2U, 3U, 8U, 9U, 10U, 11U}) {
        EXPECT_EQ(backing[index], std::byte{0x4D}) << index;
    }
}

}  // namespace
