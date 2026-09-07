#include <lumora/processing/NormalizeStage.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace lumora::processing {
namespace {

using core::BitAlignment;
using core::ImageLayout;
using core::SourcePacking;
using core::SourcePixelFormat;
using core::StorageType;

[[nodiscard]] SourcePixelFormat sourceFormat(
    std::uint8_t validBits,
    std::uint16_t sampleMaximum,
    StorageType storage,
    SourcePacking packing = SourcePacking::Unpacked,
    BitAlignment alignment = BitAlignment::LeastSignificant) {
    return {"TestMono", 1U, validBits, sampleMaximum, packing, alignment, storage};
}

template<typename Sample, std::size_t Size>
[[nodiscard]] std::array<std::uint16_t, Size> normalize(
    const std::array<Sample, Size>& samples,
    const SourcePixelFormat& format) {
    std::array<std::byte, Size * sizeof(std::uint16_t)> outputBytes{};
    const auto inputBytes = std::as_bytes(std::span(samples));
    const auto inputLayout = ImageLayout::create(
        static_cast<std::uint32_t>(Size), 1U, inputBytes.size(),
        format.applicationStorage, inputBytes.size()).value();
    const auto outputLayout = ImageLayout::create(
        static_cast<std::uint32_t>(Size), 1U, outputBytes.size(),
        StorageType::UInt16, outputBytes.size()).value();
    const auto input = ImageView::create(
        inputLayout, inputBytes, ImageDomain::SensorNative).value();
    const auto output = MutableImageView::create(
        outputLayout, outputBytes, ImageDomain::CanonicalU16).value();

    const auto result = NormalizeStage{}.process(input, output, format);
    EXPECT_TRUE(result.hasValue()) << (result.hasValue() ? "" : result.error().diagnosticDetail);

    std::array<std::uint16_t, Size> values{};
    std::memcpy(values.data(), outputBytes.data(), outputBytes.size());
    return values;
}

// Using the observed frame range or truncating division breaks these literals.
TEST(NormalizeStage, ScalesKnownBitDepthsWithExactIntegerRounding) {
    EXPECT_EQ(normalize(std::array<std::uint8_t, 4>{0U, 1U, 128U, 255U},
                  sourceFormat(8U, 255U, StorageType::UInt8)),
              (std::array<std::uint16_t, 4>{0U, 257U, 32896U, 65535U}));
    EXPECT_EQ(normalize(std::array<std::uint16_t, 4>{0U, 1U, 512U, 1023U},
                  sourceFormat(10U, 1023U, StorageType::UInt16)),
              (std::array<std::uint16_t, 4>{0U, 64U, 32800U, 65535U}));
    EXPECT_EQ(normalize(std::array<std::uint16_t, 4>{0U, 1U, 2048U, 4095U},
                  sourceFormat(12U, 4095U, StorageType::UInt16)),
              (std::array<std::uint16_t, 4>{0U, 16U, 32776U, 65535U}));
    EXPECT_EQ(normalize(std::array<std::uint16_t, 4>{0U, 1U, 32768U, 65535U},
                  sourceFormat(16U, 65535U, StorageType::UInt16)),
              (std::array<std::uint16_t, 4>{0U, 1U, 32768U, 65535U}));
}

// Special-casing powers of two or dividing by zero breaks these valid maxima.
TEST(NormalizeStage, SupportsMaximumOneAndNonPowerOfTwoMaxima) {
    EXPECT_EQ(normalize(std::array<std::uint8_t, 2>{0U, 1U},
                  sourceFormat(1U, 1U, StorageType::UInt8)),
              (std::array<std::uint16_t, 2>{0U, 65535U}));
    EXPECT_EQ(normalize(std::array<std::uint16_t, 5>{0U, 1U, 499U, 500U, 1000U},
                  sourceFormat(10U, 1000U, StorageType::UInt16)),
              (std::array<std::uint16_t, 5>{0U, 66U, 32702U, 32768U, 65535U}));
}

// Typed row access, whole-payload copying, or provenance shifts corrupt this fixture.
TEST(NormalizeStage, HandlesUnalignedPaddedRowsAndPreservesPaddingAndSource) {
    constexpr std::size_t inputStride = 7U;
    constexpr std::size_t outputStride = 9U;
    std::array<std::byte, 1U + inputStride * 2U> inputStorage{};
    std::array<std::byte, 1U + outputStride * 2U> outputStorage{};
    inputStorage.fill(std::byte{0xA5});
    outputStorage.fill(std::byte{0xCC});
    const std::array<std::uint16_t, 4> samples{1U, 2048U, 4095U, 17U};
    std::memcpy(inputStorage.data() + 1U, samples.data(), 4U);
    std::memcpy(inputStorage.data() + 1U + inputStride, samples.data() + 2U, 4U);
    const auto before = inputStorage;
    const auto inputLayout = ImageLayout::create(2U, 2U, inputStride, StorageType::UInt16,
        inputStride * 2U).value();
    const auto outputLayout = ImageLayout::create(2U, 2U, outputStride, StorageType::UInt16,
        outputStride * 2U).value();
    const auto input = ImageView::create(inputLayout,
        std::span(inputStorage).subspan(1U), ImageDomain::SensorNative).value();
    const auto output = MutableImageView::create(outputLayout,
        std::span(outputStorage).subspan(1U), ImageDomain::CanonicalU16).value();
    const auto format = sourceFormat(12U, 4095U, StorageType::UInt16,
        SourcePacking::Packed, BitAlignment::MostSignificant);

    const auto result = NormalizeStage{}.process(input, output, format);

    ASSERT_TRUE(result.hasValue());
    std::array<std::uint16_t, 4> actual{};
    std::memcpy(actual.data(), outputStorage.data() + 1U, 4U);
    std::memcpy(actual.data() + 2U, outputStorage.data() + 1U + outputStride, 4U);
    EXPECT_EQ(actual, (std::array<std::uint16_t, 4>{16U, 32776U, 65535U, 272U}));
    EXPECT_EQ(inputStorage, before);
    EXPECT_EQ(outputStorage[0], std::byte{0xCC});
    for (std::size_t index : {5U, 6U, 7U, 8U, 9U, 14U, 15U, 16U, 17U, 18U})
        EXPECT_EQ(outputStorage[index], std::byte{0xCC}) << index;
}

// Per-frame auto-ranging makes the shared value differ between these calls.
TEST(NormalizeStage, SameValueIsStableAcrossFramesWithDifferentObservedRanges) {
    const auto format = sourceFormat(12U, 4095U, StorageType::UInt16);
    const auto first = normalize(std::array<std::uint16_t, 3>{0U, 1000U, 4095U}, format);
    const auto second = normalize(std::array<std::uint16_t, 3>{900U, 1000U, 1100U}, format);
    EXPECT_EQ(first[1], 16004U);
    EXPECT_EQ(second[1], 16004U);
}

struct ViewFixture final {
    std::array<std::byte, 64> sourceBytes{};
    std::array<std::byte, 64> destinationBytes{};

    [[nodiscard]] ImageView source(
        std::uint32_t width = 2U, std::uint32_t height = 2U,
        StorageType storage = StorageType::UInt16,
        ImageDomain domain = ImageDomain::SensorNative) {
        const auto stride = static_cast<std::size_t>(width) *
            (storage == StorageType::UInt8 ? 1U : 2U);
        return ImageView::create(ImageLayout::create(width, height, stride, storage,
            stride * height).value(), sourceBytes, domain).value();
    }

    [[nodiscard]] MutableImageView destination(
        std::uint32_t width = 2U, std::uint32_t height = 2U,
        ImageDomain domain = ImageDomain::CanonicalU16) {
        const auto stride = static_cast<std::size_t>(width) * 2U;
        return MutableImageView::create(ImageLayout::create(width, height, stride,
            StorageType::UInt16, stride * height).value(), destinationBytes, domain).value();
    }
};

// Bypassing the core descriptor validator accepts malformed source metadata.
TEST(NormalizeStage, RejectsInvalidSourceDescriptorBeforeWriting) {
    ViewFixture fixture;
    fixture.destinationBytes.fill(std::byte{0x7B});
    const auto before = fixture.destinationBytes;
    auto invalid = sourceFormat(12U, 4095U, StorageType::UInt16);
    invalid.sampleMaximum = 4096U;

    const auto result = NormalizeStage{}.process(
        fixture.source(), fixture.destination(), invalid);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "sample_maximum_exceeds_valid_bits");
    EXPECT_EQ(fixture.destinationBytes, before);
}

// Omitting any compatibility gate allows reinterpretation or malformed writes.
TEST(NormalizeStage, RejectsStorageExtentAndDomainMismatchesBeforeWriting) {
    struct Case final {
        ImageView source;
        MutableImageView destination;
        SourcePixelFormat format;
        const char* code;
    };
    ViewFixture fixture;
    fixture.destinationBytes.fill(std::byte{0x61});
    const auto before = fixture.destinationBytes;
    std::vector<Case> cases;
    cases.push_back({fixture.source(2U, 2U, StorageType::UInt8), fixture.destination(),
        sourceFormat(8U, 255U, StorageType::UInt16), "source_storage_mismatch"});
    cases.push_back({fixture.source(), fixture.destination(3U, 2U),
        sourceFormat(12U, 4095U, StorageType::UInt16), "image_extent_mismatch"});
    cases.push_back({fixture.source(2U, 2U, StorageType::UInt16, ImageDomain::CanonicalU16),
        fixture.destination(), sourceFormat(12U, 4095U, StorageType::UInt16),
        "image_domain_mismatch"});
    cases.push_back({fixture.source(),
        fixture.destination(2U, 2U, ImageDomain::SensorNative),
        sourceFormat(12U, 4095U, StorageType::UInt16), "image_domain_mismatch"});

    for (const auto& testCase : cases) {
        const auto result = NormalizeStage{}.process(
            testCase.source, testCase.destination, testCase.format);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().category, core::ErrorCategory::Processing);
        EXPECT_EQ(result.error().code, testCase.code);
        EXPECT_EQ(fixture.destinationBytes, before);
    }
}

// A pointer-equality-only overlap check misses the offset destination.
TEST(NormalizeStage, RejectsFullAndPartialOverlapBeforeWriting) {
    std::array<std::byte, 32> storage{};
    storage.fill(std::byte{0x4D});
    const auto before = storage;
    const auto sourceLayout = ImageLayout::create(
        2U, 2U, 4U, StorageType::UInt16, 8U).value();
    const auto destinationLayout = ImageLayout::create(
        2U, 2U, 4U, StorageType::UInt16, 8U).value();
    const auto source = ImageView::create(
        sourceLayout, storage, ImageDomain::SensorNative).value();
    const auto format = sourceFormat(12U, 4095U, StorageType::UInt16);

    for (const std::size_t offset : {0U, 4U}) {
        const auto destination = MutableImageView::create(destinationLayout,
            std::span(storage).subspan(offset), ImageDomain::CanonicalU16).value();
        const auto result = NormalizeStage{}.process(source, destination, format);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "image_views_overlap");
        EXPECT_EQ(storage, before);
    }
}

// Clamping malformed input hides acquisition corruption and loses its location.
TEST(NormalizeStage, ReportsFirstSampleAboveMaximumWithCoordinateValueAndMaximum) {
    const std::array<std::uint16_t, 6> samples{0U, 100U, 200U, 300U, 1001U, 1200U};
    std::array<std::byte, 12> outputBytes{};
    outputBytes.fill(std::byte{0xEF});
    const auto layout = ImageLayout::create(3U, 2U, 6U, StorageType::UInt16, 12U).value();
    const auto source = ImageView::create(layout, std::as_bytes(std::span(samples)),
        ImageDomain::SensorNative).value();
    const auto destination = MutableImageView::create(layout, outputBytes,
        ImageDomain::CanonicalU16).value();

    const auto result = NormalizeStage{}.process(source, destination,
        sourceFormat(10U, 1000U, StorageType::UInt16));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::Processing);
    EXPECT_EQ(result.error().code, "sample_exceeds_source_maximum");
    EXPECT_NE(result.error().diagnosticDetail.find("x=1"), std::string::npos);
    EXPECT_NE(result.error().diagnosticDetail.find("y=1"), std::string::npos);
    EXPECT_NE(result.error().diagnosticDetail.find("value=1001"), std::string::npos);
    EXPECT_NE(result.error().diagnosticDetail.find("maximum=1000"), std::string::npos);
    std::uint16_t untouched = 0U;
    std::memcpy(&untouched, outputBytes.data() + 10U, sizeof(untouched));
    EXPECT_EQ(untouched, 0xEFEFU);
}

// Incorrect stage metadata lets the compiler/executor route incompatible views.
TEST(NormalizeStage, IdentifiesItsFixedDomainContract) {
    const NormalizeStage stage;
    EXPECT_EQ(stage.id(), StageId::Normalize);
    EXPECT_EQ(stage.traits().id, StageId::Normalize);
    EXPECT_EQ(stage.traits().inputDomain, ImageDomain::SensorNative);
    EXPECT_EQ(stage.traits().outputDomain, ImageDomain::CanonicalU16);
    EXPECT_FALSE(stage.traits().changesDimensions);
    EXPECT_EQ(stage.traits().scratchImages, 0U);
    EXPECT_EQ(stage.traits().historyFrames, 0U);
    EXPECT_FALSE(stage.traits().requiresCalibrationAsset);
    EXPECT_EQ(stage.traits().backend, ExecutionBackend::Cpu);
}

}  // namespace
}  // namespace lumora::processing
