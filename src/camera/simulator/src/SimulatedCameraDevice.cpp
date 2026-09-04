#include <lumora/camera/CameraConfigurationValidator.hpp>
#include <lumora/camera/ICameraDevice.hpp>
#include <lumora/camera/sim/IPatternGenerator.hpp>
#include <lumora/camera/sim/SimulatedCameraOptions.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/Error.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/FrameMetadata.hpp>
#include <lumora/core/ImageLayout.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace lumora::camera::sim {
namespace {

using FrameResult = core::Result<std::shared_ptr<const core::RawFrame>>;

[[nodiscard]] core::Error lifecycleError(
    core::ErrorCategory category,
    std::string code,
    std::string summary,
    std::string detail) {
    return {
        .category = category,
        .code = std::move(code),
        .operatorSummary = std::move(summary),
        .diagnosticDetail = std::move(detail),
        .recoverable = true,
    };
}

[[nodiscard]] core::Error notOpenError() {
    return lifecycleError(
        core::ErrorCategory::CameraConnection,
        "camera_not_open",
        "The camera is not open.",
        "Open the simulated camera before using this operation.");
}

[[nodiscard]] core::Error notStreamingError() {
    return lifecycleError(
        core::ErrorCategory::Acquisition,
        "stream_not_started",
        "The camera stream is not running.",
        "Start the simulated camera stream before retrieving frames.");
}

[[nodiscard]] core::Error cancelledError() {
    return lifecycleError(
        core::ErrorCategory::Cancelled,
        "cancelled",
        "Frame retrieval was cancelled.",
        "The retrieval stop token was requested before a frame was published.");
}

[[nodiscard]] core::Error timeoutError() {
    return lifecycleError(
        core::ErrorCategory::Acquisition,
        "acquisition_timeout",
        "No camera frame arrived before the timeout.",
        "The next simulated frame was scheduled after the retrieval deadline.");
}

[[nodiscard]] core::Error poolExhaustedError() {
    return lifecycleError(
        core::ErrorCategory::ResourceExhaustion,
        "buffer_pool_exhausted",
        "No image buffer is currently available.",
        "All blocks in the caller-provided buffer pool are in use.");
}

[[nodiscard]] core::Error invalidFrameError(std::string detail) {
    return {
        .category = core::ErrorCategory::InvalidFrame,
        .code = "invalid_frame",
        .operatorSummary = "The simulator could not publish a valid frame.",
        .diagnosticDetail = std::move(detail),
        .recoverable = true,
    };
}

[[nodiscard]] core::Error allocationError() {
    return {
        .category = core::ErrorCategory::ResourceExhaustion,
        .code = "simulator_allocation_failed",
        .operatorSummary = "The simulated camera could not be created.",
        .diagnosticDetail = "Allocating simulator device state failed.",
        .recoverable = true,
    };
}

template<typename Mode>
[[nodiscard]] bool contains(const std::vector<Mode>& modes, Mode wanted) {
    return std::find(modes.begin(), modes.end(), wanted) != modes.end();
}

[[nodiscard]] double quantize(
    double requested,
    const NumericCapability& capability) noexcept {
    const auto steps = std::round(
        (requested - capability.minimum) / capability.increment);
    return std::clamp(
        capability.minimum + steps * capability.increment,
        capability.minimum,
        capability.maximum);
}

[[nodiscard]] CameraConfiguration defaultConfiguration(
    const SimulatedCameraOptions& options) {
    const auto& capabilities = options.capabilities;
    const auto exposureIsAuto =
        contains(capabilities.exposureModes, ExposureMode::Auto);
    const auto gainIsAuto = contains(capabilities.gainModes, GainMode::Auto);
    return {
        .pixelFormat = capabilities.pixelFormats.empty()
                           ? core::SourcePixelFormat{}
                           : capabilities.pixelFormats.front(),
        .roi = {
            .x = capabilities.roi.minimum.x,
            .y = capabilities.roi.minimum.y,
            .width = capabilities.roi.maximum.width,
            .height = capabilities.roi.maximum.height,
        },
        .requestedFps = options.defaultFps,
        .exposure = {
            .mode = exposureIsAuto ? ExposureMode::Auto : ExposureMode::Manual,
            .requestedMicroseconds = exposureIsAuto
                                        ? std::nullopt
                                        : std::optional{capabilities.exposure.minimum},
        },
        .gain = {
            .mode = gainIsAuto ? GainMode::Auto : GainMode::Manual,
            .requestedDb = gainIsAuto
                               ? std::nullopt
                               : std::optional{capabilities.gain.minimum},
        },
        .acquisitionMode = AcquisitionMode::Continuous,
    };
}

[[nodiscard]] AppliedCameraConfiguration appliedConfiguration(
    const CameraConfiguration& requested,
    const SimulatedCameraOptions& options) {
    auto actual = requested;
    actual.requestedFps = quantize(
        requested.requestedFps.value_or(options.defaultFps),
        options.capabilities.frameRate);
    if (requested.exposure.requestedMicroseconds.has_value()) {
        actual.exposure.requestedMicroseconds = quantize(
            *requested.exposure.requestedMicroseconds,
            options.capabilities.exposure);
    }
    if (requested.gain.requestedDb.has_value()) {
        actual.gain.requestedDb = quantize(
            *requested.gain.requestedDb,
            options.capabilities.gain);
    }
    return {.requested = requested, .actual = std::move(actual)};
}

[[nodiscard]] std::size_t bytesPerSample(core::StorageType storage) noexcept {
    switch (storage) {
    case core::StorageType::UInt8:
        return sizeof(std::uint8_t);
    case core::StorageType::UInt16:
        return sizeof(std::uint16_t);
    }
    return 0U;
}

[[nodiscard]] std::chrono::steady_clock::duration framePeriod(double fps) {
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / fps));
    return std::max(period, std::chrono::steady_clock::duration{1});
}

class SimulatedCameraDevice final : public ICameraDevice {
public:
    SimulatedCameraDevice(
        SimulatedCameraOptions options,
        core::IClock& clock,
        std::unique_ptr<IPatternGenerator> generator,
        AppliedCameraConfiguration initialConfiguration)
        : options_(std::move(options)),
          clock_(&clock),
          generator_(std::move(generator)),
          applied_(std::move(initialConfiguration)) {}

    core::Result<void> open() override {
        if (state_ == State::Closed) {
            state_ = State::Open;
        }
        return core::Result<void>::success();
    }

    core::Result<CameraCapabilities> capabilities() override {
        if (state_ == State::Closed) {
            return core::Result<CameraCapabilities>::failure(notOpenError());
        }
        return core::Result<CameraCapabilities>::success(options_.capabilities);
    }

    core::Result<AppliedCameraConfiguration> applyConfiguration(
        const CameraConfiguration& configuration) override {
        if (state_ == State::Closed) {
            return core::Result<AppliedCameraConfiguration>::failure(notOpenError());
        }
        const auto validated =
            validateCameraConfiguration(configuration, options_.capabilities);
        if (!validated.hasValue()) {
            return core::Result<AppliedCameraConfiguration>::failure(
                validated.error());
        }

        applied_ = appliedConfiguration(configuration, options_);
        if (state_ == State::Streaming && hasPublishedFrame_) {
            nextFrameDeadline_ = clock_->steadyNow() + period();
        }
        return core::Result<AppliedCameraConfiguration>::success(applied_);
    }

    core::Result<void> startStream() override {
        if (state_ == State::Closed) {
            return core::Result<void>::failure(notOpenError());
        }
        if (state_ == State::Open) {
            state_ = State::Streaming;
            hasPublishedFrame_ = false;
        }
        return core::Result<void>::success();
    }

    FrameResult retrieve(
        std::chrono::milliseconds timeout,
        core::BufferPool& destination,
        std::stop_token stopToken) override {
        if (state_ != State::Streaming) {
            return FrameResult::failure(notStreamingError());
        }
        if (stopToken.stop_requested()) {
            return FrameResult::failure(cancelledError());
        }

        const auto pacing = waitForPacing(timeout, stopToken);
        if (!pacing.hasValue()) {
            return FrameResult::failure(pacing.error());
        }

        const auto& actual = applied_.actual;
        const auto sampleBytes = bytesPerSample(
            actual.pixelFormat.applicationStorage);
        const auto strideBytes =
            static_cast<std::size_t>(actual.roi.width) * sampleBytes;
        const auto payloadBytes =
            strideBytes * static_cast<std::size_t>(actual.roi.height);
        const auto layout = core::ImageLayout::create(
            actual.roi.width,
            actual.roi.height,
            strideBytes,
            actual.pixelFormat.applicationStorage,
            payloadBytes);
        if (!layout.hasValue()) {
            return FrameResult::failure(
                invalidFrameError(layout.error().diagnosticDetail));
        }

        auto lease = destination.tryAcquire();
        if (!lease.has_value()) {
            return FrameResult::failure(poolExhaustedError());
        }
        const auto generated = generator_->fill(
            lease->bytes(),
            actual.roi.width,
            actual.roi.height,
            strideBytes,
            actual.pixelFormat,
            nextFrameId_);
        if (!generated.hasValue()) {
            return FrameResult::failure(generated.error());
        }

        auto sealed = std::move(*lease).seal();
        lease.reset();
        const auto fps = *actual.requestedFps;
        const auto settings = core::AcquisitionSettingsSnapshot::create(
            identity(),
            actual.pixelFormat,
            actual.roi,
            applied_.requested.requestedFps.value_or(options_.defaultFps),
            fps,
            actual.exposure.requestedMicroseconds,
            actual.gain.requestedDb);
        if (!settings.hasValue()) {
            return FrameResult::failure(
                invalidFrameError(settings.error().diagnosticDetail));
        }

        const auto hostReceipt = clock_->steadyNow();
        const auto frame = core::RawFrame::create(
            nextFrameId_,
            layout.value(),
            std::move(sealed),
            core::FrameMetadata{
                .cameraFrameId = nextFrameId_,
                .hostReceiptTime = hostReceipt,
                .acquisitionUtcTime = clock_->utcNow(),
                .deviceTimestamp = std::nullopt,
                .acquisitionSettings = settings.value(),
            });
        if (!frame.hasValue()) {
            return FrameResult::failure(
                invalidFrameError(frame.error().diagnosticDetail));
        }

        ++nextFrameId_;
        publishPacingDeadline(hostReceipt);
        return frame;
    }

    core::Result<void> stopStream() noexcept override {
        if (state_ == State::Streaming) {
            state_ = State::Open;
            hasPublishedFrame_ = false;
        }
        return core::Result<void>::success();
    }

    core::Result<void> close() noexcept override {
        state_ = State::Closed;
        hasPublishedFrame_ = false;
        return core::Result<void>::success();
    }

private:
    enum class State {
        Closed,
        Open,
        Streaming,
    };

    [[nodiscard]] core::CameraIdentity identity() const {
        return {
            .manufacturer = "Lumora",
            .model = "Generated Camera",
            .serial = options_.id.value,
            .transport = "Simulator",
            .firmware = std::nullopt,
        };
    }

    [[nodiscard]] std::chrono::steady_clock::duration period() const {
        return framePeriod(*applied_.actual.requestedFps);
    }

    [[nodiscard]] core::Result<void> waitForPacing(
        std::chrono::milliseconds timeout,
        std::stop_token stopToken) {
        frameWasLate_ = false;
        if (options_.pacing == SimulationPacingMode::Fastest ||
            !hasPublishedFrame_) {
            return core::Result<void>::success();
        }

        const auto now = clock_->steadyNow();
        if (now > nextFrameDeadline_) {
            frameWasLate_ = true;
            invokePacingSlipHook();
            return core::Result<void>::success();
        }
        if (now == nextFrameDeadline_) {
            return core::Result<void>::success();
        }

        const auto nonnegativeTimeout = std::max(timeout, std::chrono::milliseconds::zero());
        const auto timeoutDeadline = now + nonnegativeTimeout;
        const auto waitingForFrame = nextFrameDeadline_ <= timeoutDeadline;
        const auto waitDeadline = waitingForFrame ? nextFrameDeadline_ : timeoutDeadline;
        if (!clock_->waitUntil(waitDeadline, stopToken)) {
            return core::Result<void>::failure(cancelledError());
        }
        if (!waitingForFrame) {
            return core::Result<void>::failure(timeoutError());
        }
        return core::Result<void>::success();
    }

    void publishPacingDeadline(
        std::chrono::steady_clock::time_point publicationTime) noexcept {
        if (options_.pacing == SimulationPacingMode::Fastest) {
            hasPublishedFrame_ = true;
            return;
        }
        if (!hasPublishedFrame_ || frameWasLate_) {
            nextFrameDeadline_ = publicationTime + period();
        } else {
            nextFrameDeadline_ += period();
        }
        hasPublishedFrame_ = true;
    }

    void invokePacingSlipHook() noexcept {
        if (!options_.pacingSlipHook) {
            return;
        }
        try {
            options_.pacingSlipHook();
        } catch (...) {
            // Diagnostics hooks must never interrupt frame publication.
        }
    }

    SimulatedCameraOptions options_;
    core::IClock* clock_;
    std::unique_ptr<IPatternGenerator> generator_;
    AppliedCameraConfiguration applied_;
    State state_{State::Closed};
    std::uint64_t nextFrameId_{1U};
    bool hasPublishedFrame_{false};
    bool frameWasLate_{false};
    std::chrono::steady_clock::time_point nextFrameDeadline_{};
};

}  // namespace

core::Result<std::unique_ptr<ICameraDevice>> makeSimulatedCameraDevice(
    SimulatedCameraOptions options,
    core::IClock& clock) {
    const auto initial = defaultConfiguration(options);
    const auto validated =
        validateCameraConfiguration(initial, options.capabilities);
    if (!validated.hasValue()) {
        return core::Result<std::unique_ptr<ICameraDevice>>::failure(
            validated.error());
    }

    try {
        auto generator = makePatternGenerator(options.pattern, options.seed);
        return core::Result<std::unique_ptr<ICameraDevice>>::success(
            std::make_unique<SimulatedCameraDevice>(
                options,
                clock,
                std::move(generator),
                appliedConfiguration(initial, options)));
    } catch (const std::bad_alloc&) {
        return core::Result<std::unique_ptr<ICameraDevice>>::failure(
            allocationError());
    }
}

}  // namespace lumora::camera::sim
