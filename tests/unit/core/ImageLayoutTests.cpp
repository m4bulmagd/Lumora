#include <lumora/core/ImageLayout.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

using lumora::core::ErrorCategory;
using lumora::core::ImageLayout;
using lumora::core::StorageType;

TEST(ImageLayout, AcceptsPaddedMono16Rows) {
    const auto layout = ImageLayout::create(
        2048U, 2048U, 4160U, StorageType::UInt16, 4160ULL * 2048ULL);

    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout.value().width(), 2048U);
    EXPECT_EQ(layout.value().height(), 2048U);
    EXPECT_EQ(layout.value().rowBytes(), 4096U);
    EXPECT_EQ(layout.value().strideBytes(), 4160U);
    EXPECT_EQ(layout.value().requiredBytes(), 4160ULL * 2048ULL);
    EXPECT_EQ(layout.value().payloadBytes(), 4160ULL * 2048ULL);
    EXPECT_EQ(layout.value().storage(), StorageType::UInt16);
}

TEST(ImageLayout, AcceptsPayloadExactlyEqualToRequiredBytes) {
    const auto layout = ImageLayout::create(32U, 32U, 64U, StorageType::UInt16, 2048U);

    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout.value().requiredBytes(), 2048U);
}

TEST(ImageLayout, ComputesUInt8RowsWithoutDoublingWidth) {
    const auto layout = ImageLayout::create(31U, 2U, 32U, StorageType::UInt8, 64U);

    ASSERT_TRUE(layout.hasValue());
    EXPECT_EQ(layout.value().rowBytes(), 31U);
}

TEST(ImageLayout, RejectsShortPayload) {
    const auto layout = ImageLayout::create(32U, 32U, 64U, StorageType::UInt16, 2047U);

    ASSERT_FALSE(layout.hasValue());
    EXPECT_EQ(layout.error().category, ErrorCategory::InvalidFrame);
    EXPECT_EQ(layout.error().code, "payload_too_small");
}

TEST(ImageLayout, RejectsZeroDimensions) {
    const auto zeroWidth = ImageLayout::create(0U, 10U, 10U, StorageType::UInt8, 100U);
    const auto zeroHeight = ImageLayout::create(10U, 0U, 10U, StorageType::UInt8, 0U);

    ASSERT_FALSE(zeroWidth.hasValue());
    EXPECT_EQ(zeroWidth.error().code, "zero_dimensions");
    ASSERT_FALSE(zeroHeight.hasValue());
    EXPECT_EQ(zeroHeight.error().code, "zero_dimensions");
}

TEST(ImageLayout, RejectsStrideSmallerThanRow) {
    const auto layout = ImageLayout::create(32U, 4U, 63U, StorageType::UInt16, 252U);

    ASSERT_FALSE(layout.hasValue());
    EXPECT_EQ(layout.error().code, "stride_too_small");
}

TEST(ImageLayout, RejectsRequiredPayloadOverflow) {
    const auto layout = ImageLayout::create(
        1U,
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::size_t>::max(),
        StorageType::UInt8,
        std::numeric_limits<std::size_t>::max());

    ASSERT_FALSE(layout.hasValue());
    EXPECT_EQ(layout.error().code, "layout_size_overflow");
}

}  // namespace
