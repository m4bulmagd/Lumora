#include <lumora/core/FrameMetadata.hpp>
#include <lumora/core/PixelFormat.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>

namespace {

using lumora::core::AcquisitionSettingsSnapshot;
using lumora::core::BitAlignment;
using lumora::core::CameraIdentity;
using lumora::core::RegionOfInterest;
using lumora::core::SourcePacking;
using lumora::core::SourcePixelFormat;
using lumora::core::StorageType;
using lumora::core::validateSourcePixelFormat;

[[nodiscard]] SourcePixelFormat mono12(std::uint16_t sampleMaximum = 4095U) {
    return SourcePixelFormat{
        "Mono12",
        0x01100005U,
        12U,
        sampleMaximum,
        SourcePacking::Unpacked,
        BitAlignment::LeastSignificant,
        StorageType::UInt16,
    };
}

TEST(SourcePixelFormat, AcceptsCapabilityDerivedNonPowerOfTwoMaximum) {
    const auto result = validateSourcePixelFormat(mono12(4000U));

    EXPECT_TRUE(result.hasValue());
}

TEST(SourcePixelFormat, RejectsZeroAndUnsupportedValidBitCounts) {
    auto zeroBits = mono12();
    zeroBits.validBits = 0U;
    auto tooManyBits = mono12();
    tooManyBits.validBits = 17U;
    auto tooManyForUInt8 = mono12(255U);
    tooManyForUInt8.validBits = 9U;
    tooManyForUInt8.applicationStorage = StorageType::UInt8;

    const auto zeroResult = validateSourcePixelFormat(zeroBits);
    const auto tooManyResult = validateSourcePixelFormat(tooManyBits);
    const auto storageResult = validateSourcePixelFormat(tooManyForUInt8);

    ASSERT_FALSE(zeroResult.hasValue());
    EXPECT_EQ(zeroResult.error().code, "invalid_valid_bits");
    ASSERT_FALSE(tooManyResult.hasValue());
    EXPECT_EQ(tooManyResult.error().code, "invalid_valid_bits");
    ASSERT_FALSE(storageResult.hasValue());
    EXPECT_EQ(storageResult.error().code, "valid_bits_exceed_storage");
}

TEST(SourcePixelFormat, RejectsMaximumOutsideDeclaredBitRange) {
    const auto zeroMaximum = validateSourcePixelFormat(mono12(0U));
    const auto excessiveMaximum = validateSourcePixelFormat(mono12(4096U));

    ASSERT_FALSE(zeroMaximum.hasValue());
    EXPECT_EQ(zeroMaximum.error().code, "invalid_sample_maximum");
    ASSERT_FALSE(excessiveMaximum.hasValue());
    EXPECT_EQ(excessiveMaximum.error().code, "sample_maximum_exceeds_valid_bits");
}

TEST(SourcePixelFormat, RejectsUnknownPackingAndAlignment) {
    auto unknownPacking = mono12();
    unknownPacking.packing = static_cast<SourcePacking>(99);
    auto unknownAlignment = mono12();
    unknownAlignment.alignment = static_cast<BitAlignment>(99);

    const auto packingResult = validateSourcePixelFormat(unknownPacking);
    const auto alignmentResult = validateSourcePixelFormat(unknownAlignment);

    ASSERT_FALSE(packingResult.hasValue());
    EXPECT_EQ(packingResult.error().code, "unsupported_source_packing");
    ASSERT_FALSE(alignmentResult.hasValue());
    EXPECT_EQ(alignmentResult.error().code, "unsupported_bit_alignment");
}

TEST(FrameMetadata, SnapshotPreservesCompleteAppliedContext) {
    const CameraIdentity identity{
        "Basler",
        "ExampleModel",
        "40123456",
        "GigE",
        std::optional<std::string>{"1.2.3"},
    };
    const RegionOfInterest roi{16U, 24U, 2048U, 2048U};

    const auto snapshot = AcquisitionSettingsSnapshot::create(
        identity, mono12(), roi, 30.0, 29.97, 8000.0, 1.5);

    ASSERT_TRUE(snapshot.hasValue());
    EXPECT_EQ(snapshot.value().camera.serial, "40123456");
    EXPECT_EQ(snapshot.value().sourceFormat.canonicalName, "Mono12");
    EXPECT_EQ(snapshot.value().roi.width, 2048U);
    EXPECT_DOUBLE_EQ(snapshot.value().requestedFps, 30.0);
    EXPECT_DOUBLE_EQ(snapshot.value().actualFps, 29.97);
    ASSERT_TRUE(snapshot.value().exposureMicroseconds.has_value());
    EXPECT_DOUBLE_EQ(*snapshot.value().exposureMicroseconds, 8000.0);
    ASSERT_TRUE(snapshot.value().gainDb.has_value());
    EXPECT_DOUBLE_EQ(*snapshot.value().gainDb, 1.5);
}

TEST(FrameMetadata, SnapshotRejectsInvalidSourceDescriptor) {
    auto format = mono12();
    format.sampleMaximum = 5000U;

    const auto snapshot = AcquisitionSettingsSnapshot::create(
        CameraIdentity{"Vendor", "Model", "Serial", "Simulator", std::nullopt},
        format,
        RegionOfInterest{0U, 0U, 16U, 16U},
        30.0,
        30.0,
        std::nullopt,
        std::nullopt);

    ASSERT_FALSE(snapshot.hasValue());
    EXPECT_EQ(snapshot.error().code, "sample_maximum_exceeds_valid_bits");
}

}  // namespace
