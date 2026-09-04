#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Frame.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using lumora::core::AcquisitionSettingsSnapshot;
using lumora::core::BitAlignment;
using lumora::core::BufferPool;
using lumora::core::CameraIdentity;
using lumora::core::DisplayFrame;
using lumora::core::DisplayMapping;
using lumora::core::DisplayStorage;
using lumora::core::FrameBundle;
using lumora::core::FrameMetadata;
using lumora::core::ImageLayout;
using lumora::core::Orientation;
using lumora::core::PipelineVersion;
using lumora::core::ProcessedFrame;
using lumora::core::ProcessingTimings;
using lumora::core::RawFrame;
using lumora::core::RegionOfInterest;
using lumora::core::Rotation;
using lumora::core::SharedBuffer;
using lumora::core::SourcePacking;
using lumora::core::SourcePixelFormat;
using lumora::core::StageTiming;
using lumora::core::StorageType;

[[nodiscard]] SourcePixelFormat mono8() {
    return SourcePixelFormat{
        "Mono8",
        0x01080001U,
        8U,
        255U,
        SourcePacking::Unpacked,
        BitAlignment::LeastSignificant,
        StorageType::UInt8,
    };
}

[[nodiscard]] FrameMetadata metadataFor(
    std::uint32_t width,
    std::uint32_t height) {
    auto snapshot = AcquisitionSettingsSnapshot::create(
        CameraIdentity{"Vendor", "Model", "SIM-1", "Simulator", std::nullopt},
        mono8(),
        RegionOfInterest{0U, 0U, width, height},
        30.0,
        30.0,
        std::nullopt,
        std::nullopt);
    return FrameMetadata{
        std::optional<std::uint64_t>{77U},
        std::chrono::steady_clock::time_point{std::chrono::milliseconds{10}},
        std::chrono::system_clock::time_point{std::chrono::milliseconds{20}},
        std::optional<std::uint64_t>{1234U},
        std::move(snapshot).value(),
    };
}

[[nodiscard]] SharedBuffer bufferOfSize(
    std::size_t size,
    std::byte fill = std::byte{0U}) {
    auto pool = BufferPool::create(1U, size).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), fill);
    return std::move(*lease).seal();
}

[[nodiscard]] ImageLayout layout(
    std::uint32_t width,
    std::uint32_t height,
    StorageType storage) {
    const std::size_t bytesPerPixel = storage == StorageType::UInt8 ? 1U : 2U;
    const auto stride = static_cast<std::size_t>(width) * bytesPerPixel;
    return ImageLayout::create(
               width,
               height,
               stride,
               storage,
               stride * static_cast<std::size_t>(height))
        .value();
}

[[nodiscard]] std::shared_ptr<const RawFrame> rawFrame(
    std::uint64_t frameId,
    std::uint32_t width = 4U,
    std::uint32_t height = 3U,
    std::byte fill = std::byte{0x21}) {
    const auto imageLayout = layout(width, height, StorageType::UInt8);
    return RawFrame::create(
               frameId,
               imageLayout,
               bufferOfSize(imageLayout.payloadBytes(), fill),
               metadataFor(width, height))
        .value();
}

[[nodiscard]] std::shared_ptr<const ProcessedFrame> processedFrame(
    std::uint64_t frameId,
    std::uint32_t width = 4U,
    std::uint32_t height = 3U) {
    const auto imageLayout = layout(width, height, StorageType::UInt16);
    return ProcessedFrame::create(
               frameId,
               imageLayout,
               bufferOfSize(imageLayout.payloadBytes(), std::byte{0x33}),
               PipelineVersion{1U, 1U, 9U},
               ProcessingTimings{
                   {StageTiming{"normalize", std::chrono::microseconds{50}}},
                   std::chrono::microseconds{75},
               })
        .value();
}

[[nodiscard]] std::shared_ptr<const DisplayFrame> displayFrame(
    std::uint64_t frameId,
    Orientation orientation = Orientation{false, false, Rotation::Degrees0},
    std::uint32_t width = 4U,
    std::uint32_t height = 3U) {
    const auto imageLayout = layout(width, height, StorageType::UInt8);
    return DisplayFrame::create(
               frameId,
               imageLayout,
               bufferOfSize(imageLayout.payloadBytes(), std::byte{0x55}),
               DisplayStorage::Gray8,
               DisplayMapping{0U, 65'535U, 255U, 9U},
               orientation)
        .value();
}

[[nodiscard]] std::uint64_t hashBytes(const SharedBuffer& buffer) {
    std::uint64_t hash = 1'469'598'103'934'665'603ULL;
    for (const auto value : buffer.bytes()) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

TEST(RawFrame, AcceptsCompleteValidatedNativeFrame) {
    const auto frame = rawFrame(41U);

    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->frameId, 41U);
    EXPECT_EQ(frame->layout.width(), 4U);
    EXPECT_EQ(frame->pixels.bytes().size(), 12U);
    EXPECT_EQ(frame->metadata.acquisitionSettings.sourceFormat.canonicalName, "Mono8");
}

TEST(RawFrame, RejectsBufferAndAcquisitionDescriptionMismatch) {
    const auto imageLayout = layout(4U, 3U, StorageType::UInt8);
    const auto shortBuffer = RawFrame::create(
        1U, imageLayout, bufferOfSize(11U), metadataFor(4U, 3U));
    const auto wrongRoi = RawFrame::create(
        1U, imageLayout, bufferOfSize(12U), metadataFor(3U, 4U));

    ASSERT_FALSE(shortBuffer.hasValue());
    EXPECT_EQ(shortBuffer.error().code, "frame_buffer_too_small");
    ASSERT_FALSE(wrongRoi.hasValue());
    EXPECT_EQ(wrongRoi.error().code, "frame_roi_mismatch");
}

TEST(ProcessedFrame, RequiresUnsignedSixteenBitNativePixelsAndValidTimings) {
    const auto uint8Layout = layout(4U, 3U, StorageType::UInt8);
    const auto wrongStorage = ProcessedFrame::create(
        1U,
        uint8Layout,
        bufferOfSize(uint8Layout.payloadBytes()),
        PipelineVersion{1U, 1U, 1U},
        ProcessingTimings{{}, std::chrono::nanoseconds{1}});
    const auto negativeTiming = ProcessedFrame::create(
        1U,
        layout(4U, 3U, StorageType::UInt16),
        bufferOfSize(24U),
        PipelineVersion{1U, 1U, 1U},
        ProcessingTimings{
            {StageTiming{"normalize", std::chrono::nanoseconds{-1}}},
            std::chrono::nanoseconds{1},
        });

    ASSERT_FALSE(wrongStorage.hasValue());
    EXPECT_EQ(wrongStorage.error().code, "processed_storage_not_u16");
    ASSERT_FALSE(negativeTiming.hasValue());
    EXPECT_EQ(negativeTiming.error().code, "negative_processing_timing");
}

TEST(DisplayFrame, ValidatesStorageAndDisplayMapping) {
    const auto uint16Layout = layout(4U, 3U, StorageType::UInt16);
    const auto storageMismatch = DisplayFrame::create(
        1U,
        uint16Layout,
        bufferOfSize(uint16Layout.payloadBytes()),
        DisplayStorage::Gray8,
        DisplayMapping{0U, 65'535U, 255U, 1U},
        Orientation{false, false, Rotation::Degrees0});
    const auto invalidMapping = DisplayFrame::create(
        1U,
        layout(4U, 3U, StorageType::UInt8),
        bufferOfSize(12U),
        DisplayStorage::Gray8,
        DisplayMapping{100U, 99U, 255U, 1U},
        Orientation{false, false, Rotation::Degrees0});
    const auto unknownStorage = DisplayFrame::create(
        1U,
        layout(4U, 3U, StorageType::UInt8),
        bufferOfSize(12U),
        static_cast<DisplayStorage>(99),
        DisplayMapping{0U, 65'535U, 255U, 1U},
        Orientation{false, false, Rotation::Degrees0});

    ASSERT_FALSE(storageMismatch.hasValue());
    EXPECT_EQ(storageMismatch.error().code, "display_storage_mismatch");
    ASSERT_FALSE(invalidMapping.hasValue());
    EXPECT_EQ(invalidMapping.error().code, "invalid_display_mapping");
    ASSERT_FALSE(unknownStorage.hasValue());
    EXPECT_EQ(unknownStorage.error().code, "unsupported_display_storage");
}

TEST(FrameBundle, RejectsMismatchedFrameIds) {
    const auto bundle = FrameBundle::create(
        rawFrame(41U), displayFrame(42U), nullptr, nullptr);

    ASSERT_FALSE(bundle.hasValue());
    EXPECT_EQ(bundle.error().code, "frame_id_mismatch");
}

TEST(FrameBundle, RejectsIncompleteEnhancedPair) {
    const auto missingDisplay = FrameBundle::create(
        rawFrame(41U), displayFrame(41U), processedFrame(41U), nullptr);
    const auto missingProcessed = FrameBundle::create(
        rawFrame(41U), displayFrame(41U), nullptr, displayFrame(41U));

    ASSERT_FALSE(missingDisplay.hasValue());
    EXPECT_EQ(missingDisplay.error().code, "incomplete_enhanced_pair");
    ASSERT_FALSE(missingProcessed.hasValue());
    EXPECT_EQ(missingProcessed.error().code, "incomplete_enhanced_pair");
}

TEST(FrameBundle, ValidatesRotatedDimensionsAndSharedOrientation) {
    const Orientation clockwise{true, false, Rotation::Degrees90};
    const auto raw = rawFrame(51U, 4U, 3U);
    const auto processed = processedFrame(51U, 4U, 3U);

    const auto wrongDimensions = FrameBundle::create(
        raw, displayFrame(51U, clockwise, 4U, 3U), nullptr, nullptr);
    const auto wrongEnhancedOrientation = FrameBundle::create(
        raw,
        displayFrame(51U, clockwise, 3U, 4U),
        processed,
        displayFrame(51U, Orientation{false, false, Rotation::Degrees90}, 3U, 4U));
    const auto valid = FrameBundle::create(
        raw,
        displayFrame(51U, clockwise, 3U, 4U),
        processed,
        displayFrame(51U, clockwise, 3U, 4U));

    ASSERT_FALSE(wrongDimensions.hasValue());
    EXPECT_EQ(wrongDimensions.error().code, "presentation_dimensions_mismatch");
    ASSERT_FALSE(wrongEnhancedOrientation.hasValue());
    EXPECT_EQ(wrongEnhancedOrientation.error().code, "presentation_orientation_mismatch");
    ASSERT_TRUE(valid.hasValue());
    EXPECT_EQ(valid.value()->sourceFrameId(), 51U);
    EXPECT_EQ(valid.value()->enhanced->sourceFrameId, 51U);
    EXPECT_EQ(valid.value()->enhancedDisplay->sourceFrameId, 51U);
}

TEST(FrameBundle, ConstructionCannotMutateRawPixels) {
    const auto raw = rawFrame(61U, 4U, 3U, std::byte{0x6B});
    const auto before = hashBytes(raw->pixels);
    auto unrelatedPool = BufferPool::create(1U, 12U).value();
    auto unrelatedLease = unrelatedPool->tryAcquire();

    const auto bundle = FrameBundle::create(
        raw, displayFrame(61U), processedFrame(61U), displayFrame(61U));
    std::ranges::fill(unrelatedLease->bytes(), std::byte{0xFF});

    ASSERT_TRUE(bundle.hasValue());
    EXPECT_EQ(hashBytes(raw->pixels), before);
}

}  // namespace
