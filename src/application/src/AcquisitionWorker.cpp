#include <lumora/application/AcquisitionWorker.hpp>

#include <lumora/application/CameraSessionStateMachine.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <algorithm>
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
bool isMono8(const core::SourcePixelFormat& format) {
    return sameFormat(format, {"Mono8", 0x01080001U, 8U, 255U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant, core::StorageType::UInt8});
}
}  // namespace

struct AcquisitionWorker::Impl final {
    camera::ICameraProvider& provider;
    CameraCommandMailbox& commands;
    core::BufferPool& rawPool;
    core::LatestValueSlot<core::RawFrame>& rawSlot;
    core::IClock& clock;
    core::LatestValueSlot<CameraStatusSnapshot>& statusSlot;
    CameraStatusSnapshot status;
    CameraSessionStateMachine machine;
    std::unique_ptr<camera::ICameraDevice> device;
    std::optional<camera::CameraConfiguration> fixedMode;
    std::optional<std::uint64_t> lastFrameId;
    std::stop_source cancellation;
    std::jthread thread;
    bool started{false};

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
    Result validateMode(const camera::CameraConfiguration& configuration) const {
        if (!isMono8(configuration.pixelFormat)) {
            return rejected("camera_format_not_available", "This pipeline requires full-range Mono8.");
        }
        if (fixedMode && !sameMode(configuration, *fixedMode)) {
            return rejected("camera_mode_change_requires_rebinding", "Changing the source mode requires new pipeline resources.");
        }
        const auto layout = core::ImageLayout::create(configuration.roi.width, configuration.roi.height,
            configuration.roi.width, core::StorageType::UInt8, rawPool.stats().bytesPerBuffer);
        if (!layout.hasValue()) {
            return rejected("camera_raw_pool_layout", "The selected mode does not fit the raw buffer pool.");
        }
        return Result::success();
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
        if (std::none_of(capabilities.value().pixelFormats.begin(), capabilities.value().pixelFormats.end(), isMono8)) {
            return failAndCleanup(failure(ErrorCategory::CameraConfiguration, "camera_format_not_available",
                "The camera does not support this pipeline's Mono8 format."), E::CapabilitiesFailed);
        }
        status.capabilities = std::move(capabilities.value());
        status.actualIdentity = id;
        status.consecutiveTimeouts = 0U;
        return event(E::OpenSucceeded);
    }
    Result apply(const ApplyConfiguration& request) {
        auto valid = generation(request.sessionGeneration);
        if (!valid.hasValue()) { return valid; }
        valid = event(E::ApplyRequested);
        if (!valid.hasValue()) { return valid; }
        if (request.requestRevision == 0U || request.requestRevision <= status.requestedRevision) {
            return rejected("stale_configuration_revision", "Configuration revisions must increase.");
        }
        status.requestedConfiguration = request.configuration;
        status.requestedRevision = request.requestRevision;
        valid = camera::validateCameraConfiguration(request.configuration, *status.capabilities);
        if (valid.hasValue()) { valid = validateMode(request.configuration); }
        if (!valid.hasValue()) { (void)event(E::ApplyFailed); return valid; }
        auto result = device->applyConfiguration(request.configuration);
        if (stopping()) { return cancelled(); }
        if (!result.hasValue()) {
            if (result.error().category != ErrorCategory::CameraConfiguration) {
                return failAndCleanup(result.error(), E::DisconnectFailed);
            }
            (void)event(E::ApplyFailed);
            return Result::failure(result.error());
        }
        valid = camera::validateCameraConfiguration(result.value().actual, *status.capabilities);
        if (valid.hasValue()) { valid = validateMode(result.value().actual); }
        if (valid.hasValue() && (!result.value().actual.requestedFps
            || !std::isfinite(*result.value().actual.requestedFps) || *result.value().actual.requestedFps <= 0.0)) {
            valid = rejected("camera_actual_fps_invalid", "Camera readback must include positive finite actual FPS.");
        }
        if (!valid.hasValue()) { return failAndCleanup(valid.error(), E::DisconnectFailed); }
        // Composition chooses the first accepted mode and matching downstream
        // pools. Byte capacity alone cannot identify a width, height or ROI.
        if (!fixedMode) { fixedMode = result.value().actual; }
        status.appliedConfiguration = std::move(result.value());
        status.appliedRevision = request.requestRevision;
        status.confirmedRevision.reset();
        status.restoreEligible = false;
        return event(E::ApplySucceeded);
    }
    Result confirm(const ConfirmConfiguration& request) {
        auto valid = generation(request.sessionGeneration);
        if (!valid.hasValue()) { return valid; }
        valid = event(E::ConfirmRequested);
        if (!valid.hasValue()) { return valid; }
        if (!status.appliedConfiguration || request.appliedRequestRevision != status.appliedRevision) {
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
        return event(E::StartSucceeded);
    }
    Result stopStream() {
        status.desiredStreaming = false;
        auto requested = event(E::StopRequested);
        if (!requested.hasValue()) { return requested; }
        if (machine.state() != S::Streaming) { return Result::success(); }
        auto stopped = device->stopStream();
        if (!stopped.hasValue()) { return failAndCleanup(stopped.error(), E::StopFailed); }
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
    Result dispatch(const CameraCommand& command, std::optional<CameraCommand>& deferredPriority) {
        return std::visit([&](const auto& request) -> Result {
            using T = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<T, Discover>) { return discover(); }
            else if constexpr (std::is_same_v<T, Connect>) { return connect(request.cameraId, false); }
            else if constexpr (std::is_same_v<T, ApplyConfiguration>) { return apply(request); }
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
        // Only Start can defer one priority command. That priority payload
        // cannot defer another, so completion requires at most two iterations.
        // Publish the interrupted Start first, then complete priority cleanup
        // even if cancellation has arrived. Its result must remain the newest.
        for (std::size_t executed = 0U; executed < 2U; ++executed) {
            std::optional<CameraCommand> deferredPriority;
            auto result = Result::success();
            try { result = dispatch(command, deferredPriority); }
            catch (const std::exception& exception) {
                result = failAndCleanup(failure(ErrorCategory::Internal, "camera_worker_exception", exception.what()), E::DisconnectFailed);
            } catch (...) {
                result = failAndCleanup(failure(ErrorCategory::Internal, "camera_worker_exception", "Unknown camera worker exception."), E::DisconnectFailed);
            }
            commands.completeBarrier(command.requestId);
            status.latestOutcome = CameraCommandOutcome{command.requestId,
                result.hasValue() ? std::nullopt : std::optional{result.error()}};
            if (!result.hasValue()) { status.latestError = result.error(); }
            publish();
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
    void retrievalFailure(core::Error error) {
        status.latestError = error;
        if (error.category == ErrorCategory::Cancelled && stopping()) { return; }
        if (error.category == ErrorCategory::ResourceExhaustion) {
            increment(status.acquisitionCounters.droppedNoRawBuffer);
        } else if (error.category == ErrorCategory::InvalidFrame) {
            increment(status.acquisitionCounters.droppedInvalidFrame);
        } else if (error.category == ErrorCategory::Acquisition && error.code == "acquisition_timeout") {
            increment(status.acquisitionCounters.timeouts);
            increment(status.consecutiveTimeouts);
            if (status.consecutiveTimeouts >= 3U) {
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
            auto result = device->retrieve(std::chrono::milliseconds{250}, rawPool, cancellation.get_token());
            if (stopping()) { return; }
            if (!result.hasValue()) { retrievalFailure(result.error()); }
            else if (!validFrame(result.value())) {
                result.value().reset();
                retrievalFailure(failure(ErrorCategory::InvalidFrame, "invalid_frame", "Discarded a frame incompatible with the applied mode."));
            } else {
                if (stopping()) { return; }
                lastFrameId = result.value()->frameId;
                status.consecutiveTimeouts = 0U;
                status.lastAcquiredAt = clock.steadyNow();
                increment(status.acquisitionCounters.acquired);
                if (rawSlot.publish(std::move(result.value())).replacedUnconsumed) {
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
        try { publish(); } catch (...) { /* No allocation can be guaranteed during exhaustion. */ }
    }
};

AcquisitionWorker::AcquisitionWorker(camera::ICameraProvider& provider, CameraCommandMailbox& commands,
    core::BufferPool& pool, core::LatestValueSlot<core::RawFrame>& raw, core::IClock& clock,
    core::LatestValueSlot<CameraStatusSnapshot>& status, CameraStatusSnapshot initial)
    : impl_(std::make_unique<Impl>(provider, commands, pool, raw, clock, status, std::move(initial))) {}
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
    auto result = impl_->commands.post(std::move(command));
    if (shutdown && result.hasValue()) { impl_->cancellation.request_stop(); }
    return result;
}
void AcquisitionWorker::requestStop() noexcept { impl_->cancellation.request_stop(); impl_->commands.close(); }
void AcquisitionWorker::join() noexcept { if (impl_->thread.joinable()) { impl_->thread.join(); } }

}  // namespace lumora::application
