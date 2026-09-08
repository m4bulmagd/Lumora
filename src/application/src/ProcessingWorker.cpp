#include <lumora/application/ProcessingWorker.hpp>

#include <lumora/core/Error.hpp>

#include "ProcessingWorkerCounters.hpp"

#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace lumora::application {

static_assert(std::is_nothrow_move_constructible_v<core::Error>);

struct ProcessingWorker::Impl final {
    Impl(core::LatestValueSlot<core::RawFrame>& rawSlotValue,
         core::LatestValueSlot<core::FrameBundle>& bundleSlotValue,
         processing::IFrameProcessor& processorValue)
        : rawSlot(&rawSlotValue),
          bundleSlot(&bundleSlotValue),
          processor(&processorValue),
          standardExceptionFailure({
              core::ErrorCategory::Internal,
              "processing_worker_exception",
              "Frame processing stopped unexpectedly.",
              "The processing worker caught a standard exception at its thread boundary.",
              false,
          }),
          unknownExceptionFailure({
              core::ErrorCategory::Internal,
              "processing_worker_unknown_exception",
              "Frame processing stopped unexpectedly.",
              "The processing worker caught an unknown exception at its thread boundary.",
              false,
          }) {}

    core::LatestValueSlot<core::RawFrame>* rawSlot;
    core::LatestValueSlot<core::FrameBundle>* bundleSlot;
    processing::IFrameProcessor* processor;
    std::stop_source cancellation;
    std::jthread thread;
    bool started{false};
    mutable std::mutex snapshotMutex;
    ProcessingWorkerSnapshot snapshot;
    core::Error standardExceptionFailure;
    core::Error unknownExceptionFailure;

    void recordBoundaryFailure(core::Error&& error) noexcept {
        try {
            std::lock_guard lock(snapshotMutex);
            detail::saturatingIncrement(snapshot.processingErrors);
            snapshot.currentError.emplace(std::move(error));
        } catch (...) {
            // The thread boundary remains contained if reporting itself fails.
        }
    }

    void recordFailure(const core::Error& error) {
        const bool displayPoolExhausted =
            error.category == core::ErrorCategory::ResourceExhaustion &&
            error.code == processing::displayBufferPoolExhaustedCode;
        core::Error retainedError(error);
        std::lock_guard lock(snapshotMutex);
        if (displayPoolExhausted) {
            detail::saturatingIncrement(snapshot.displayPoolExhaustions);
        } else {
            detail::saturatingIncrement(snapshot.processingErrors);
        }
        snapshot.currentError.emplace(std::move(retainedError));
    }

    void recordPublication(bool replaced) {
        std::lock_guard lock(snapshotMutex);
        if (replaced) {
            detail::saturatingIncrement(snapshot.bundlesReplaced);
        }
        snapshot.currentError.reset();
    }

    void run() {
        std::uint64_t rawRevision = 0U;
        std::uint64_t bundleRevision = 0U;
        const auto token = cancellation.get_token();
        while (!token.stop_requested()) {
            auto raw = rawSlot->waitForNewer(rawRevision, token);
            if (token.stop_requested() || rawSlot->closed() ||
                bundleSlot->closed() || !raw) {
                break;
            }
            {
                std::lock_guard lock(snapshotMutex);
                detail::saturatingAdd(
                    snapshot.rawFramesSkipped,
                    raw->revision - rawRevision - 1U);
            }
            rawRevision = raw->revision;
            auto bundle = processor->process(raw->value);
            auto processorStatus=processor->status();
            { std::lock_guard lock(snapshotMutex); snapshot.processorStatus=std::move(processorStatus); }
            if (token.stop_requested()) {
                break;
            }
            if (!bundle.hasValue()) {
                recordFailure(bundle.error());
                continue;
            }
            if (!bundle.value() ||
                bundle.value()->raw != raw->value ||
                bundle.value()->sourceFrameId() != raw->value->frameId) {
                recordFailure({
                    core::ErrorCategory::Processing,
                    "processing_result_invalid",
                    "The processed frame could not be published.",
                    "The processor returned a null bundle or a bundle for a different source frame.",
                    false,
                });
                continue;
            }
            if (bundleSlot->closed()) {
                break;
            }
            const auto publication = bundleSlot->publish(std::move(bundle).value());
            if (publication.revision > bundleRevision) {
                bundleRevision = publication.revision;
                recordPublication(publication.replacedUnconsumed);
            } else {
                break;
            }
        }
    }

    void runBoundary() noexcept {
        try {
            run();
        } catch (const std::exception&) {
            recordBoundaryFailure(std::move(standardExceptionFailure));
        } catch (...) {
            recordBoundaryFailure(std::move(unknownExceptionFailure));
        }
    }
};

ProcessingWorker::ProcessingWorker(
    core::LatestValueSlot<core::RawFrame>& rawSlot,
    core::LatestValueSlot<core::FrameBundle>& bundleSlot,
    processing::IFrameProcessor& processor)
    : impl_(std::make_unique<Impl>(rawSlot, bundleSlot, processor)) {}

ProcessingWorker::~ProcessingWorker() {
    requestStop();
    join();
}

core::Result<void> ProcessingWorker::start() {
    if (impl_->started) {
        return core::Result<void>::failure({
            core::ErrorCategory::Internal,
            "processing_worker_already_started",
            "The processing worker could not start.",
            "ProcessingWorker::start may only be called once.",
            false,
        });
    }
    if (impl_->cancellation.stop_requested()) {
        return core::Result<void>::failure({
            core::ErrorCategory::Cancelled,
            "processing_worker_cancelled",
            "The processing worker did not start.",
            "Cancellation was requested before ProcessingWorker::start.",
            false,
        });
    }
    try {
        impl_->thread = std::jthread([impl = impl_.get()] { impl->runBoundary(); });
    } catch (const std::exception& exception) {
        return core::Result<void>::failure({
            core::ErrorCategory::Internal,
            "processing_worker_start_failed",
            "The processing worker could not start.",
            exception.what(),
            true,
        });
    }
    impl_->started = true;
    return core::Result<void>::success();
}

void ProcessingWorker::requestStop() noexcept {
    impl_->cancellation.request_stop();
}

void ProcessingWorker::join() noexcept {
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

ProcessingWorkerSnapshot ProcessingWorker::snapshot() const {
    std::lock_guard lock(impl_->snapshotMutex);
    return impl_->snapshot;
}

}  // namespace lumora::application
