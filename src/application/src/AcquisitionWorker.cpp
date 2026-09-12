#include <lumora/application/AcquisitionWorker.hpp>

#include <lumora/application/CameraSessionStateMachine.hpp>
#include <lumora/application/LiveSessionContext.hpp>
#include <lumora/application/CameraSettingsPolicy.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>
#include <lumora/core/CheckedMath.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

namespace lumora::application {
namespace {
using S = CameraSessionState;
using E = CameraSessionEvent;
using core::ErrorCategory;
using Result = core::Result<void>;

core::Error failure(ErrorCategory category, std::string code, std::string summary) {
    return {category, std::move(code), std::move(summary), "", false};
}
Result rejected(std::string code, std::string summary) {
    return Result::failure(failure(ErrorCategory::CameraConfiguration, std::move(code), std::move(summary)));
}
Result cancelled() {
    return Result::failure(failure(ErrorCategory::Cancelled, "cancelled", "Camera operation was cancelled."));
}
void increment(std::uint64_t& count) noexcept {
    if (count != std::numeric_limits<std::uint64_t>::max()) { ++count; }
}
bool isAcquisitionTimeout(const core::Error& error) noexcept {
    return error.category == ErrorCategory::Acquisition && error.code == "acquisition_timeout";
}
bool sameFormat(const core::SourcePixelFormat& a, const core::SourcePixelFormat& b) {
    return a.canonicalName == b.canonicalName && a.canonicalEncoding == b.canonicalEncoding
        && a.validBits == b.validBits && a.sampleMaximum == b.sampleMaximum
        && a.packing == b.packing && a.alignment == b.alignment
        && a.applicationStorage == b.applicationStorage;
}
bool sameRoi(const core::RegionOfInterest& a, const core::RegionOfInterest& b) noexcept {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool sameMode(const camera::CameraConfiguration& a, const camera::CameraConfiguration& b) {
    return sameFormat(a.pixelFormat, b.pixelFormat) && sameRoi(a.roi, b.roi);
}
}  // namespace

struct AcquisitionWorker::Impl final {
    camera::ICameraProvider& provider;
    CameraCommandMailbox& commands;
    core::BufferPool* rawPool;
    core::LatestValueSlot<core::RawFrame>* rawSlot;
    std::shared_ptr<LiveSessionContext> boundContext;
    struct Reconfiguration final {
        std::uint64_t requestId;
        std::shared_ptr<LiveSessionContext> context;
        camera::CameraConfiguration mode;
    };
    std::mutex reconfigurationMutex;
    std::optional<Reconfiguration> staged;
    std::optional<CameraReconfigurationCompletion> completion;
    bool reconfigurationPending{false};
    std::optional<std::uint64_t> cancelledReconfigurationRequest;
    bool reconfigurationRequiresApply{false};
    core::IClock& clock;
    core::LatestValueSlot<CameraStatusSnapshot>& statusSlot;
    CameraStatusSnapshot status;
    CameraSessionStateMachine machine;
    std::unique_ptr<camera::ICameraDevice> device;
    std::optional<camera::CameraConfiguration> fixedMode;
    std::optional<std::uint64_t> lastFrameId;
    std::optional<std::chrono::steady_clock::time_point> acquisitionProgressAt;
    std::stop_source cancellation;
    std::jthread thread;
    bool started{false};

    Impl(camera::ICameraProvider& p, CameraCommandMailbox& c, core::BufferPool& pool,
         core::LatestValueSlot<core::RawFrame>& slot, core::IClock& time,
         core::LatestValueSlot<CameraStatusSnapshot>& snapshots, CameraStatusSnapshot seed)
        : provider(p), commands(c), rawPool(&pool), rawSlot(&slot), clock(time),
          statusSlot(snapshots), status(std::move(seed)) {}

    [[nodiscard]] bool stopping() const noexcept {
        return cancellation.stop_requested() || commands.closed() || machine.state() == S::ShuttingDown;
    }
    Result event(E value) {
        auto result = machine.apply(value);
        status.state = machine.state();
        return result;
    }
    void publish() {
        status.mailboxStats = commands.stats();
        (void)statusSlot.publish(std::make_shared<const CameraStatusSnapshot>(status));
    }
    void clearDeviceFacts() {
        status.actualIdentity.reset();
        status.capabilities.reset();
        status.appliedConfiguration.reset();
        status.appliedRevision = 0U;
        status.confirmedRevision.reset();
        status.restoreEligible = false;
        status.desiredStreaming = false;
    }
    std::optional<core::Error> cleanup() noexcept {
        acquisitionProgressAt.reset();
        std::optional<core::Error> error;
        if (device) {
            auto stopped = device->stopStream();
            auto closed = device->close();
            device.reset();
            status.sourceReplacementRequired = true;
            // Finish every device operation before composing diagnostics. Error
            // moves do not allocate; diagnostic enrichment is best-effort.
            if (!stopped.hasValue()) { error = std::move(stopped.error()); }
            if (!closed.hasValue()) {
                auto closeError = std::move(closed.error());
                try {
                    if (error) { closeError.diagnosticDetail += " Prior cleanup failure: " + error->code; }
                } catch (...) {}
                error = std::move(closeError);
            }
        }
        clearDeviceFacts();
        return error;
    }
    Result failAndCleanup(core::Error error, E failedEvent) {
        if (auto cleanupError = cleanup()) {
            cleanupError->diagnosticDetail += " Original failure: " + error.code;
            error = std::move(*cleanupError);
            failedEvent = E::DisconnectFailed;
        }
        (void)event(failedEvent);
        increment(status.acquisitionCounters.terminalFailures);
        return Result::failure(std::move(error));
    }
    Result validateMode(const camera::CameraConfiguration& configuration,
                        const camera::CameraConfiguration* prepared,
                        const core::BufferPool& pool) const {
        if (!core::validateSourcePixelFormat(configuration.pixelFormat).hasValue()) {
            return rejected("camera_format_not_available", "The source format must describe valid native numeric storage.");
        }
        if (prepared && !sameMode(configuration, *prepared)) {
            return rejected("camera_mode_change_requires_rebinding", "Changing the source mode requires new pipeline resources.");
        }
        const auto sampleBytes = configuration.pixelFormat.applicationStorage == core::StorageType::UInt8 ? 1U : 2U;
        const auto stride = core::checkedMultiply(configuration.roi.width, sampleBytes);
        if (!stride.hasValue()) return rejected("camera_raw_pool_layout", "Source row size overflows native storage.");
        const auto payload = core::checkedMultiply(stride.value(), configuration.roi.height);
        if (!payload.hasValue() || payload.value() > pool.stats().bytesPerBuffer) {
            return rejected("camera_raw_pool_layout", "The selected mode does not fit the raw buffer pool.");
        }
        const auto layout = core::ImageLayout::create(configuration.roi.width, configuration.roi.height,
            stride.value(), configuration.pixelFormat.applicationStorage, payload.value());
        if (!layout.hasValue()) {
            return rejected("camera_raw_pool_layout", "The selected mode does not fit the raw buffer pool.");
        }
        return Result::success();
    }
    Result validateMode(const camera::CameraConfiguration& configuration) const {
        return validateMode(configuration, fixedMode ? &*fixedMode : nullptr, *rawPool);
    }
    Result generation(std::uint64_t value) const {
        return value == status.sessionGeneration ? Result::success()
            : rejected("stale_camera_session", "Camera command belongs to a retired session.");
    }
    Result discover() {
        auto requested = event(E::DiscoverRequested);
        if (!requested.hasValue()) { return requested; }
        publish();
        auto result = provider.discover(cancellation.get_token());
        if (stopping()) { return cancelled(); }
        if (!result.hasValue()) {
            (void)event(E::DiscoverFailed);
            return Result::failure(result.error());
        }
        status.discoveredDescriptors = std::move(result.value());
        return event(E::DiscoverSucceeded);
    }
    Result connect(const camera::CameraId& id, bool retry) {
        if (id.value.empty()) { return rejected("camera_identity_required", "Select a camera before connecting."); }
        if (device) {
            if (!retry && status.actualIdentity == id
                && (machine.state() == S::ConnectedIdle || machine.state() == S::Streaming)) {
                return Result::success();
            }
            return rejected("invalid_camera_state", "Disconnect the current camera first.");
        }
        if (status.sourceReplacementRequired) {
            return Result::failure(failure(ErrorCategory::CameraConnection, "camera_context_replacement_required",
                "A fresh pipeline context is required before reconnecting."));
        }
        auto requested = event(retry ? E::RetryRequested : E::ConnectRequested);
        if (!requested.hasValue()) { return requested; }
        status.desiredIdentity = id;
        status.desiredStreaming = false;
        publish();
        if (retry) {
            auto found = provider.discover(cancellation.get_token());
            if (stopping()) { return cancelled(); }
            if (!found.hasValue()) { return failAndCleanup(found.error(), E::OpenFailed); }
            status.discoveredDescriptors = std::move(found.value());
            const bool available = std::any_of(status.discoveredDescriptors.begin(), status.discoveredDescriptors.end(),
                [&](const auto& descriptor) { return descriptor.id == id && descriptor.available; });
            if (!available) {
                return failAndCleanup(failure(ErrorCategory::CameraConnection, "camera_not_found",
                    "The selected camera is unavailable. Automatic retry is not enabled."), E::OpenFailed);
            }
        }
        if (stopping()) { return cancelled(); }
        auto created = provider.create(id);
        if (!created.hasValue()) { return failAndCleanup(created.error(), E::OpenFailed); }
        device = std::move(created.value());
        if (!device) { return failAndCleanup(failure(ErrorCategory::Internal, "camera_device_missing", "Provider returned no camera."), E::OpenFailed); }
        if (stopping()) { return cancelled(); }
        auto opened = device->open();
        if (!opened.hasValue()) { return failAndCleanup(opened.error(), E::OpenFailed); }
        if (stopping()) { return cancelled(); }
        auto capabilities = device->capabilities();
        if (!capabilities.hasValue()) { return failAndCleanup(capabilities.error(), E::CapabilitiesFailed); }
        if (stopping()) { return cancelled(); }
        if (std::none_of(capabilities.value().pixelFormats.begin(), capabilities.value().pixelFormats.end(),
            [&](const auto& format) {
                return core::validateSourcePixelFormat(format).hasValue()
                    && (!fixedMode || sameFormat(format, fixedMode->pixelFormat));
            })) {
            return failAndCleanup(failure(ErrorCategory::CameraConfiguration, "camera_format_not_available",
                "The camera does not support the prepared native source format."), E::CapabilitiesFailed);
        }
        status.capabilities = std::move(capabilities.value());
        status.actualIdentity = id;
        status.consecutiveTimeouts = 0U;
        return event(E::OpenSucceeded);
    }
    Result restoreAfterReconfigurationFailure(core::Error error,
        const std::optional<camera::AppliedCameraConfiguration>& previous) {
        status.confirmedRevision.reset();
        status.restoreEligible = false;
        reconfigurationRequiresApply = true;
        if (!previous) return failAndCleanup(std::move(error), E::DisconnectFailed);
        auto restored = device->applyConfiguration(previous->actual);
        if (!restored.hasValue()) return failAndCleanup(restored.error(), E::DisconnectFailed);
        const auto& actual = restored.value().actual;
        const auto& expected = previous->actual;
        auto valid = camera::validateCameraConfiguration(actual, *status.capabilities);
        if (valid.hasValue()) valid = validateMode(actual);
        const bool exact = sameMode(actual, expected)
            && actual.requestedFps == expected.requestedFps
            && actual.exposure.mode == expected.exposure.mode
            && actual.exposure.requestedMicroseconds == expected.exposure.requestedMicroseconds
            && actual.gain.mode == expected.gain.mode
            && actual.gain.requestedDb == expected.gain.requestedDb
            && actual.acquisitionMode == expected.acquisitionMode;
        if (!valid.hasValue() || !exact)
            return failAndCleanup(failure(ErrorCategory::CameraConfiguration,
                "camera_restore_mismatch", "The previous camera configuration could not be verified."), E::DisconnectFailed);
        (void)event(E::ApplyFailed);
        return Result::failure(std::move(error));
    }
    Result apply(const ApplyConfiguration& request, Reconfiguration* reconfiguration,
                 std::optional<CameraCommand>& deferredPriority) {
        auto valid = generation(request.sessionGeneration);
        if (!valid.hasValue()) return valid;
        valid = event(E::ApplyRequested);
        if (!valid.hasValue()) return valid;
        if (request.requestRevision == 0U || request.requestRevision <= status.requestedRevision)
            return rejected("stale_configuration_revision", "Configuration revisions must increase.");
        status.requestedConfiguration = request.configuration;
        status.requestedRevision = request.requestRevision;
        valid = camera::validateCameraConfiguration(request.configuration, *status.capabilities);
        if (valid.hasValue()) valid = reconfiguration
            ? validateMode(request.configuration, &reconfiguration->mode, *reconfiguration->context->rawPool)
            : validateMode(request.configuration);
        if (!valid.hasValue()) { (void)event(E::ApplyFailed); return valid; }
        if (reconfiguration && status.sessionGeneration == std::numeric_limits<std::uint64_t>::max())
            return rejected("camera_generation_exhausted", "Camera generation cannot advance.");
        deferredPriority = commands.tryPopPriority();
        if (deferredPriority || stopping()) return cancelled();
        const auto previous = status.appliedConfiguration;
        auto result = device->applyConfiguration(request.configuration);
        if (stopping()) return cancelled();
        if (!result.hasValue()) {
            if (reconfiguration) return restoreAfterReconfigurationFailure(result.error(), previous);
            if (result.error().category != ErrorCategory::CameraConfiguration)
                return failAndCleanup(result.error(), E::DisconnectFailed);
            (void)event(E::ApplyFailed);
            return Result::failure(result.error());
        }
        valid = camera::validateCameraConfiguration(result.value().actual, *status.capabilities);
        if (valid.hasValue()) valid = reconfiguration
            ? validateMode(result.value().actual, &reconfiguration->mode, *reconfiguration->context->rawPool)
            : validateMode(result.value().actual);
        if (valid.hasValue() && !isSupportedLiveCameraConfiguration(result.value().actual))
            valid = rejected("camera_actual_fps_invalid", "Camera readback must include positive finite actual FPS and continuous acquisition.");
        if (!valid.hasValue()) {
            if (reconfiguration) return restoreAfterReconfigurationFailure(valid.error(), previous);
            return failAndCleanup(valid.error(), E::DisconnectFailed);
        }
        if (reconfiguration) {
            // Keep the old device and frame-ID history. Ownership of its new raw
            // destination remains typed and lives until the camera worker joins.
            boundContext = reconfiguration->context;
            rawPool = boundContext->rawPool.get();
            rawSlot = &boundContext->rawSlot;
            fixedMode = reconfiguration->mode;
            ++status.sessionGeneration;
        } else if (!fixedMode) fixedMode = result.value().actual;
        status.appliedConfiguration = std::move(result.value());
        status.appliedRevision = request.requestRevision;
        status.confirmedRevision.reset();
        status.restoreEligible = false;
        reconfigurationRequiresApply = false;
        return event(E::ApplySucceeded);
    }
    Result confirm(const ConfirmConfiguration& request) {
        auto valid = generation(request.sessionGeneration);
        if (!valid.hasValue()) { return valid; }
        valid = event(E::ConfirmRequested);
        if (!valid.hasValue()) { return valid; }
        if (reconfigurationRequiresApply || !status.appliedConfiguration || request.appliedRequestRevision != status.appliedRevision) {
            return rejected("configuration_not_applied", "Confirm the current successfully applied configuration.");
        }
        status.confirmedRevision = request.appliedRequestRevision;
        status.restoreEligible = true;
        return Result::success();
    }
    Result startStream(const StartStream& request, std::optional<CameraCommand>& deferredPriority) {
        auto valid = generation(request.sessionGeneration);
        if (!valid.hasValue()) { return valid; }
        valid = event(E::StartRequested);
        if (!valid.hasValue()) { return valid; }
        if (!status.appliedConfiguration || !status.confirmedRevision
            || *status.confirmedRevision != status.appliedRevision
            || request.confirmedAppliedRequestRevision != status.appliedRevision) {
            return rejected("configuration_not_confirmed", "Review and confirm actual settings before Start.");
        }
        if (machine.state() == S::Streaming) { return Result::success(); }
        valid = validateMode(status.appliedConfiguration->actual);
        if (!valid.hasValue()) { return valid; }
        // Final linearization check before beginning the operation. Ordinary
        // work stays in FIFO position, even at the batch boundary.
        deferredPriority = commands.tryPopPriority();
        if (deferredPriority) { return cancelled(); }
        if (stopping()) { return cancelled(); }
        status.desiredStreaming = true;
        auto startedStream = device->startStream();
        if (!startedStream.hasValue()) { return failAndCleanup(startedStream.error(), E::StartFailed); }
        if (stopping()) { return cancelled(); }
        acquisitionProgressAt = clock.steadyNow();
        return event(E::StartSucceeded);
    }
    Result stopStream() {
        status.desiredStreaming = false;
        auto requested = event(E::StopRequested);
        if (!requested.hasValue()) { return requested; }
        if (machine.state() != S::Streaming) { return Result::success(); }
        auto stopped = device->stopStream();
        if (!stopped.hasValue()) { return failAndCleanup(stopped.error(), E::StopFailed); }
        acquisitionProgressAt.reset();
        return event(E::StopSucceeded);
    }
    Result disconnect() {
        status.desiredIdentity.reset();
        status.desiredStreaming = false;
        status.requestedConfiguration.reset();
        status.requestedRevision = 0U;
        (void)event(E::DisconnectRequested);
        if (auto error = cleanup()) {
            (void)event(E::DisconnectFailed);
            return Result::failure(std::move(*error));
        }
        return event(E::DisconnectSucceeded);
    }
    Result dispatch(const CameraCommand& command, std::optional<CameraCommand>& deferredPriority, Reconfiguration* reconfiguration) {
        return std::visit([&](const auto& request) -> Result {
            using T = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<T, Discover>) { return discover(); }
            else if constexpr (std::is_same_v<T, Connect>) { return connect(request.cameraId, false); }
            else if constexpr (std::is_same_v<T, ApplyConfiguration>) { return apply(request, reconfiguration, deferredPriority); }
            else if constexpr (std::is_same_v<T, ConfirmConfiguration>) { return confirm(request); }
            else if constexpr (std::is_same_v<T, StartStream>) { return startStream(request, deferredPriority); }
            else if constexpr (std::is_same_v<T, StopStream>) { return stopStream(); }
            else if constexpr (std::is_same_v<T, Disconnect>) { return disconnect(); }
            else if constexpr (std::is_same_v<T, Retry>) {
                if (!status.desiredIdentity) { return rejected("camera_identity_required", "Connect a selected camera before Retry."); }
                const auto id = *status.desiredIdentity;
                return connect(id, true);
            } else {
                cancellation.request_stop();
                auto cleanupError = cleanup();
                (void)event(E::ShutdownRequested);
                if (cleanupError) { return Result::failure(std::move(*cleanupError)); }
                return Result::success();
            }
        }, command.payload);
    }
    void execute(CameraCommand command) {
        // Apply and Start can defer one priority command. That priority payload
        // cannot defer another, so completion requires at most two iterations.
        // Publish the interrupted operation first, then complete priority cleanup
        // even if cancellation has arrived. Its result must remain the newest.
        for (std::size_t executed = 0U; executed < 2U; ++executed) {
            std::optional<CameraCommand> deferredPriority;
            std::optional<Reconfiguration> reconfiguration;
            std::optional<std::uint64_t> cancelledAttachment;
            {
                std::lock_guard lock(reconfigurationMutex);
                if (staged && staged->requestId == command.requestId) {
                    reconfiguration = std::move(staged);
                    staged.reset();
                } else if (staged && (std::holds_alternative<StopStream>(command.payload)
                    || std::holds_alternative<Disconnect>(command.payload)
                    || std::holds_alternative<Shutdown>(command.payload))) {
                    cancelledAttachment = staged->requestId;
                    cancelledReconfigurationRequest = staged->requestId;
                    staged.reset();
                }
            }
            auto result = Result::success();
            const auto previousGeneration = status.sessionGeneration;
            try {
                if (cancelledReconfigurationRequest == command.requestId) {
                    cancelledReconfigurationRequest.reset();
                    // Stop already completed this attached Apply through the
                    // retained completion. Draining its leftover queue entry
                    // must not overwrite the newer lifecycle outcome.
                    return;
                } else result = dispatch(command, deferredPriority, reconfiguration ? &*reconfiguration : nullptr);
            }
            catch (const std::exception& exception) {
                result = failAndCleanup(failure(ErrorCategory::Internal, "camera_worker_exception", exception.what()), E::DisconnectFailed);
            } catch (...) {
                result = failAndCleanup(failure(ErrorCategory::Internal, "camera_worker_exception", "Unknown camera worker exception."), E::DisconnectFailed);
            }
            commands.completeBarrier(command.requestId);
            status.latestOutcome = CameraCommandOutcome{command.requestId,
                result.hasValue() ? std::nullopt : std::optional{result.error()}};
            if (!result.hasValue()) { status.latestError = result.error(); }
            status.mailboxStats = commands.stats();
            auto published = std::make_shared<const CameraStatusSnapshot>(status);
            if (reconfiguration || cancelledAttachment) {
                std::lock_guard lock(reconfigurationMutex);
                completion = CameraReconfigurationCompletion{
                    cancelledAttachment ? CameraCommandOutcome{*cancelledAttachment, cancelled().error()}
                                        : *status.latestOutcome,
                    status.sessionGeneration != previousGeneration, 0U, published};
                completion->publicationRevision = statusSlot.publish(std::move(published)).revision;
            } else (void)statusSlot.publish(std::move(published));
            if (!deferredPriority) { return; }
            command = std::move(*deferredPriority);
        }
    }
    bool validFrame(const std::shared_ptr<const core::RawFrame>& frame) const {
        if (!frame || !status.appliedConfiguration) { return false; }
        const auto& configuration = status.appliedConfiguration->actual;
        const auto& settings = frame->metadata.acquisitionSettings;
        return sameFormat(settings.sourceFormat, configuration.pixelFormat)
            && sameRoi(settings.roi, configuration.roi)
            && frame->layout.width() == configuration.roi.width
            && frame->layout.height() == configuration.roi.height
            && frame->layout.storage() == configuration.pixelFormat.applicationStorage
            && std::isfinite(settings.actualFps) && settings.actualFps > 0.0
            && (!lastFrameId || frame->frameId > *lastFrameId);
    }
    bool acquisitionStalled() const noexcept {
        if (!acquisitionProgressAt || !status.appliedConfiguration) { return false; }
        const auto now = clock.steadyNow();
        if (now < *acquisitionProgressAt) { return false; }
        using Duration = std::chrono::steady_clock::duration;
        using UnsignedTicks = std::make_unsigned_t<Duration::rep>;
        // Subtract in unsigned ticks before converting: opposite extreme epochs
        // cannot overflow, and nearby epochs retain precision on MSVC as well.
        const auto elapsedTicks = static_cast<UnsignedTicks>(now.time_since_epoch().count())
            - static_cast<UnsignedTicks>(acquisitionProgressAt->time_since_epoch().count());
        constexpr auto minimumGrace = std::chrono::duration_cast<Duration>(std::chrono::milliseconds{750});
        if (elapsedTicks < static_cast<UnsignedTicks>(minimumGrace.count())) { return false; }
        const auto elapsed = std::chrono::duration<UnsignedTicks, Duration::period>{elapsedTicks};
        // Actual FPS was validated at Apply. Avoid reciprocal duration conversion
        // so even tiny positive rates keep their full three-frame grace.
        return std::chrono::duration<long double>{elapsed}.count()
            * *status.appliedConfiguration->actual.requestedFps >= 3.0L;
    }
    void retrievalFailure(core::Error error) {
        status.latestError = error;
        if (error.category == ErrorCategory::Cancelled && stopping()) { return; }
        if (error.category == ErrorCategory::ResourceExhaustion) {
            increment(status.acquisitionCounters.droppedNoRawBuffer);
        } else if (error.category == ErrorCategory::InvalidFrame) {
            increment(status.acquisitionCounters.droppedInvalidFrame);
        } else if (isAcquisitionTimeout(error)) {
            increment(status.acquisitionCounters.timeouts);
            increment(status.consecutiveTimeouts);
            if (acquisitionStalled()) {
                auto result = failAndCleanup(std::move(error), E::TimeoutThresholdReached);
                status.latestError = result.error();
            }
        } else {
            const bool removed = error.category == ErrorCategory::CameraConnection && error.recoverable;
            auto result = failAndCleanup(std::move(error), removed ? E::DeviceRemoved : E::DisconnectFailed);
            status.latestError = result.error();
        }
    }
    void retrieve() {
        try {
            constexpr auto pollingBudget = std::chrono::milliseconds{250};
            const auto sliceDeadline = std::chrono::steady_clock::now() + pollingBudget;
            auto result = device->retrieve(pollingBudget, *rawPool, cancellation.get_token());
            if (stopping()) { return; }
            if (!result.hasValue() && isAcquisitionTimeout(result.error())) {
                // A quick timeout consumes only the remainder of this real I/O
                // slice. Policy time stays on the injected clock, and Shutdown
                // interrupts padding before any timeout classification.
                (void)core::SystemClock{}.waitUntil(sliceDeadline, cancellation.get_token());
                if (stopping()) { return; }
            }
            if (!result.hasValue()) { retrievalFailure(result.error()); }
            else if (!validFrame(result.value())) {
                result.value().reset();
                retrievalFailure(failure(ErrorCategory::InvalidFrame, "invalid_frame", "Discarded a frame incompatible with the applied mode."));
            } else {
                if (stopping()) { return; }
                lastFrameId = result.value()->frameId;
                status.consecutiveTimeouts = 0U;
                status.lastAcquiredAt = clock.steadyNow();
                acquisitionProgressAt = status.lastAcquiredAt;
                increment(status.acquisitionCounters.acquired);
                if (rawSlot->publish(std::move(result.value())).replacedUnconsumed) {
                    increment(status.acquisitionCounters.droppedBeforeProcessing);
                }
            }
        } catch (const std::exception& exception) {
            retrievalFailure(failure(ErrorCategory::Internal, "camera_worker_exception", exception.what()));
        } catch (...) {
            retrievalFailure(failure(ErrorCategory::Internal, "camera_worker_exception", "Unknown camera worker exception."));
        }
        publish();
    }
    void run() noexcept {
        try {
            publish();
            while (!stopping()) {
                if (machine.state() != S::Streaming) {
                    auto command = commands.waitPop(cancellation.get_token());
                    if (!command) { break; }
                    if (stopping() && !std::holds_alternative<Shutdown>(command->payload)) { break; }
                    execute(std::move(*command));
                    continue;
                }
                for (std::size_t count = 0U; count < 32U && !stopping(); ++count) {
                    auto command = commands.tryPop();
                    if (!command) { break; }
                    execute(std::move(*command));
                }
                if (stopping()) { break; }
                if (auto priority = commands.tryPopPriority()) { execute(std::move(*priority)); continue; }
                if (!stopping() && machine.state() == S::Streaming) { retrieve(); }
            }
        } catch (...) {
            try {
                status.latestError = failure(ErrorCategory::Internal, "camera_worker_exception", "Camera worker could not continue.");
            } catch (...) {}
        }
        // A successfully posted Shutdown cancels active I/O immediately. It
        // remains in the sealed mailbox so cleanup can report its own outcome.
        if (auto command = commands.tryPopPriority(); command && std::holds_alternative<Shutdown>(command->payload)) {
            try { execute(std::move(*command)); } catch (...) { /* Cleanup below still runs. */ }
        }
        // Device ownership never leaves this thread, including all error exits.
        if (auto error = cleanup()) { status.latestError = std::move(*error); }
        (void)event(E::ShutdownRequested);
        commands.close();
        { std::lock_guard lock(reconfigurationMutex); staged.reset(); }
        try { publish(); } catch (...) { /* No allocation can be guaranteed during exhaustion. */ }
    }
};

AcquisitionWorker::AcquisitionWorker(camera::ICameraProvider& provider, CameraCommandMailbox& commands,
    core::BufferPool& pool, core::LatestValueSlot<core::RawFrame>& raw, core::IClock& clock,
    core::LatestValueSlot<CameraStatusSnapshot>& status, CameraStatusSnapshot initial,
    std::optional<camera::CameraConfiguration> preparedMode)
    : impl_(std::make_unique<Impl>(provider, commands, pool, raw, clock, status, std::move(initial))) {
    impl_->fixedMode = std::move(preparedMode);
}
AcquisitionWorker::~AcquisitionWorker() { requestStop(); join(); }
Result AcquisitionWorker::start() {
    if (impl_->started) { return rejected("camera_worker_already_started", "Camera worker can only start once."); }
    auto machine = CameraSessionStateMachine::fromInitialState(impl_->status.state);
    if (!machine.hasValue()) { return Result::failure(machine.error()); }
    const auto& status = impl_->status;
    if (status.actualIdentity || status.capabilities || status.appliedConfiguration || status.appliedRevision != 0U
        || status.confirmedRevision || status.restoreEligible || status.desiredStreaming
        || status.sourceReplacementRequired || status.requestedRevision != 0U
        || (status.desiredIdentity && status.desiredIdentity->value.empty())) {
        return rejected("invalid_initial_camera_status", "Initial camera status must be device-free and unconfirmed.");
    }
    if (impl_->stopping()) { return cancelled(); }
    impl_->machine = std::move(machine.value());
    try { impl_->thread = std::jthread([impl = impl_.get()] { impl->run(); }); }
    catch (const std::exception& exception) {
        return Result::failure(failure(ErrorCategory::Internal, "camera_worker_start_failed", exception.what()));
    }
    impl_->started = true;
    return Result::success();
}
Result AcquisitionWorker::post(CameraCommand command) {
    const bool shutdown = std::holds_alternative<Shutdown>(command.payload);
    std::lock_guard lock(impl_->reconfigurationMutex);
    if (std::holds_alternative<ApplyConfiguration>(command.payload) && impl_->reconfigurationPending)
        return rejected("camera_reconfiguration_busy", "A prepared Apply is already outstanding.");
    auto result = impl_->commands.post(std::move(command));
    if (shutdown && result.hasValue()) { impl_->cancellation.request_stop(); }
    return result;
}
Result AcquisitionWorker::postReconfiguration(CameraCommand command,
    std::shared_ptr<LiveSessionContext> context, camera::CameraConfiguration mode) {
    const auto* apply = std::get_if<ApplyConfiguration>(&command.payload);
    if (!apply || !context || !context->rawPool || !sameMode(apply->configuration, mode)
        || apply->sessionGeneration == std::numeric_limits<std::uint64_t>::max()
        || context->generation != apply->sessionGeneration + 1U)
        return rejected("invalid_reconfiguration_attachment", "A matching prepared Apply context is required.");
    std::lock_guard lock(impl_->reconfigurationMutex);
    if (impl_->reconfigurationPending)
        return rejected("camera_reconfiguration_busy", "A prepared Apply is already outstanding.");
    impl_->staged.emplace(Impl::Reconfiguration{command.requestId, std::move(context), std::move(mode)});
    auto result = impl_->commands.post(std::move(command));
    if (!result.hasValue()) impl_->staged.reset();
    else impl_->reconfigurationPending = true;
    return result;
}
std::optional<CameraReconfigurationCompletion> AcquisitionWorker::takeReconfigurationCompletion() {
    std::lock_guard lock(impl_->reconfigurationMutex);
    if (impl_->completion) impl_->reconfigurationPending = false;
    return std::exchange(impl_->completion, std::nullopt);
}
void AcquisitionWorker::requestStop() noexcept { impl_->cancellation.request_stop(); impl_->commands.close(); }
void AcquisitionWorker::join() noexcept { if (impl_->thread.joinable()) { impl_->thread.join(); } }

}  // namespace lumora::application
