#include <lumora/application/ProcessingWorker.hpp>

#include <lumora/core/BufferPool.hpp>

#include "ProcessingWorkerCounters.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace lumora::application {
namespace {
using namespace std::chrono_literals;

// Wrapping event counters corrupt bounded lifetime diagnostics at the limit.
TEST(ProcessingWorker, CounterArithmeticSaturatesZeroOrdinaryAndBoundaries) {
    std::uint64_t value = 0U;
    detail::saturatingAdd(value, 0U);
    EXPECT_EQ(value, 0U);
    detail::saturatingIncrement(value);
    EXPECT_EQ(value, 1U);
    detail::saturatingAdd(value, 41U);
    EXPECT_EQ(value, 42U);

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    detail::saturatingAdd(value, maximum - 42U);
    EXPECT_EQ(value, maximum);
    detail::saturatingIncrement(value);
    EXPECT_EQ(value, maximum);

    value = maximum - 2U;
    detail::saturatingAdd(value, 3U);
    EXPECT_EQ(value, maximum);
}

core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
            core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
}

core::FrameMetadata metadata() {
    auto settings = core::AcquisitionSettingsSnapshot::create(
        {"Test", "Camera", "1", "virtual", std::nullopt}, mono8(),
        {0U, 0U, 2U, 2U}, 30.0, 30.0, std::nullopt, std::nullopt);
    return {{}, std::chrono::steady_clock::time_point{},
            std::chrono::system_clock::time_point{}, {},
            std::move(settings).value()};
}

std::shared_ptr<const core::FrameBundle> makeBundle(
    core::BufferPool& rawPool,
    core::BufferPool& displayPool,
    std::uint64_t id) {
    auto rawLease = rawPool.tryAcquire();
    auto displayLease = displayPool.tryAcquire();
    EXPECT_TRUE(rawLease);
    EXPECT_TRUE(displayLease);
    auto layout = core::ImageLayout::create(
        2U, 2U, 2U, core::StorageType::UInt8, 4U).value();
    auto raw = core::RawFrame::create(
        id, layout, std::move(*rawLease).seal(), metadata()).value();
    auto display = core::DisplayFrame::create(
        id, layout, std::move(*displayLease).seal(), core::DisplayStorage::Gray8,
        {0U, 255U, 255U, 1U}, {false, false, core::Rotation::Degrees0}).value();
    return core::FrameBundle::create(
        std::move(raw), std::move(display), nullptr, nullptr).value();
}

class BlockingProcessor final : public processing::IFrameProcessor {
public:
    explicit BlockingProcessor(core::BufferPool& displayPool)
        : displayPool_(&displayPool) {}

    core::Result<std::shared_ptr<const core::FrameBundle>> process(
        std::shared_ptr<const core::RawFrame> raw) override {
        std::size_t call;
        {
            std::unique_lock lock(mutex_);
            inputs_.push_back(raw->frameId);
            call = inputs_.size();
            changed_.notify_all();
            changed_.wait(lock, [&] { return releaseAll_ || released_ >= call; });
            if (throwStandard_) {
                throwStandard_ = false;
                throw std::runtime_error("scripted processor exception");
            }
            if (throwUnknown_) {
                throwUnknown_ = false;
                throw 17;
            }
            if (nextFailure_) {
                auto error = std::move(*nextFailure_);
                nextFailure_.reset();
                return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
                    std::move(error));
            }
            if (hasBundleOverride_) {
                hasBundleOverride_ = false;
                return core::Result<std::shared_ptr<const core::FrameBundle>>::success(
                    std::move(bundleOverride_));
            }
        }
        auto lease = displayPool_->tryAcquire();
        if (!lease) {
            return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
                {core::ErrorCategory::ResourceExhaustion, "buffer_pool_exhausted",
                 "Display memory is unavailable.", "Test display pool is exhausted.", true});
        }
        auto layout = core::ImageLayout::create(
            2U, 2U, 2U, core::StorageType::UInt8, 4U).value();
        auto display = core::DisplayFrame::create(
            raw->frameId, layout, std::move(*lease).seal(), core::DisplayStorage::Gray8,
            {0U, 255U, 255U, 1U}, {false, false, core::Rotation::Degrees0});
        return core::FrameBundle::create(raw, std::move(display).value(), nullptr, nullptr);
    }

    processing::ProcessorStatus status() const noexcept override {
        std::lock_guard lock(mutex_); return status_;
    }
    void setStatus(processing::ProcessorStatus status) { std::lock_guard lock(mutex_); status_=std::move(status); }
    bool waitForCall(std::size_t call) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 2s, [&] { return inputs_.size() >= call; });
    }

    std::uint64_t inputId(std::size_t call) {
        std::lock_guard lock(mutex_);
        return inputs_.at(call - 1U);
    }

    std::size_t callCount() {
        std::lock_guard lock(mutex_);
        return inputs_.size();
    }

    void releaseCall(std::size_t call) {
        std::lock_guard lock(mutex_);
        released_ = std::max(released_, call);
        changed_.notify_all();
    }

    void releaseAll() {
        std::lock_guard lock(mutex_);
        releaseAll_ = true;
        changed_.notify_all();
    }

    void failNext(core::Error error) {
        std::lock_guard lock(mutex_);
        nextFailure_ = std::move(error);
    }

    void throwStandardNext() {
        std::lock_guard lock(mutex_);
        throwStandard_ = true;
    }

    void throwUnknownNext() {
        std::lock_guard lock(mutex_);
        throwUnknown_ = true;
    }

    void returnNext(std::shared_ptr<const core::FrameBundle> bundle) {
        std::lock_guard lock(mutex_);
        hasBundleOverride_ = true;
        bundleOverride_ = std::move(bundle);
    }

private:
    core::BufferPool* displayPool_;
    mutable std::mutex mutex_;
    processing::ProcessorStatus status_;
    std::condition_variable changed_;
    std::vector<std::uint64_t> inputs_;
    std::size_t released_{0U};
    bool releaseAll_{false};
    bool throwStandard_{false};
    bool throwUnknown_{false};
    std::optional<core::Error> nextFailure_;
    bool hasBundleOverride_{false};
    std::shared_ptr<const core::FrameBundle> bundleOverride_;
};

struct ProcessingFixture final {
    std::shared_ptr<core::BufferPool> rawPool =
        core::BufferPool::create(4U, 4U).value();
    std::shared_ptr<core::BufferPool> displayPool =
        core::BufferPool::create(4U, 4U).value();
    core::LatestValueSlot<core::RawFrame> rawSlot;
    core::LatestValueSlot<core::FrameBundle> bundleSlot;
    BlockingProcessor processor{*displayPool};
    ProcessingWorker worker{rawSlot, bundleSlot, processor};
    std::uint64_t bundleRevision{0U};

    ~ProcessingFixture() {
        processor.releaseAll();
        worker.requestStop();
        worker.join();
    }

    void publishRaw(std::uint64_t id) {
        auto lease = rawPool->tryAcquire();
        ASSERT_TRUE(lease.has_value());
        std::ranges::fill(lease->bytes(), static_cast<std::byte>(id));
        auto layout = core::ImageLayout::create(
            2U, 2U, 2U, core::StorageType::UInt8, 4U).value();
        auto raw = core::RawFrame::create(
            id, layout, std::move(*lease).seal(), metadata()).value();
        (void)rawSlot.publish(std::move(raw));
    }

    bool waitForBundleId(std::uint64_t id) {
        std::stop_source timeout;
        std::condition_variable_any wake;
        std::mutex mutex;
        std::jthread watchdog([&](std::stop_token token) {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, token, 2s, [] { return false; });
            if (!token.stop_requested()) {
                timeout.request_stop();
            }
        });
        while (!timeout.stop_requested()) {
            auto bundle = bundleSlot.waitForNewer(bundleRevision, timeout.get_token());
            if (!bundle) {
                break;
            }
            bundleRevision = bundle->revision;
            if (bundle->value->sourceFrameId() == id) {
                return true;
            }
        }
        return false;
    }

    bool waitForSnapshot(
        const std::function<bool(const ProcessingWorkerSnapshot&)>& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate(worker.snapshot())) {
                return true;
            }
            std::this_thread::yield();
        }
        return predicate(worker.snapshot());
    }
};

// Draining raw history instead of selecting the latest value breaks call 2.
TEST(ProcessingWorker, SuccessfulOriginalPublicationPreservesIndependentProcessorWarning) {
    ProcessingFixture fixture;
    processing::ProcessorStatus status;
    status.mode=processing::ProcessorMode::OriginalOnlyLatched;
    status.enhancementFailures=3;
    status.error=std::make_shared<const core::Error>(core::Error{core::ErrorCategory::Processing,"enhancement_failed","Enhancement paused.","Retained original diagnostic",true});
    fixture.processor.setStatus(status);
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1); ASSERT_TRUE(fixture.processor.waitForCall(1)); fixture.processor.releaseCall(1);
    ASSERT_TRUE(fixture.waitForBundleId(1));
    auto snapshot=fixture.worker.snapshot();
    EXPECT_FALSE(snapshot.currentError.has_value());
    EXPECT_EQ(snapshot.processorStatus.mode,processing::ProcessorMode::OriginalOnlyLatched);
    EXPECT_EQ(snapshot.processorStatus.error,status.error);
    EXPECT_EQ(snapshot.processorStatus.enhancementFailures,3U);
    EXPECT_EQ(snapshot.processingErrors,0U);
}

TEST(ProcessingWorker, ProcessesNewestAvailableFrameAfterDelay) {
    ProcessingFixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.publishRaw(2U);
    fixture.publishRaw(3U);
    fixture.processor.releaseCall(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(2U));
    EXPECT_EQ(fixture.processor.inputId(2U), 3U);
    fixture.processor.releaseCall(2U);
    ASSERT_TRUE(fixture.waitForBundleId(3U));
    const auto snapshot = fixture.worker.snapshot();
    EXPECT_EQ(snapshot.rawFramesSkipped, 1U);
}

// Publishing a new bundle over an unconsumed predecessor must be observable.
TEST(ProcessingWorker, CountsBundleReplacementWithoutConsumerTiming) {
    ProcessingFixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);
    fixture.publishRaw(2U);
    ASSERT_TRUE(fixture.processor.waitForCall(2U));
    fixture.processor.releaseCall(2U);
    fixture.publishRaw(3U);
    ASSERT_TRUE(fixture.processor.waitForCall(3U));

    EXPECT_EQ(fixture.worker.snapshot().bundlesReplaced, 1U);
    fixture.worker.requestStop();
    fixture.processor.releaseCall(3U);
    fixture.worker.join();
}

// Starting from revision one instead of the retained latest revision processes stale input.
TEST(ProcessingWorker, ProcessesNewestFrameOnFirstConsume) {
    ProcessingFixture fixture;
    fixture.publishRaw(1U);
    fixture.publishRaw(2U);
    fixture.publishRaw(3U);

    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    EXPECT_EQ(fixture.processor.inputId(1U), 3U);
    fixture.processor.releaseCall(1U);
    ASSERT_TRUE(fixture.waitForBundleId(3U));
    EXPECT_EQ(fixture.worker.snapshot().rawFramesSkipped, 2U);
}

// Dropping processor failures loses the operator-visible typed diagnostic.
TEST(ProcessingWorker, CountsProcessorFailureAndClearsItAfterPublication) {
    ProcessingFixture fixture;
    fixture.processor.failNext({
        core::ErrorCategory::Processing,
        "scripted_processing_failure",
        "Processing failed.",
        "Scripted test failure.",
        true,
        17,
    });
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);
    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return snapshot.processingErrors == 1U; }));
    auto failed = fixture.worker.snapshot();
    ASSERT_TRUE(failed.currentError);
    EXPECT_EQ(failed.currentError->category, core::ErrorCategory::Processing);
    EXPECT_EQ(failed.currentError->code, "scripted_processing_failure");
    EXPECT_EQ(failed.currentError->nativeCode, 17);

    fixture.publishRaw(2U);
    ASSERT_TRUE(fixture.processor.waitForCall(2U));
    fixture.processor.releaseCall(2U);
    ASSERT_TRUE(fixture.waitForBundleId(2U));
    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return !snapshot.currentError; }));
    EXPECT_FALSE(fixture.worker.snapshot().currentError);
}

// Classifying every resource failure as pool exhaustion hides metadata allocation faults.
TEST(ProcessingWorker, SeparatesDisplayPoolExhaustionFromOtherFailures) {
    ProcessingFixture fixture;
    fixture.processor.failNext({
        core::ErrorCategory::ResourceExhaustion,
        "display_buffer_pool_exhausted",
        "Display pool exhausted.",
        "No display lease is available.",
        true,
    });
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);
    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return snapshot.displayPoolExhaustions == 1U; }));
    EXPECT_EQ(fixture.worker.snapshot().processingErrors, 0U);

    fixture.processor.failNext({
        core::ErrorCategory::ResourceExhaustion,
        "frame_allocation_failed",
        "Frame allocation failed.",
        "Immutable frame ownership could not be allocated.",
        true,
    });
    fixture.publishRaw(2U);
    ASSERT_TRUE(fixture.processor.waitForCall(2U));
    fixture.processor.releaseCall(2U);
    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return snapshot.processingErrors == 1U; }));
    const auto snapshot = fixture.worker.snapshot();
    EXPECT_EQ(snapshot.displayPoolExhaustions, 1U);
    ASSERT_TRUE(snapshot.currentError);
    EXPECT_EQ(snapshot.currentError->code, "frame_allocation_failed");
}

// Letting a processor exception escape the jthread entry terminates the process.
TEST(ProcessingWorker, ContainsStandardProcessorExceptionAsTypedFailure) {
    ProcessingFixture fixture;
    fixture.processor.throwStandardNext();
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);

    fixture.worker.join();
    const auto snapshot = fixture.worker.snapshot();
    EXPECT_EQ(snapshot.processingErrors, 1U);
    ASSERT_TRUE(snapshot.currentError);
    EXPECT_EQ(snapshot.currentError->category, core::ErrorCategory::Internal);
    EXPECT_EQ(snapshot.currentError->code, "processing_worker_exception");
    EXPECT_FALSE(fixture.bundleSlot.consumeAfter(0U));
}

// Non-standard exceptions require the same fail-closed worker containment.
TEST(ProcessingWorker, ContainsUnknownProcessorExceptionAsTypedFailure) {
    ProcessingFixture fixture;
    fixture.processor.throwUnknownNext();
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);

    fixture.worker.join();
    const auto snapshot = fixture.worker.snapshot();
    EXPECT_EQ(snapshot.processingErrors, 1U);
    ASSERT_TRUE(snapshot.currentError);
    EXPECT_EQ(snapshot.currentError->category, core::ErrorCategory::Internal);
    EXPECT_EQ(snapshot.currentError->code, "processing_worker_unknown_exception");
    EXPECT_FALSE(fixture.bundleSlot.consumeAfter(0U));
}

// Failing to pass the stop token into the raw wait leaves join blocked forever.
TEST(ProcessingWorker, StopWhileWaitingJoinsPromptly) {
    ProcessingFixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());

    const auto started = std::chrono::steady_clock::now();
    fixture.worker.requestStop();
    fixture.worker.join();

    EXPECT_LT(std::chrono::steady_clock::now() - started, 500ms);
    EXPECT_EQ(fixture.processor.callCount(), 0U);
}

// Publishing a completed synchronous call after cancellation exposes stale work.
TEST(ProcessingWorker, CancellationDuringInFlightCallDiscardsItsBundle) {
    ProcessingFixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));

    fixture.worker.requestStop();
    fixture.processor.releaseCall(1U);
    fixture.worker.join();

    EXPECT_FALSE(fixture.bundleSlot.consumeAfter(0U));
    const auto snapshot = fixture.worker.snapshot();
    EXPECT_EQ(snapshot.processingErrors, 0U);
    EXPECT_EQ(snapshot.displayPoolExhaustions, 0U);
    EXPECT_FALSE(snapshot.currentError);
    EXPECT_EQ(fixture.displayPool->stats().inUse, 0U);
}

// Treating closed-empty as a transient wake causes a tight wait loop or stuck join.
TEST(ProcessingWorker, ClosedEmptyRawSlotTerminatesWithoutProcessing) {
    ProcessingFixture fixture;
    fixture.rawSlot.close();
    ASSERT_TRUE(fixture.worker.start().hasValue());
    std::jthread watchdog([&](std::stop_token token) {
        std::condition_variable_any wake;
        std::mutex mutex;
        std::unique_lock lock(mutex);
        wake.wait_for(lock, token, 500ms, [] { return false; });
        if (!token.stop_requested()) {
            fixture.worker.requestStop();
        }
    });

    const auto started = std::chrono::steady_clock::now();
    fixture.worker.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    watchdog.request_stop();

    EXPECT_LT(elapsed, 250ms);
    EXPECT_EQ(fixture.processor.callCount(), 0U);
}

// Treating null success as valid can clear a real error without publishing a frame.
TEST(ProcessingWorker, RejectsNullSuccessfulProcessorResult) {
    ProcessingFixture fixture;
    fixture.processor.returnNext(nullptr);
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);

    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return snapshot.processingErrors == 1U; }));
    const auto snapshot = fixture.worker.snapshot();
    ASSERT_TRUE(snapshot.currentError);
    EXPECT_EQ(snapshot.currentError->category, core::ErrorCategory::Processing);
    EXPECT_EQ(snapshot.currentError->code, "processing_result_invalid");
    EXPECT_FALSE(fixture.bundleSlot.consumeAfter(0U));
}

// Publishing a processor result for another input breaks source identity ordering.
TEST(ProcessingWorker, RejectsSuccessfulBundleForDifferentInput) {
    ProcessingFixture fixture;
    auto wrongRawPool = core::BufferPool::create(1U, 4U).value();
    auto wrongDisplayPool = core::BufferPool::create(1U, 4U).value();
    fixture.processor.returnNext(makeBundle(*wrongRawPool, *wrongDisplayPool, 99U));
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);

    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return snapshot.processingErrors == 1U; }));
    const auto snapshot = fixture.worker.snapshot();
    ASSERT_TRUE(snapshot.currentError);
    EXPECT_EQ(snapshot.currentError->code, "processing_result_invalid");
    EXPECT_FALSE(fixture.bundleSlot.consumeAfter(0U));
}

// Equal numeric IDs do not make a substituted raw owner the selected input.
TEST(ProcessingWorker, RejectsSameIdBundleWithDifferentRawOwner) {
    ProcessingFixture fixture;
    auto wrongRawPool = core::BufferPool::create(1U, 4U).value();
    auto wrongDisplayPool = core::BufferPool::create(1U, 4U).value();
    fixture.processor.returnNext(makeBundle(*wrongRawPool, *wrongDisplayPool, 1U));
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);

    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return snapshot.processingErrors == 1U; }));
    const auto snapshot = fixture.worker.snapshot();
    ASSERT_TRUE(snapshot.currentError);
    EXPECT_EQ(snapshot.currentError->code, "processing_result_invalid");
    EXPECT_FALSE(fixture.bundleSlot.consumeAfter(0U));
}

// A successful process into a closed slot must not clear the last visible error.
TEST(ProcessingWorker, ClosedOutputRejectsPublicationAndRetainsError) {
    ProcessingFixture fixture;
    fixture.processor.failNext({
        core::ErrorCategory::Processing,
        "retained_failure",
        "Processing failed.",
        "Keep this error until an accepted publication.",
        true,
    });
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1U);
    ASSERT_TRUE(fixture.processor.waitForCall(1U));
    fixture.processor.releaseCall(1U);
    ASSERT_TRUE(fixture.waitForSnapshot(
        [](const auto& snapshot) { return snapshot.processingErrors == 1U; }));

    fixture.publishRaw(2U);
    ASSERT_TRUE(fixture.processor.waitForCall(2U));
    fixture.bundleSlot.close();
    fixture.processor.releaseCall(2U);
    fixture.worker.join();

    const auto snapshot = fixture.worker.snapshot();
    ASSERT_TRUE(snapshot.currentError);
    EXPECT_EQ(snapshot.currentError->code, "retained_failure");
    EXPECT_EQ(snapshot.bundlesReplaced, 0U);
    EXPECT_FALSE(fixture.bundleSlot.consumeAfter(0U));
}

// Starting after cancellation can consume a retained raw value despite stop intent.
TEST(ProcessingWorker, RejectsStartAfterStopRequest) {
    ProcessingFixture fixture;
    fixture.publishRaw(1U);
    fixture.worker.requestStop();

    const auto result = fixture.worker.start();

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, core::ErrorCategory::Cancelled);
    EXPECT_EQ(result.error().code, "processing_worker_cancelled");
    EXPECT_EQ(fixture.processor.callCount(), 0U);
}

// A second start must not replace or race the owned worker thread.
TEST(ProcessingWorker, StartsOnlyOnce) {
    ProcessingFixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());

    const auto second = fixture.worker.start();

    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.error().category, core::ErrorCategory::Internal);
    EXPECT_EQ(second.error().code, "processing_worker_already_started");
}

}  // namespace
}  // namespace lumora::application
