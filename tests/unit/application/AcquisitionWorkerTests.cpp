#include <lumora/application/AcquisitionWorker.hpp>

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
enum class Grab { Frame, Timeout, Invalid, Exhausted, Removed, Fatal, Null, WrongRoi, Throw, FrameAfterStop };

struct Script final {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::vector<std::pair<std::string, std::thread::id>> calls;
    std::deque<Grab> grabs;
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
    void push(Grab grab) {
        std::lock_guard lock(mutex);
        grabs.push_back(grab);
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
    Device(Script& script, core::IClock& clock) : script_(script), clock_(clock) { script_.record("construct"); }
    ~Device() override { script_.record("destroy"); }
    Result<void> open() override {
        script_.record("open");
        return script_.failOpen ? Result<void>::failure(error(ErrorCategory::CameraConnection, "open_failed")) : Result<void>::success();
    }
    Result<camera::CameraCapabilities> capabilities() override {
        script_.record("capabilities");
        if (script_.failCapabilities) { return Result<camera::CameraCapabilities>::failure(error(ErrorCategory::CameraConfiguration, "capabilities_failed")); }
        return Result<camera::CameraCapabilities>::success(application::capabilities());
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
        script_.record("retrieve");
        EXPECT_EQ(timeout, 250ms);
        Grab grab{Grab::Timeout};
        {
            std::unique_lock lock(script_.mutex);
            script_.changed.wait_for(lock, token, timeout, [&] { return !script_.grabs.empty() || script_.released; });
            if (token.stop_requested()) { return Result<std::shared_ptr<const core::RawFrame>>::failure(error(ErrorCategory::Cancelled, "cancelled")); }
            if (!script_.grabs.empty()) { grab = script_.grabs.front(); script_.grabs.pop_front(); }
        }
        if (grab == Grab::FrameAfterStop) {
            script_.record("await-cancellation");
            std::unique_lock lock(script_.mutex);
            script_.changed.wait(lock, token, [] { return false; });
        }
        switch (grab) {
        case Grab::Timeout: return failure(ErrorCategory::Acquisition, "acquisition_timeout");
        case Grab::Invalid: return failure(ErrorCategory::InvalidFrame, "invalid_frame");
        case Grab::Exhausted: return failure(ErrorCategory::ResourceExhaustion, "buffer_pool_exhausted");
        case Grab::Removed: return failure(ErrorCategory::CameraConnection, "camera_removed", true);
        case Grab::Fatal: return failure(ErrorCategory::Acquisition, "terminal_grab_failure");
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
        auto layout = core::ImageLayout::create(roi.width, roi.height, roi.width,
            core::StorageType::UInt8, static_cast<std::size_t>(roi.width) * roi.height);
        auto settings = core::AcquisitionSettingsSnapshot::create({"Test", "Camera", "1", "virtual", {}},
            configuration_.pixelFormat, roi, 30.0, 30.0, 100.0, 0.0);
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
    core::IClock& clock_;
    camera::CameraConfiguration configuration_{application::configuration()};
    std::uint64_t frameId_{0U};
};

class Provider final : public camera::ICameraProvider {
public:
    Provider(Script& script, core::IClock& clock) : script_(script), clock_(clock) {}
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
    core::IClock& clock_;
};

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
    explicit Fixture(CameraStatusSnapshot initial = {}, std::size_t bytesPerBuffer = 48U)
        : pool(core::BufferPool::create(10U, bytesPerBuffer).value()),
          worker(provider, mailbox, *pool, raw, clock, status, std::move(initial)) {}
    ~Fixture() { script.release(); worker.requestStop(); worker.join(); }
    bool await(const std::function<bool(const CameraStatusSnapshot&)>& predicate) {
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

TEST(AcquisitionWorker, TimeoutThresholdAndValidResetClassifyDropsWithoutResettingStreak) {
    Fixture fixture;
    ASSERT_TRUE(fixture.ready());
    fixture.script.push(Grab::Timeout);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.consecutiveTimeouts == 1U; }));
    EXPECT_EQ(fixture.latest->state, S::Streaming);
    fixture.script.push(Grab::Invalid);
    fixture.script.push(Grab::Null);
    fixture.script.push(Grab::WrongRoi);
    fixture.script.push(Grab::Exhausted);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.acquisitionCounters.droppedNoRawBuffer == 1U; }));
    EXPECT_EQ(fixture.latest->consecutiveTimeouts, 1U);
    EXPECT_EQ(fixture.latest->acquisitionCounters.droppedInvalidFrame, 3U);
    fixture.script.push(Grab::Timeout);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.consecutiveTimeouts == 2U; }));
    EXPECT_EQ(fixture.latest->state, S::Streaming);
    fixture.script.push(Grab::Frame);
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.acquisitionCounters.acquired == 1U; }));
    EXPECT_EQ(fixture.latest->consecutiveTimeouts, 0U);
    for (int i = 0; i < 3; ++i) { fixture.script.push(Grab::Timeout); }
    ASSERT_TRUE(fixture.await([](const auto& s) { return s.state == S::Reconnecting; }));
    EXPECT_EQ(fixture.latest->acquisitionCounters.timeouts, 5U);
    EXPECT_TRUE(fixture.latest->sourceReplacementRequired);
    EXPECT_EQ(fixture.latest->desiredIdentity->value, "camera-1");
    EXPECT_FALSE(fixture.latest->actualIdentity);
    EXPECT_EQ(fixture.script.count("destroy"), 1U);
    ASSERT_TRUE(fixture.command(Retry{}, false));
    EXPECT_EQ(fixture.latest->latestError->code, "camera_context_replacement_required");
    EXPECT_EQ(fixture.script.count("create"), 1U);
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
    Fixture fixture({}, 12U);
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

}  // namespace
}  // namespace lumora::application
