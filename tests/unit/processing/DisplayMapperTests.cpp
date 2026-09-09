#include <lumora/processing/DisplayMapper.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {
using lumora::core::DisplayMapping;
using lumora::core::DisplayStorage;
using lumora::core::ImageLayout;
using lumora::core::StorageType;
using lumora::processing::DisplayMapper;
using lumora::processing::ImageDomain;
using lumora::processing::ImageView;

[[nodiscard]] ImageView canonicalView(
    const ImageLayout& layout,
    std::span<const std::byte> bytes) {
    return ImageView::create(layout, bytes, ImageDomain::CanonicalU16).value();
}

TEST(DisplayMapper, MapsEveryCanonicalValueWithExactIntegerFormula) {
    std::vector<std::uint16_t> samples(65536U);
    for (std::uint32_t value = 0U; value <= 65535U; ++value) {
        samples[value] = static_cast<std::uint16_t>(value);
    }
    const auto sourceBytes = std::as_bytes(std::span(samples));
    const auto sourceLayout = ImageLayout::create(
        65536U, 1U, sourceBytes.size(), StorageType::UInt16, sourceBytes.size()).value();
    const auto destinationLayout = ImageLayout::create(
        65536U, 1U, samples.size(), StorageType::UInt8, samples.size()).value();
    std::vector<std::byte> destination(samples.size(), std::byte{0xA5});

    const auto result = DisplayMapper{}.map(canonicalView(sourceLayout, sourceBytes),
        destinationLayout, DisplayStorage::Gray8, destination, 73U);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), (DisplayMapping{0U, 65535U, 255U, 73U}));
    for (std::uint32_t value = 0U; value <= 65535U; ++value) {
        EXPECT_EQ(std::to_integer<std::uint8_t>(destination[value]),
            (value + 128U) / 257U)
            << "input=" << value;
    }
}

TEST(DisplayMapper, MapsUnalignedVectorBoundaryWidthsAcrossPaddedRows) {
    constexpr std::array<std::uint32_t, 9> widths{
        7U, 8U, 9U, 15U, 16U, 17U, 31U, 32U, 33U};
    constexpr std::array<std::uint16_t, 6> boundarySamples{
        0U, 128U, 129U, 32768U, 65406U, 65535U};
    constexpr std::uint32_t height = 3U;
    constexpr std::size_t sourcePrefix = 1U;
    constexpr std::size_t sourceSuffix = 5U;
    constexpr std::size_t destinationPrefix = 3U;
    constexpr std::size_t destinationSuffix = 7U;

    for (const auto width : widths) {
        const auto sourceRowBytes = static_cast<std::size_t>(width) * 2U;
        const auto sourceStride = sourceRowBytes + 5U;
        const auto destinationRowBytes = static_cast<std::size_t>(width);
        const auto destinationStride = destinationRowBytes
            + (width % 2U == 0U ? 7U : 6U);
        const auto sourcePayload = sourceStride * height;
        const auto destinationPayload = destinationStride * height;
        std::vector<std::byte> sourceBacking(
            sourcePrefix + sourcePayload + sourceSuffix, std::byte{0xC7});
        std::vector<std::byte> destinationBacking(
            destinationPrefix + destinationPayload + destinationSuffix,
            std::byte{0x5B});
        std::vector<std::uint16_t> samples(
            static_cast<std::size_t>(width) * height);
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const auto sampleIndex = static_cast<std::size_t>(y) * width + x;
                const auto boundaryIndex = (static_cast<std::size_t>(x) * 5U
                    + static_cast<std::size_t>(y) * 3U + width)
                    % boundarySamples.size();
                samples[sampleIndex] = boundarySamples[boundaryIndex];
                std::memcpy(sourceBacking.data() + sourcePrefix
                        + static_cast<std::size_t>(y) * sourceStride
                        + static_cast<std::size_t>(x) * 2U,
                    &samples[sampleIndex], sizeof(samples[sampleIndex]));
            }
        }
        const auto sourceBefore = sourceBacking;
        const auto destinationBefore = destinationBacking;
        const auto sourceLayout = ImageLayout::create(width, height,
            sourceStride, StorageType::UInt16, sourcePayload).value();
        const auto destinationLayout = ImageLayout::create(width, height,
            destinationStride, StorageType::UInt8, destinationPayload).value();
        const auto source = canonicalView(sourceLayout,
            std::span(sourceBacking).subspan(sourcePrefix, sourcePayload));
        auto destination = std::span(destinationBacking).subspan(
            destinationPrefix, destinationPayload);
        const auto revision = static_cast<std::uint64_t>(1000U + width);

        const auto result = DisplayMapper{}.map(source, destinationLayout,
            DisplayStorage::Gray8, destination, revision);

        ASSERT_TRUE(result.hasValue()) << "width=" << width;
        EXPECT_EQ(result.value(), (DisplayMapping{0U, 65535U, 255U, revision}));
        EXPECT_EQ(sourceBacking, sourceBefore) << "width=" << width;
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const auto sampleIndex = static_cast<std::size_t>(y) * width + x;
                const auto actual = std::to_integer<std::uint8_t>(
                    destination[static_cast<std::size_t>(y) * destinationStride + x]);
                const auto expected = (static_cast<std::uint32_t>(samples[sampleIndex])
                    + 128U) / 257U;
                EXPECT_EQ(actual, expected)
                    << "width=" << width << " x=" << x << " y=" << y;
            }
            for (std::size_t x = destinationRowBytes;
                 x < destinationStride; ++x) {
                EXPECT_EQ(destination[static_cast<std::size_t>(y) * destinationStride + x],
                    std::byte{0x5B})
                    << "width=" << width << " padding=" << x << " y=" << y;
            }
        }
        for (std::size_t index = 0U; index < destinationPrefix; ++index) {
            EXPECT_EQ(destinationBacking[index], destinationBefore[index])
                << "width=" << width << " prefix=" << index;
        }
        for (std::size_t index = destinationPrefix + destinationPayload;
             index < destinationBacking.size(); ++index) {
            EXPECT_EQ(destinationBacking[index], destinationBefore[index])
                << "width=" << width << " suffix=" << index;
        }
    }
}

TEST(DisplayMapper, PreservesPaddedDestinationAndCanonicalSource) {
    constexpr std::size_t sourceStride = 7U;
    constexpr std::size_t destinationStride = 4U;
    std::array<std::byte, sourceStride * 2U> sourceStorage{};
    std::array<std::byte, destinationStride * 2U> destinationStorage{};
    sourceStorage.fill(std::byte{0xC7});
    destinationStorage.fill(std::byte{0x5B});
    constexpr std::array<std::uint16_t, 4> samples{0U, 32768U, 65535U, 129U};
    std::memcpy(sourceStorage.data(), samples.data(), 4U);
    std::memcpy(sourceStorage.data() + sourceStride, samples.data() + 2U, 4U);
    const auto sourceBefore = sourceStorage;
    const auto sourceLayout = ImageLayout::create(
        2U, 2U, sourceStride, StorageType::UInt16, sourceStorage.size()).value();
    const auto destinationLayout = ImageLayout::create(
        2U, 2U, destinationStride, StorageType::UInt8, destinationStorage.size()).value();

    const auto result = DisplayMapper{}.map(canonicalView(sourceLayout, sourceStorage),
        destinationLayout, DisplayStorage::Gray8, destinationStorage, 0U);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), (DisplayMapping{0U, 65535U, 255U, 0U}));
    EXPECT_EQ(destinationStorage[0], std::byte{0});
    EXPECT_EQ(destinationStorage[1], std::byte{128});
    EXPECT_EQ(destinationStorage[4], std::byte{255});
    EXPECT_EQ(destinationStorage[5], std::byte{1});
    for (const auto index : {2U, 3U, 6U, 7U}) {
        EXPECT_EQ(destinationStorage[index], std::byte{0x5B}) << index;
    }
    EXPECT_EQ(sourceStorage, sourceBefore);
}

TEST(DisplayMapper, RejectsGray16WithStableUnsupportedFormatErrorBeforeWriting) {
    constexpr std::array<std::uint16_t, 2> samples{0U, 65535U};
    const auto sourceBytes = std::as_bytes(std::span(samples));
    const auto sourceLayout = ImageLayout::create(
        2U, 1U, 4U, StorageType::UInt16, 4U).value();
    const auto destinationLayout = ImageLayout::create(
        2U, 1U, 4U, StorageType::UInt16, 4U).value();
    std::array<std::byte, 4> destination{};
    destination.fill(std::byte{0x7D});

    const auto result = DisplayMapper{}.map(canonicalView(sourceLayout, sourceBytes),
        destinationLayout, DisplayStorage::Gray16, destination, 4U);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "unsupported_display_storage");
    EXPECT_EQ(destination,
        (std::array<std::byte, 4>{std::byte{0x7D}, std::byte{0x7D},
            std::byte{0x7D}, std::byte{0x7D}}));
}

TEST(DisplayMapper, RejectsStorageDomainExtentAndShortSpanBeforeWriting) {
    constexpr std::array<std::uint16_t, 4> samples{0U, 1U, 2U, 3U};
    const auto sourceBytes = std::as_bytes(std::span(samples));
    const auto sourceLayout = ImageLayout::create(
        2U, 2U, 4U, StorageType::UInt16, 8U).value();
    const auto source = canonicalView(sourceLayout, sourceBytes);
    const auto gray8Layout = ImageLayout::create(
        2U, 2U, 2U, StorageType::UInt8, 4U).value();
    std::array<std::byte, 8> destination{};
    destination.fill(std::byte{0x6C});

    const auto u16Layout = ImageLayout::create(
        2U, 2U, 4U, StorageType::UInt16, 8U).value();
    auto result = DisplayMapper{}.map(
        source, u16Layout, DisplayStorage::Gray8, destination, 1U);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "destination_storage_mismatch");

    const auto sensorSource = ImageView::create(
        sourceLayout, sourceBytes, ImageDomain::SensorNative).value();
    result = DisplayMapper{}.map(
        sensorSource, gray8Layout, DisplayStorage::Gray8, destination, 1U);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "image_domain_mismatch");

    const auto narrowLayout = ImageLayout::create(
        1U, 2U, 1U, StorageType::UInt8, 2U).value();
    result = DisplayMapper{}.map(
        source, narrowLayout, DisplayStorage::Gray8, destination, 1U);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "image_extent_mismatch");

    result = DisplayMapper{}.map(source, gray8Layout, DisplayStorage::Gray8,
        std::span(destination).first(3U), 1U);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "destination_span_too_small");
    EXPECT_EQ(destination,
        (std::array<std::byte, 8>{std::byte{0x6C}, std::byte{0x6C},
            std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C}, std::byte{0x6C},
            std::byte{0x6C}, std::byte{0x6C}}));
}

TEST(DisplayMapper, RejectsPartialAndIdenticalAliasingBeforeWriting) {
    std::array<std::byte, 20> storage{};
    storage.fill(std::byte{0x4D});
    constexpr std::array<std::uint16_t, 4> samples{0U, 1U, 2U, 3U};
    std::memcpy(storage.data() + 4U, samples.data(), sizeof(samples));
    const auto sourceLayout = ImageLayout::create(
        2U, 2U, 4U, StorageType::UInt16, 8U).value();
    const auto destinationLayout = ImageLayout::create(
        2U, 2U, 2U, StorageType::UInt8, 4U).value();
    const auto source = canonicalView(sourceLayout, std::span(storage).subspan(4U));

    for (const auto offset : {4U, 5U, 8U, 11U}) {
        const auto before = storage;
        const auto result = DisplayMapper{}.map(source, destinationLayout,
            DisplayStorage::Gray8, std::span(storage).subspan(offset), 1U);
        ASSERT_FALSE(result.hasValue()) << offset;
        EXPECT_EQ(result.error().code, "image_views_overlap") << offset;
        EXPECT_EQ(storage, before) << offset;
    }
}

}  // namespace
