#include <lumora/processing/IProcessingStage.hpp>
#include <gtest/gtest.h>
#include <array>

namespace {
using namespace lumora::processing;
using lumora::core::ImageLayout;
using lumora::core::StorageType;

TEST(ImageView, RejectsSpanShorterThanDeclaredPayloadForBothAccessModes) {
    const auto layout = ImageLayout::create(2, 2, 6, StorageType::UInt16, 14).value();
    std::array<std::byte, 16> storage{};
    for (std::size_t size : {0U, 11U, 12U, 13U}) {
        const auto bytes = std::span(storage).first(size);
        EXPECT_FALSE(ImageView::create(layout, bytes, ImageDomain::CanonicalU16).hasValue());
        EXPECT_FALSE(MutableImageView::create(layout, bytes, ImageDomain::CanonicalU16).hasValue());
    }
}

TEST(ImageView, RejectsCanonicalU8AndUnknownDomainForBothAccessModes) {
    std::array<std::byte, 4> storage{};
    const auto layout = ImageLayout::create(2, 2, 2, StorageType::UInt8, 4).value();
    for (auto domain : {ImageDomain::CanonicalU16, static_cast<ImageDomain>(99)}) {
        EXPECT_FALSE(ImageView::create(layout, storage, domain).hasValue());
        EXPECT_FALSE(MutableImageView::create(layout, storage, domain).hasValue());
    }
}

TEST(ImageView, ExposesPixelRowsWithoutPaddingOrSurplusStorage) {
    std::array<std::byte, 16> storage{};
    const auto layout = ImageLayout::create(2, 2, 6, StorageType::UInt16, 12).value();
    auto result = MutableImageView::create(layout, storage, ImageDomain::CanonicalU16);
    ASSERT_TRUE(result.hasValue());
    auto view = result.value();
    EXPECT_EQ(view.bytes().size(), 12U);
    ASSERT_EQ(view.row(1).size(), 4U);
    view.row(1)[2] = std::byte{42};
    EXPECT_EQ(storage[8], std::byte{42});
    EXPECT_EQ(storage[10], std::byte{0});
    auto readOnly = view.asConst();
    EXPECT_EQ(readOnly.bytes().size(), 12U);
    ASSERT_EQ(readOnly.row(1).size(), 4U);
    EXPECT_EQ(readOnly.row(1)[2], std::byte{42});
    EXPECT_TRUE(readOnly.row(2).empty());
    EXPECT_TRUE(view.row(2).empty());
}

TEST(ImageView, SupportsBothSensorStorageTypesAndOddByteStridesWithoutTypedPointerAccess) {
    std::array<std::byte, 16> storage{};
    for (auto type : {StorageType::UInt8, StorageType::UInt16}) {
        const auto layout = ImageLayout::create(2, 2, 5, type, 10).value();
        const auto result = ImageView::create(layout, std::span(storage).subspan(1), ImageDomain::SensorNative);
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(result.value().row(1).data(), storage.data() + 6);
        EXPECT_EQ(result.value().row(1).size(), type == StorageType::UInt8 ? 2U : 4U);
    }
}

TEST(ImageView, DetectsPartialIdenticalAndPaddingOverlapButAllowsTouchingRanges) {
    std::array<std::byte, 24> storage{};
    const auto layout = ImageLayout::create(2, 2, 4, StorageType::UInt8, 8).value();
    const auto source = ImageView::create(layout, std::span(storage).subspan(8), ImageDomain::SensorNative).value();
    for (std::size_t offset : {0U, 1U, 7U, 8U, 9U, 15U, 16U}) {
        auto destination = MutableImageView::create(layout, std::span(storage).subspan(offset), ImageDomain::SensorNative).value();
        EXPECT_EQ(overlaps(source, destination), offset != 0 && offset != 16) << offset;
    }
}
} // namespace
