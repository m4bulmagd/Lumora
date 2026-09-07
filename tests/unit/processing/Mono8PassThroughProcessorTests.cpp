#include <lumora/processing/Mono8PassThroughProcessor.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace lumora::processing {
namespace {

core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
            core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
}

core::FrameMetadata metadata(
    std::uint32_t width,
    std::uint32_t height,
    core::SourcePixelFormat format = mono8(),
    double actualFps = 30.0) {
    auto settings = core::AcquisitionSettingsSnapshot::create(
        {"Test", "Camera", "1", "virtual", std::nullopt}, std::move(format),
        {0U, 0U, width, height}, 30.0, actualFps, std::nullopt, std::nullopt);
    return {{}, std::chrono::steady_clock::time_point{},
            std::chrono::system_clock::time_point{}, {},
            std::move(settings).value()};
}

std::shared_ptr<const core::RawFrame> makeRaw(
    core::BufferPool& pool, std::uint64_t id,
    std::uint32_t width, std::uint32_t height, std::size_t stride) {
    auto lease = pool.tryAcquire();
    EXPECT_TRUE(lease.has_value());
    auto bytes = lease->bytes();
    std::ranges::fill(bytes, std::byte{0xEE});
    bytes[0] = std::byte{1U};
    bytes[1] = std::byte{2U};
    bytes[2] = std::byte{3U};
    bytes[stride] = std::byte{4U};
    bytes[stride + 1U] = std::byte{5U};
    bytes[stride + 2U] = std::byte{6U};
    auto layout = core::ImageLayout::create(
        width, height, stride, core::StorageType::UInt8, stride * height);
    return core::RawFrame::create(
        id, std::move(layout).value(), std::move(*lease).seal(),
        metadata(width, height)).value();
}

// Copying payloads wholesale instead of active row bytes breaks this test.
TEST(Mono8PassThroughProcessor, CopiesOnlyActiveBytesFromPaddedRows) {
    auto rawPool = core::BufferPool::create(1U, 10U).value();
    auto displayPool = core::BufferPool::create(1U, 6U).value();
    auto raw = makeRaw(*rawPool, 41U, 3U, 2U, 5U);
    Mono8PassThroughProcessor processor(*displayPool);

    auto result = processor.process(raw);

    ASSERT_TRUE(result.hasValue());
    const auto& display = result.value()->originalDisplay;
    ASSERT_TRUE(display);
    EXPECT_EQ(display->sourceFrameId, 41U);
    EXPECT_EQ(display->layout.strideBytes(), 3U);
    const auto bytes = display->pixels.bytes();
    ASSERT_EQ(bytes.size(), 6U);
    EXPECT_EQ(bytes[0], std::byte{1U});
    EXPECT_EQ(bytes[1], std::byte{2U});
    EXPECT_EQ(bytes[2], std::byte{3U});
    EXPECT_EQ(bytes[3], std::byte{4U});
    EXPECT_EQ(bytes[4], std::byte{5U});
    EXPECT_EQ(bytes[5], std::byte{6U});
}

// Narrowing or remapping a legal Mono8 sample breaks an exact byte assertion.
TEST(Mono8PassThroughProcessor, PreservesEveryMono8SampleValue) {
    auto rawPool = core::BufferPool::create(1U, 256U).value();
    auto displayPool = core::BufferPool::create(1U, 256U).value();
    auto lease = rawPool->tryAcquire();
    ASSERT_TRUE(lease);
    for (std::size_t value = 0U; value < 256U; ++value) {
        lease->bytes()[value] = static_cast<std::byte>(value);
    }
    auto layout = core::ImageLayout::create(
        256U, 1U, 256U, core::StorageType::UInt8, 256U).value();
    auto raw = core::RawFrame::create(
        7U, layout, std::move(*lease).seal(), metadata(256U, 1U)).value();
    Mono8PassThroughProcessor processor(*displayPool);

    auto result = processor.process(raw);

    ASSERT_TRUE(result.hasValue());
    const auto output = result.value()->originalDisplay->pixels.bytes();
    ASSERT_EQ(output.size(), 256U);
    for (std::size_t value = 0U; value < 256U; ++value) {
        EXPECT_EQ(output[value], static_cast<std::byte>(value));
    }
}

// Reusing raw storage or mislabelling pass-through output breaks bundle shape.
TEST(Mono8PassThroughProcessor, ProducesOriginalOnlyIdentityOrientedGray8Bundle) {
    auto rawPool = core::BufferPool::create(1U, 6U).value();
    auto displayPool = core::BufferPool::create(1U, 6U).value();
    auto raw = makeRaw(*rawPool, 19U, 3U, 2U, 3U);
    const auto rawBytes = raw->pixels.bytes().data();
    Mono8PassThroughProcessor processor(*displayPool);

    auto result = processor.process(raw);

    ASSERT_TRUE(result.hasValue());
    const auto& bundle = result.value();
    EXPECT_EQ(bundle->raw, raw);
    EXPECT_FALSE(bundle->enhanced);
    EXPECT_FALSE(bundle->enhancedDisplay);
    ASSERT_TRUE(bundle->originalDisplay);
    EXPECT_NE(bundle->originalDisplay->pixels.bytes().data(), rawBytes);
    EXPECT_EQ(bundle->originalDisplay->storage, core::DisplayStorage::Gray8);
    EXPECT_EQ(bundle->originalDisplay->mapping,
              (core::DisplayMapping{0U, 255U, 255U, 1U}));
    EXPECT_EQ(bundle->originalDisplay->presentationOrientation,
              (core::Orientation{false, false, core::Rotation::Degrees0}));
}

// Accepting a near-Mono8 descriptor can silently reinterpret source pixels.
TEST(Mono8PassThroughProcessor, RejectsEveryDescriptorMismatch) {
    auto displayPool = core::BufferPool::create(1U, 12U).value();
    Mono8PassThroughProcessor processor(*displayPool);
    std::vector<core::SourcePixelFormat> mismatches;
    auto append = [&](auto mutation) {
        auto format = mono8();
        mutation(format);
        mismatches.push_back(std::move(format));
    };
    append([](auto& format) { format.canonicalName = "Mono8Alternate"; });
    append([](auto& format) { format.canonicalEncoding = 0x01080002U; });
    append([](auto& format) { format.validBits = 7U; format.sampleMaximum = 127U; });
    append([](auto& format) { format.sampleMaximum = 254U; });
    append([](auto& format) { format.packing = core::SourcePacking::Packed; });
    append([](auto& format) { format.alignment = core::BitAlignment::MostSignificant; });
    append([](auto& format) { format.applicationStorage = core::StorageType::UInt16; });

    for (auto& format : mismatches) {
        const auto bytesPerPixel =
            format.applicationStorage == core::StorageType::UInt8 ? 1U : 2U;
        const auto stride = 3U * bytesPerPixel;
        auto rawPool = core::BufferPool::create(1U, stride * 2U).value();
        auto lease = rawPool->tryAcquire();
        ASSERT_TRUE(lease);
        auto layout = core::ImageLayout::create(
            3U, 2U, stride, format.applicationStorage, stride * 2U).value();
        auto raw = core::RawFrame::create(
            1U, layout, std::move(*lease).seal(), metadata(3U, 2U, format)).value();

        const auto result = processor.process(std::move(raw));

        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().category, core::ErrorCategory::Processing);
        EXPECT_EQ(result.error().code, "processing_format_not_available");
    }
}

// Presenting frames with an invalid measured rate violates metadata validity.
TEST(Mono8PassThroughProcessor, RejectsNonPositiveOrNonFiniteActualFps) {
    auto displayPool = core::BufferPool::create(1U, 4U).value();
    Mono8PassThroughProcessor processor(*displayPool);
    for (const double actualFps : {
             0.0,
             -1.0,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        auto rawPool = core::BufferPool::create(1U, 4U).value();
        auto lease = rawPool->tryAcquire();
        ASSERT_TRUE(lease);
        auto layout = core::ImageLayout::create(
            2U, 2U, 2U, core::StorageType::UInt8, 4U).value();
        auto raw = core::RawFrame::create(
            1U, layout, std::move(*lease).seal(),
            metadata(2U, 2U, mono8(), actualFps)).value();

        const auto result = processor.process(std::move(raw));

        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().category, core::ErrorCategory::Processing);
        EXPECT_EQ(result.error().code, "processing_metadata_invalid");
    }
}

// Writing display pixels into the source lease breaks the immutable raw copy.
TEST(Mono8PassThroughProcessor, LeavesRawPixelsIncludingPaddingUnchanged) {
    auto rawPool = core::BufferPool::create(1U, 10U).value();
    auto displayPool = core::BufferPool::create(1U, 6U).value();
    auto raw = makeRaw(*rawPool, 2U, 3U, 2U, 5U);
    const std::vector<std::byte> before(
        raw->pixels.bytes().begin(), raw->pixels.bytes().end());
    Mono8PassThroughProcessor processor(*displayPool);

    const auto result = processor.process(raw);

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(std::ranges::equal(before, raw->pixels.bytes()));
}

// Leaking either shared buffer owner prevents the pool leases from returning.
TEST(Mono8PassThroughProcessor, ReleasesSharedOwnersWithTheBundle) {
    auto rawPool = core::BufferPool::create(1U, 6U).value();
    auto displayPool = core::BufferPool::create(1U, 6U).value();
    Mono8PassThroughProcessor processor(*displayPool);
    {
        auto raw = makeRaw(*rawPool, 3U, 3U, 2U, 3U);
        auto result = processor.process(std::move(raw));
        ASSERT_TRUE(result.hasValue());
        EXPECT_EQ(rawPool->stats().inUse, 1U);
        EXPECT_EQ(displayPool->stats().inUse, 1U);
    }
    EXPECT_EQ(rawPool->stats().inUse, 0U);
    EXPECT_EQ(displayPool->stats().inUse, 0U);
}

// Falling back to heap allocation hides deterministic display-pool exhaustion.
TEST(Mono8PassThroughProcessor, PreservesDisplayPoolExhaustion) {
    auto rawPool = core::BufferPool::create(1U, 6U).value();
    auto displayPool = core::BufferPool::create(1U, 6U).value();
    auto heldDisplayLease = displayPool->tryAcquire();
    ASSERT_TRUE(heldDisplayLease);
    auto raw = makeRaw(*rawPool, 4U, 3U, 2U, 3U);
    Mono8PassThroughProcessor processor(*displayPool);

    const auto result = processor.process(std::move(raw));

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::ResourceExhaustion);
    EXPECT_EQ(result.error().code, "display_buffer_pool_exhausted");
    EXPECT_EQ(displayPool->stats().acquisitionFailures, 1U);
}

}  // namespace
}  // namespace lumora::processing
