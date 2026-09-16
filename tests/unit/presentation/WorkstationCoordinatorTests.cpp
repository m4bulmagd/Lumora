#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <lumora/presentation/CameraActionPolicy.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/core/Clock.hpp>
#include "WorkstationCoordinatorTestHook.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace {
using namespace lumora;
using namespace std::chrono_literals;
using Intent = presentation::CameraStartupIntent;
camera::CameraConfiguration request() {
    return {{"Mono8",0x01080001U,8U,255U,core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant,core::StorageType::UInt8},
        {0,0,8,6},30.0,{camera::ExposureMode::Manual,100.0},
        {camera::GainMode::Manual,0.0},camera::AcquisitionMode::Continuous};
}
camera::sim::SimulatedCameraOptions options() {
    return {{"SIM-LIVE"}, {{request().pixelFormat},{{0,0,1,1},{0,0,8,6},{1,1,1,1}},
        {1,60,1,camera::ControlAccess::WritableStopped},{1,1000,1,camera::ControlAccess::WritableStopped},{camera::ExposureMode::Manual},
        {0,10,1,camera::ControlAccess::WritableStopped},{camera::GainMode::Manual}},camera::sim::SimulationPattern::MovingBar,
        30.0,0x4C554D4FU,camera::sim::SimulationPacingMode::Manual};
}
class MemoryIo final : public configuration::IStartupPreferencesIo {
public:
    std::optional<application::StartupPreferences> record;
    std::mutex mutex;
    std::optional<application::StartupPreferences> saved;
    application::PresetState presetState;
    std::optional<application::PresetState> savedPresets;
    bool failLoad{false};
    bool failSave{false};
    std::function<void()> beforeLoad;
    core::Result<configuration::ApplicationConfiguration> load() override {
        if(beforeLoad) beforeLoad();
        if(failLoad) return core::Result<configuration::ApplicationConfiguration>::failure(
            {core::ErrorCategory::Configuration,"load_failed","Load failed.","",false});
        configuration::ApplicationConfiguration value;
        value.startup=record; value.presets=presetState;
        return core::Result<configuration::ApplicationConfiguration>::success(value);
    }
    core::Result<void> save(const configuration::ApplicationConfiguration& value) override {
        std::lock_guard lock(mutex);saved=value.startup;savedPresets=value.presets;
        if(failSave) return core::Result<void>::failure(
            {core::ErrorCategory::Configuration,"save_failed","Save failed.","",false});
        return core::Result<void>::success();
    }
};
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{false};bool released{false};
    void block() { std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;}); }
    bool wait() { std::unique_lock lock(mutex);return changed.wait_for(lock,2s,[&]{return entered;}); }
    void release() { std::lock_guard lock(mutex);released=true;changed.notify_all(); }
};
struct ReleaseGate { Gate& gate;~ReleaseGate(){gate.release();} };
struct CameraObservation {
    const std::thread::id uiThread{std::this_thread::get_id()};
    std::atomic<bool> wrongThread{false};
    std::atomic<int> starts{0};std::atomic<int> creates{0};std::atomic<int> opens{0};std::atomic<int> destroyed{0};
    std::atomic<bool> failApply{false};
    Gate* discoveryGate{nullptr};Gate* openGate{nullptr};Gate* applyGate{nullptr};bool failOpen{false};
};
class ObservedDevice final : public camera::ICameraDevice {
public:
    ObservedDevice(std::unique_ptr<camera::ICameraDevice> device,CameraObservation& observation)
        :device_(std::move(device)),observation_(observation),owner_(std::this_thread::get_id()) {}
    ~ObservedDevice() override { check();++observation_.destroyed; }
    core::Result<void> open() override {
        check();++observation_.opens;if(observation_.openGate) observation_.openGate->block();
        if(observation_.failOpen) return core::Result<void>::failure({core::ErrorCategory::CameraConnection,"open_failed","Open failed.","",false});
        return device_->open();
    }
    core::Result<camera::CameraCapabilities> capabilities() override { check();return device_->capabilities(); }
    core::Result<camera::CameraConfiguration> readConfiguration() override { check();return device_->readConfiguration(); }
    core::Result<camera::AppliedCameraConfiguration> applyConfiguration(const camera::CameraConfiguration& value) override {
        check();if(observation_.applyGate) observation_.applyGate->block();
        if(observation_.failApply.load()) return core::Result<camera::AppliedCameraConfiguration>::failure(
            {core::ErrorCategory::CameraConfiguration,"apply_failed","Apply failed.","",false});
        return device_->applyConfiguration(value);
    }
    core::Result<void> startStream() override { check();++observation_.starts;return device_->startStream(); }
    core::Result<std::shared_ptr<const core::RawFrame>> retrieve(std::chrono::milliseconds timeout,core::BufferPool& pool,std::stop_token token) override {
        check();return device_->retrieve(timeout,pool,token);
    }
    core::Result<void> stopStream() noexcept override { check();return device_->stopStream(); }
    core::Result<void> close() noexcept override { check();return device_->close(); }
private:
    void check() noexcept { if(std::this_thread::get_id()!=owner_ || owner_==observation_.uiThread) observation_.wrongThread.store(true); }
    std::unique_ptr<camera::ICameraDevice> device_;CameraObservation& observation_;std::thread::id owner_;
};
class ObservedProvider final : public camera::ICameraProvider {
public:
    ObservedProvider(core::IClock& clock,CameraObservation& observation,camera::sim::SimulatedCameraOptions simulation=options())
        :delegate_(std::move(simulation),clock),observation_(observation) {}
    core::Result<std::vector<camera::CameraDescriptor>> discover(std::stop_token token) override {
        if(std::this_thread::get_id()==observation_.uiThread) observation_.wrongThread.store(true);
        if(observation_.discoveryGate) { observation_.discoveryGate->block(); }
        return delegate_.discover(token);
    }
    core::Result<std::unique_ptr<camera::ICameraDevice>> create(const camera::CameraId& id) override {
        if(std::this_thread::get_id()==observation_.uiThread) observation_.wrongThread.store(true);
        ++observation_.creates;auto result=delegate_.create(id);
        if(!result.hasValue()) return result;
        return core::Result<std::unique_ptr<camera::ICameraDevice>>::success(std::make_unique<ObservedDevice>(std::move(result.value()),observation_));
    }
private:camera::sim::SimulatedCameraProvider delegate_;CameraObservation& observation_;
};
application::StartupPreferences savedRecord() {
    return {1,{"SIM-LIVE"},{"Lumora","Generated Camera","SIM-LIVE","Simulator",std::nullopt},
        options().capabilities,request(),request(),true};
}
struct Fixture {
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider{options(), clock};
    application::LivePipeline pipeline;
    configuration::StartupPreferencesService preferences;
    presentation::WorkstationCoordinator coordinator{pipeline, preferences, clock, request()};
    explicit Fixture(std::unique_ptr<MemoryIo> io = std::make_unique<MemoryIo>(), camera::ICameraProvider* alternate = nullptr,
        application::IInstallationProfiles* installation = nullptr)
        : pipeline(alternate ? *alternate : provider, clock, request(), {}, {}, installation), preferences(std::move(io)) {}
    ~Fixture() {
        coordinator.beginShutdown();
        coordinator.completeRendererShutdown();
        preferences.requestStop(); preferences.join();
    }
    bool wait(const std::function<bool()>& condition, bool bind = true) {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        do {
            coordinator.poll();
            if (bind) {
                if (auto handoff = coordinator.pendingContextHandoff())
                    if (!coordinator.completeContextHandoff(handoff->id).hasValue()) return false;
            }
            if (condition()) return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    bool act(Intent intent, bool bind = true) {
        if (!coordinator.dispatch(intent).hasValue()) return false;
        return wait([&] {
            if (intent == Intent::Stop || intent == Intent::Disconnect) clock.advance(34ms);
            return !coordinator.state().ordinaryOperationPending;
        }, bind);
    }
    bool initialize(bool bind = true) {
        if (!preferences.start().hasValue() || !pipeline.start().hasValue() || !coordinator.start().hasValue()) return false;
        return wait([&] { const auto& state = coordinator.state();
            return state.preferencesLoadCompleted && state.cameraStatus &&
                !state.cameraStatus->discoveredDescriptors.empty() && !state.ordinaryOperationPending;
        }, bind);
    }
};

camera::sim::SimulatedCameraOptions negotiatedOptions() {
    auto value=options();
    value.capabilities.pixelFormats.insert(value.capabilities.pixelFormats.begin(),
        {"Mono12",0x01100005U,12U,4095U,core::SourcePacking::Unpacked,
            core::BitAlignment::LeastSignificant,core::StorageType::UInt16});
    value.capabilities.roi.maximum={0,0,16,12};
    return value;
}

TEST(WorkstationCoordinator, MatchingSourceLayoutRetainsConfiguredDefaultsBeforeApply) {
    Fixture f;
    ASSERT_TRUE(f.initialize());
    f.coordinator.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(f.act(Intent::Connect));
    const auto& state=f.coordinator.state();
    ASSERT_TRUE(state.cameraStatus->currentConfiguration);
    ASSERT_TRUE(state.requestedConfiguration);
    EXPECT_EQ(state.cameraStatus->currentConfiguration->exposure.requestedMicroseconds,1.0);
    EXPECT_EQ(state.requestedConfiguration->exposure.requestedMicroseconds,100.0);
    EXPECT_FALSE(state.cameraStatus->appliedConfiguration);
    ASSERT_TRUE(f.act(Intent::Apply));
    ASSERT_TRUE(f.pipeline.snapshot().camera->appliedConfiguration);
    EXPECT_EQ(f.pipeline.snapshot().camera->appliedConfiguration->actual.exposure.requestedMicroseconds,100.0);
}

TEST(WorkstationCoordinator, MatchingSourceLayoutRetainsConfiguredManualModesOverAutomaticReadback) {
    auto simulation=options();
    simulation.capabilities.exposureModes.push_back(camera::ExposureMode::Auto);
    simulation.capabilities.gainModes.push_back(camera::GainMode::Auto);
    core::ManualClock clock;
    CameraObservation observation;
    ObservedProvider provider(clock,observation,std::move(simulation));
    Fixture f(std::make_unique<MemoryIo>(),&provider);
    ASSERT_TRUE(f.initialize());
    f.coordinator.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(f.act(Intent::Connect));
    const auto& state=f.coordinator.state();
    ASSERT_TRUE(state.cameraStatus->currentConfiguration);
    ASSERT_TRUE(state.requestedConfiguration);
    EXPECT_EQ(state.cameraStatus->currentConfiguration->exposure.mode,camera::ExposureMode::Auto);
    EXPECT_EQ(state.cameraStatus->currentConfiguration->gain.mode,camera::GainMode::Auto);
    EXPECT_EQ(state.requestedConfiguration->exposure.mode,camera::ExposureMode::Manual);
    EXPECT_EQ(state.requestedConfiguration->exposure.requestedMicroseconds,100.0);
    EXPECT_EQ(state.requestedConfiguration->gain.mode,camera::GainMode::Manual);
    EXPECT_EQ(state.requestedConfiguration->gain.requestedDb,0.0);
    EXPECT_EQ(observation.starts.load(),0);
}

TEST(WorkstationCoordinator, ConfiguredManualModeRequiresAbsentReadOnlyMeasurementBeforeApply) {
    auto simulation=options();
    simulation.capabilities.exposureModes.push_back(camera::ExposureMode::Auto);
    simulation.capabilities.exposure.access=camera::ControlAccess::ReadOnly;
    core::ManualClock clock;
    CameraObservation observation;
    ObservedProvider provider(clock,observation,std::move(simulation));
    Fixture f(std::make_unique<MemoryIo>(),&provider);
    ASSERT_TRUE(f.initialize());
    f.coordinator.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(f.act(Intent::Connect));
    const auto& state=f.coordinator.state();
    ASSERT_TRUE(state.cameraStatus->currentConfiguration);
    ASSERT_TRUE(state.requestedConfiguration);
    EXPECT_EQ(state.cameraStatus->currentConfiguration->exposure.mode,camera::ExposureMode::Auto);
    EXPECT_FALSE(state.cameraStatus->currentConfiguration->exposure.requestedMicroseconds);
    EXPECT_EQ(state.requestedConfiguration->exposure.mode,camera::ExposureMode::Manual);
    EXPECT_FALSE(state.requestedConfiguration->exposure.requestedMicroseconds);
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Apply).hasValue());
    EXPECT_EQ(f.pipeline.snapshot().camera->requestedRevision,0U);
    EXPECT_EQ(observation.starts.load(),0);
}

TEST(WorkstationCoordinator, NewSourceDefaultsUseActualReadbackWithoutStartingCapture) {
    core::ManualClock clock;
    CameraObservation observation;
    ObservedProvider provider(clock,observation,negotiatedOptions());
    Fixture f(std::make_unique<MemoryIo>(),&provider);
    ASSERT_TRUE(f.initialize());
    f.coordinator.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(f.act(Intent::Connect));
    const auto& state=f.coordinator.state();
    ASSERT_TRUE(state.cameraStatus->currentConfiguration);
    ASSERT_TRUE(state.requestedConfiguration);
    EXPECT_EQ(state.requestedConfiguration->pixelFormat.canonicalName,"Mono12");
    EXPECT_EQ(state.requestedConfiguration->roi.width,16U);
    EXPECT_EQ(state.requestedConfiguration->roi.height,12U);
    EXPECT_EQ(observation.starts.load(),0);
    EXPECT_FALSE(state.cameraStatus->appliedConfiguration);
    ASSERT_TRUE(f.act(Intent::Apply));
    EXPECT_EQ(f.pipeline.snapshot().camera->appliedConfiguration->actual.pixelFormat.canonicalName,"Mono12");
}

TEST(WorkstationCoordinator, MatchingSavedProfileTakesPrecedenceOverConnectedReadback) {
    core::ManualClock clock;
    CameraObservation observation;
    ObservedProvider provider(clock,observation,negotiatedOptions());
    auto io=std::make_unique<MemoryIo>();
    io->record=savedRecord();
    io->record->confirmedCapabilities=negotiatedOptions().capabilities;
    io->record->requested.exposure.requestedMicroseconds=200.0;
    io->record->lastApplied=io->record->requested;
    Fixture f(std::move(io),&provider);
    ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->state==application::CameraSessionState::ConnectedIdle;}));
    const auto& state=f.coordinator.state();
    ASSERT_TRUE(state.cameraStatus->currentConfiguration);
    ASSERT_TRUE(state.requestedConfiguration);
    EXPECT_EQ(state.cameraStatus->currentConfiguration->pixelFormat.canonicalName,"Mono12");
    EXPECT_EQ(state.requestedConfiguration->pixelFormat.canonicalName,"Mono8");
    EXPECT_EQ(state.requestedConfiguration->roi.width,8U);
    EXPECT_EQ(state.requestedConfiguration->exposure.requestedMicroseconds,200.0);
    EXPECT_EQ(observation.starts.load(),0);
}

TEST(WorkstationCoordinator, ChangedCapabilitiesUseConnectedReadbackInsteadOfSavedDefaults) {
    core::ManualClock clock;
    CameraObservation observation;
    ObservedProvider provider(clock,observation,negotiatedOptions());
    auto io=std::make_unique<MemoryIo>();io->record=savedRecord();
    Fixture f(std::move(io),&provider);
    ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->state==application::CameraSessionState::ConnectedIdle;}));
    ASSERT_TRUE(f.coordinator.state().requestedConfiguration);
    EXPECT_EQ(f.coordinator.state().requestedConfiguration->pixelFormat.canonicalName,"Mono12");
    EXPECT_EQ(f.coordinator.state().requestedConfiguration->roi.width,16U);
    EXPECT_FALSE(f.coordinator.state().resumeLiveAvailable);
}

struct AcknowledgementRace final {
    application::LivePipeline* pipeline{nullptr};
    std::uint64_t candidateGeneration{0U};
    bool replacementCompleted{false};
};

thread_local AcknowledgementRace* acknowledgementRace{nullptr};

bool waitForPipeline(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        if (condition()) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void replaceContextBeforeAcknowledgement() {
    auto& race = *acknowledgementRace;
    auto& pipeline = *race.pipeline;
    if (!pipeline.acknowledgeContext(race.candidateGeneration).hasValue()) return;
    if (!pipeline.post({9'001U, application::Connect{{"SIM-LIVE"}}}).hasValue()) return;
    if (!waitForPipeline([&] {
            const auto outcome = pipeline.snapshot().ordinaryOutcome;
            return outcome && outcome->requestId == 9'001U && !outcome->error;
        })) return;
    if (!pipeline.post({9'002U, application::Disconnect{}}).hasValue()) return;
    if (!waitForPipeline([&] {
            const auto outcome = pipeline.snapshot().priorityOutcome;
            return outcome && outcome->requestId == 9'002U && !outcome->error;
        })) return;
    if (!pipeline.post({9'003U, application::Connect{{"SIM-LIVE"}}}).hasValue()) return;
    race.replacementCompleted = waitForPipeline([&] {
        const auto snapshot = pipeline.snapshot();
        return snapshot.context &&
            snapshot.context->generation != race.candidateGeneration &&
            snapshot.ordinaryOutcome &&
            snapshot.ordinaryOutcome->requestId == 9'003U &&
            !snapshot.ordinaryOutcome->error;
    });
}

TEST(WorkstationCoordinator, DirectCommandsRejectUnavailableActions) {
    Fixture f; ASSERT_TRUE(f.initialize());
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Connect).hasValue());
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Retry).hasValue());
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Apply).hasValue());
    f.coordinator.selectCamera({"SIM-LIVE"}); ASSERT_TRUE(f.act(Intent::Connect));
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Refresh).hasValue());
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Connect).hasValue());
    ASSERT_TRUE(f.act(Intent::Apply)); ASSERT_TRUE(f.act(Intent::Confirm));
    f.coordinator.selectCamera({"other"});
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Confirm).hasValue());
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Start).hasValue());
}

TEST(WorkstationCoordinator, ContextRequiresExactRetirementCompletionBeforeStart) {
    Fixture f; ASSERT_TRUE(f.initialize(false));
    auto handoff = f.coordinator.pendingContextHandoff(); ASSERT_TRUE(handoff);
    EXPECT_FALSE(f.pipeline.snapshot().contextBound);
    EXPECT_FALSE(f.coordinator.completeContextHandoff(handoff->id + 1).hasValue());
    f.coordinator.selectCamera({"SIM-LIVE"}); ASSERT_TRUE(f.act(Intent::Connect, false));
    ASSERT_TRUE(f.act(Intent::Apply, false)); ASSERT_TRUE(f.act(Intent::Confirm, false));
    EXPECT_FALSE(f.coordinator.state().contextBound);
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Start).hasValue());
    ASSERT_TRUE(f.coordinator.completeContextHandoff(handoff->id).hasValue());
    EXPECT_TRUE(f.pipeline.snapshot().contextBound);
    EXPECT_FALSE(f.coordinator.completeContextHandoff(handoff->id).hasValue());
    ASSERT_TRUE(f.act(Intent::Start));
}

TEST(WorkstationCoordinator, ReplacementRetainsOldContextAndRejectsStaleHandoff) {
    Fixture f; ASSERT_TRUE(f.initialize(false));
    auto initial=f.coordinator.pendingContextHandoff(); ASSERT_TRUE(initial);
    const auto oldId=initial->id;
    std::weak_ptr<application::LiveSessionContext> oldContext=initial->candidate;
    ASSERT_TRUE(f.coordinator.completeContextHandoff(oldId).hasValue()); initial.reset();
    f.coordinator.selectCamera({"SIM-LIVE"}); ASSERT_TRUE(f.act(Intent::Connect));
    ASSERT_TRUE(f.act(Intent::Disconnect));
    ASSERT_TRUE(f.act(Intent::Connect,false));
    auto replacement=f.coordinator.pendingContextHandoff(); ASSERT_TRUE(replacement);
    EXPECT_NE(replacement->id,oldId);
    EXPECT_FALSE(oldContext.expired());
    EXPECT_FALSE(f.coordinator.completeContextHandoff(oldId).hasValue());
    EXPECT_FALSE(f.pipeline.snapshot().contextBound);
    for(int i=0;i<20;++i) f.coordinator.poll();
    EXPECT_FALSE(oldContext.expired());
    ASSERT_TRUE(f.coordinator.completeContextHandoff(replacement->id).hasValue());
    ASSERT_TRUE(f.wait([&]{return oldContext.expired();}));
}

TEST(WorkstationCoordinator,
    AcknowledgementRaceRetainsBoundCandidateUntilLaterRendererRetirement) {
    Fixture f;
    ASSERT_TRUE(f.initialize(false));
    auto handoff = f.coordinator.pendingContextHandoff();
    ASSERT_TRUE(handoff);
    ASSERT_TRUE(handoff->candidate);
    const auto staleHandoffId = handoff->id;
    std::weak_ptr<application::LiveSessionContext> retained = handoff->candidate;
    AcknowledgementRace race{&f.pipeline, handoff->candidate->generation};
    const auto raced = [&] {
        acknowledgementRace = &race;
        presentation::testing::ScopedContextAcknowledgementHook hook(
            replaceContextBeforeAcknowledgement);
        auto result = f.coordinator.completeContextHandoff(staleHandoffId);
        acknowledgementRace = nullptr;
        return result;
    }();
    EXPECT_TRUE(race.replacementCompleted);
    ASSERT_FALSE(raced.hasValue());
    EXPECT_EQ(raced.error().code, "stale_camera_session");
    EXPECT_FALSE(f.coordinator.state().contextBound);
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Start).hasValue());
    handoff.reset();
    EXPECT_FALSE(retained.expired());

    f.coordinator.poll();
    auto replacement = f.coordinator.pendingContextHandoff();
    ASSERT_TRUE(replacement);
    EXPECT_NE(replacement->id, staleHandoffId);
    EXPECT_FALSE(f.coordinator.completeContextHandoff(staleHandoffId).hasValue());
    EXPECT_FALSE(retained.expired());
    ASSERT_TRUE(f.coordinator.completeContextHandoff(replacement->id).hasValue());
    replacement.reset();
    ASSERT_TRUE(f.wait([&] { return retained.expired(); }));
}

TEST(WorkstationCoordinator, ShutdownRetainsContextUntilRendererRetires) {
    Fixture f; ASSERT_TRUE(f.initialize());
    std::weak_ptr<application::LiveSessionContext> retained = f.pipeline.snapshot().context;
    ASSERT_FALSE(retained.expired());
    f.coordinator.beginShutdown();
    EXPECT_FALSE(retained.expired());
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Refresh).hasValue());
    f.coordinator.completeRendererShutdown();
    EXPECT_TRUE(retained.expired());
}

TEST(WorkstationCoordinator, ExplicitResumeWaitsForRendererBindingThenStartsOnce) {
    auto io=std::make_unique<MemoryIo>(); io->record=savedRecord();
    Fixture f(std::move(io)); ASSERT_TRUE(f.initialize(false));
    ASSERT_TRUE(f.wait([&]{return f.coordinator.state().resumeLiveAvailable;},false));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    ASSERT_TRUE(f.coordinator.dispatch(Intent::ResumeLive).hasValue());
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->confirmedRevision.has_value();},false));
    for(int i=0;i<20;++i) f.coordinator.poll();
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_FALSE(f.coordinator.state().contextBound);
    auto handoff=f.coordinator.pendingContextHandoff(); ASSERT_TRUE(handoff);
    ASSERT_TRUE(f.coordinator.completeContextHandoff(handoff->id).hasValue());
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->state==application::CameraSessionState::Streaming;}));
    EXPECT_FALSE(f.coordinator.pendingContextHandoff());
}

TEST(WorkstationCoordinator, PriorityOrSelectionCancelsResumeWaitingForBinding) {
    for(int cancellation=0;cancellation<3;++cancellation) {
        SCOPED_TRACE(cancellation);
        auto io=std::make_unique<MemoryIo>(); io->record=savedRecord();
        Fixture f(std::move(io)); ASSERT_TRUE(f.initialize(false));
        ASSERT_TRUE(f.wait([&]{return f.coordinator.state().resumeLiveAvailable;},false));
        ASSERT_TRUE(f.coordinator.dispatch(Intent::ResumeLive).hasValue());
        ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->confirmedRevision.has_value();},false));
        f.coordinator.poll();
        if(cancellation==2) f.coordinator.selectCamera({"other"});
        else ASSERT_TRUE(f.act(cancellation==0?Intent::Stop:Intent::Disconnect,false));
        ASSERT_TRUE(f.wait([&]{return !f.coordinator.state().ordinaryOperationPending;}));
        for(int i=0;i<20;++i) f.coordinator.poll();
        EXPECT_NE(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
        EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired,0U);
    }
}

TEST(WorkstationCoordinator, PriorityDuplicatesAndDisconnectSupersessionIgnoreLateResumeApply) {
    core::ManualClock clock; CameraObservation observed; Gate gate; observed.applyGate=&gate;
    ObservedProvider provider(clock,observed);
    auto io=std::make_unique<MemoryIo>(); io->record=savedRecord();
    Fixture f(std::move(io),&provider); ReleaseGate release{gate}; ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.coordinator.state().resumeLiveAvailable;}));
    ASSERT_TRUE(f.coordinator.dispatch(Intent::ResumeLive).hasValue()); ASSERT_TRUE(gate.wait());
    ASSERT_TRUE(f.coordinator.dispatch(Intent::Stop).hasValue());
    ASSERT_TRUE(f.coordinator.dispatch(Intent::Stop).hasValue());
    ASSERT_TRUE(f.coordinator.dispatch(Intent::Disconnect).hasValue());
    ASSERT_TRUE(f.coordinator.dispatch(Intent::Disconnect).hasValue());
    ASSERT_TRUE(f.coordinator.dispatch(Intent::Stop).hasValue());
    gate.release();
    ASSERT_TRUE(f.wait([&]{return !f.coordinator.state().ordinaryOperationPending;}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
    EXPECT_EQ(observed.starts.load(),0);
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
}

TEST(WorkstationCoordinator, FailedNewerApplyCannotReuseEarlierConfirmation) {
    core::ManualClock clock; CameraObservation observed; ObservedProvider provider(clock,observed);
    Fixture f(std::make_unique<MemoryIo>(),&provider); ASSERT_TRUE(f.initialize());
    f.coordinator.selectCamera({"SIM-LIVE"}); ASSERT_TRUE(f.act(Intent::Connect));
    ASSERT_TRUE(f.act(Intent::Apply)); ASSERT_TRUE(f.act(Intent::Confirm));
    const auto oldRevision=f.pipeline.snapshot().camera->appliedRevision;
    observed.failApply.store(true);
    auto edited=request(); edited.exposure.requestedMicroseconds=200.0;
    ASSERT_TRUE(f.coordinator.applyCameraSettings(f.pipeline.snapshot().camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
    ASSERT_TRUE(f.wait([&]{return !f.coordinator.state().ordinaryOperationPending;}));
    ASSERT_TRUE(f.pipeline.snapshot().ordinaryOutcome->error);
    EXPECT_GT(f.pipeline.snapshot().camera->requestedRevision,oldRevision);
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Confirm).hasValue());
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Start).hasValue());
}

TEST(WorkstationCoordinator, LegacyFingerprintRestoresReviewableRequestWithoutResumeAuthority) {
    auto io=std::make_unique<MemoryIo>(); io->record=savedRecord();
    io->record->capabilityFingerprintVersion=1;
    io->record->requested.exposure.requestedMicroseconds=200.0;
    io->record->lastApplied=io->record->requested;
    Fixture f(std::move(io)); ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->state==application::CameraSessionState::ConnectedIdle;}));
    EXPECT_FALSE(f.coordinator.state().resumeLiveAvailable);
    EXPECT_FALSE(f.coordinator.dispatch(Intent::ResumeLive).hasValue());
    ASSERT_TRUE(f.coordinator.state().requestedConfiguration);
    EXPECT_EQ(f.coordinator.state().requestedConfiguration->exposure.requestedMicroseconds,200.0);
    EXPECT_FALSE(f.coordinator.dispatch(Intent::Start).hasValue());
    ASSERT_TRUE(f.act(Intent::Apply)); ASSERT_TRUE(f.act(Intent::Confirm)); ASSERT_TRUE(f.act(Intent::Start));
}

TEST(WorkstationCoordinator, ChangedResumeReadbackRequiresExplicitReview) {
    auto io=std::make_unique<MemoryIo>(); io->record=savedRecord();
    io->record->lastApplied.exposure.requestedMicroseconds=101.0;
    Fixture f(std::move(io)); ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.coordinator.state().resumeLiveAvailable;}));
    ASSERT_TRUE(f.act(Intent::ResumeLive));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
    ASSERT_TRUE(f.coordinator.state().startupWarning);
    EXPECT_EQ(f.coordinator.state().startupWarning->code,"startup_readback_changed");
}

TEST(WorkstationCoordinator, ProcessingCompletionPersistsMatchingRevisionOnlyOnce) {
    auto io=std::make_unique<MemoryIo>(); auto* memory=io.get();
    Fixture f(std::move(io)); ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision.has_value();}));
    auto* model=f.coordinator.processingControls(); ASSERT_TRUE(model);
    const auto first=f.preferences.latestStatus()->latestSavedPresetRevision;
    ASSERT_TRUE(model->acknowledged());
    for(int i=0;i<20;++i) f.coordinator.poll();
    EXPECT_EQ(f.preferences.latestStatus()->latestAttemptedPresetSaveRevision,first);
    auto edited=model->draft().activePipeline;
    edited.stages.back().enabled=!edited.stages.back().enabled;
    ASSERT_TRUE(model->edit(edited).hasValue());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision>first;}));
    const auto second=f.preferences.latestStatus()->latestSavedPresetRevision;
    for(int i=0;i<20;++i) f.coordinator.poll();
    EXPECT_EQ(f.preferences.latestStatus()->latestAttemptedPresetSaveRevision,second);
    std::lock_guard lock(memory->mutex); ASSERT_TRUE(memory->savedPresets);
    EXPECT_EQ(memory->savedPresets->activePipeline.version.configurationRevision,
        model->acknowledged()->activePipeline.version.configurationRevision);
}

TEST(WorkstationCoordinator, FailedProcessingActivationDoesNotPersistOrOverwriteAcknowledgedState) {
    Fixture f; ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision.has_value();}));
    const auto saved=f.preferences.latestStatus()->latestSavedPresetRevision;
    auto* model=f.coordinator.processingControls(); ASSERT_TRUE(model);
    const auto acceptedRevision=model->acknowledged()->activePipeline.version.configurationRevision;
    ASSERT_TRUE(model->selectPreset({"standard"}).hasValue());
    ASSERT_TRUE(f.wait([&]{return model->error().has_value() && !model->pending();}));
    EXPECT_EQ(f.pipeline.snapshot().processingConfigurationOutcome->error->code,"clahe_image_too_small");
    EXPECT_EQ(model->acknowledged()->activePipeline.version.configurationRevision,acceptedRevision);
    EXPECT_EQ(f.preferences.latestStatus()->latestAttemptedPresetSaveRevision,saved);
    for(int i=0;i<20;++i) f.coordinator.poll();
    EXPECT_EQ(f.preferences.latestStatus()->latestAttemptedPresetSaveRevision,saved);
}

class OperatorInstallation final : public application::IInstallationProfiles {
public:
    OperatorInstallation() {
        auto value=std::make_shared<application::InstallationProfilesSnapshot>();
        value->loadCompleted=true;
        value->policy=application::InstallationProfilePolicy::SimulatorIdentityFallback;
        status=std::move(value);
    }
    std::shared_ptr<const application::InstallationProfilesSnapshot> latestStatus() const override { return status; }
    core::Result<void> postSave(std::uint64_t,application::InstallationCameraProfile,bool) override {
        ++saveCalls;
        return core::Result<void>::success();
    }
    std::shared_ptr<const application::InstallationProfilesSnapshot> status;
    std::atomic<int> saveCalls{0};
};
TEST(WorkstationCoordinator, InstallationOperatorModeAndStaleDialogSourceRejectBeforeSaving) {
    OperatorInstallation installation;
    Fixture f(std::make_unique<MemoryIo>(),nullptr,&installation); ASSERT_TRUE(f.initialize());
    f.coordinator.selectCamera({"SIM-LIVE"}); ASSERT_TRUE(f.act(Intent::Connect));
    const auto generation=f.pipeline.snapshot().camera->sessionGeneration;
    const core::Orientation orientation{true,false,core::Rotation::Degrees0};
    EXPECT_FALSE(f.coordinator.saveInstallationProfile(generation,{"SIM-LIVE"},orientation,true,false).hasValue());
    EXPECT_EQ(installation.saveCalls.load(),0);
    EXPECT_FALSE(f.coordinator.state().installationProfilePending);
    EXPECT_FALSE(f.coordinator.beginCameraSettingsEdit(generation+1,{"SIM-LIVE"}).hasValue());
    EXPECT_FALSE(f.coordinator.applyCameraSettings(generation+1,{"SIM-LIVE"},request()).hasValue());
    EXPECT_FALSE(f.coordinator.applyCameraSettings(generation,{"other"},request()).hasValue());
}
} // namespace
