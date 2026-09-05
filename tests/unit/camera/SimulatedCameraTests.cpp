#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/camera/sim/IPatternGenerator.hpp>
#include <lumora/camera/sim/FaultScript.hpp>
#include <lumora/camera/sim/SequenceSource.hpp>
#include "SimulatedFaultPreparationHook.hpp"

#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/Error.hpp>
#include <lumora/core/Frame.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <filesystem>
#include <future>
#include <ios>
#include <limits>
#include <memory>
#include <new>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lumora::camera::sim {
namespace {

using namespace std::chrono_literals;

class CancellingDeadlineClock final : public core::IClock {
public:
    explicit CancellingDeadlineClock(std::stop_source& stopSource) noexcept
        : stopSource_(&stopSource) {}

    std::chrono::steady_clock::time_point steadyNow() const noexcept override {
        return now_;
    }

    std::chrono::system_clock::time_point utcNow() const noexcept override {
        return {};
    }

    core::ClockWaitOutcome waitUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token stopToken,
        std::chrono::milliseconds maximumRealWait) const override {
        static_cast<void>(stopToken);
        static_cast<void>(maximumRealWait);
        now_ = deadline;
        stopSource_->request_stop();
        return core::ClockWaitOutcome::DeadlineReached;
    }

private:
    std::stop_source* stopSource_;
    mutable std::chrono::steady_clock::time_point now_{};
};

core::SourcePixelFormat mono8() {
    return {
        .canonicalName = "Mono8",
        .canonicalEncoding = 0x01080001U,
        .validBits = 8U,
        .sampleMaximum = 255U,
        .packing = core::SourcePacking::Unpacked,
        .alignment = core::BitAlignment::LeastSignificant,
        .applicationStorage = core::StorageType::UInt8,
    };
}

core::SourcePixelFormat mono12() {
    return {
        .canonicalName = "Mono12",
        .canonicalEncoding = 0x01100005U,
        .validBits = 12U,
        .sampleMaximum = 4095U,
        .packing = core::SourcePacking::Unpacked,
        .alignment = core::BitAlignment::LeastSignificant,
        .applicationStorage = core::StorageType::UInt16,
    };
}

CameraCapabilities capabilities() {
    return {
        .pixelFormats = {mono8(), mono12()},
        .roi = {
            .minimum = {.x = 0U, .y = 0U, .width = 2U, .height = 2U},
            .maximum = {.x = 6U, .y = 6U, .width = 8U, .height = 8U},
            .increment = {.x = 1U, .y = 1U, .width = 2U, .height = 2U},
        },
        .frameRate = {
            .minimum = 1.0,
            .maximum = 60.0,
            .increment = 0.1,
            .writableWhileStreaming = true,
        },
        .exposure = {
            .minimum = 10.0,
            .maximum = 20'000.0,
            .increment = 1.0,
            .writableWhileStreaming = true,
        },
        .exposureModes = {ExposureMode::Manual, ExposureMode::Auto},
        .gain = {
            .minimum = 0.0,
            .maximum = 24.0,
            .increment = 0.1,
            .writableWhileStreaming = true,
        },
        .gainModes = {GainMode::Manual, GainMode::Auto},
    };
}

SimulatedCameraOptions options(
    SimulationPattern pattern = SimulationPattern::Ramp,
    SimulationPacingMode pacing = SimulationPacingMode::Fastest,
    std::function<void()> pacingSlipHook = {}) {
    return {
        .id = {.value = "sim-001"},
        .capabilities = capabilities(),
        .pattern = pattern,
        .defaultFps = 30.0,
        .seed = 0xC0FFEEU,
        .pacing = pacing,
        .pacingSlipHook = std::move(pacingSlipHook),
    };
}

CameraConfiguration configuration(
    core::SourcePixelFormat format,
    core::RegionOfInterest roi = {.x = 0U, .y = 0U, .width = 8U, .height = 8U},
    double fps = 30.0) {
    return {
        .pixelFormat = std::move(format),
        .roi = roi,
        .requestedFps = fps,
        .exposure = {
            .mode = ExposureMode::Manual,
            .requestedMicroseconds = 1000.0,
        },
        .gain = {.mode = GainMode::Manual, .requestedDb = 6.0},
        .acquisitionMode = AcquisitionMode::Continuous,
    };
}

std::shared_ptr<core::BufferPool> pool(std::size_t bytes = 128U) {
    return core::BufferPool::create(1U, bytes).value();
}

std::unique_ptr<ICameraDevice> createDevice(
    core::IClock& clock,
    SimulatedCameraOptions cameraOptions = options()) {
    SimulatedCameraProvider provider(std::move(cameraOptions), clock);
    return provider.create(CameraId{"sim-001"}).value();
}

std::uint16_t readU16(const core::RawFrame& frame, std::uint32_t x, std::uint32_t y) {
    std::uint16_t value = 0U;
    const auto offset = static_cast<std::size_t>(y) * frame.layout.strideBytes() +
                        static_cast<std::size_t>(x) * sizeof(value);
    std::memcpy(&value, frame.pixels.bytes().data() + offset, sizeof(value));
    return value;
}

TEST(SimulatedCamera, RampFramesAreRepeatable) {
    core::ManualClock clock;
    auto device = createDevice(clock);
    auto destination = pool();

    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->applyConfiguration(configuration(
        mono12(), {.x = 0U, .y = 0U, .width = 8U, .height = 4U})).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    auto first = device->retrieve(100ms, *destination);

    ASSERT_TRUE(first.hasValue());
    EXPECT_EQ(readU16(*first.value(), 0U, 0U), 0U);
    EXPECT_EQ(readU16(*first.value(), 7U, 0U), 4095U);
    EXPECT_EQ(first.value()->frameId, 1U);
}

TEST(SimulatedCamera, ProviderDiscoversOneStableDescriptorAndRejectsWrongId) {
    core::ManualClock clock;
    SimulatedCameraProvider provider(options(), clock);

    const auto first = provider.discover({});
    const auto second = provider.discover({});
    const auto wrong = provider.create(CameraId{"missing"});

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    ASSERT_EQ(first.value().size(), 1U);
    ASSERT_EQ(second.value().size(), 1U);
    EXPECT_EQ(first.value().front().id, second.value().front().id);
    EXPECT_EQ(first.value().front().identity.serial, "sim-001");
    EXPECT_TRUE(first.value().front().available);
    ASSERT_FALSE(wrong.hasValue());
    EXPECT_EQ(wrong.error().category, core::ErrorCategory::CameraDiscovery);
    EXPECT_EQ(wrong.error().code, "camera_not_found");
}

TEST(SimulatedCamera, DiscoveryHonorsCancellation) {
    core::ManualClock clock;
    SimulatedCameraProvider provider(options(), clock);
    std::stop_source cancelled;
    cancelled.request_stop();

    const auto result = provider.discover(cancelled.get_token());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::Cancelled);
    EXPECT_EQ(result.error().code, "cancelled");
}

TEST(SimulatedCamera, LifecycleIsIdempotentAndCapabilitiesRequireOpen) {
    core::ManualClock clock;
    auto device = createDevice(clock);

    const auto closedCapabilities = device->capabilities();
    ASSERT_FALSE(closedCapabilities.hasValue());
    EXPECT_EQ(closedCapabilities.error().code, "camera_not_open");
    EXPECT_TRUE(device->open().hasValue());
    EXPECT_TRUE(device->open().hasValue());
    EXPECT_TRUE(device->capabilities().hasValue());
    EXPECT_TRUE(device->startStream().hasValue());
    EXPECT_TRUE(device->startStream().hasValue());
    EXPECT_TRUE(device->stopStream().hasValue());
    EXPECT_TRUE(device->stopStream().hasValue());
    EXPECT_TRUE(device->close().hasValue());
    EXPECT_TRUE(device->close().hasValue());
}

TEST(SimulatedCamera, StartRequiresOpenAndRetrieveRequiresStreaming) {
    core::ManualClock clock;
    auto device = createDevice(clock);
    auto destination = pool();

    const auto startClosed = device->startStream();
    ASSERT_FALSE(startClosed.hasValue());
    EXPECT_EQ(startClosed.error().code, "camera_not_open");
    ASSERT_TRUE(device->open().hasValue());
    const auto stopped = device->retrieve(10ms, *destination);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.error().category, core::ErrorCategory::Acquisition);
    EXPECT_EQ(stopped.error().code, "stream_not_started");
}

TEST(SimulatedCamera, ConfigurationPreservesRequestedAndQuantizesActualValues) {
    core::ManualClock clock;
    auto device = createDevice(clock);
    ASSERT_TRUE(device->open().hasValue());
    auto requested = configuration(
        mono12(), {.x = 2U, .y = 2U, .width = 4U, .height = 4U}, 30.07);
    requested.exposure.requestedMicroseconds = 1000.4;
    requested.gain.requestedDb = 6.03;

    const auto result = device->applyConfiguration(requested);

    ASSERT_TRUE(result.hasValue());
    EXPECT_DOUBLE_EQ(*result.value().requested.requestedFps, 30.07);
    EXPECT_DOUBLE_EQ(*result.value().requested.exposure.requestedMicroseconds, 1000.4);
    EXPECT_DOUBLE_EQ(*result.value().requested.gain.requestedDb, 6.03);
    EXPECT_NEAR(*result.value().actual.requestedFps, 30.1, 1e-9);
    EXPECT_NEAR(*result.value().actual.exposure.requestedMicroseconds, 1000.0, 1e-9);
    EXPECT_NEAR(*result.value().actual.gain.requestedDb, 6.0, 1e-9);
    EXPECT_EQ(result.value().actual.roi.width, 4U);
}

TEST(SimulatedCamera, InvalidConfigurationUsesSharedValidatorError) {
    core::ManualClock clock;
    auto device = createDevice(clock);
    ASSERT_TRUE(device->open().hasValue());
    auto requested = configuration(mono12());
    requested.roi.width = 3U;

    const auto result = device->applyConfiguration(requested);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::CameraConfiguration);
    EXPECT_EQ(result.error().code, "roi_width_increment");
}

TEST(SimulatedCamera, RejectsFrameRatesWithoutRepresentableClockPeriods) {
    const std::vector frameRates{
        std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(),
    };

    for (const auto frameRate : frameRates) {
        core::ManualClock clock;
        auto cameraOptions = options();
        cameraOptions.defaultFps = frameRate;
        cameraOptions.capabilities.frameRate = {
            .minimum = frameRate,
            .maximum = frameRate,
            .increment = frameRate,
            .writableWhileStreaming = true,
        };
        SimulatedCameraProvider provider(std::move(cameraOptions), clock);

        const auto result = provider.create(CameraId{"sim-001"});

        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(
            result.error().category,
            core::ErrorCategory::CameraConfiguration);
        EXPECT_EQ(result.error().code, "frame_period_unrepresentable");
    }
}

TEST(SimulatedCamera, RejectsBinary64RoundedFramePeriodBoundary) {
    using Duration = std::chrono::steady_clock::duration;
    const auto ticksPerSecond =
        static_cast<double>(Duration::period::den) /
        static_cast<double>(Duration::period::num);
    const auto boundaryFps = std::ldexp(
        ticksPerSecond,
        -std::numeric_limits<Duration::rep>::digits);
    core::ManualClock clock;
    auto cameraOptions = options();
    cameraOptions.defaultFps = boundaryFps;
    cameraOptions.capabilities.frameRate = {
        .minimum = boundaryFps,
        .maximum = boundaryFps,
        .increment = boundaryFps,
        .writableWhileStreaming = true,
    };
    SimulatedCameraProvider provider(std::move(cameraOptions), clock);

    const auto result = provider.create(CameraId{"sim-001"});

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(
        result.error().category,
        core::ErrorCategory::CameraConfiguration);
    EXPECT_EQ(result.error().code, "frame_period_unrepresentable");
}

TEST(SimulatedCamera, DeadlineArithmeticSaturatesNearClockMaximum) {
    const auto initialTime =
        std::chrono::steady_clock::time_point::max() - 50ms;
    core::ManualClock clock(initialTime);
    auto device = createDevice(clock, options(
        SimulationPattern::Ramp, SimulationPacingMode::Manual));
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->applyConfiguration(configuration(mono8(),
        {.x = 0U, .y = 0U, .width = 8U, .height = 8U}, 10.0)).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    auto first = device->retrieve(100ms, *destination);
    ASSERT_TRUE(first.hasValue());
    first.value().reset();

    const auto second = device->retrieve(10ms, *destination);

    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.error().category, core::ErrorCategory::Acquisition);
    EXPECT_EQ(second.error().code, "acquisition_timeout");
}

TEST(SimulatedCamera, PublishesMono8AndAppliedRoiMetadata) {
    core::ManualClock clock;
    auto device = createDevice(clock);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->applyConfiguration(configuration(
        mono8(), {.x = 2U, .y = 2U, .width = 4U, .height = 4U})).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());

    const auto result = device->retrieve(10ms, *destination);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value()->layout.storage(), core::StorageType::UInt8);
    EXPECT_EQ(result.value()->layout.width(), 4U);
    EXPECT_EQ(result.value()->layout.height(), 4U);
    EXPECT_EQ(result.value()->pixels.bytes()[0], std::byte{0U});
    EXPECT_EQ(result.value()->pixels.bytes()[3], std::byte{255U});
    EXPECT_EQ(result.value()->metadata.acquisitionSettings.roi.x, 2U);
    EXPECT_EQ(result.value()->metadata.acquisitionSettings.roi.y, 2U);
}

TEST(SimulatedCamera, PoolExhaustionDoesNotAdvanceFrameIdOrRetainLease) {
    core::ManualClock clock;
    auto device = createDevice(clock);
    auto destination = pool();
    auto occupied = destination->tryAcquire();
    ASSERT_TRUE(occupied.has_value());
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());

    const auto exhausted = device->retrieve(10ms, *destination);
    ASSERT_FALSE(exhausted.hasValue());
    EXPECT_EQ(exhausted.error().category, core::ErrorCategory::ResourceExhaustion);
    EXPECT_EQ(exhausted.error().code, "buffer_pool_exhausted");
    EXPECT_EQ(destination->stats().inUse, 1U);

    occupied.reset();
    auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 1U);
    EXPECT_EQ(destination->stats().inUse, 1U);
    recovered.value().reset();
    EXPECT_EQ(destination->stats().inUse, 0U);
}

TEST(SimulatedCamera, UndersizedPoolReturnsInvalidFrameAndReleasesLease) {
    core::ManualClock clock;
    auto device = createDevice(clock);
    auto destination = pool(4U);
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());

    const auto result = device->retrieve(10ms, *destination);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::InvalidFrame);
    EXPECT_EQ(result.error().code, "invalid_frame");
    EXPECT_EQ(destination->stats().inUse, 0U);
}

TEST(SimulatedCamera, LargePatternFillHasBoundedCancellationChecks) {
    constexpr std::uint32_t dimension = 4096U;
    std::vector<std::byte> destination(
        static_cast<std::size_t>(dimension) * dimension,
        std::byte{0xA5U});
    auto generator = makePatternGenerator(SimulationPattern::ImpulseNoise, 42U);
    std::stop_source stopSource;
    std::promise<void> fillStarted;
    auto started = fillStarted.get_future();
    auto pending = std::async(std::launch::async, [&] {
        fillStarted.set_value();
        return generator->fill(
            destination,
            dimension,
            dimension,
            dimension,
            mono8(),
            1U,
            stopSource.get_token());
    });

    started.wait();
    std::this_thread::sleep_for(2ms);
    const auto cancelledAt = std::chrono::steady_clock::now();
    stopSource.request_stop();
    const auto result = pending.get();
    const auto cancellationLatency =
        std::chrono::steady_clock::now() - cancelledAt;

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::Cancelled);
    EXPECT_EQ(result.error().code, "cancelled");
    EXPECT_LT(cancellationLatency, 200ms);
}

TEST(SimulatedCamera, PatternGeneratorRejectsRequiredSizeOverflow) {
    std::array<std::byte, 1U> destination{};
    auto generator = makePatternGenerator(SimulationPattern::Ramp, 42U);

    const auto result = generator->fill(
        destination,
        1U,
        2U,
        std::numeric_limits<std::size_t>::max(),
        mono8(),
        1U);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::InvalidFrame);
    EXPECT_EQ(result.error().code, "invalid_frame");
    EXPECT_EQ(
        result.error().diagnosticDetail,
        "Computing the generated frame size overflowed size_t.");
}

TEST(SimulatedCamera, EveryGeneratedPatternIsDeterministic) {
    const std::vector patterns{
        SimulationPattern::Ramp,
        SimulationPattern::Gradient,
        SimulationPattern::Checkerboard,
        SimulationPattern::ImpulseNoise,
        SimulationPattern::MovingBar,
    };

    for (const auto pattern : patterns) {
        core::ManualClock firstClock;
        core::ManualClock secondClock;
        auto firstDevice = createDevice(firstClock, options(pattern));
        auto secondDevice = createDevice(secondClock, options(pattern));
        auto firstPool = pool();
        auto secondPool = pool();
        ASSERT_TRUE(firstDevice->open().hasValue());
        ASSERT_TRUE(secondDevice->open().hasValue());
        ASSERT_TRUE(firstDevice->startStream().hasValue());
        ASSERT_TRUE(secondDevice->startStream().hasValue());

        const auto first = firstDevice->retrieve(10ms, *firstPool);
        const auto second = secondDevice->retrieve(10ms, *secondPool);

        ASSERT_TRUE(first.hasValue());
        ASSERT_TRUE(second.hasValue());
        EXPECT_TRUE(std::equal(
            first.value()->pixels.bytes().begin(),
            first.value()->pixels.bytes().end(),
            second.value()->pixels.bytes().begin(),
            second.value()->pixels.bytes().end()));
    }
}

TEST(SimulatedCamera, MovingBarChangesWithPublishedFrameId) {
    core::ManualClock clock;
    auto device = createDevice(clock, options(SimulationPattern::MovingBar));
    auto firstPool = pool();
    auto secondPool = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());

    const auto first = device->retrieve(10ms, *firstPool);
    const auto second = device->retrieve(10ms, *secondPool);

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(first.value()->pixels.bytes()[0], std::byte{255U});
    EXPECT_EQ(first.value()->pixels.bytes()[1], std::byte{0U});
    EXPECT_EQ(second.value()->pixels.bytes()[0], std::byte{0U});
    EXPECT_EQ(second.value()->pixels.bytes()[1], std::byte{255U});
    EXPECT_EQ(second.value()->frameId, 2U);
}

TEST(SimulatedCamera, CheckerboardAlternatesWithinTheConfiguredFrame) {
    core::ManualClock clock;
    auto device = createDevice(clock, options(SimulationPattern::Checkerboard));
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());

    const auto frame = device->retrieve(10ms, *destination);

    ASSERT_TRUE(frame.hasValue());
    EXPECT_EQ(frame.value()->pixels.bytes()[0], std::byte{0U});
    EXPECT_EQ(frame.value()->pixels.bytes()[2], std::byte{255U});
    EXPECT_EQ(frame.value()->pixels.bytes()[16], std::byte{255U});
    EXPECT_EQ(frame.value()->pixels.bytes()[18], std::byte{0U});
}

TEST(SimulatedCamera, ManualPacingWaitsForAdvanceWithoutHoldingPoolLease) {
    core::ManualClock clock;
    auto destination = pool();
    std::promise<void> firstPublished;
    auto firstReady = firstPublished.get_future();
    auto pending = std::async(std::launch::async, [&] {
        auto device = createDevice(clock, options(
            SimulationPattern::Ramp, SimulationPacingMode::Manual));
        if (!device->open().hasValue() ||
            !device->applyConfiguration(configuration(mono8(),
                {.x = 0U, .y = 0U, .width = 8U, .height = 8U}, 10.0)).hasValue() ||
            !device->startStream().hasValue() ||
            !device->retrieve(100ms, *destination).hasValue()) {
            firstPublished.set_value();
            return core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                core::ErrorCategory::Internal, "test_setup_failed", "", "", false});
        }
        firstPublished.set_value();
        return device->retrieve(100ms, *destination);
    });

    firstReady.wait();
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    EXPECT_EQ(destination->stats().inUse, 0U);
    clock.advance(99ms);
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    clock.advance(1ms);
    ASSERT_EQ(pending.wait_for(100ms), std::future_status::ready);
    const auto second = pending.get();
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(second.value()->frameId, 2U);
}

TEST(SimulatedCamera, TimedRetrievalReturnsTimeoutAtItsClockDeadline) {
    core::ManualClock clock;
    auto destination = pool();
    std::promise<void> firstPublished;
    auto firstReady = firstPublished.get_future();
    auto pending = std::async(std::launch::async, [&] {
        auto device = createDevice(clock, options(
            SimulationPattern::Ramp, SimulationPacingMode::Manual));
        if (!device->open().hasValue() ||
            !device->applyConfiguration(configuration(mono8(),
                {.x = 0U, .y = 0U, .width = 8U, .height = 8U}, 10.0)).hasValue() ||
            !device->startStream().hasValue() ||
            !device->retrieve(100ms, *destination).hasValue()) {
            firstPublished.set_value();
            return core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                core::ErrorCategory::Internal, "test_setup_failed", "", "", false});
        }
        firstPublished.set_value();
        return device->retrieve(40ms, *destination);
    });
    firstReady.wait();
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    clock.advance(40ms);

    ASSERT_EQ(pending.wait_for(100ms), std::future_status::ready);
    const auto result = pending.get();
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::Acquisition);
    EXPECT_EQ(result.error().code, "acquisition_timeout");
}

TEST(SimulatedCamera, ManualPacingTimeoutDoesNotRequireClockAdvance) {
    core::ManualClock clock;
    auto destination = pool();
    std::stop_source cleanupStop;
    std::promise<void> firstPublished;
    auto firstReady = firstPublished.get_future();
    auto pending = std::async(std::launch::async, [&] {
        auto device = createDevice(clock, options(
            SimulationPattern::Ramp, SimulationPacingMode::Manual));
        if (!device->open().hasValue() ||
            !device->applyConfiguration(configuration(mono8(),
                {.x = 0U, .y = 0U, .width = 8U, .height = 8U}, 10.0)).hasValue() ||
            !device->startStream().hasValue() ||
            !device->retrieve(100ms, *destination).hasValue()) {
            firstPublished.set_value();
            return core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                core::ErrorCategory::Internal, "test_setup_failed", "", "", false});
        }
        firstPublished.set_value();
        return device->retrieve(30ms, *destination, cleanupStop.get_token());
    });

    firstReady.wait();
    const auto completion = pending.wait_for(100ms);
    if (completion != std::future_status::ready) {
        cleanupStop.request_stop();
    }
    const auto result = pending.get();

    EXPECT_EQ(completion, std::future_status::ready);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::Acquisition);
    EXPECT_EQ(result.error().code, "acquisition_timeout");
}

TEST(SimulatedCamera, TimedRetrievalHonorsCancellation) {
    core::ManualClock clock;
    auto destination = pool();
    std::stop_source stopSource;
    std::promise<void> firstPublished;
    auto firstReady = firstPublished.get_future();
    auto pending = std::async(std::launch::async, [&] {
        auto device = createDevice(clock, options(
            SimulationPattern::Ramp, SimulationPacingMode::Manual));
        if (!device->open().hasValue() || !device->startStream().hasValue() ||
            !device->retrieve(100ms, *destination).hasValue()) {
            firstPublished.set_value();
            return core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                core::ErrorCategory::Internal, "test_setup_failed", "", "", false});
        }
        firstPublished.set_value();
        return device->retrieve(100ms, *destination, stopSource.get_token());
    });
    firstReady.wait();
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    stopSource.request_stop();

    ASSERT_EQ(pending.wait_for(100ms), std::future_status::ready);
    const auto result = pending.get();
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::Cancelled);
    EXPECT_EQ(result.error().code, "cancelled");
}

TEST(SimulatedCamera, CancellationAfterPacingDoesNotPublishAFrame) {
    std::stop_source stopSource;
    CancellingDeadlineClock clock(stopSource);
    auto device = createDevice(clock, options(
        SimulationPattern::Ramp, SimulationPacingMode::Manual));
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    auto first = device->retrieve(100ms, *destination);
    ASSERT_TRUE(first.hasValue());
    first.value().reset();

    const auto cancelled =
        device->retrieve(100ms, *destination, stopSource.get_token());

    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.error().category, core::ErrorCategory::Cancelled);
    EXPECT_EQ(cancelled.error().code, "cancelled");
    EXPECT_EQ(destination->stats().inUse, 0U);

    const auto recovered = device->retrieve(100ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 2U);
}

TEST(SimulatedCamera, CancellationDuringFillReleasesLeaseAndPreservesFrameId) {
    constexpr std::uint32_t dimension = 4096U;
    core::ManualClock clock;
    auto largeOptions = options(SimulationPattern::ImpulseNoise);
    largeOptions.capabilities.roi.maximum.width = dimension;
    largeOptions.capabilities.roi.maximum.height = dimension;
    auto destination = pool(
        static_cast<std::size_t>(dimension) * dimension);
    std::stop_source stopSource;
    std::promise<void> retrievalStarting;
    auto started = retrievalStarting.get_future();
    auto pending = std::async(std::launch::async, [&] {
        auto device = createDevice(clock, std::move(largeOptions));
        if (!device->open().hasValue() || !device->startStream().hasValue()) {
            retrievalStarting.set_value();
            return std::pair{
                core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                    core::ErrorCategory::Internal,
                    "test_setup_failed",
                    "",
                    "",
                    false}),
                core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                    core::ErrorCategory::Internal,
                    "test_setup_failed",
                    "",
                    "",
                    false})};
        }
        retrievalStarting.set_value();
        auto cancelled =
            device->retrieve(1s, *destination, stopSource.get_token());
        if (cancelled.hasValue()) {
            cancelled.value().reset();
        }
        auto recovered = device->retrieve(1s, *destination);
        return std::pair{std::move(cancelled), std::move(recovered)};
    });

    started.wait();
    bool observedLease = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (destination->stats().inUse == 1U) {
            observedLease = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    stopSource.request_stop();
    auto [cancelled, recovered] = pending.get();

    EXPECT_TRUE(observedLease);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.error().category, core::ErrorCategory::Cancelled);
    EXPECT_EQ(cancelled.error().code, "cancelled");
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 1U);
    recovered.value().reset();
    EXPECT_EQ(destination->stats().inUse, 0U);
}

TEST(SimulatedCamera, MissedDeadlineReschedulesAndReportsOnePacingSlip) {
    core::ManualClock clock;
    std::atomic_uint32_t slips{0U};
    auto destination = pool();
    std::promise<void> firstPublished;
    auto firstReady = firstPublished.get_future();
    std::promise<void> proceedLate;
    auto proceed = proceedLate.get_future();
    std::promise<void> latePublished;
    auto lateReady = latePublished.get_future();
    auto pending = std::async(std::launch::async, [&] {
        auto device = createDevice(clock, options(
            SimulationPattern::Ramp,
            SimulationPacingMode::Manual,
            [&] { ++slips; }));
        if (!device->open().hasValue() ||
            !device->applyConfiguration(configuration(mono8(),
                {.x = 0U, .y = 0U, .width = 8U, .height = 8U}, 10.0)).hasValue() ||
            !device->startStream().hasValue() ||
            !device->retrieve(100ms, *destination).hasValue()) {
            firstPublished.set_value();
            latePublished.set_value();
            return core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                core::ErrorCategory::Internal, "test_setup_failed", "", "", false});
        }
        firstPublished.set_value();
        proceed.wait();
        auto late = device->retrieve(100ms, *destination);
        latePublished.set_value();
        if (!late.hasValue()) {
            return late;
        }
        late.value().reset();
        return device->retrieve(100ms, *destination);
    });

    firstReady.wait();
    clock.advance(350ms);
    proceedLate.set_value();
    lateReady.wait();
    EXPECT_EQ(slips.load(), 1U);
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    clock.advance(99ms);
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    clock.advance(1ms);
    ASSERT_EQ(pending.wait_for(100ms), std::future_status::ready);
    EXPECT_TRUE(pending.get().hasValue());
    EXPECT_EQ(slips.load(), 1U);
}

TEST(SimulatedCamera, FastestPacingNeverReportsDeadlineSlips) {
    core::ManualClock clock(
        std::chrono::steady_clock::time_point{std::chrono::seconds{1}});
    std::uint32_t slips = 0U;
    auto device = createDevice(clock, options(
        SimulationPattern::Ramp,
        SimulationPacingMode::Fastest,
        [&] { ++slips; }));
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());

    for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
        auto frame = device->retrieve(100ms, *destination);
        ASSERT_TRUE(frame.hasValue());
        frame.value().reset();
    }

    EXPECT_EQ(slips, 0U);
}

TEST(SimulatedCamera, OvershootWhileWaitingReschedulesFromCurrentTime) {
    struct OvershootResult final {
        std::uint32_t slipsAfterOvershotFrame;
        core::Result<std::shared_ptr<const core::RawFrame>> followingFrame;
    };

    core::ManualClock clock;
    std::atomic_uint32_t slips{0U};
    auto destination = pool();
    std::promise<void> firstPublished;
    auto firstReady = firstPublished.get_future();
    auto pending = std::async(std::launch::async, [&] {
        auto device = createDevice(clock, options(
            SimulationPattern::Ramp,
            SimulationPacingMode::Manual,
            [&] { ++slips; }));
        if (!device->open().hasValue() ||
            !device->applyConfiguration(configuration(mono8(),
                {.x = 0U, .y = 0U, .width = 8U, .height = 8U}, 10.0)).hasValue() ||
            !device->startStream().hasValue() ||
            !device->retrieve(100ms, *destination).hasValue()) {
            firstPublished.set_value();
            return OvershootResult{
                slips.load(),
                core::Result<std::shared_ptr<const core::RawFrame>>::failure({
                    core::ErrorCategory::Internal,
                    "test_setup_failed",
                    "",
                    "",
                    false})};
        }
        firstPublished.set_value();
        auto overshot = device->retrieve(500ms, *destination);
        const auto slipsAfterOvershotFrame = slips.load();
        if (!overshot.hasValue()) {
            return OvershootResult{slipsAfterOvershotFrame, std::move(overshot)};
        }
        overshot.value().reset();
        return OvershootResult{
            slipsAfterOvershotFrame,
            device->retrieve(30ms, *destination)};
    });

    firstReady.wait();
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    clock.advance(350ms);
    const auto result = pending.get();

    EXPECT_EQ(result.slipsAfterOvershotFrame, 1U);
    ASSERT_FALSE(result.followingFrame.hasValue());
    EXPECT_EQ(
        result.followingFrame.error().category,
        core::ErrorCategory::Acquisition);
    EXPECT_EQ(result.followingFrame.error().code, "acquisition_timeout");
    EXPECT_EQ(slips.load(), 1U);
}

TEST(SimulatedCamera, RealTimePacingUsesStopAwareClockWait) {
    core::SystemClock clock;
    auto device = createDevice(clock, options(
        SimulationPattern::Ramp, SimulationPacingMode::RealTime));
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->applyConfiguration(configuration(mono8(),
        {.x = 0U, .y = 0U, .width = 8U, .height = 8U}, 60.0)).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    ASSERT_TRUE(device->retrieve(100ms, *destination).hasValue());

    const auto before = std::chrono::steady_clock::now();
    const auto second = device->retrieve(100ms, *destination);
    const auto elapsed = std::chrono::steady_clock::now() - before;

    ASSERT_TRUE(second.hasValue());
    EXPECT_GE(elapsed, 10ms);
}

SimulatedCameraOptions replayOptions(
    SequenceEnd end = SequenceEnd::Loop,
    SimulationPacingMode pacing = SimulationPacingMode::Fastest) {
    auto result = options(SimulationPattern::Ramp, pacing);
    result.sequence = SequenceReplayOptions{
        std::filesystem::path{LUMORA_SEQUENCE_FIXTURES} / "ramp16", end};
    return result;
}

TEST(SimulatedCamera, ReplaysImmutableNumericFramesThroughPoolAndSharedRoiValidator) {
    core::ManualClock clock;
    auto device = createDevice(clock, replayOptions());
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    const auto caps = device->capabilities().value();
    ASSERT_EQ(caps.pixelFormats.size(), 1U);
    EXPECT_EQ(caps.pixelFormats.front().sampleMaximum, 4095U);
    EXPECT_EQ(caps.roi.maximum.width, 3U);
    EXPECT_EQ(caps.roi.maximum.height, 2U);
    const auto format = caps.pixelFormats.front();
    const auto invalid = device->applyConfiguration(configuration(
        format, {.x = 2U, .y = 0U, .width = 2U, .height = 1U}));
    ASSERT_FALSE(invalid.hasValue());
    EXPECT_EQ(invalid.error().code, "roi_containment");
    ASSERT_TRUE(device->applyConfiguration(configuration(
        format, {.x = 1U, .y = 0U, .width = 2U, .height = 2U}, 10.0)).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    auto first = device->retrieve(20ms, *destination);
    ASSERT_TRUE(first.hasValue());
    EXPECT_EQ(readU16(*first.value(), 0U, 0U), 0xABCU);
    EXPECT_EQ(readU16(*first.value(), 1U, 1U), 0x30U);
    EXPECT_EQ(first.value()->metadata.acquisitionSettings.actualFps, 10.0);
    EXPECT_FALSE(device->retrieve(20ms, *destination).hasValue());
    EXPECT_EQ(readU16(*first.value(), 0U, 0U), 0xABCU);
    first.value().reset();
    auto second = device->retrieve(20ms, *destination);
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(second.value()->frameId, 2U);
    EXPECT_EQ(readU16(*second.value(), 0U, 0U), 0x789U);
    second.value().reset();
    auto looped = device->retrieve(20ms, *destination);
    ASSERT_TRUE(looped.hasValue());
    EXPECT_EQ(looped.value()->frameId, 3U);
    EXPECT_EQ(readU16(*looped.value(), 0U, 0U), 0xABCU);
}

TEST(SimulatedCamera, ReplayStopEndsStreamAndErrorKeepsReportingExhaustion) {
    for (const auto end : {SequenceEnd::Stop, SequenceEnd::Error}) {
        core::ManualClock clock;
        auto device = createDevice(clock, replayOptions(end));
        auto destination = pool();
        ASSERT_TRUE(device->open().hasValue());
        ASSERT_TRUE(device->startStream().hasValue());
        ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
        ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
        const auto exhausted = device->retrieve(10ms, *destination);
        ASSERT_FALSE(exhausted.hasValue());
        EXPECT_EQ(exhausted.error().code,
                  end == SequenceEnd::Stop ? "sequence_stopped" : "sequence_exhausted");
        const auto repeated = device->retrieve(10ms, *destination);
        ASSERT_FALSE(repeated.hasValue());
        EXPECT_EQ(repeated.error().code,
                  end == SequenceEnd::Stop ? "stream_not_started" : "sequence_exhausted");
        EXPECT_EQ(destination->stats().inUse, 0U);
    }
}

TEST(SimulatedCamera, FaultFailuresRepeatAtExactNextIdWithoutConsumingReplayPosition) {
    core::ManualClock clock;
    auto opts = replayOptions();
    opts.faults = FaultScript::create({
        {2U, SimulatedFault::Timeout, 2U},
        {2U, SimulatedFault::MalformedFrame, 2U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
    for (const auto* code : {"acquisition_timeout", "acquisition_timeout", "zero_dimensions", "zero_dimensions"}) {
        const auto failed = device->retrieve(1ms, *destination);
        ASSERT_FALSE(failed.hasValue());
        EXPECT_EQ(failed.error().code, code);
        EXPECT_EQ(destination->stats().inUse, 0U);
    }
    const auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 2U);
    EXPECT_EQ(readU16(*recovered.value(), 0U, 0U), 0x0456U);
}

TEST(SimulatedCamera, DisconnectPersistsAcrossLifecycleUntilExplicitRestore) {
    core::ManualClock clock;
    auto opts = replayOptions();
    opts.faults = FaultScript::create({{1U, SimulatedFault::Disconnect, 1U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    const auto disconnected = device->retrieve(10ms, *destination);
    ASSERT_FALSE(disconnected.hasValue());
    EXPECT_EQ(disconnected.error().category, core::ErrorCategory::CameraConnection);
    EXPECT_EQ(disconnected.error().code, "simulated_disconnect");
    ASSERT_TRUE(device->close().hasValue());
    EXPECT_FALSE(device->open().hasValue());
    opts.faults->restoreConnection();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    const auto restored = device->retrieve(10ms, *destination);
    ASSERT_TRUE(restored.hasValue());
    EXPECT_EQ(restored.value()->frameId, 1U);
    EXPECT_EQ(readU16(*restored.value(), 0U, 0U), 0x0123U);
}

TEST(SimulatedCamera, ConfigurationFaultConsumesOnlyAnApplicableValidatedAttempt) {
    core::ManualClock clock;
    auto opts = options();
    opts.faults = FaultScript::create({{1U, SimulatedFault::ConfigurationFailure, 2U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    auto invalid = configuration(mono8());
    invalid.requestedFps = -1.0;
    EXPECT_EQ(device->applyConfiguration(invalid).error().code, "frame_rate_positive");
    for (int repeat = 0; repeat != 2; ++repeat) {
        const auto failed = device->applyConfiguration(configuration(mono8(),
            {.x = 0U, .y = 0U, .width = 4U, .height = 4U}, 10.0));
        ASSERT_FALSE(failed.hasValue());
        EXPECT_EQ(failed.error().category, core::ErrorCategory::CameraConfiguration);
        EXPECT_EQ(failed.error().code, "simulated_configuration_failure");
    }
    ASSERT_TRUE(device->startStream().hasValue());
    auto unchanged = device->retrieve(10ms, *destination);
    ASSERT_TRUE(unchanged.hasValue());
    EXPECT_EQ(unchanged.value()->layout.width(), 8U);
    unchanged.value().reset();
    EXPECT_TRUE(device->applyConfiguration(configuration(mono8())).hasValue());
}

TEST(SimulatedCamera, ElapsedFaultsUseStreamStartAndEvaluateOnOperations) {
    core::ManualClock clock;
    auto opts = options();
    opts.faults = FaultScript::create({{0U, SimulatedFault::Timeout, 1U, 100ms}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    clock.advance(5s);
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
    clock.advance(99ms);
    ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
    clock.advance(1ms);
    const auto failed = device->retrieve(1ms, *destination);
    ASSERT_FALSE(failed.hasValue());
    EXPECT_EQ(failed.error().code, "acquisition_timeout");
    const auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 3U);
}

TEST(SimulatedCamera, CancellationDuringInjectedTimeoutPreservesOccurrenceAndSequence) {
    core::ManualClock clock;
    auto opts = replayOptions();
    opts.faults = FaultScript::create({{1U, SimulatedFault::Timeout, 1U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&] {
        return device->retrieve(1s, *destination, stop.get_token());
    });
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    EXPECT_EQ(destination->stats().inUse, 0U);
    stop.request_stop();
    ASSERT_EQ(pending.wait_for(100ms), std::future_status::ready);
    const auto cancelled = pending.get();
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.error().category, core::ErrorCategory::Cancelled);
    const auto stillDue = device->retrieve(1ms, *destination);
    ASSERT_FALSE(stillDue.hasValue());
    EXPECT_EQ(stillDue.error().code, "acquisition_timeout");
    auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 1U);
    EXPECT_EQ(readU16(*recovered.value(), 0U, 0U), 0x0123U);
}

TEST(SimulatedCamera, ReplayManualTimeoutAndCancellationPreservePendingFrameAndFault) {
    core::ManualClock clock;
    auto opts = replayOptions(SequenceEnd::Loop, SimulationPacingMode::Manual);
    opts.faults = FaultScript::create({{2U, SimulatedFault::MalformedFrame, 1U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    auto format = device->capabilities().value().pixelFormats.front();
    ASSERT_TRUE(device->applyConfiguration(configuration(format,
        {.x = 0U, .y = 0U, .width = 3U, .height = 2U}, 10.0)).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
    EXPECT_EQ(device->retrieve(1ms, *destination).error().code, "acquisition_timeout");
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&] {
        return device->retrieve(1s, *destination, stop.get_token());
    });
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    stop.request_stop();
    const auto cancelled = pending.get();
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.error().code, "cancelled");
    EXPECT_EQ(destination->stats().inUse, 0U);
    clock.advance(100ms);
    EXPECT_EQ(device->retrieve(10ms, *destination).error().code, "zero_dimensions");
    const auto second = device->retrieve(10ms, *destination);
    ASSERT_TRUE(second.hasValue());
    EXPECT_EQ(second.value()->frameId, 2U);
    EXPECT_EQ(readU16(*second.value(), 0U, 0U), 0x0456U);
}

TEST(SimulatedCamera, ReplayUsesConfiguredRealTimePacing) {
    core::SystemClock clock;
    auto device = createDevice(clock, replayOptions(SequenceEnd::Loop, SimulationPacingMode::RealTime));
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->applyConfiguration(configuration(device->capabilities().value().pixelFormats.front(),
        {.x = 0U, .y = 0U, .width = 3U, .height = 2U}, 20.0)).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    ASSERT_TRUE(device->retrieve(100ms, *destination).hasValue());
    const auto before = std::chrono::steady_clock::now();
    const auto second = device->retrieve(150ms, *destination);
    ASSERT_TRUE(second.hasValue());
    EXPECT_GE(std::chrono::steady_clock::now() - before, 30ms);
    EXPECT_EQ(second.value()->frameId, 2U);
}

TEST(SimulatedCamera, ReplayClearsPoolTailForDeterministicPublishedBytes) {
    core::ManualClock clock;
    auto device = createDevice(clock, replayOptions());
    auto destination = pool(32U);
    auto dirty = destination->tryAcquire();
    ASSERT_TRUE(dirty.has_value());
    std::fill(dirty->bytes().begin(), dirty->bytes().end(), std::byte{0xA5U});
    dirty.reset();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    auto frame = device->retrieve(10ms, *destination);
    ASSERT_TRUE(frame.hasValue());
    EXPECT_EQ(readU16(*frame.value(), 0U, 0U), 0x0123U);
    for (std::size_t offset = 12U; offset < 32U; ++offset) {
        EXPECT_EQ(frame.value()->pixels.bytes()[offset], std::byte{0U});
    }
}

TEST(SimulatedCamera, ReplayTooSmallPoolAndPreCancellationDoNotConsumeMalformedFault) {
    core::ManualClock clock;
    auto opts = replayOptions();
    opts.faults = FaultScript::create({{1U, SimulatedFault::MalformedFrame, 1U}}).value();
    auto device = createDevice(clock, opts);
    auto tooSmall = pool(1U);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    EXPECT_EQ(device->retrieve(10ms, *tooSmall).error().code, "invalid_frame");
    EXPECT_EQ(tooSmall->stats().inUse, 0U);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(device->retrieve(10ms, *destination, stop.get_token()).error().code, "cancelled");
    EXPECT_EQ(device->retrieve(10ms, *destination).error().code, "zero_dimensions");
    const auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 1U);
    EXPECT_EQ(readU16(*recovered.value(), 0U, 0U), 0x0123U);
}

TEST(SimulatedCamera, ReplayAndFaultTimeoutShareOneRealDeadline) {
    core::SystemClock clock;
    auto opts = replayOptions(SequenceEnd::Loop, SimulationPacingMode::RealTime);
    opts.faults = FaultScript::create({{2U, SimulatedFault::Timeout, 1U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->applyConfiguration(configuration(device->capabilities().value().pixelFormats.front(),
        {.x = 0U, .y = 0U, .width = 3U, .height = 2U}, 10.0)).hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
    const auto before = std::chrono::steady_clock::now();
    const auto timedOut = device->retrieve(150ms, *destination);
    const auto elapsed = std::chrono::steady_clock::now() - before;
    ASSERT_FALSE(timedOut.hasValue());
    EXPECT_EQ(timedOut.error().code, "acquisition_timeout");
    EXPECT_GE(elapsed, 140ms);
    EXPECT_LT(elapsed, 230ms);
    const auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 2U);
    EXPECT_EQ(readU16(*recovered.value(), 0U, 0U), 0x0456U);
}

TEST(SimulatedCamera, ExtremeInjectedTimeoutRemainsCancellableWithoutConsumingOccurrence) {
    core::ManualClock clock;
    auto opts = options();
    opts.faults = FaultScript::create({{1U, SimulatedFault::Timeout, 1U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&] {
        return device->retrieve(std::chrono::milliseconds::max(), *destination, stop.get_token());
    });
    EXPECT_EQ(pending.wait_for(10ms), std::future_status::timeout);
    stop.request_stop();
    const auto cancelled = pending.get();
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.error().code, "cancelled");
    EXPECT_EQ(device->retrieve(0ms, *destination).error().code, "acquisition_timeout");
    const auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 1U);
}

void failFaultPreparationAllocationOnce() {
    static_cast<void>(testing::exchangeFaultPreparationHook(nullptr));
    throw std::bad_alloc{};
}

void failFinalFaultResultAllocationOnce() {
    static thread_local int checkpointCount{};
    if (++checkpointCount == 2) {
        checkpointCount = 0;
        static_cast<void>(testing::exchangeFaultPreparationHook(nullptr));
        throw std::bad_alloc{};
    }
}

TEST(SimulatedCamera, MalformedFrameReleasesLeaseBeforeFailureResultPreparation) {
    core::ManualClock clock;
    auto opts = replayOptions();
    opts.faults = FaultScript::create({{1U, SimulatedFault::MalformedFrame, 1U}}).value();
    auto device = createDevice(clock, opts);
    auto destination = pool();
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    static thread_local core::BufferPool* preparationPool{};
    static thread_local bool releasedBeforePreparation{};
    static thread_local unsigned preparationCount{};
    preparationPool = destination.get();
    releasedBeforePreparation = true;
    preparationCount = 0U;
    {
        testing::ScopedFaultPreparationHook observation([] {
            ++preparationCount;
            releasedBeforePreparation &= preparationPool->stats().inUse == 0U;
        });
        const auto failure = device->retrieve(0ms, *destination);
        ASSERT_FALSE(failure.hasValue());
        EXPECT_EQ(failure.error().code, "zero_dimensions");
    }
    preparationPool = nullptr;
    EXPECT_EQ(preparationCount, 2U);
    EXPECT_TRUE(releasedBeforePreparation);
    EXPECT_EQ(destination->stats().inUse, 0U);
    EXPECT_FALSE(opts.faults->match(1U, 0ms, FaultPoint::Retrieval).has_value());
    const auto recovered = device->retrieve(10ms, *destination);
    ASSERT_TRUE(recovered.hasValue());
    EXPECT_EQ(recovered.value()->frameId, 1U);
    EXPECT_EQ(readU16(*recovered.value(), 0U, 0U), 0x0123U);
}

TEST(SimulatedCamera, FailureResultAllocationDoesNotEscapeOrCommitFaultOccurrence) {
    struct Case {
        SimulatedFault fault;
        const char* code;
        testing::FaultPreparationHook hook{failFaultPreparationAllocationOnce};
    };
    for (const auto& test : std::vector<Case>{
             {SimulatedFault::Timeout, "acquisition_timeout"},
             {SimulatedFault::Disconnect, "simulated_disconnect"},
             {SimulatedFault::MalformedFrame, "zero_dimensions"},
             {SimulatedFault::ConfigurationFailure, "simulated_configuration_failure"},
             {SimulatedFault::Timeout, "acquisition_timeout", failFinalFaultResultAllocationOnce},
             {SimulatedFault::Disconnect, "simulated_disconnect", failFinalFaultResultAllocationOnce},
             {SimulatedFault::MalformedFrame, "zero_dimensions", failFinalFaultResultAllocationOnce},
             {SimulatedFault::ConfigurationFailure, "simulated_configuration_failure", failFinalFaultResultAllocationOnce}}) {
        SCOPED_TRACE(test.code);
        core::ManualClock clock;
        auto opts = replayOptions();
        opts.faults = FaultScript::create({{2U, test.fault, 1U}}).value();
        auto device = createDevice(clock, opts);
        auto destination = pool();
        ASSERT_TRUE(device->open().hasValue());
        const auto requested = configuration(device->capabilities().value().pixelFormats.front(),
            {.x = 0U, .y = 0U, .width = 3U, .height = 2U});
        ASSERT_TRUE(device->startStream().hasValue());
        ASSERT_TRUE(device->retrieve(10ms, *destination).hasValue());
        const bool configuring = test.fault == SimulatedFault::ConfigurationFailure;
        const auto point = configuring ? FaultPoint::Configuration : FaultPoint::Retrieval;
        std::optional<core::Error> returnedError;
        bool allocationEscaped = false;
        {
            testing::ScopedFaultPreparationHook injection(test.hook);
            try {
                if (configuring) {
                    const auto result = device->applyConfiguration(requested);
                    ASSERT_FALSE(result.hasValue());
                    returnedError = result.error();
                } else {
                    const auto result = device->retrieve(0ms, *destination);
                    ASSERT_FALSE(result.hasValue());
                    returnedError = result.error();
                }
            } catch (const std::bad_alloc&) {
                allocationEscaped = true;
            }
        }
        EXPECT_FALSE(allocationEscaped);
        EXPECT_TRUE(returnedError.has_value());
        EXPECT_FALSE(opts.faults->disconnected());
        EXPECT_EQ(destination->stats().inUse, 0U);
        const auto pending = opts.faults->match(2U, 0ms, point);
        EXPECT_TRUE(pending.has_value());
        if (!returnedError || !pending || opts.faults->disconnected()) {
            continue;
        }
        EXPECT_EQ(returnedError->category, core::ErrorCategory::ResourceExhaustion);
        EXPECT_EQ(returnedError->code, "alloc_failed");
        if (configuring) {
            const auto failure = device->applyConfiguration(requested);
            ASSERT_FALSE(failure.hasValue());
            EXPECT_EQ(failure.error().code, test.code);
        } else {
            const auto failure = device->retrieve(0ms, *destination);
            ASSERT_FALSE(failure.hasValue());
            EXPECT_EQ(failure.error().code, test.code);
        }
        EXPECT_FALSE(opts.faults->match(2U, 0ms, point).has_value());
        EXPECT_EQ(opts.faults->disconnected(), test.fault == SimulatedFault::Disconnect);
        opts.faults->restoreConnection();
        if (configuring) {
            EXPECT_TRUE(device->applyConfiguration(requested).hasValue());
        }
        const auto recovered = device->retrieve(10ms, *destination);
        ASSERT_TRUE(recovered.hasValue());
        EXPECT_EQ(recovered.value()->frameId, 2U);
        EXPECT_EQ(readU16(*recovered.value(), 0U, 0U), 0x0456U);
    }
}

TEST(SimulatedCamera, FailurePreparationTranslatesExpectedSizeAndIoExceptions) {
    struct Case {
        testing::FaultPreparationHook throwFailure;
        core::ErrorCategory category;
        const char* code;
    };
    for (const auto& test : std::vector<Case>{
             {[] {
                  static_cast<void>(testing::exchangeFaultPreparationHook(nullptr));
                  throw std::length_error{"Injected size limit"}; },
              core::ErrorCategory::ResourceExhaustion, "size_limit"},
             {[] {
                  static_cast<void>(testing::exchangeFaultPreparationHook(nullptr));
                  throw std::filesystem::filesystem_error{
                       "Injected filesystem failure", std::make_error_code(std::errc::io_error)}; },
              core::ErrorCategory::Storage, "io_failed"},
             {[] {
                  static_cast<void>(testing::exchangeFaultPreparationHook(nullptr));
                  throw std::ios_base::failure{"Injected stream failure"}; },
              core::ErrorCategory::Storage, "io_failed"}}) {
        SCOPED_TRACE(test.code);
        core::ManualClock clock;
        auto opts = replayOptions();
        opts.faults = FaultScript::create({{1U, SimulatedFault::Disconnect, 1U}}).value();
        auto device = createDevice(clock, opts);
        auto destination = pool();
        ASSERT_TRUE(device->open().hasValue());
        ASSERT_TRUE(device->startStream().hasValue());
        {
            testing::ScopedFaultPreparationHook injection(test.throwFailure);
            const auto result = device->retrieve(0ms, *destination);
            ASSERT_FALSE(result.hasValue());
            EXPECT_EQ(result.error().category, test.category);
            EXPECT_EQ(result.error().code, test.code);
        }
        EXPECT_FALSE(opts.faults->disconnected());
        EXPECT_TRUE(opts.faults->match(1U, 0ms, FaultPoint::Retrieval).has_value());
        EXPECT_EQ(destination->stats().inUse, 0U);
    }
}

TEST(SimulatedCamera, CancellationDuringFailurePreparationDoesNotCommitOccurrence) {
    for (const auto fault : {SimulatedFault::Timeout, SimulatedFault::Disconnect,
                            SimulatedFault::MalformedFrame}) {
        SCOPED_TRACE(static_cast<int>(fault));
        core::ManualClock clock;
        auto opts = replayOptions();
        opts.faults = FaultScript::create({{1U, fault, 1U}}).value();
        auto device = createDevice(clock, opts);
        auto destination = pool();
        ASSERT_TRUE(device->open().hasValue());
        ASSERT_TRUE(device->startStream().hasValue());
        std::stop_source stop;
        static thread_local std::stop_source* preparationStop{};
        preparationStop = &stop;
        {
            testing::ScopedFaultPreparationHook injection([] { preparationStop->request_stop(); });
            const auto result = device->retrieve(0ms, *destination, stop.get_token());
            ASSERT_FALSE(result.hasValue());
            EXPECT_EQ(result.error().code, "cancelled");
        }
        preparationStop = nullptr;
        EXPECT_FALSE(opts.faults->disconnected());
        EXPECT_TRUE(opts.faults->match(1U, 0ms, FaultPoint::Retrieval).has_value());
        EXPECT_EQ(destination->stats().inUse, 0U);
    }
}

TEST(SimulatedCamera, FailedAllocationTranslationPropagatesWithoutCommittingFaultState) {
    for (const auto fault : {SimulatedFault::Disconnect, SimulatedFault::MalformedFrame}) {
        core::ManualClock clock;
        auto opts = replayOptions();
        opts.faults = FaultScript::create({{1U, fault, 1U}}).value();
        auto device = createDevice(clock, opts);
        auto destination = pool();
        ASSERT_TRUE(device->open().hasValue());
        ASSERT_TRUE(device->startStream().hasValue());
        {
            testing::ScopedFaultPreparationHook injection([] { throw std::bad_alloc{}; });
            EXPECT_THROW(static_cast<void>(device->retrieve(0ms, *destination)), std::bad_alloc);
        }
        EXPECT_FALSE(opts.faults->disconnected());
        EXPECT_TRUE(opts.faults->match(1U, 0ms, FaultPoint::Retrieval).has_value());
        EXPECT_EQ(destination->stats().inUse, 0U);
        const auto failure = device->retrieve(0ms, *destination);
        ASSERT_FALSE(failure.hasValue());
        EXPECT_EQ(failure.error().code,
            fault == SimulatedFault::Disconnect ? "simulated_disconnect" : "zero_dimensions");
        opts.faults->restoreConnection();
        const auto recovered = device->retrieve(10ms, *destination);
        ASSERT_TRUE(recovered.hasValue());
        EXPECT_EQ(recovered.value()->frameId, 1U);
        EXPECT_EQ(readU16(*recovered.value(), 0U, 0U), 0x0123U);
    }
}

}  // namespace
}  // namespace lumora::camera::sim
