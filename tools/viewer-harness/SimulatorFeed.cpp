#include "SimulatorFeed.hpp"

#include <lumora/camera/ICameraDevice.hpp>
#include <lumora/camera/sim/SimulatedCameraOptions.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/Error.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace lumora::tools {
namespace {

using namespace std::chrono_literals;

core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8U, 255U,
            core::SourcePacking::Unpacked,
            core::BitAlignment::LeastSignificant,
            core::StorageType::UInt8};
}

camera::CameraCapabilities capabilities() {
    return {
        .pixelFormats = {mono8()},
        .roi = {
            .minimum = {0U, 0U, 1U, 1U},
            .maximum = {0U, 0U, 640U, 480U},
            .increment = {1U, 1U, 1U, 1U},
        },
        .frameRate = {1.0, 60.0, 0.1, true},
        .exposure = {10.0, 20'000.0, 1.0, true},
        .exposureModes = {camera::ExposureMode::Manual},
        .gain = {0.0, 24.0, 0.1, true},
        .gainModes = {camera::GainMode::Manual},
    };
}

camera::sim::SimulatedCameraOptions options() {
    return {
        .id = {.value = "sim-viewer"},
        .capabilities = capabilities(),
        .pattern = camera::sim::SimulationPattern::MovingBar,
        .defaultFps = 30.0,
        .seed = 0x4C554D4FU,
        .pacing = camera::sim::SimulationPacingMode::RealTime,
    };
}

camera::CameraConfiguration configuration() {
    return {
        .pixelFormat = mono8(),
        .roi = {0U, 0U, 640U, 480U},
        .requestedFps = 30.0,
        .exposure = {camera::ExposureMode::Manual, 1000.0},
        .gain = {camera::GainMode::Manual, 0.0},
        .acquisitionMode = camera::AcquisitionMode::Continuous,
    };
}

core::Error workerError(
    core::ErrorCategory category,
    std::string code,
    std::string summary,
    std::string detail) {
    return {category, std::move(code), std::move(summary), std::move(detail), true};
}

}  // namespace

SimulatorFeed::SimulatorFeed(
    core::LatestValueSlot<core::FrameBundle>& slot,
    core::IClock& clock)
    : slot_(&slot), clock_(&clock) {}

SimulatorFeed::~SimulatorFeed() {
    stop();
}

void SimulatorFeed::start() {
    if (worker_.joinable()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        failure_.reset();
    }
    timeoutCount_.store(0U, std::memory_order_relaxed);
    try {
        worker_ = std::jthread([this](std::stop_token stopToken) {
            run(stopToken);
        });
    } catch (const std::exception& exception) {
        recordFailure(workerError(
            core::ErrorCategory::ResourceExhaustion,
            "simulator_worker_start_failed",
            "The simulated viewer worker could not start.",
            exception.what()));
    }
}

void SimulatorFeed::stop() noexcept {
    if (!worker_.joinable()) {
        return;
    }
    worker_.request_stop();
    worker_.join();
}

core::Result<void> SimulatorFeed::result() const {
    std::lock_guard lock(mutex_);
    if (failure_) {
        return core::Result<void>::failure(*failure_);
    }
    return core::Result<void>::success();
}

std::uint64_t SimulatorFeed::timeoutCount() const noexcept {
    return timeoutCount_.load(std::memory_order_relaxed);
}

void SimulatorFeed::recordFailure(core::Error error) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (!failure_ || failure_->code == "acquisition_timeout") {
            failure_ = std::move(error);
        }
    } catch (...) {
        // The worker must remain joinable even if diagnostic allocation fails.
    }
}

void SimulatorFeed::recordTimeout(core::Error error) noexcept {
    try {
        auto count = timeoutCount_.load(std::memory_order_relaxed);
        while (count != std::numeric_limits<std::uint64_t>::max() &&
               !timeoutCount_.compare_exchange_weak(
                   count, count + 1U, std::memory_order_relaxed)) {}
        std::lock_guard lock(mutex_);
        if (failure_ && failure_->code != "acquisition_timeout") {
            return;
        }
        failure_ = std::move(error);
    } catch (...) {
        // Keep acquisition running if diagnostic snapshotting cannot allocate.
    }
}

void SimulatorFeed::run(std::stop_token stopToken) noexcept {
    try {
        camera::sim::SimulatedCameraProvider provider(options(), *clock_);
        auto discovered = provider.discover(stopToken);
        if (!discovered.hasValue()) {
            recordFailure(std::move(discovered).error());
            return;
        }
        if (discovered.value().empty()) {
            recordFailure(workerError(
                core::ErrorCategory::CameraDiscovery,
                "simulator_not_found",
                "The simulated viewer camera was not found.",
                "The simulator provider returned no camera descriptors."));
            return;
        }

        auto created = provider.create(discovered.value().front().id);
        if (!created.hasValue()) {
            recordFailure(std::move(created).error());
            return;
        }
        auto device = std::move(created).value();
        auto poolResult = core::BufferPool::create(10U, 640U * 480U);
        if (!poolResult.hasValue()) {
            recordFailure(std::move(poolResult).error());
            return;
        }
        auto pool = std::move(poolResult).value();

        auto opened = device->open();
        if (!opened.hasValue()) {
            recordFailure(std::move(opened).error());
            return;
        }
        auto applied = device->applyConfiguration(configuration());
        if (!applied.hasValue()) {
            recordFailure(std::move(applied).error());
            static_cast<void>(device->close());
            return;
        }
        auto started = device->startStream();
        if (!started.hasValue()) {
            recordFailure(std::move(started).error());
            static_cast<void>(device->close());
            return;
        }

        while (!stopToken.stop_requested()) {
            auto retrieved = device->retrieve(50ms, *pool, stopToken);
            if (!retrieved.hasValue()) {
                auto error = std::move(retrieved).error();
                if (stopToken.stop_requested() &&
                    error.category == core::ErrorCategory::Cancelled) {
                    break;
                }
                if (error.code == "acquisition_timeout") {
                    recordTimeout(std::move(error));
                    continue;
                }
                recordFailure(std::move(error));
                break;
            }
            auto raw = std::move(retrieved).value();
            auto display = core::DisplayFrame::create(
                raw->frameId, raw->layout, raw->pixels,
                core::DisplayStorage::Gray8,
                core::DisplayMapping{0U, 255U, 255U, 1U},
                core::Orientation{false, false, core::Rotation::Degrees0});
            if (!display.hasValue()) {
                recordFailure(std::move(display).error());
                break;
            }
            auto bundle = core::FrameBundle::create(
                raw, std::move(display).value(), nullptr, nullptr);
            if (!bundle.hasValue()) {
                recordFailure(std::move(bundle).error());
                break;
            }
            if (slot_->closed()) {
                recordFailure(workerError(
                    core::ErrorCategory::Acquisition,
                    "viewer_slot_closed",
                    "The simulated frame could not be published.",
                    "The latest-frame slot was closed before publication."));
                break;
            }
            const auto published = slot_->publish(std::move(bundle).value());
            if (slot_->closed()) {
                recordFailure(workerError(
                    core::ErrorCategory::Acquisition,
                    "viewer_slot_closed",
                    "The simulated frame could not be published.",
                    "The latest-frame slot closed during publication."));
                break;
            }
            static_cast<void>(published);
        }

        auto stopped = device->stopStream();
        if (!stopped.hasValue()) {
            recordFailure(std::move(stopped).error());
        }
        auto closed = device->close();
        if (!closed.hasValue()) {
            recordFailure(std::move(closed).error());
        }
    } catch (const std::bad_alloc&) {
        recordFailure(workerError(
            core::ErrorCategory::ResourceExhaustion,
            "simulator_worker_allocation_failed",
            "The simulated viewer ran out of memory.",
            "A worker-owned simulator object could not be allocated."));
    } catch (const std::exception& exception) {
        recordFailure(workerError(
            core::ErrorCategory::Internal,
            "simulator_worker_exception",
            "The simulated viewer worker stopped unexpectedly.",
            exception.what()));
    } catch (...) {
        recordFailure(workerError(
            core::ErrorCategory::Internal,
            "simulator_worker_exception",
            "The simulated viewer worker stopped unexpectedly.",
            "An unknown exception escaped simulator acquisition."));
    }
}

}  // namespace lumora::tools
