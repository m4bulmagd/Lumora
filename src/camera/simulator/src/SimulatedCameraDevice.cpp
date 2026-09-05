#include "FrameSource.hpp"
#include "RetrievalBudget.hpp"
#ifdef LUMORA_SIMULATOR_TEST_HOOKS
#include "SimulatedFaultPreparationHook.hpp"
#endif

#include <lumora/camera/CameraConfigurationValidator.hpp>
#include <lumora/camera/ICameraDevice.hpp>
#include <lumora/camera/sim/FaultScript.hpp>
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
#include <filesystem>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace lumora::camera::sim {
#ifdef LUMORA_SIMULATOR_TEST_HOOKS
namespace testing {
namespace {
thread_local FaultPreparationHook faultPreparationHook{};
}

FaultPreparationHook exchangeFaultPreparationHook(FaultPreparationHook hook) noexcept {
    return std::exchange(faultPreparationHook, hook);
}
}  // namespace testing
#endif
namespace {

using FrameResult = core::Result<std::shared_ptr<const core::RawFrame>>;

void faultPreparationCheckpoint() {
#ifdef LUMORA_SIMULATOR_TEST_HOOKS
    if (testing::faultPreparationHook) {
        testing::faultPreparationHook();
    }
#endif
}

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
    return {
        core::ErrorCategory::Cancelled,
        "cancelled",
        "Frame retrieval was cancelled.",
        "The retrieval stop token was requested before a frame was published.", true};
}

[[nodiscard]] core::Error timeoutError() {
    return {
        core::ErrorCategory::Acquisition,
        "acquisition_timeout",
        "No camera frame arrived before the timeout.",
        "The retrieval budget expired before frame publication.", true};
}

[[nodiscard]] core::Error disconnectedError() {
    return {core::ErrorCategory::CameraConnection,
        "simulated_disconnect", "The simulated camera is disconnected.",
        "The test controller must call FaultScript::restoreConnection.", true};
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

template<typename Value, typename PrepareError>
[[nodiscard]] core::Result<Value> commitScriptedFailure(
    FaultScript& script,
    FaultScript::Occurrence occurrence,
    std::stop_token stopToken,
    PrepareError&& prepareError,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) {
    using Result = core::Result<Value>;
    try {
        faultPreparationCheckpoint();
        const auto error = std::forward<PrepareError>(prepareError)();
        auto pending = script.prepareConsumption(occurrence);
        faultPreparationCheckpoint();
        // This is the cancellation boundary. A stop racing with the final error
        // copy after this check does not replace the selected scripted failure.
        if (stopToken.stop_requested()) {
            return Result::failureAndCommit(cancelledError(), []() noexcept {});
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return Result::failureAndCommit(timeoutError(), []() noexcept {});
        }
        // Every return from here through retrieve/applyConfiguration is a
        // same-type prvalue. C++20 constructs the caller's final result directly:
        // error copy first, scalar commit second, no Result/Error move afterward.
        return Result::failureAndCommit(error, [&pending]() noexcept { pending.commit(); });
    } catch (const std::bad_alloc&) {
        faultPreparationCheckpoint();
        // Translation can itself allocate and fail. If it does, the exception
        // propagates while the uncommitted occurrence and connection stay intact.
        return Result::failureAndCommit({core::ErrorCategory::ResourceExhaustion,
            "alloc_failed", "Failure preparation could not allocate memory.",
            "The scripted occurrence remains pending.", true}, []() noexcept {});
    } catch (const std::length_error&) {
        faultPreparationCheckpoint();
        return Result::failureAndCommit({core::ErrorCategory::ResourceExhaustion,
            "size_limit", "Failure preparation exceeded a storage limit.",
            "The scripted occurrence remains pending.", true}, []() noexcept {});
    } catch (const std::filesystem::filesystem_error&) {
        faultPreparationCheckpoint();
        return Result::failureAndCommit({core::ErrorCategory::Storage,
            "io_failed", "Failure preparation encountered an I/O error.",
            "The scripted occurrence remains pending.", true}, []() noexcept {});
    } catch (const std::ios_base::failure&) {
        faultPreparationCheckpoint();
        return Result::failureAndCommit({core::ErrorCategory::Storage,
            "io_failed", "Failure preparation encountered an I/O error.",
            "The scripted occurrence remains pending.", true}, []() noexcept {});
    }
}

[[nodiscard]] core::Error unrepresentableFramePeriodError(double fps) {
    return {
        .category = core::ErrorCategory::CameraConfiguration,
        .code = "frame_period_unrepresentable",
        .operatorSummary = "The requested frame rate cannot be scheduled.",
        .diagnosticDetail =
            "FPS " + std::to_string(fps) +
            " is outside the injected steady clock's duration range.",
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
        requested.requestedFps.value_or(options.capabilities.frameRate.maximum),
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

[[nodiscard]] std::optional<core::Error> streamingConfigurationError(
    const CameraConfiguration& requested,
    const CameraConfiguration& previous,
    const CameraCapabilities& capabilities) {
    const auto formatValues = [](const core::SourcePixelFormat& f) {
        return std::tie(f.canonicalName, f.canonicalEncoding, f.validBits, f.sampleMaximum,
            f.packing, f.alignment, f.applicationStorage);
    };
    const auto roiValues = [](const core::RegionOfInterest& roi) {
        return std::tie(roi.x, roi.y, roi.width, roi.height);
    };
    const char* code = nullptr;
    if (formatValues(requested.pixelFormat) != formatValues(previous.pixelFormat)) {
        code = "pixel_format_not_writable_while_streaming";
    } else if (roiValues(requested.roi) != roiValues(previous.roi)) {
        code = "roi_not_writable_while_streaming";
    } else if (!capabilities.frameRate.writableWhileStreaming &&
               requested.requestedFps != previous.requestedFps) {
        code = "frame_rate_not_writable_while_streaming";
    } else if (!capabilities.exposure.writableWhileStreaming &&
               (requested.exposure.mode != previous.exposure.mode ||
                requested.exposure.requestedMicroseconds != previous.exposure.requestedMicroseconds)) {
        code = "exposure_not_writable_while_streaming";
    } else if (!capabilities.gain.writableWhileStreaming &&
               (requested.gain.mode != previous.gain.mode ||
                requested.gain.requestedDb != previous.gain.requestedDb)) {
        code = "gain_not_writable_while_streaming";
    }
    if (!code) {
        return std::nullopt;
    }
    return lifecycleError(core::ErrorCategory::CameraConfiguration, code,
        "Stop the stream before changing this setting.",
        "The requested setting cannot be changed while the simulated camera is streaming.");
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

[[nodiscard]] core::Result<std::chrono::steady_clock::duration>
validatedFramePeriod(double fps) {
    using Duration = std::chrono::steady_clock::duration;
    using Period = Duration::period;
    using Representation = Duration::rep;

    const auto ticksPerSecond =
        static_cast<long double>(Period::den) /
        static_cast<long double>(Period::num);
    const auto ticks = ticksPerSecond / static_cast<long double>(fps);
    static_assert(std::numeric_limits<Representation>::is_integer);
    static_assert(std::numeric_limits<Representation>::is_signed);
    const auto exclusiveMaximumTicks = std::ldexp(
        1.0L,
        std::numeric_limits<Representation>::digits);
    if (!std::isfinite(ticks) || ticks < 1.0L ||
        ticks >= exclusiveMaximumTicks) {
        return core::Result<Duration>::failure(
            unrepresentableFramePeriodError(fps));
    }
    return core::Result<Duration>::success(
        Duration{static_cast<Representation>(ticks)});
}

[[nodiscard]] std::chrono::steady_clock::time_point saturatingDeadline(
    std::chrono::steady_clock::time_point base,
    std::chrono::steady_clock::duration delay) noexcept {
    using Duration = std::chrono::steady_clock::duration;
    using Representation = Duration::rep;

    if (delay <= Duration::zero()) {
        return base;
    }
    const auto baseTicks = base.time_since_epoch().count();
    const auto delayTicks = delay.count();
    constexpr auto maximumTicks = std::numeric_limits<Representation>::max();
    if (baseTicks > maximumTicks - delayTicks) {
        return std::chrono::steady_clock::time_point::max();
    }
    return std::chrono::steady_clock::time_point{
        Duration{static_cast<Representation>(baseTicks + delayTicks)}};
}

[[nodiscard]] std::chrono::steady_clock::time_point retrievalDeadline(
    std::chrono::milliseconds timeout) noexcept {
    using Clock = std::chrono::steady_clock;
    // Clamp before converting milliseconds to the finer steady-clock duration.
    if (timeout >= std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration::max())) {
        return Clock::time_point::max();
    }
    return saturatingDeadline(Clock::now(),
        std::chrono::duration_cast<Clock::duration>(std::max(timeout, std::chrono::milliseconds::zero())));
}

class SimulatedCameraDevice final : public ICameraDevice {
public:
    SimulatedCameraDevice(
        SimulatedCameraOptions options,
        core::IClock& clock,
        std::unique_ptr<FrameSource> source,
        AppliedCameraConfiguration initialConfiguration,
        std::chrono::steady_clock::duration framePeriod)
        : options_(std::move(options)),
          clock_(&clock),
          source_(std::move(source)),
          applied_(std::move(initialConfiguration)),
          framePeriod_(framePeriod) {}

    core::Result<void> open() override {
        if (disconnected()) {
            return core::Result<void>::failure(disconnectedError());
        }
        if (state_ == State::Closed) {
            state_ = State::Open;
        }
        return core::Result<void>::success();
    }

    core::Result<CameraCapabilities> capabilities() override {
        if (state_ == State::Closed) {
            return core::Result<CameraCapabilities>::failure(notOpenError());
        }
        if (disconnected()) {
            return core::Result<CameraCapabilities>::failure(disconnectedError());
        }
        return core::Result<CameraCapabilities>::success(options_.capabilities);
    }

    core::Result<AppliedCameraConfiguration> applyConfiguration(
        const CameraConfiguration& configuration) override {
        if (state_ == State::Closed) {
            return core::Result<AppliedCameraConfiguration>::failure(notOpenError());
        }
        if (disconnected()) {
            return core::Result<AppliedCameraConfiguration>::failure(disconnectedError());
        }
        const auto validated =
            validateCameraConfiguration(configuration, options_.capabilities);
        if (!validated.hasValue()) {
            return core::Result<AppliedCameraConfiguration>::failure(
                validated.error());
        }

        if (state_ == State::Streaming) {
            if (const auto error = streamingConfigurationError(
                    configuration, applied_.requested, options_.capabilities)) {
                return core::Result<AppliedCameraConfiguration>::failure(*error);
            }
        }

        auto candidate = appliedConfiguration(configuration, options_);
        const auto candidatePeriod =
            validatedFramePeriod(*candidate.actual.requestedFps);
        if (!candidatePeriod.hasValue()) {
            return core::Result<AppliedCameraConfiguration>::failure(
                candidatePeriod.error());
        }
        if (const auto fault = matchingFault(FaultPoint::Configuration)) {
            return commitScriptedFailure<AppliedCameraConfiguration>(
                *options_.faults, *fault, {}, [] {
                    return core::Error{
                        core::ErrorCategory::CameraConfiguration, "simulated_configuration_failure",
                        "The simulated configuration failed.",
                        "The fault script rejected this validated configuration attempt.", true};
                });
        }
        const auto periodChanged = candidatePeriod.value() != framePeriod_;
        applied_ = std::move(candidate);
        framePeriod_ = candidatePeriod.value();
        if (state_ == State::Streaming && hasPublishedFrame_ && periodChanged) {
            nextFrameDeadline_ =
                saturatingDeadline(clock_->steadyNow(), period());
        }
        return core::Result<AppliedCameraConfiguration>::success(applied_);
    }

    core::Result<void> startStream() override {
        if (state_ == State::Closed) {
            return core::Result<void>::failure(notOpenError());
        }
        if (disconnected()) {
            return core::Result<void>::failure(disconnectedError());
        }
        if (state_ == State::Open) {
            state_ = State::Streaming;
            hasPublishedFrame_ = false;
            streamStart_ = clock_->steadyNow();
        }
        return core::Result<void>::success();
    }

    FrameResult retrieve(
        std::chrono::milliseconds timeout,
        core::BufferPool& destination,
        std::stop_token stopToken) override {
        const auto realDeadline = retrievalDeadline(timeout);
        const RetrievalBudget budget(realDeadline, stopToken);
        if (state_ != State::Streaming) {
            return FrameResult::failure(notStreamingError());
        }
        if (stopToken.stop_requested()) {
            return FrameResult::failure(cancelledError());
        }

        if (disconnected()) {
            return FrameResult::failure(disconnectedError());
        }

        const auto pacing = waitForPacing(budget, stopToken);
        if (!pacing.hasValue()) {
            return FrameResult::failure(pacing.error());
        }
        if (stopToken.stop_requested()) {
            return FrameResult::failure(cancelledError());
        }

        if (disconnected()) {
            return FrameResult::failure(disconnectedError());
        }
        const auto fault = matchingFault(FaultPoint::Retrieval);
        // Preserve the explicit zero-budget scripted Timeout attempt. A
        // positive budget already spent in pacing or fault lookup must not
        // consume a newly selected occurrence.
        if (timeout > std::chrono::milliseconds::zero()) {
            if (const auto interrupted = budget.interruption()) {
                return FrameResult::failure(*interrupted);
            }
        }
        if (fault && fault->fault == SimulatedFault::Timeout) {
            core::SystemClock realClock;
            const auto wait = realClock.waitUntil(realDeadline, stopToken,
                budget.remainingWait());
            if (wait == core::ClockWaitOutcome::Cancelled || stopToken.stop_requested()) {
                return FrameResult::failure(cancelledError());
            }
            return commitScriptedFailure<std::shared_ptr<const core::RawFrame>>(
                *options_.faults, *fault, stopToken, [] {
                    return timeoutError();
                });
        }
        if (const auto interrupted = budget.interruption()) {
            return FrameResult::failure(*interrupted);
        }
        if (fault && fault->fault == SimulatedFault::Disconnect) {
            return commitScriptedFailure<std::shared_ptr<const core::RawFrame>>(
                *options_.faults, *fault, stopToken, [] {
                    return disconnectedError();
                }, realDeadline);
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
        if (const auto interrupted = budget.interruption()) {
            return FrameResult::failure(*interrupted);
        }

        auto lease = destination.tryAcquire();
        if (!lease.has_value()) {
            return FrameResult::failure(poolExhaustedError());
        }
        const auto generated = source_->fill(
            lease->bytes(),
            actual,
            strideBytes,
            nextFrameId_,
            stopToken,
            realDeadline);
        if (!generated.hasValue()) {
            return FrameResult::failure(generated.error());
        }
        if (const auto interrupted = budget.interruption()) {
            return FrameResult::failure(*interrupted);
        }
        if (!generated.value()) {
            // Stop is a completed end-of-stream transition. Reopening does not
            // rewind a recording; create another device for a fresh replay.
            state_ = State::Open;
            hasPublishedFrame_ = false;
            return FrameResult::failure(lifecycleError(core::ErrorCategory::Acquisition,
                "sequence_stopped", "The evaluation sequence has ended.",
                "The Stop end policy stopped the camera stream."));
        }
        if (fault && fault->fault == SimulatedFault::MalformedFrame) {
            // Return writable storage before the failure's state commit so no
            // pool-release locking remains on the committed return path.
            lease.reset();
            // Construct a genuinely invalid candidate and return the normal
            // layout validator's typed error; never fabricate a malformed error.
            return commitScriptedFailure<std::shared_ptr<const core::RawFrame>>(
                *options_.faults, *fault, stopToken, [&] {
                    auto malformed = core::ImageLayout::create(
                        0U, actual.roi.height, strideBytes,
                        actual.pixelFormat.applicationStorage, payloadBytes);
                    return malformed.error();
                }, realDeadline);
        }

        auto sealed = std::move(*lease).seal();
        lease.reset();
        const auto fps = *actual.requestedFps;
        const auto settings = core::AcquisitionSettingsSnapshot::create(
            identity(),
            actual.pixelFormat,
            actual.roi,
            applied_.requested.requestedFps.value_or(options_.capabilities.frameRate.maximum),
            fps,
            actual.exposure.requestedMicroseconds,
            actual.gain.requestedDb);
        if (!settings.hasValue()) {
            return FrameResult::failure(
                invalidFrameError(settings.error().diagnosticDetail));
        }
        if (const auto interrupted = budget.interruption()) {
            return FrameResult::failure(*interrupted);
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
        const auto publicationTime = clock_->steadyNow();
        markFrameLateIfBehind(publicationTime);
        if (const auto interrupted = budget.interruption()) {
            return FrameResult::failure(*interrupted);
        }
        ++nextFrameId_;
        source_->commit();
        publishPacingDeadline(publicationTime);
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
            .model = source_->model(),
            .serial = options_.id.value,
            .transport = "Simulator",
            .firmware = std::nullopt,
        };
    }

    [[nodiscard]] std::chrono::steady_clock::duration period() const {
        return framePeriod_;
    }

    [[nodiscard]] bool disconnected() const {
        return options_.faults && options_.faults->disconnected();
    }

    [[nodiscard]] std::optional<FaultScript::Occurrence> matchingFault(FaultPoint point) const {
        if (!options_.faults) {
            return std::nullopt;
        }
        std::optional<std::chrono::milliseconds> elapsed;
        if (streamStart_) {
            using Duration = std::chrono::steady_clock::duration;
            using Unsigned = std::make_unsigned_t<Duration::rep>;
            using UnsignedDuration = std::chrono::duration<Unsigned, Duration::period>;
            using UnsignedMilliseconds = std::chrono::duration<Unsigned, std::milli>;
            // Linux/GCC and Windows/MSVC steady clocks have sub-ms ticks.
            // Unsigned subtraction avoids overflow across negative origins,
            // and integer division preserves exact thresholds on both platforms.
            static_assert(std::ratio_divide<Duration::period, std::milli>::num == 1);
            const auto now = clock_->steadyNow();
            const auto ticks = now <= *streamStart_ ? Unsigned{0U}
                : static_cast<Unsigned>(now.time_since_epoch().count()) -
                  static_cast<Unsigned>(streamStart_->time_since_epoch().count());
            const auto milliseconds = std::chrono::duration_cast<UnsignedMilliseconds>(
                UnsignedDuration{ticks}).count();
            const auto maximum = static_cast<Unsigned>(std::chrono::milliseconds::max().count());
            elapsed = milliseconds >= maximum ? std::chrono::milliseconds::max()
                : std::chrono::milliseconds{
                      static_cast<std::chrono::milliseconds::rep>(milliseconds)};
        }
        return options_.faults->match(nextFrameId_, elapsed, point);
    }

    [[nodiscard]] core::Result<void> waitForPacing(
        const RetrievalBudget& budget,
        std::stop_token stopToken) {
        frameWasLate_ = false;
        if (options_.pacing == SimulationPacingMode::Fastest ||
            !hasPublishedFrame_) {
            return core::Result<void>::success();
        }

        const auto now = clock_->steadyNow();
        if (now > nextFrameDeadline_) {
            markFrameLateIfBehind(now);
            return core::Result<void>::success();
        }
        if (now == nextFrameDeadline_) {
            return core::Result<void>::success();
        }

        const auto outcome = clock_->waitUntil(
            nextFrameDeadline_,
            stopToken,
            budget.remainingWait());
        if (outcome == core::ClockWaitOutcome::Cancelled) {
            return core::Result<void>::failure(cancelledError());
        }
        if (outcome == core::ClockWaitOutcome::MaximumWaitElapsed) {
            return core::Result<void>::failure(timeoutError());
        }
        markFrameLateIfBehind(clock_->steadyNow());
        return core::Result<void>::success();
    }

    void markFrameLateIfBehind(
        std::chrono::steady_clock::time_point now) noexcept {
        if (options_.pacing == SimulationPacingMode::Fastest ||
            !hasPublishedFrame_ || frameWasLate_ || now <= nextFrameDeadline_) {
            return;
        }
        frameWasLate_ = true;
        invokePacingSlipHook();
    }

    void publishPacingDeadline(
        std::chrono::steady_clock::time_point publicationTime) noexcept {
        if (options_.pacing == SimulationPacingMode::Fastest) {
            hasPublishedFrame_ = true;
            return;
        }
        if (!hasPublishedFrame_ || frameWasLate_) {
            nextFrameDeadline_ = saturatingDeadline(publicationTime, period());
        } else {
            nextFrameDeadline_ = saturatingDeadline(nextFrameDeadline_, period());
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
    std::unique_ptr<FrameSource> source_;
    AppliedCameraConfiguration applied_;
    std::chrono::steady_clock::duration framePeriod_;
    State state_{State::Closed};
    std::uint64_t nextFrameId_{1U};
    bool hasPublishedFrame_{false};
    bool frameWasLate_{false};
    std::chrono::steady_clock::time_point nextFrameDeadline_{};
    std::optional<std::chrono::steady_clock::time_point> streamStart_{};
};

}  // namespace

core::Result<std::unique_ptr<ICameraDevice>> makeSimulatedCameraDevice(
    SimulatedCameraOptions options,
    core::IClock& clock) {
    try {
        auto source = makeFrameSource(options);
        if (!source.hasValue()) {
            return core::Result<std::unique_ptr<ICameraDevice>>::failure(source.error());
        }
        const auto initial = defaultConfiguration(options);
        const auto validated =
            validateCameraConfiguration(initial, options.capabilities);
        if (!validated.hasValue()) {
            return core::Result<std::unique_ptr<ICameraDevice>>::failure(
                validated.error());
        }
        auto initialApplied = appliedConfiguration(initial, options);
        const auto initialPeriod =
            validatedFramePeriod(*initialApplied.actual.requestedFps);
        if (!initialPeriod.hasValue()) {
            return core::Result<std::unique_ptr<ICameraDevice>>::failure(
                initialPeriod.error());
        }

        return core::Result<std::unique_ptr<ICameraDevice>>::success(
            std::make_unique<SimulatedCameraDevice>(
                options,
                clock,
                std::move(source).value(),
                std::move(initialApplied),
                initialPeriod.value()));
    } catch (const std::bad_alloc&) {
        return core::Result<std::unique_ptr<ICameraDevice>>::failure(
            allocationError());
    }
}

}  // namespace lumora::camera::sim
