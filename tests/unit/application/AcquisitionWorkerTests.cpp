#include <lumora/application/AcquisitionWorker.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace lumora::application {
namespace {
using namespace std::chrono_literals;
using S = CameraSessionState;
using core::ErrorCategory;
template<typename T> using Result = core::Result<T>;

core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
}
camera::CameraConfiguration configuration() {
    return {mono8(), {0U, 0U, 4U, 3U}, 30.0,
        {camera::ExposureMode::Manual, 100.0}, {camera::GainMode::Manual, 0.0},
        camera::AcquisitionMode::Continuous};
}
camera::CameraCapabilities capabilities() {
    return {{mono8()}, {{0U, 0U, 1U, 1U}, {0U, 0U, 8U, 6U}, {1U, 1U, 1U, 1U}},
        {1.0, 60.0, 1.0, false}, {1.0, 1000.0, 1.0, false},
        {camera::ExposureMode::Manual}, {0.0, 10.0, 1.0, false}, {camera::GainMode::Manual}};
}
core::Error error(ErrorCategory category, std::string code, bool recoverable = false) {
    return {category, std::move(code), "Scripted camera error.", "", recoverable};
}
enum class Grab { Frame, Timeout, Invalid, Exhausted, Removed, Fatal, WrongTimeoutCategory, Null, WrongRoi, Throw, FrameAfterStop };

struct ScriptedGrab final {
    Grab value;
    std::chrono::nanoseconds advance{0ns};
};

struct Script final {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::vector<std::pair<std::string, std::thread::id>> calls;
    std::deque<ScriptedGrab> grabs;
    std::vector<std::chrono::steady_clock::time_point> retrieveTimes;
    std::chrono::milliseconds minimumRetrieveTime{0ms};
    std::string blockedOperation;
    bool released{false};
    bool missing{false};
    bool rejectApply{false};
    std::optional<ErrorCategory> applyFailure;
    bool failOpen{false};
    bool failCapabilities{false};
    bool failDiscover{false};
    bool cooperativeDiscovery{false};
    bool failStart{false};
    bool failStop{false};
    bool failClose{false};
    std::optional<camera::CameraConfiguration> readback;
    std::optional<camera::CameraCapabilities> offeredCapabilities;

    void record(std::string operation) {
        std::unique_lock lock(mutex);
        calls.emplace_back(operation, std::this_thread::get_id());
        changed.notify_all();
        changed.wait(lock, [&] { return released || operation != blockedOperation; });
    }
    bool waitFor(std::string_view operation, std::size_t count = 1U) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 2s, [&] {
            return static_cast<std::size_t>(std::count_if(calls.begin(), calls.end(),
                [&](const auto& call) { return call.first == operation; })) >= count;
        });
    }
    std::size_t count(std::string_view operation) {
        std::lock_guard lock(mutex);
        return static_cast<std::size_t>(std::count_if(calls.begin(), calls.end(),
            [&](const auto& call) { return call.first == operation; }));
    }
    void push(Grab grab, std::chrono::nanoseconds advance = 0ns) {
        std::lock_guard lock(mutex);
        grabs.push_back({grab, advance});
        changed.notify_all();
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};

class Device final : public camera::ICameraDevice {
public:
    Device(Script& script, core::ManualClock& clock) : script_(script), clock_(clock) { script_.record("construct"); }
    ~Device() override { script_.record("destroy"); }
    Result<void> open() override {
        script_.record("open");
        return script_.failOpen ? Result<void>::failure(error(ErrorCategory::CameraConnection, "open_failed")) : Result<void>::success();
    }
    Result<camera::CameraCapabilities> capabilities() override {
        script_.record("capabilities");
        if (script_.failCapabilities) { return Result<camera::CameraCapabilities>::failure(error(ErrorCategory::CameraConfiguration, "capabilities_failed")); }
        return Result<camera::CameraCapabilities>::success(script_.offeredCapabilities.value_or(application::capabilities()));
    }
    Result<camera::AppliedCameraConfiguration> applyConfiguration(const camera::CameraConfiguration& value) override {
        script_.record("apply");
        if (script_.rejectApply) { return Result<camera::AppliedCameraConfiguration>::failure(error(ErrorCategory::CameraConfiguration, "settings_rejected")); }
        if (script_.applyFailure) {
            return Result<camera::AppliedCameraConfiguration>::failure(error(*script_.applyFailure, "apply_failed"));
        }
        configuration_ = script_.readback.value_or(value);
        return Result<camera::AppliedCameraConfiguration>::success({value, configuration_});
    }
    Result<void> startStream() override {
        script_.record("start");
        return script_.failStart ? Result<void>::failure(error(ErrorCategory::CameraConnection, "start_failed")) : Result<void>::success();
    }
    Result<std::shared_ptr<const core::RawFrame>> retrieve(std::chrono::milliseconds timeout,
        core::BufferPool& pool, std::stop_token token) override {
        const auto started = std::chrono::steady_clock::now();
        script_.record("retrieve");
        EXPECT_EQ(timeout, 250ms);
        Grab grab{Grab::Timeout};
        {
            std::unique_lock lock(script_.mutex);
            script_.retrieveTimes.push_back(started);
            script_.changed.wait_for(lock, token, timeout, [&] { return !script_.grabs.empty() || script_.released; });
            if (token.stop_requested()) { return Result<std::shared_ptr<const core::RawFrame>>::failure(error(ErrorCategory::Cancelled, "cancelled")); }
            if (!script_.grabs.empty()) {
                grab = script_.grabs.front().value;
                clock_.advance(script_.grabs.front().advance);
                script_.grabs.pop_front();
            }
            script_.changed.wait_until(lock, token, started + script_.minimumRetrieveTime, [] { return false; });
            if (token.stop_requested()) { return Result<std::shared_ptr<const core::RawFrame>>::failure(error(ErrorCategory::Cancelled, "cancelled")); }
        }
        if (grab == Grab::FrameAfterStop) {
            script_.record("await-cancellation");
            std::unique_lock lock(script_.mutex);
            script_.changed.wait(lock, token, [] { return false; });
        }
        switch (grab) {
        case Grab::Timeout:
            script_.record("timeout-return");
            return failure(ErrorCategory::Acquisition, "acquisition_timeout");
        case Grab::Invalid: return failure(ErrorCategory::InvalidFrame, "invalid_frame");
        case Grab::Exhausted: return failure(ErrorCategory::ResourceExhaustion, "buffer_pool_exhausted");
        case Grab::Removed: return failure(ErrorCategory::CameraConnection, "camera_removed", true);
        case Grab::Fatal: return failure(ErrorCategory::Acquisition, "terminal_grab_failure");
        case Grab::WrongTimeoutCategory: return failure(ErrorCategory::Internal, "acquisition_timeout");
        case Grab::Null: return Result<std::shared_ptr<const core::RawFrame>>::success(nullptr);
        case Grab::Throw: throw std::runtime_error("scripted retrieval exception");
        case Grab::Frame:
        case Grab::WrongRoi:
        case Grab::FrameAfterStop: break;
        }
        auto lease = pool.tryAcquire();
        if (!lease) { return failure(ErrorCategory::ResourceExhaustion, "buffer_pool_exhausted"); }
        auto roi = configuration_.roi;
        if (grab == Grab::WrongRoi) { roi.width = 2U; }
        const auto sampleBytes = configuration_.pixelFormat.applicationStorage == core::StorageType::UInt8 ? 1U : 2U;
        auto layout = core::ImageLayout::create(roi.width, roi.height, roi.width * sampleBytes,
            configuration_.pixelFormat.applicationStorage, static_cast<std::size_t>(roi.width) * roi.height * sampleBytes);
        auto settings = core::AcquisitionSettingsSnapshot::create({"Test", "Camera", "1", "virtual", {}},
            configuration_.pixelFormat, roi, configuration_.requestedFps.value(),
            configuration_.requestedFps.value(), 100.0, 0.0);
        return core::RawFrame::create(++frameId_, layout.value(), std::move(*lease).seal(),
            {frameId_, clock_.steadyNow(), clock_.utcNow(), {}, settings.value()});
    }
    Result<void> stopStream() noexcept override {
        script_.record("stop");
        return script_.failStop ? Result<void>::failure(error(ErrorCategory::CameraConnection, "stop_failed")) : Result<void>::success();
    }
    Result<void> close() noexcept override {
        script_.record("close");
        return script_.failClose ? Result<void>::failure(error(ErrorCategory::CameraConnection, "close_failed")) : Result<void>::success();
    }
private:
    static Result<std::shared_ptr<const core::RawFrame>> failure(ErrorCategory category, std::string code, bool recoverable = false) {
        return Result<std::shared_ptr<const core::RawFrame>>::failure(error(category, std::move(code), recoverable));
    }
    Script& script_;
    core::ManualClock& clock_;
    camera::CameraConfiguration configuration_{application::configuration()};
    std::uint64_t frameId_{0U};
};

class Provider final : public camera::ICameraProvider {
public:
    Provider(Script& script, core::ManualClock& clock) : script_(script), clock_(clock) {}
    Result<std::vector<camera::CameraDescriptor>> discover(std::stop_token token) override {
        script_.record("discover");
        if (script_.cooperativeDiscovery) {
            std::unique_lock lock(script_.mutex);
            script_.changed.wait(lock, token, [&] { return script_.released; });
            return Result<std::vector<camera::CameraDescriptor>>::failure(error(ErrorCategory::Cancelled, "cancelled"));
        }
        if (script_.failDiscover) { return Result<std::vector<camera::CameraDescriptor>>::failure(error(ErrorCategory::CameraDiscovery, "discovery_failed")); }
        std::vector<camera::CameraDescriptor> descriptors;
        if (!script_.missing) { descriptors.push_back({{"camera-1"}, {"Test", "Camera", "1", "virtual", {}}, true}); }
        return Result<std::vector<camera::CameraDescriptor>>::success(std::move(descriptors));
    }
    Result<std::unique_ptr<camera::ICameraDevice>> create(const camera::CameraId& id) override {
        script_.record("create");
        EXPECT_EQ(id.value, "camera-1");
        return Result<std::unique_ptr<camera::ICameraDevice>>::success(std::make_unique<Device>(script_, clock_));
    }
private:
    Script& script_;
    core::ManualClock& clock_;
};

bool awaitStatus(core::LatestValueSlot<CameraStatusSnapshot>& status, std::uint64_t& revision,
    std::shared_ptr<const CameraStatusSnapshot>& latest,
    const std::function<bool(const CameraStatusSnapshot&)>& predicate) {
    std::stop_source timeout;
    std::condition_variable_any wake;
    std::mutex mutex;
    std::jthread watchdog([&](std::stop_token token) {
        std::unique_lock lock(mutex);
        wake.wait_for(lock, token, 2s, [] { return false; });
        if (!token.stop_requested()) { timeout.request_stop(); }
    });
    while (!timeout.stop_requested()) {
        auto value = status.waitForNewer(revision, timeout.get_token());
        if (!value) { break; }
        revision = value->revision;
        latest = value->value;
        if (predicate(*latest)) { return true; }
    }
    return false;
}

struct Fixture final {
    Script script;
    core::ManualClock clock;
    Provider provider{script, clock};
    std::shared_ptr<core::BufferPool> pool;
    CameraCommandMailbox mailbox;
    core::LatestValueSlot<core::RawFrame> raw;
    core::LatestValueSlot<CameraStatusSnapshot> status;
    AcquisitionWorker worker;
    std::shared_ptr<const CameraStatusSnapshot> latest;
    std::uint64_t revision{0U};
    std::uint64_t requestId{0U};
    explicit Fixture(CameraStatusSnapshot initial = {}, std::size_t bytesPerBuffer = 48U,
                     std::optional<camera::CameraConfiguration> prepared = configuration(),
                     std::chrono::steady_clock::time_point epoch = {})
        : clock(epoch, std::chrono::system_clock::time_point::min()),
          pool(core::BufferPool::create(10U, bytesPerBuffer).value()),
          worker(provider, mailbox, *pool, raw, clock, status, std::move(initial), std::move(prepared)) {}
    ~Fixture() { script.release(); worker.requestStop(); worker.join(); }
    bool await(const std::function<bool(const CameraStatusSnapshot&)>& predicate) {
        return awaitStatus(status, revision, latest, predicate);
    }
    template<typename Payload> bool command(Payload payload, bool success = true) {
        const auto id = ++requestId;
        if (!worker.post({id, std::move(payload)}).hasValue()) { return false; }
        return await([&](const auto& snapshot) {
            return snapshot.latestOutcome && snapshot.latestOutcome->requestId == id
                && snapshot.latestOutcome->error.has_value() != success;
        });
    }
    bool ready() {
        return worker.start().hasValue() && command(Discover{}) && command(Connect{{"camera-1"}})
            && command(ApplyConfiguration{0U, configuration(), 1U})
            && command(ConfirmConfiguration{0U, 1U}) && command(StartStream{0U, 1U});
    }
    bool deliver(Grab grab, std::chrono::nanoseconds advance = 0ns) {
        const auto count = [](const auto& s) {
            const auto& counters = s.acquisitionCounters;
            return counters.acquired + counters.timeouts + counters.droppedInvalidFrame
                + counters.droppedNoRawBuffer + counters.terminalFailures;
        };
        const auto before = count(*latest);
        script.push(grab, advance);
        return await([&](const auto& s) { return count(s) > before; });
    }
};

// Missing dispatch or off-thread ownership fails the command/status and thread assertions.
TEST(AcquisitionWorker, AllDeviceCallsOccurOnWorkerThread) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    fixture.script.push(Grab::Frame);
    fixture.script.push(Grab::Frame);
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& status) { return status.acquisitionCounters.acquired == 3U; }));
    fixture.worker.requestStop();
    fixture.worker.join();
    std::set<std::thread::id> threads;
    for (const auto& call : fixture.script.calls) { threads.insert(call.second); }
    ASSERT_EQ(threads.size(), 1U);
    EXPECT_NE(*threads.begin(), std::this_thread::get_id());
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
}

// Publishing FIFO/allocation fallbacks instead of replacement breaks count, ID, or lease assertions.
TEST(AcquisitionWorker, ReplacesUnconsumedRawFrameAndRetainsOneLease) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    fixture.clock.advance(10ms);
    for (int i = 0; i < 3; ++i) { fixture.script.push(Grab::Frame); }
    ASSERT_TRUE(fixture.await([](const auto& status) { return status.acquisitionCounters.acquired == 3U; }));
    EXPECT_EQ(fixture.latest->acquisitionCounters.droppedBeforeProcessing, 2U);
    const auto frame = fixture.raw.consumeAfter(0U);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->value->frameId, 3U);
    EXPECT_EQ(frame->value->metadata.hostReceiptTime, fixture.clock.steadyNow());
    EXPECT_EQ(fixture.latest->lastAcquiredAt, fixture.clock.steadyNow());
    EXPECT_EQ(fixture.pool->stats().inUse, 1U);
}

TEST(AcquisitionWorker, InitialSnapshotRejectsDeviceFactsAndTerminalStates) {
    for (auto state : {S::Streaming, S::ConnectedIdle, S::Connecting, S::Discovering, S::ShuttingDown}) {
        CameraStatusSnapshot initial;
        initial.state = state;
        Fixture fixture(initial);
        EXPECT_FALSE(fixture.worker.start().hasValue());
        EXPECT_TRUE(fixture.script.calls.empty());
    }
    CameraStatusSnapshot initial;
    initial.actualIdentity = camera::CameraId{"camera-1"};
    Fixture fixture(initial);
    EXPECT_FALSE(fixture.worker.start().hasValue());
}

TEST(AcquisitionWorker, StartRequiresCurrentGenerationAppliedRevisionAndConfirmation) {
    Fixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    EXPECT_FALSE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.command(StartStream{0U, 1U}, false));
    ASSERT_TRUE(fixture.command(ApplyConfiguration{99U, configuration(), 1U}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "stale_camera_session");
    EXPECT_EQ(fixture.script.count("apply"), 0U);
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 1U}));
    ASSERT_TRUE(fixture.command(ConfirmConfiguration{99U, 1U}, false));
    ASSERT_TRUE(fixture.command(ConfirmConfiguration{0U, 2U}, false));
    ASSERT_TRUE(fixture.command(StartStream{0U, 1U}, false));
    ASSERT_TRUE(fixture.command(ConfirmConfiguration{0U, 1U}));
    ASSERT_TRUE(fixture.command(StartStream{99U, 1U}, false));
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 1U}, false));
    EXPECT_EQ(fixture.script.count("start"), 0U);
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 2U}));
    ASSERT_TRUE(fixture.command(StartStream{0U, 1U}, false));
    EXPECT_FALSE(fixture.latest->confirmedRevision);
}

// Drops cannot renew the last accepted frame's grace or trigger terminal timeout themselves.
TEST(AcquisitionWorker, WatchdogValidFrameResetsGraceButInvalidAndExhaustedResultsDoNot) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    EXPECT_FALSE(fixture.latest->lastAcquiredAt);
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 749ms));
    EXPECT_EQ(fixture.latest->state, S::Streaming);
    // A ready frame at the old deadline must be accepted before any terminal check.
    ASSERT_TRUE(fixture.deliver(Grab::Frame, 1ms));
    EXPECT_EQ(fixture.latest->lastAcquiredAt, std::chrono::steady_clock::time_point{750ms});
    EXPECT_EQ(fixture.latest->consecutiveTimeouts, 0U);
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 749ms));
    ASSERT_EQ(fixture.latest->state, S::Streaming);
    ASSERT_TRUE(fixture.deliver(Grab::Invalid, 1ms));
    ASSERT_TRUE(fixture.deliver(Grab::Null));
    ASSERT_TRUE(fixture.deliver(Grab::WrongRoi));
    ASSERT_TRUE(fixture.deliver(Grab::Exhausted));
    EXPECT_EQ(fixture.latest->state, S::Streaming);
    EXPECT_EQ(fixture.latest->consecutiveTimeouts, 1U);
    EXPECT_EQ(fixture.latest->acquisitionCounters.droppedInvalidFrame, 3U);
    EXPECT_EQ(fixture.latest->acquisitionCounters.droppedNoRawBuffer, 1U);
    ASSERT_TRUE(fixture.deliver(Grab::Timeout));
    EXPECT_EQ(fixture.latest->state, S::Reconnecting);
    EXPECT_EQ(fixture.latest->acquisitionCounters.timeouts, 3U);
    EXPECT_EQ(fixture.latest->acquisitionCounters.terminalFailures, 1U);
    EXPECT_TRUE(fixture.latest->sourceReplacementRequired);
    EXPECT_EQ(fixture.latest->desiredIdentity->value, "camera-1");
    EXPECT_FALSE(fixture.latest->actualIdentity);
    EXPECT_FALSE(fixture.latest->appliedConfiguration);
    EXPECT_FALSE(fixture.latest->confirmedRevision);
    EXPECT_EQ(fixture.latest->lastAcquiredAt, std::chrono::steady_clock::time_point{750ms});
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
    ASSERT_TRUE(fixture.command(Retry{}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "camera_context_replacement_required");
    EXPECT_EQ(fixture.script.count("create"), 1U);
}

// The first-frame deadline is elapsed time, independent of the number of timeout results.
TEST(AcquisitionWorker, WatchdogFirstFrameStallUsesExactAppliedRateBoundaries) {
    for (const auto actualFps : {30.0, 1.0}) {
        SCOPED_TRACE(actualFps);
        Fixture fixture;
        auto actual = configuration();
        actual.requestedFps = actualFps;
        fixture.script.readback = actual;
        ASSERT_TRUE(fixture.ready());
        const auto beforeDeadline = actualFps == 30.0 ? 749ms : 2999ms;
        ASSERT_TRUE(fixture.deliver(Grab::Timeout, beforeDeadline));
        ASSERT_EQ(fixture.latest->state, S::Streaming);
        EXPECT_FALSE(fixture.latest->lastAcquiredAt);
        ASSERT_TRUE(fixture.deliver(Grab::Timeout, 1ms));
        EXPECT_EQ(fixture.latest->state, S::Reconnecting);
        EXPECT_EQ(fixture.latest->acquisitionCounters.timeouts, 2U);
        EXPECT_EQ(fixture.latest->acquisitionCounters.terminalFailures, 1U);
        EXPECT_EQ(fixture.latest->desiredIdentity, camera::CameraId{"camera-1"});
        EXPECT_FALSE(fixture.latest->actualIdentity);
        EXPECT_FALSE(fixture.latest->appliedConfiguration);
        EXPECT_FALSE(fixture.latest->confirmedRevision);
        EXPECT_TRUE(fixture.latest->sourceReplacementRequired);
        EXPECT_FALSE(fixture.latest->lastAcquiredAt);
        EXPECT_EQ(fixture.script.count("create"), 1U);
        EXPECT_EQ(fixture.script.count("stop"), 1U);
        EXPECT_EQ(fixture.script.count("close"), 1U);
        EXPECT_EQ(fixture.script.count("destroy"), 1U);
    }
}

TEST(AcquisitionWorker, WatchdogUsesActualOneFpsDespiteRequestedThirtyFps) {
    Fixture fixture;
    auto actual = configuration();
    actual.requestedFps = 1.0;
    fixture.script.readback = actual;
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.deliver(Grab::Frame));
    for (int timeout = 0; timeout < 3; ++timeout) {
        ASSERT_TRUE(fixture.deliver(Grab::Timeout, 250ms));
        ASSERT_EQ(fixture.latest->state, S::Streaming);
    }
    ASSERT_TRUE(fixture.deliver(Grab::Frame, 250ms));
    ASSERT_EQ(fixture.latest->acquisitionCounters.acquired, 2U);
    EXPECT_EQ(fixture.latest->consecutiveTimeouts, 0U);
    EXPECT_EQ(fixture.latest->lastAcquiredAt, std::chrono::steady_clock::time_point{1s});
    EXPECT_EQ(fixture.latest->appliedConfiguration->requested.requestedFps, 30.0);
    EXPECT_EQ(fixture.latest->appliedConfiguration->actual.requestedFps, 1.0);
    const auto frame = fixture.raw.consumeAfter(0U);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->value->frameId, 2U);
    EXPECT_EQ(frame->value->metadata.acquisitionSettings.actualFps, 1.0);
    EXPECT_FALSE(fixture.latest->sourceReplacementRequired);
}

// Quick backend failures consume real retrieval slices but cannot advance a frozen policy clock.
TEST(AcquisitionWorker, WatchdogFrozenClockKeepsStreamingAndPadsQuickTimeouts) {
    Fixture fixture;
    for (int timeout = 0; timeout < 4; ++timeout) { fixture.script.push(Grab::Timeout); }
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.await([](const auto& s) {
        return s.acquisitionCounters.timeouts >= 4U || s.state == S::Reconnecting;
    }));
    ASSERT_EQ(fixture.latest->state, S::Streaming);
    EXPECT_GE(fixture.latest->consecutiveTimeouts, 4U);
    EXPECT_EQ(fixture.clock.steadyNow(), std::chrono::steady_clock::time_point{});
    {
        std::lock_guard lock(fixture.script.mutex);
        ASSERT_GE(fixture.script.retrieveTimes.size(), 4U);
        for (std::size_t index = 1; index < 4U; ++index) {
            EXPECT_GE(fixture.script.retrieveTimes[index] - fixture.script.retrieveTimes[index - 1U], 245ms);
        }
    }
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 750ms));
    EXPECT_EQ(fixture.latest->state, S::Reconnecting);
}

TEST(AcquisitionWorker, WatchdogPaddingUsesRemainderOfSlowRetrievalSlice) {
    Fixture fixture;
    fixture.script.minimumRetrieveTime = 180ms;
    fixture.script.push(Grab::Timeout);
    fixture.script.push(Grab::Timeout);
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.acquisitionCounters.timeouts >= 2U; }));
    std::lock_guard lock(fixture.script.mutex);
    ASSERT_GE(fixture.script.retrieveTimes.size(), 2U);
    const auto betweenCalls = fixture.script.retrieveTimes[1] - fixture.script.retrieveTimes[0];
    EXPECT_GE(betweenCalls, 245ms);
    EXPECT_LT(betweenCalls, 400ms);
}

TEST(AcquisitionWorker, WatchdogRestartAfterIdleRearmsWithoutForgingAcquisitionTime) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.deliver(Grab::Frame, 10ms));
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 749ms));
    ASSERT_TRUE(fixture.command(StopStream{}));
    fixture.clock.advance(1h);
    ASSERT_TRUE(fixture.command(StartStream{0U, 1U}));
    EXPECT_EQ(fixture.latest->lastAcquiredAt, std::chrono::steady_clock::time_point{10ms});
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 749ms));
    ASSERT_EQ(fixture.latest->state, S::Streaming);
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 1ms));
    EXPECT_EQ(fixture.latest->state, S::Reconnecting);
    EXPECT_EQ(fixture.script.count("start"), 2U);
}

TEST(AcquisitionWorker, WatchdogIdempotentStartCannotRenewGrace) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 749ms));
    ASSERT_TRUE(fixture.command(StartStream{0U, 1U}));
    ASSERT_TRUE(fixture.deliver(Grab::Timeout, 1ms));
    EXPECT_EQ(fixture.latest->state, S::Reconnecting);
    EXPECT_EQ(fixture.script.count("start"), 1U);
}

TEST(AcquisitionWorker, WatchdogUsesLatestActualRateAfterStoppedApply) {
    for (const auto updatedActualFps : {1.0, 30.0}) {
        SCOPED_TRACE(updatedActualFps);
        Fixture fixture;
        auto actual = configuration();
        actual.requestedFps = updatedActualFps == 1.0 ? 30.0 : 1.0;
        fixture.script.readback = actual;
        ASSERT_TRUE(fixture.ready());
        ASSERT_TRUE(fixture.command(StopStream{}));
        actual.requestedFps = updatedActualFps;
        fixture.script.readback = actual;
        ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 2U}));
        ASSERT_TRUE(fixture.command(ConfirmConfiguration{0U, 2U}));
        ASSERT_TRUE(fixture.command(StartStream{0U, 2U}));
        const auto beforeDeadline = updatedActualFps == 1.0 ? 2999ms : 749ms;
        ASSERT_TRUE(fixture.deliver(Grab::Timeout, beforeDeadline));
        ASSERT_EQ(fixture.latest->state, S::Streaming);
        ASSERT_TRUE(fixture.deliver(Grab::Timeout, 1ms));
        EXPECT_EQ(fixture.latest->state, S::Reconnecting);
        EXPECT_EQ(fixture.script.count("create"), 1U);
    }
}

// Neither signed duration subtraction nor a reciprocal FPS-to-duration conversion may overflow.
TEST(AcquisitionWorker, WatchdogHandlesExtremeClockEpochsAndTinyPositiveActualRate) {
    for (const auto actualFps : {30.0, std::numeric_limits<double>::min()}) {
        SCOPED_TRACE(actualFps);
        Fixture fixture({}, 48U, configuration(), std::chrono::steady_clock::time_point::min());
        auto offered = capabilities();
        offered.frameRate.minimum = std::numeric_limits<double>::min();
        fixture.script.offeredCapabilities = offered;
        auto actual = configuration();
        actual.requestedFps = actualFps;
        fixture.script.readback = actual;
        ASSERT_TRUE(fixture.ready());
        ASSERT_TRUE(fixture.deliver(Grab::Invalid, std::chrono::nanoseconds::max()));
        ASSERT_EQ(fixture.latest->state, S::Streaming);
        ASSERT_TRUE(fixture.deliver(Grab::Timeout, std::chrono::nanoseconds::max()));
        EXPECT_EQ(fixture.latest->state, actualFps == 30.0 ? S::Reconnecting : S::Streaming);
        EXPECT_EQ(fixture.latest->acquisitionCounters.terminalFailures, actualFps == 30.0 ? 1U : 0U);
    }
}

TEST(AcquisitionWorker, WatchdogOnlyExactAcquisitionTimeoutReceivesGrace) {
    for (const auto grab : {Grab::WrongTimeoutCategory, Grab::Fatal}) {
        Fixture fixture;
        ASSERT_TRUE(fixture.ready());
        ASSERT_TRUE(fixture.deliver(grab));
        EXPECT_EQ(fixture.latest->state, S::Error);
        EXPECT_EQ(fixture.latest->acquisitionCounters.timeouts, 0U);
        EXPECT_EQ(fixture.latest->acquisitionCounters.terminalFailures, 1U);
        EXPECT_EQ(fixture.script.count("destroy"), 1U);
    }
}

TEST(AcquisitionWorker, WatchdogShutdownCancelsQuickTimeoutPaddingAndPreservesCleanupFailure) {
    Fixture fixture;
    fixture.script.push(Grab::Timeout);
    fixture.script.failClose = true;
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.script.waitFor("timeout-return"));
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(fixture.worker.post({99U, Shutdown{}}).hasValue());
    fixture.worker.join();
    EXPECT_LT(std::chrono::steady_clock::now() - started, 200ms);
    const auto finalStatus = fixture.status.consumeAfter(0U);
    ASSERT_TRUE(finalStatus);
    EXPECT_EQ(finalStatus->value->state, S::ShuttingDown);
    ASSERT_TRUE(finalStatus->value->latestOutcome);
    ASSERT_TRUE(finalStatus->value->latestOutcome->error);
    EXPECT_EQ(finalStatus->value->latestOutcome->requestId, 99U);
    EXPECT_EQ(finalStatus->value->latestOutcome->error->code, "close_failed");
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
    EXPECT_FALSE(fixture.raw.consumeAfter(0U));
}

TEST(AcquisitionWorker, WatchdogRealSimulatorOneFpsPublishesSecondFrameAndHonorsPriority) {
    for (const bool disconnect : {false, true}) {
        SCOPED_TRACE(disconnect);
        core::SystemClock clock;
        auto requested = configuration();
        requested.requestedFps = 1.0;
        camera::sim::SimulatedCameraProvider provider({{"camera-1"}, capabilities(),
            camera::sim::SimulationPattern::Ramp, 1.0, 42U,
            camera::sim::SimulationPacingMode::RealTime}, clock);
        auto pool = core::BufferPool::create(4U, 48U).value();
        CameraCommandMailbox mailbox;
        core::LatestValueSlot<core::RawFrame> raw;
        core::LatestValueSlot<CameraStatusSnapshot> status;
        AcquisitionWorker worker(provider, mailbox, *pool, raw, clock, status, {}, requested);
        std::uint64_t revision{0U};
        std::shared_ptr<const CameraStatusSnapshot> latest;
        ASSERT_TRUE(worker.start().hasValue());
        ASSERT_TRUE(worker.post({1U, Connect{{"camera-1"}}}).hasValue());
        ASSERT_TRUE(worker.post({2U, ApplyConfiguration{0U, requested, 1U}}).hasValue());
        ASSERT_TRUE(worker.post({3U, ConfirmConfiguration{0U, 1U}}).hasValue());
        ASSERT_TRUE(worker.post({4U, StartStream{0U, 1U}}).hasValue());
        ASSERT_TRUE(awaitStatus(status, revision, latest, [](const auto& s) {
            return s.acquisitionCounters.acquired >= 1U || s.state == S::Error;
        }));
        ASSERT_EQ(latest->acquisitionCounters.acquired, 1U);
        const auto first = raw.consumeAfter(0U);
        ASSERT_TRUE(first);
        ASSERT_TRUE(awaitStatus(status, revision, latest, [](const auto& s) {
            return s.acquisitionCounters.acquired >= 2U || s.state == S::Reconnecting || s.state == S::Error;
        }));
        ASSERT_EQ(latest->state, S::Streaming);
        ASSERT_EQ(latest->acquisitionCounters.acquired, 2U);
        EXPECT_GE(latest->acquisitionCounters.timeouts, 3U);
        EXPECT_FALSE(latest->sourceReplacementRequired);
        const auto second = raw.consumeAfter(first->revision);
        ASSERT_TRUE(second);
        EXPECT_GT(second->value->frameId, first->value->frameId);
        EXPECT_EQ(second->value->metadata.acquisitionSettings.actualFps, 1.0);
        // A timeout after frame two proves another low-FPS pacing slice is active.
        const auto timeouts = latest->acquisitionCounters.timeouts;
        ASSERT_TRUE(awaitStatus(status, revision, latest, [&](const auto& s) {
            return s.acquisitionCounters.timeouts > timeouts;
        }));
        const auto started = std::chrono::steady_clock::now();
        CameraCommand priority{99U, StopStream{}};
        if (disconnect) { priority.payload = Disconnect{}; }
        ASSERT_TRUE(worker.post(std::move(priority)).hasValue());
        ASSERT_TRUE(awaitStatus(status, revision, latest, [](const auto& s) {
            return s.latestOutcome && s.latestOutcome->requestId == 99U;
        }));
        EXPECT_LE(std::chrono::steady_clock::now() - started, 500ms);
        EXPECT_EQ(latest->state, disconnect ? S::Disconnected : S::ConnectedIdle);
        EXPECT_EQ(latest->acquisitionCounters.acquired, 2U);
        EXPECT_FALSE(latest->latestOutcome->error);
    }
}

TEST(AcquisitionWorker, ReplacementRetryDiscoversOnlyRetainedIdentityAndOpensIdle) {
    CameraStatusSnapshot initial;
    initial.state = S::Reconnecting;
    initial.desiredIdentity = camera::CameraId{"camera-1"};
    initial.sessionGeneration = 7U;
    Fixture fixture(initial);
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Retry{}));
    EXPECT_EQ(fixture.latest->state, S::ConnectedIdle);
    EXPECT_EQ(fixture.latest->sessionGeneration, 7U);
    EXPECT_EQ(fixture.script.count("discover"), 1U);
    EXPECT_EQ(fixture.script.count("create"), 1U);
    EXPECT_EQ(fixture.script.count("open"), 1U);
    EXPECT_EQ(fixture.script.count("start"), 0U);
    EXPECT_FALSE(fixture.latest->confirmedRevision);
    ASSERT_TRUE(fixture.command(ApplyConfiguration{6U, configuration(), 1U}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "stale_camera_session");
}

TEST(AcquisitionWorker, RetryDoesNotSubstituteDifferentIdentityOrAutoRetry) {
    CameraStatusSnapshot initial;
    initial.state = S::Error;
    initial.desiredIdentity = camera::CameraId{"missing-camera"};
    Fixture fixture(initial);
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Retry{}, false));
    EXPECT_EQ(fixture.latest->state, S::Error);
    EXPECT_EQ(fixture.script.count("discover"), 1U);
    EXPECT_EQ(fixture.script.count("create"), 0U);
    ASSERT_TRUE(fixture.command(Disconnect{}));
    ASSERT_TRUE(fixture.command(Retry{}, false));
    EXPECT_FALSE(fixture.latest->desiredIdentity);
    EXPECT_EQ(fixture.script.count("discover"), 1U);
}

TEST(AcquisitionWorker, StopStartSameDeviceRetainsGenerationConfirmationAndFrameSequence) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.acquisitionCounters.acquired == 1U; }));
    ASSERT_TRUE(fixture.command(StopStream{}));
    EXPECT_EQ(fixture.latest->state, S::ConnectedIdle);
    EXPECT_EQ(fixture.latest->confirmedRevision, 1U);
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.command(StartStream{0U, 1U}));
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.acquisitionCounters.acquired == 2U; }));
    EXPECT_EQ(fixture.raw.consumeAfter(0U)->value->frameId, 2U);
    EXPECT_EQ(fixture.script.count("create"), 1U);
    EXPECT_EQ(fixture.script.count("start"), 2U);
}

TEST(AcquisitionWorker, InvalidConfigurationPreservesPriorAppliedSettingsAndMode) {
    Fixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 1U}));
    ASSERT_TRUE(fixture.command(ConfirmConfiguration{0U, 1U}));
    auto changed = configuration();
    changed.roi.width = 2U;
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, changed, 2U}, false));
    EXPECT_EQ(fixture.script.count("apply"), 1U);
    EXPECT_EQ(fixture.latest->appliedRevision, 1U);
    fixture.script.rejectApply = true;
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 3U}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "settings_rejected");
    EXPECT_EQ(fixture.latest->appliedRevision, 1U);
    EXPECT_EQ(fixture.latest->state, S::ConnectedIdle);
    EXPECT_EQ(fixture.latest->confirmedRevision, 1U);
}

TEST(AcquisitionWorker, TerminalApplyFailureRetiresConfirmedDevice) {
    for (const auto category : {ErrorCategory::CameraConnection, ErrorCategory::Internal}) {
        SCOPED_TRACE(static_cast<int>(category));
        Fixture fixture;
        ASSERT_TRUE(fixture.worker.start().hasValue());
        ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
        ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 1U}));
        ASSERT_TRUE(fixture.command(ConfirmConfiguration{0U, 1U}));
        fixture.script.applyFailure = category;
        ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 2U}, false));
        EXPECT_EQ(fixture.latest->state, S::Error);
        EXPECT_EQ(fixture.latest->latestError->category, category);
        EXPECT_FALSE(fixture.latest->actualIdentity);
        EXPECT_FALSE(fixture.latest->appliedConfiguration);
        EXPECT_FALSE(fixture.latest->confirmedRevision);
        EXPECT_TRUE(fixture.latest->sourceReplacementRequired);
        EXPECT_EQ(fixture.latest->desiredIdentity, camera::CameraId{"camera-1"});
        EXPECT_EQ(fixture.script.count("stop"), 1U);
        EXPECT_EQ(fixture.script.count("close"), 1U);
        EXPECT_EQ(fixture.script.count("destroy"), 1U);
        ASSERT_TRUE(fixture.command(Retry{}, false));
        EXPECT_EQ(fixture.latest->latestError->code, "camera_context_replacement_required");
    }
}

TEST(AcquisitionWorker, RemovalAndFatalFailureDestroyOnOwnerAndPublishHonestState) {
    for (auto grab : {Grab::Removed, Grab::Fatal, Grab::Throw}) {
        Fixture fixture;
        ASSERT_TRUE(fixture.ready());
        fixture.script.push(grab);
        ASSERT_TRUE(fixture.await([](const auto& s) { return s.state == S::Reconnecting || s.state == S::Error; }));
        EXPECT_EQ(fixture.latest->state, grab == Grab::Removed ? S::Reconnecting : S::Error);
        EXPECT_EQ(fixture.script.count("destroy"), 1U);
        EXPECT_FALSE(fixture.latest->actualIdentity);
        ASSERT_TRUE(fixture.command(Disconnect{}));
        EXPECT_FALSE(fixture.latest->desiredIdentity);
    }
}

TEST(AcquisitionWorker, FailedOpenRequiresFreshContextAndCleanupFailureRemainsVisible) {
    Fixture fixture;
    fixture.script.failOpen = true;
    fixture.script.failClose = true;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}, false));
    EXPECT_EQ(fixture.latest->state, S::Error);
    EXPECT_TRUE(fixture.latest->sourceReplacementRequired);
    EXPECT_EQ(fixture.script.count("stop"), 1U);
    EXPECT_EQ(fixture.script.count("close"), 1U);
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
    EXPECT_EQ(fixture.latest->latestError->code, "close_failed");
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}, false));
    EXPECT_EQ(fixture.script.count("create"), 1U);
}

TEST(AcquisitionWorker, StopFailureAttemptsCloseAndDoesNotClaimIdle) {
    Fixture fixture;
    fixture.script.failStop = true;
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.command(StopStream{}, false));
    EXPECT_EQ(fixture.latest->state, S::Error);
    EXPECT_EQ(fixture.script.count("close"), 1U);
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
}

TEST(AcquisitionWorker, ShutdownOutcomeIncludesCleanupFailure) {
    Fixture fixture;
    fixture.script.failClose = true;
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.worker.post({99U, Shutdown{}}).hasValue());
    fixture.worker.join();
    const auto finalStatus = fixture.status.consumeAfter(0U);
    ASSERT_TRUE(finalStatus);
    EXPECT_EQ(finalStatus->value->state, S::ShuttingDown);
    ASSERT_TRUE(finalStatus->value->latestOutcome);
    ASSERT_TRUE(finalStatus->value->latestOutcome->error);
    EXPECT_EQ(finalStatus->value->latestOutcome->error->code, "close_failed");
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
}

TEST(AcquisitionWorker, PriorityCleanupFailureSurvivesLaterCancellation) {
    for (int priority = 0; priority < 3; ++priority) {
        SCOPED_TRACE(priority);
        Fixture fixture;
        fixture.script.blockedOperation = "retrieve";
        fixture.script.failStop = priority == 0;
        fixture.script.failClose = priority != 0;
        ASSERT_TRUE(fixture.ready());
        ASSERT_TRUE(fixture.script.waitFor("retrieve"));
        CameraCommand command{99U, StopStream{}};
        if (priority == 1) { command.payload = Disconnect{}; }
        if (priority == 2) { command.payload = Shutdown{}; }
        ASSERT_TRUE(fixture.worker.post(std::move(command)).hasValue());
        fixture.script.release();
        ASSERT_TRUE(fixture.await([](const auto& s) {
            return s.latestOutcome && s.latestOutcome->requestId == 99U;
        }));
        fixture.worker.requestStop();
        fixture.worker.join();
        const auto finalStatus = fixture.status.consumeAfter(0U);
        ASSERT_TRUE(finalStatus);
        ASSERT_TRUE(finalStatus->value->latestOutcome);
        EXPECT_EQ(finalStatus->value->latestOutcome->requestId, 99U);
        ASSERT_TRUE(finalStatus->value->latestOutcome->error);
        const auto expectedCode = priority == 0 ? "stop_failed" : "close_failed";
        EXPECT_EQ(finalStatus->value->latestOutcome->error->code, expectedCode);
        ASSERT_TRUE(finalStatus->value->latestError);
        EXPECT_EQ(finalStatus->value->latestError->code, expectedCode);
        EXPECT_EQ(fixture.script.count("destroy"), 1U);
        EXPECT_EQ(fixture.script.count("retrieve"), 1U);
    }
}

TEST(AcquisitionWorker, PostedShutdownDirectlyCancelsCooperativeRetrieval) {
    Fixture fixture;
    fixture.script.push(Grab::FrameAfterStop);
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.script.waitFor("await-cancellation"));
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(fixture.worker.post({99U, Shutdown{}}).hasValue());
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.state == S::ShuttingDown; }));
    fixture.worker.join();
    EXPECT_LE(std::chrono::steady_clock::now() - started, 500ms);
    ASSERT_TRUE(fixture.latest->latestOutcome);
    EXPECT_EQ(fixture.latest->latestOutcome->requestId, 99U);
    EXPECT_FALSE(fixture.raw.consumeAfter(0U));
}

TEST(AcquisitionWorker, CancellationDiscardsFrameReturnedAfterStopWithinRetrievalBudget) {
    Fixture fixture;
    fixture.script.push(Grab::FrameAfterStop);
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.script.waitFor("await-cancellation"));
    const auto start = std::chrono::steady_clock::now();
    fixture.worker.requestStop();
    fixture.worker.join();
    EXPECT_LE(std::chrono::steady_clock::now() - start, 500ms);
    EXPECT_FALSE(fixture.raw.consumeAfter(0U));
    EXPECT_EQ(fixture.pool->stats().inUse, 0U);
    fixture.worker.join();
}

TEST(AcquisitionWorker, ClosedMailboxTerminatesIdleAndStreamingWorkers) {
    for (bool streaming : {false, true}) {
        Fixture fixture;
        ASSERT_TRUE(streaming ? fixture.ready() : fixture.worker.start().hasValue());
        fixture.mailbox.close();
        ASSERT_TRUE(fixture.await([](const auto& s) { return s.state == S::ShuttingDown; }));
        fixture.worker.join();
    }
}

TEST(AcquisitionWorker, PriorityUnderFullLoadStopsBeforeNextRetrieval) {
    for (int priority = 0; priority < 3; ++priority) {
        Fixture fixture;
        fixture.script.blockedOperation = "retrieve";
        ASSERT_TRUE(fixture.ready());
        ASSERT_TRUE(fixture.script.waitFor("retrieve"));
        ASSERT_TRUE(fixture.worker.post({11U, ConfirmConfiguration{0U, 1U}}).hasValue());
        ASSERT_TRUE(fixture.worker.post({12U, StartStream{0U, 1U}}).hasValue());
        for (std::uint64_t id = 13U; id < 43U; ++id) { ASSERT_TRUE(fixture.worker.post({id, Discover{}}).hasValue()); }
        ASSERT_EQ(fixture.mailbox.size(), 32U);
        CameraCommand command{99U, StopStream{}};
        if (priority == 1) { command.payload = Disconnect{}; }
        if (priority == 2) { command.payload = Shutdown{}; }
        ASSERT_TRUE(fixture.worker.post(std::move(command)).hasValue());
        fixture.script.release();
        ASSERT_TRUE(fixture.await([&](const auto& s) {
            if (priority == 2) { return s.state == S::ShuttingDown; }
            return s.mailboxStats.cancelled >= 2U && s.state == (priority == 1 ? S::Disconnected : S::ConnectedIdle);
        }));
        EXPECT_GE(fixture.script.count("stop"), 1U);
        fixture.worker.requestStop();
        fixture.worker.join();
        EXPECT_EQ(fixture.script.count("start"), 1U);
        EXPECT_EQ(fixture.script.count("retrieve"), 1U);
    }
}

TEST(AcquisitionWorker, DiscoveryPublishesEmptyAndFailedResultsWithoutStarting) {
    Fixture fixture;
    fixture.script.missing = true;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Discover{}));
    EXPECT_TRUE(fixture.latest->discoveredDescriptors.empty());
    EXPECT_EQ(fixture.latest->state, S::Disconnected);
    fixture.script.failDiscover = true;
    ASSERT_TRUE(fixture.command(Discover{}, false));
    EXPECT_EQ(fixture.latest->state, S::Error);
    EXPECT_EQ(fixture.latest->latestError->category, ErrorCategory::CameraDiscovery);
    EXPECT_EQ(fixture.script.count("create"), 0U);
}

TEST(AcquisitionWorker, ShutdownCancelsDiscoveryAndWaitsForOwningThreadCleanup) {
    Fixture fixture;
    fixture.script.cooperativeDiscovery = true;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.worker.post({1U, Discover{}}).hasValue());
    ASSERT_TRUE(fixture.script.waitFor("discover"));
    ASSERT_TRUE(fixture.worker.post({99U, Shutdown{}}).hasValue());
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.state == S::ShuttingDown; }));
    fixture.worker.join();
    EXPECT_EQ(fixture.latest->latestOutcome->requestId, 99U);
    EXPECT_EQ(fixture.script.count("create"), 0U);
}

TEST(AcquisitionWorker, CapabilitiesAndStartFailuresClosePartialDevices) {
    for (bool capabilityFailure : {false, true}) {
        Fixture fixture;
        fixture.script.failCapabilities = capabilityFailure;
        fixture.script.failStart = !capabilityFailure;
        ASSERT_TRUE(fixture.worker.start().hasValue());
        ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}, !capabilityFailure));
        if (!capabilityFailure) {
            ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 1U}));
            ASSERT_TRUE(fixture.command(ConfirmConfiguration{0U, 1U}));
            ASSERT_TRUE(fixture.command(StartStream{0U, 1U}, false));
        }
        EXPECT_EQ(fixture.latest->state, S::Error);
        EXPECT_FALSE(fixture.latest->actualIdentity);
        EXPECT_EQ(fixture.script.count("close"), 1U);
        EXPECT_EQ(fixture.script.count("destroy"), 1U);
    }
}

TEST(AcquisitionWorker, ExhaustedRealPoolDropsIncomingFrameWithoutFallbackAllocation) {
    Fixture fixture;
    std::vector<core::WritableBufferLease> held;
    for (int i = 0; i < 10; ++i) { held.push_back(std::move(*fixture.pool->tryAcquire())); }
    ASSERT_TRUE(fixture.ready());
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.acquisitionCounters.droppedNoRawBuffer == 1U; }));
    EXPECT_EQ(fixture.latest->state, S::Streaming);
    EXPECT_EQ(fixture.latest->acquisitionCounters.acquired, 0U);
    EXPECT_FALSE(fixture.raw.consumeAfter(0U));
    EXPECT_EQ(fixture.pool->stats().inUse, 10U);
    held.clear();
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.acquisitionCounters.acquired == 1U; }));
}

TEST(AcquisitionWorker, InvalidActualReadbackCannotBecomeConfirmedOrStream) {
    Fixture fixture;
    auto invalid = configuration();
    invalid.requestedFps.reset();
    fixture.script.readback = invalid;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 1U}, false));
    EXPECT_EQ(fixture.latest->state, S::Error);
    EXPECT_EQ(fixture.latest->latestError->code, "camera_actual_fps_invalid");
    EXPECT_FALSE(fixture.latest->appliedConfiguration);
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
}

TEST(AcquisitionWorker, StreamingApplyAndOtherIdentityAreRejectedWithoutDeviceMutation) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 2U}, false));
    EXPECT_EQ(fixture.latest->state, S::Streaming);
    EXPECT_EQ(fixture.latest->appliedRevision, 1U);
    EXPECT_EQ(fixture.script.count("apply"), 1U);
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.command(Connect{{"other-camera"}}, false));
    EXPECT_EQ(fixture.latest->actualIdentity->value, "camera-1");
    EXPECT_EQ(fixture.script.count("create"), 1U);
}

TEST(AcquisitionWorker, FirstModeMustFitPoolBeforeApplyingToDevice) {
    Fixture fixture({}, 12U, std::nullopt);
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    auto tooLarge = configuration();
    tooLarge.roi = {0U, 0U, 8U, 6U};
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, tooLarge, 1U}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "camera_raw_pool_layout");
    EXPECT_EQ(fixture.script.count("apply"), 0U);
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0U, configuration(), 2U}));
    EXPECT_EQ(fixture.latest->appliedConfiguration->actual.roi.width, 4U);
}

TEST(AcquisitionWorker, AcquisitionTotalsSaturateInsteadOfWrapping) {
    CameraStatusSnapshot initial;
    initial.acquisitionCounters.acquired = std::numeric_limits<std::uint64_t>::max();
    Fixture fixture(initial);
    ASSERT_TRUE(fixture.ready());
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.lastAcquiredAt.has_value(); }));
    EXPECT_EQ(fixture.latest->acquisitionCounters.acquired, std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(fixture.raw.consumeAfter(0U));
}

TEST(AcquisitionWorker, PriorityCancelsPendingConfirmationAndStartDuringApply) {
    Fixture fixture;
    fixture.script.blockedOperation = "apply";
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.worker.post({10U, ApplyConfiguration{0U, configuration(), 1U}}).hasValue());
    ASSERT_TRUE(fixture.script.waitFor("apply"));
    ASSERT_TRUE(fixture.worker.post({11U, ConfirmConfiguration{0U, 1U}}).hasValue());
    ASSERT_TRUE(fixture.worker.post({12U, StartStream{0U, 1U}}).hasValue());
    ASSERT_TRUE(fixture.worker.post({99U, StopStream{}}).hasValue());
    fixture.script.release();
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.latestOutcome && s.latestOutcome->requestId == 99U; }));
    EXPECT_EQ(fixture.script.count("start"), 0U);
    EXPECT_EQ(fixture.script.count("retrieve"), 0U);
    EXPECT_FALSE(fixture.latest->confirmedRevision);
    ASSERT_TRUE(fixture.command(ConfirmConfiguration{0U, 1U}));
    ASSERT_TRUE(fixture.command(StartStream{0U, 1U}));
}


camera::CameraConfiguration highDepthConfiguration() {
    auto value = configuration();
    value.pixelFormat = {"Mono12", 0x01100005U, 12, 4095, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt16};
    return value;
}
TEST(AcquisitionWorker, PreparedHighDepthModeStreamsNativeSamples) {
    const auto prepared = highDepthConfiguration();
    Fixture fixture({}, 24, prepared);
    auto offered = capabilities(); offered.pixelFormats = {prepared.pixelFormat};
    fixture.script.offeredCapabilities = offered;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0, prepared, 1}));
    ASSERT_TRUE(fixture.command(ConfirmConfiguration{0, 1}));
    ASSERT_TRUE(fixture.command(StartStream{0, 1}));
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& state) { return state.acquisitionCounters.acquired == 1; }));
    auto frame = fixture.raw.consumeAfter(0);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->value->layout.storage(), core::StorageType::UInt16);
    EXPECT_EQ(frame->value->layout.strideBytes(), 8U);
    EXPECT_EQ(frame->value->metadata.acquisitionSettings.sourceFormat.sampleMaximum, 4095U);
}
TEST(AcquisitionWorker, PreparedModeRejectsRequestedDescriptorAndCompleteRoiBeforeApply) {
    for (int change = 0; change < 4; ++change) {
        SCOPED_TRACE(change);
        Fixture fixture;
        auto offered = capabilities(); offered.pixelFormats.push_back(highDepthConfiguration().pixelFormat);
        offered.roi.maximum.x = 4; offered.roi.maximum.y = 3;
        fixture.script.offeredCapabilities = offered;
        ASSERT_TRUE(fixture.worker.start().hasValue());
        ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
        auto requested = configuration();
        if (change == 0) requested.pixelFormat = highDepthConfiguration().pixelFormat;
        if (change == 1) requested.roi.width = 2;
        if (change == 2) requested.roi.height = 2;
        if (change == 3) requested.roi.x = 1;
        ASSERT_TRUE(fixture.command(ApplyConfiguration{0, requested, 1}, false));
        EXPECT_EQ(fixture.latest->latestError->code, "camera_mode_change_requires_rebinding");
        EXPECT_EQ(fixture.script.count("apply"), 0U);
    }
}
TEST(AcquisitionWorker, PreparedModeRejectsChangedReadbackAndCleansUp) {
    for (int change = 0; change < 4; ++change) {
        SCOPED_TRACE(change);
        Fixture fixture;
        auto offered = capabilities(); offered.pixelFormats.push_back(highDepthConfiguration().pixelFormat);
        offered.roi.maximum.x = 4; offered.roi.maximum.y = 3;
        fixture.script.offeredCapabilities = offered;
        auto actual = configuration();
        if (change == 0) actual.pixelFormat = highDepthConfiguration().pixelFormat;
        if (change == 1) actual.roi.width = 2;
        if (change == 2) actual.roi.height = 2;
        if (change == 3) actual.roi.y = 1;
        fixture.script.readback = actual;
        ASSERT_TRUE(fixture.worker.start().hasValue());
        ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
        ASSERT_TRUE(fixture.command(ApplyConfiguration{0, configuration(), 1}, false));
        EXPECT_EQ(fixture.latest->latestError->code, "camera_mode_change_requires_rebinding");
        EXPECT_FALSE(fixture.latest->appliedConfiguration);
        EXPECT_FALSE(fixture.latest->confirmedRevision);
        EXPECT_EQ(fixture.script.count("apply"), 1U);
        EXPECT_EQ(fixture.script.count("close"), 1U);
        EXPECT_EQ(fixture.script.count("destroy"), 1U);
        EXPECT_EQ(fixture.script.count("start"), 0U);
    }
}
TEST(AcquisitionWorker, PreparedCapabilitiesRequireExactDescriptorBeforeApply) {
    auto prepared = highDepthConfiguration();
    Fixture fixture({}, 24, prepared);
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "camera_format_not_available");
    EXPECT_EQ(fixture.script.count("close"), 1U);
    EXPECT_EQ(fixture.script.count("apply"), 0U);
}
TEST(AcquisitionWorker, UnpreparedStandaloneWorkerBindsFirstSuccessfulActualMode) {
    Fixture fixture({}, 48, std::nullopt);
    auto offered = capabilities(); offered.pixelFormats = {highDepthConfiguration().pixelFormat};
    offered.roi.maximum.x = 4;
    fixture.script.offeredCapabilities = offered;
    auto first = highDepthConfiguration(); first.roi.width = 2;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0, first, 1}));
    auto changed = first; changed.roi.x = 1;
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0, changed, 2}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "camera_mode_change_requires_rebinding");
    EXPECT_EQ(fixture.script.count("apply"), 1U);
}
TEST(AcquisitionWorker, PreparedModePreservesQuantizedExposureGainAndActualFps) {
    Fixture fixture;
    auto actual = configuration();
    actual.requestedFps = 29; actual.exposure.requestedMicroseconds = 101; actual.gain.requestedDb = 1;
    fixture.script.readback = actual;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.command(Connect{{"camera-1"}}));
    ASSERT_TRUE(fixture.command(ApplyConfiguration{0, configuration(), 1}));
    ASSERT_TRUE(fixture.command(ConfirmConfiguration{0, 1}));
    EXPECT_EQ(fixture.latest->appliedConfiguration->actual.requestedFps, 29);
    EXPECT_EQ(fixture.latest->appliedConfiguration->actual.exposure.requestedMicroseconds, 101);
    EXPECT_EQ(fixture.latest->appliedConfiguration->actual.gain.requestedDb, 1);
}

}  // namespace
}  // namespace lumora::application
