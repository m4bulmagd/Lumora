#include "ViewportTestSupport.hpp"
#include "SimulatorComposition.hpp"
#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/WorkstationController.hpp>
#include <lumora/ui/WorkstationView.hpp>
#include <lumora/processing/Mono8PassThroughProcessor.hpp>
#include <lumora/camera/sim/FaultScript.hpp>
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QPushButton>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace {
using namespace lumora;
using namespace std::chrono_literals;
using Intent = ui::CameraStartupIntent;
camera::CameraConfiguration request() {
    return {{"Mono8",0x01080001U,8U,255U,core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant,core::StorageType::UInt8},
        {0,0,8,6},30.0,{camera::ExposureMode::Manual,100.0},
        {camera::GainMode::Manual,0.0},camera::AcquisitionMode::Continuous};
}
camera::sim::SimulatedCameraOptions options() {
    return {{"SIM-LIVE"}, {{request().pixelFormat},{{0,0,1,1},{0,0,8,6},{1,1,1,1}},
        {1,60,1,false},{1,1000,1,false},{camera::ExposureMode::Manual},
        {0,10,1,false},{camera::GainMode::Manual}},camera::sim::SimulationPattern::MovingBar,
        30.0,0x4C554D4FU,camera::sim::SimulationPacingMode::Manual};
}
class MemoryIo final : public configuration::IStartupPreferencesIo {
public:
    std::optional<application::StartupPreferences> record;
    std::mutex mutex;
    std::optional<application::StartupPreferences> saved;
    bool failLoad{false};
    bool failSave{false};
    core::Result<configuration::ApplicationConfiguration> load() override {
        if(failLoad) return core::Result<configuration::ApplicationConfiguration>::failure(
            {core::ErrorCategory::Configuration,"load_failed","Load failed.","",false});
        configuration::ApplicationConfiguration value;
        value.startup=record;
        return core::Result<configuration::ApplicationConfiguration>::success(value);
    }
    core::Result<void> save(const configuration::ApplicationConfiguration& value) override {
        std::lock_guard lock(mutex);saved=value.startup;
        if(failSave) return core::Result<void>::failure(
            {core::ErrorCategory::Configuration,"save_failed","Save failed.","",false});
        return core::Result<void>::success();
    }
};
struct Fixture {
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider;
    application::LivePipeline pipeline;
    configuration::StartupPreferencesService preferences;
    ui::WorkstationView view;
    ui::CameraStartupPanel panel;
    ui::WorkstationController controller{pipeline,preferences,view,panel,clock,request()};
    explicit Fixture(std::unique_ptr<MemoryIo> io=std::make_unique<MemoryIo>(),
        camera::sim::SimulatedCameraOptions simulation=options(), application::LivePipeline::ProcessorFactory factory={},
        camera::CameraConfiguration fixed=request(),camera::ICameraProvider* alternate=nullptr,core::IClock* cameraClock=nullptr)
        :provider(std::move(simulation),cameraClock ? *cameraClock : clock),
        pipeline(alternate ? *alternate : provider,clock,fixed,std::move(factory)),preferences(std::move(io)),
        controller(pipeline,preferences,view,panel,clock,std::move(fixed)) {
        view.resize(900,600);
    }
    ~Fixture() { controller.shutdown(); preferences.requestStop(); preferences.join(); }
    bool wait(const std::function<bool()>& condition) {
        const auto deadline=std::chrono::steady_clock::now()+2s;
        do { controller.poll(); QCoreApplication::processEvents();
            if(condition()) return true;
            std::this_thread::yield();
        } while(std::chrono::steady_clock::now()<deadline);
        return false;
    }
    bool act(Intent intent) {
        if(!controller.dispatch(intent).hasValue()) return false;
        if(intent==Intent::Stop || intent==Intent::Disconnect) clock.advance(34ms);
        return wait([&]{
            const bool pending=panel.presentation().ordinaryOperationPending;
            if(pending && (intent==Intent::Stop || intent==Intent::Disconnect)) clock.advance(34ms);
            return !pending;
        });
    }
    bool begin() {
        if(!initialize()) return false;
        controller.selectCamera({"SIM-LIVE"});
        return act(Intent::Connect)&&act(Intent::Apply)&&act(Intent::Confirm)&&act(Intent::Start);
    }
    bool initialize() {
        if(!preferences.start().hasValue() || !pipeline.start().hasValue() || !controller.start().hasValue()) return false;
        if(!wait([&]{return panel.presentation().cameraStatus &&
            !panel.presentation().cameraStatus->discoveredDescriptors.empty();})) return false;
        return true;
    }
    std::shared_ptr<const core::FrameBundle> latest() {
        auto context=pipeline.snapshot().context;
        if(!context) return {};
        auto value=context->bundleSlot.consumeAfter(0);
        return value ? value->value : nullptr;
    }
    bool next() {
        auto previous=latest();
        clock.advance(34ms);
        return wait([&]{auto value=latest();return value && (!previous || value->sourceFrameId()>previous->sourceFrameId())
            && value->raw->metadata.hostReceiptTime==clock.steadyNow();});
    }
    bool paint() {
        if(!controller.presenter()) return false;
        controller.presenter()->refresh();
        (void)test::paintWidget(view);
        return controller.presenter()->presentedBundle()!=nullptr;
    }
};
TEST(LivePipeline, PauseKeepsAcquiringAndResumeJumpsToNewest) {
    Fixture f;
    ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.next()); ASSERT_TRUE(f.paint());
    auto shown=f.controller.presenter()->presentedBundle();
    const auto acquired=f.pipeline.snapshot().camera->acquisitionCounters.acquired;
    f.controller.presenter()->pause();
    for(int i=0;i<10;++i) ASSERT_TRUE(f.next());
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->acquisitionCounters.acquired>acquired;}));
    EXPECT_EQ(f.controller.presenter()->presentedBundle(),shown);
    auto newest=f.latest(); ASSERT_NE(newest,nullptr);
    f.controller.presenter()->resume(); ASSERT_TRUE(f.paint());
    EXPECT_EQ(f.controller.presenter()->presentedBundle()->sourceFrameId(),newest->sourceFrameId());
}
TEST(LivePipeline, OneHundredBoundedLifecycleCyclesReleasePoolsAndResetSessions) {
    Fixture f;ASSERT_TRUE(f.initialize());
    for(int cycle=0;cycle<100;++cycle) {
        SCOPED_TRACE(cycle);
        f.controller.selectCamera({"SIM-LIVE"});
        ASSERT_TRUE(f.act(Intent::Connect));
        ASSERT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
        ASSERT_TRUE(f.act(Intent::Apply));ASSERT_TRUE(f.act(Intent::Confirm));ASSERT_TRUE(f.act(Intent::Start));
        ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
        auto context=f.pipeline.snapshot().context;
        auto raw=context->rawPool;auto display=context->displayPool;auto u16=context->processingPool;
        auto id=f.controller.presenter()->presentedBundle()->sourceFrameId();
        f.controller.presenter()->pause();ASSERT_TRUE(f.next());
        f.controller.presenter()->resume();ASSERT_TRUE(f.paint());
        EXPECT_GT(f.controller.presenter()->presentedBundle()->sourceFrameId(),id);
        ASSERT_TRUE(f.act(Intent::Stop));ASSERT_TRUE(f.act(Intent::Start));ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
        EXPECT_GT(f.controller.presenter()->presentedBundle()->sourceFrameId(),id);
        ASSERT_TRUE(f.act(Intent::Disconnect));
        EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
        context.reset();
        if(cycle==99) f.controller.shutdown();
        else {
            // The next explicit session binds a fresh exchange and releases the old contextual image.
            ASSERT_TRUE(f.act(Intent::Connect));
        }
        EXPECT_EQ(raw->stats().inUse,0U);EXPECT_EQ(display->stats().inUse,0U);EXPECT_EQ(u16->stats().inUse,0U);
    }
}
TEST(LivePipeline, ConfirmationSubmitsPreferencesInBackground) {
    Fixture f;ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedRevision.has_value();}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
}
TEST(LivePipeline, InvalidFixedModeFailsBeforeCreatingContext) {
    core::ManualClock clock;camera::sim::SimulatedCameraProvider provider(options(),clock);
    auto invalid=request();invalid.pixelFormat.validBits=7;
    application::LivePipeline pipeline(provider,clock,invalid);
    auto result=pipeline.start();
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(pipeline.snapshot().context,nullptr);
}
application::StartupPreferences savedRecord() {
    return {1,{"SIM-LIVE"},{"Lumora","Generated Camera","SIM-LIVE","Simulator",std::nullopt},
        options().capabilities,request(),request(),true};
}
TEST(LivePipeline, MatchingSavedRecordProbesIdleAndResumesOnlyOnOperatorIntent) {
    auto io=std::make_unique<MemoryIo>();io->record=savedRecord();
    Fixture f(std::move(io));ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable;}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired,0U);
    ASSERT_TRUE(f.act(Intent::ResumeLive));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
    ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
}
TEST(LivePipeline, ChangedReadbackCancelsResumeAndExplainsReview) {
    auto io=std::make_unique<MemoryIo>();io->record=savedRecord();
    io->record->lastApplied.exposure.requestedMicroseconds=101.0;
    Fixture f(std::move(io));ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable;}));
    ASSERT_TRUE(f.act(Intent::ResumeLive));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision.has_value());
    ASSERT_TRUE(f.panel.presentation().startupWarning.has_value());
    EXPECT_EQ(f.panel.presentation().startupWarning->code,"startup_readback_changed");
    ASSERT_TRUE(f.act(Intent::Confirm));ASSERT_TRUE(f.act(Intent::Start));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{false};bool released{false};
    void block() { std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;}); }
    bool wait() { std::unique_lock lock(mutex);return changed.wait_for(lock,2s,[&]{return entered;}); }
    void release() { std::lock_guard lock(mutex);released=true;changed.notify_all(); }
};
struct ReleaseGate { Gate& gate;~ReleaseGate(){gate.release();} };
TEST(LivePipeline, DuplicatePendingDisconnectRetainsItsCompletionCorrelation) {
    Gate gate;
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool& pool) {
        gate.block();
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(
            std::make_unique<processing::Mono8PassThroughProcessor>(pool));
    });
    ReleaseGate release{gate};
    ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(gate.wait());ASSERT_TRUE(f.controller.start().hasValue());
    ASSERT_TRUE(f.controller.dispatch(Intent::Disconnect).hasValue());
    ASSERT_TRUE(f.controller.dispatch(Intent::Disconnect).hasValue());
    gate.release();
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
}
bool waitUntil(const std::function<bool()>& condition) {
    const auto deadline=std::chrono::steady_clock::now()+2s;
    do { if(condition()) return true;std::this_thread::yield(); } while(std::chrono::steady_clock::now()<deadline);
    return false;
}
TEST(LivePipeline, ReplacementWaitsForTheOutstandingContextAcknowledgement) {
    Fixture f;ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().context!=nullptr;}));
    auto context=f.pipeline.snapshot().context;
    ASSERT_TRUE(f.pipeline.post({1,application::Connect{{"SIM-LIVE"}}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().ordinaryOutcome.has_value();}));
    ASSERT_TRUE(f.pipeline.post({2,application::Disconnect{}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().priorityOutcome.has_value();}));
    auto second=f.pipeline.post({3,application::Connect{{"SIM-LIVE"}}});
    EXPECT_FALSE(second.hasValue());
    EXPECT_EQ(f.pipeline.snapshot().context,context);
    ASSERT_TRUE(f.pipeline.acknowledgeContext(context->generation).hasValue());
    ASSERT_TRUE(f.pipeline.post({4,application::Connect{{"SIM-LIVE"}}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().ordinaryOutcome->requestId==4;}));
    EXPECT_GT(f.pipeline.snapshot().context->generation,context->generation);
    EXPECT_FALSE(f.pipeline.acknowledgeContext(context->generation).hasValue());
}
class DelayedFailure final : public processing::IFrameProcessor {
public:
    explicit DelayedFailure(Gate& gate):gate_(gate) {}
    core::Result<std::shared_ptr<const core::FrameBundle>> process(std::shared_ptr<const core::RawFrame>) override {
        gate_.block();return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
            {core::ErrorCategory::Processing,"delayed_processing_failure","Processing failed.","",false});
    }
private:Gate& gate_;
};
TEST(LivePipeline, ProcessingDiagnosticsAdvanceAfterTheFinalCameraStatus) {
    Gate gate;
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&) {
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::make_unique<DelayedFailure>(gate));
    });
    ReleaseGate release{gate};ASSERT_TRUE(f.begin());ASSERT_TRUE(gate.wait());
    ASSERT_TRUE(f.act(Intent::Stop));
    gate.release();
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().processing.processingErrors>0;}));
    ASSERT_TRUE(f.pipeline.snapshot().processing.currentError.has_value());
    EXPECT_EQ(f.pipeline.snapshot().processing.currentError->code,"delayed_processing_failure");
}
TEST(LivePipeline, ProcessorConstructionFailureCancelsPendingStartupAndClosesAdmission) {
    Gate gate;
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&) {
        gate.block();return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(
            {core::ErrorCategory::Processing,"factory_failed","Processor unavailable.","",false});
    });
    ReleaseGate release{gate};
    ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(gate.wait());ASSERT_TRUE(f.controller.start().hasValue());gate.release();
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().startupWarning.has_value();}));
    EXPECT_FALSE(f.panel.presentation().ordinaryOperationPending);
    EXPECT_FALSE(f.panel.presentation().controlsEnabled);
    EXPECT_FALSE(f.pipeline.post({100,application::Discover{}}).hasValue());
}
TEST(LivePipeline, ShippingCompositionProducesTheApprovedFullRangeOriginal) {
    Fixture f(std::make_unique<MemoryIo>(),app::simulatorOptions(),{},app::simulatorConfiguration());
    ASSERT_TRUE(f.begin());ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
    auto bundle=f.controller.presenter()->presentedBundle();
    EXPECT_EQ(bundle->raw->layout.width(),640U);EXPECT_EQ(bundle->raw->layout.height(),480U);
    EXPECT_DOUBLE_EQ(bundle->raw->metadata.acquisitionSettings.actualFps,30.0);
    EXPECT_EQ(bundle->raw->metadata.acquisitionSettings.sourceFormat.sampleMaximum,255U);
    auto context=f.pipeline.snapshot().context;
    EXPECT_EQ(context->rawPool->stats().capacity,10U);EXPECT_EQ(context->processingPool->stats().capacity,9U);
    EXPECT_EQ(context->displayPool->stats().capacity,16U);
    EXPECT_EQ(context->rawPool->stats().bytesPerBuffer,307200U);
    EXPECT_EQ(context->processingPool->stats().bytesPerBuffer,614400U);
}
TEST(LivePipeline, RefreshAfterDisconnectBindsWaitingSourceWithoutReconnecting) {
    auto io=std::make_unique<MemoryIo>();io->record=savedRecord();Fixture f(std::move(io));
    ASSERT_TRUE(f.initialize());ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable;}));
    ASSERT_TRUE(f.act(Intent::ResumeLive));ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
    const auto generation=f.pipeline.snapshot().context->generation;
    ASSERT_TRUE(f.act(Intent::Disconnect));ASSERT_TRUE(f.act(Intent::Refresh));
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().context->generation>generation;}));
    EXPECT_EQ(f.controller.presenter()->presentedBundle(),nullptr);
    EXPECT_EQ(f.view.status().freshness,ui::FrameFreshness::WaitingForFrame);
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
}
class BlockingDiscovery final : public camera::ICameraProvider {
public:
    BlockingDiscovery(core::IClock& clock,Gate& gate):delegate_(options(),clock),gate_(gate) {}
    core::Result<std::vector<camera::CameraDescriptor>> discover(std::stop_token token) override {
        gate_.block();return delegate_.discover(token);
    }
    core::Result<std::unique_ptr<camera::ICameraDevice>> create(const camera::CameraId& id) override { return delegate_.create(id); }
private:camera::sim::SimulatedCameraProvider delegate_;Gate& gate_;
};
TEST(LivePipeline, DisconnectButtonCancelsDiscoveryAndSuppressesSavedProbe) {
    core::ManualClock sourceClock;Gate gate;BlockingDiscovery provider(sourceClock,gate);
    auto io=std::make_unique<MemoryIo>();io->record=savedRecord();
    Fixture f(std::move(io),options(),{},request(),&provider);ReleaseGate release{gate};
    ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(f.controller.start().hasValue());
    auto* disconnect=f.panel.findChild<QPushButton*>("disconnectCameraButton");
    EXPECT_TRUE(disconnect->isEnabled());
    ASSERT_TRUE(gate.wait());f.controller.poll();
    EXPECT_TRUE(disconnect->isEnabled());disconnect->click();gate.release();
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted && !f.panel.presentation().ordinaryOperationPending;}));
    ASSERT_NE(f.pipeline.snapshot().camera,nullptr);
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
    EXPECT_FALSE(f.pipeline.snapshot().camera->desiredIdentity.has_value());
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
}
class StallProcessor final : public processing::IFrameProcessor {
public:
    StallProcessor(core::BufferPool& pool,Gate& gate,std::atomic<bool>& stall):delegate_(pool),gate_(gate),stall_(stall) {}
    core::Result<std::shared_ptr<const core::FrameBundle>> process(std::shared_ptr<const core::RawFrame> raw) override {
        if(stall_.load()) { gate_.block(); }
        return delegate_.process(std::move(raw));
    }
private:processing::Mono8PassThroughProcessor delegate_;Gate& gate_;std::atomic<bool>& stall_;
};
TEST(LivePipeline, IndependentCameraProcessingAndPresentationStallsUseCompletedPaintDeadline) {
    for(int boundary=0;boundary<3;++boundary) {
        SCOPED_TRACE(boundary);core::ManualClock sourceClock;Gate gate;std::atomic<bool> stall{false};
        Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool& pool) {
            return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::make_unique<StallProcessor>(pool,gate,stall));
        },request(),nullptr,boundary==0 ? &sourceClock : nullptr);
        ReleaseGate release{gate};ASSERT_TRUE(f.begin());
        sourceClock.advance(34ms);ASSERT_TRUE(f.next());
        ASSERT_TRUE(f.wait([&]{auto frame=f.latest();return frame && frame->raw->metadata.hostReceiptTime==f.clock.steadyNow();}));
        ASSERT_TRUE(f.paint());
        auto contextual=f.controller.presenter()->presentedBundle();const auto paintedAt=f.clock.steadyNow();
        const auto acquired=f.pipeline.snapshot().camera->acquisitionCounters.acquired;
        if(boundary==1) {
            stall.store(true);f.clock.advance(34ms);ASSERT_TRUE(gate.wait());
            // A held processing call must not stop the UI event loop or camera.
            const auto until=std::chrono::steady_clock::now()+100ms;
            std::size_t polls=0;
            while(std::chrono::steady_clock::now()<until) { f.controller.poll();QCoreApplication::processEvents();++polls; }
            EXPECT_GT(polls,1U);
        } else if(boundary==2) { ASSERT_TRUE(f.next()); }
        f.clock.advance(499ms-(f.clock.steadyNow()-paintedAt));f.controller.poll();
        EXPECT_EQ(f.view.status().freshness,ui::FrameFreshness::Current);
        f.clock.advance(1ms);f.controller.poll();
        EXPECT_EQ(f.view.status().freshness,ui::FrameFreshness::Stale);
        EXPECT_EQ(f.controller.presenter()->presentedBundle(),contextual);
        if(boundary==0) EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired,acquired);
        else ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->acquisitionCounters.acquired>acquired;}));
        stall.store(false);gate.release();
        if(boundary==0) sourceClock.advance(f.clock.steadyNow()-sourceClock.steadyNow()+34ms);
        ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
        EXPECT_EQ(f.view.status().freshness,ui::FrameFreshness::Current);
        auto raw=f.pipeline.snapshot().context->rawPool;auto display=f.pipeline.snapshot().context->displayPool;
        contextual.reset();f.controller.shutdown();
        EXPECT_EQ(raw->stats().inUse,0U);EXPECT_EQ(display->stats().inUse,0U);
    }
}
class CountingProvider final : public camera::ICameraProvider {
public:
    explicit CountingProvider(core::IClock& clock):delegate_(options(),clock) {}
    std::atomic<int> creates{0};
    core::Result<std::vector<camera::CameraDescriptor>> discover(std::stop_token token) override { return delegate_.discover(token); }
    core::Result<std::unique_ptr<camera::ICameraDevice>> create(const camera::CameraId& id) override {
        ++creates;return delegate_.create(id);
    }
private:camera::sim::SimulatedCameraProvider delegate_;
};
TEST(LivePipeline, LifecycleCancellationDuringPreparationPreventsLateCameraOpen) {
    for(bool shutdown:{false,true}) {
        SCOPED_TRACE(shutdown);core::ManualClock sourceClock;CountingProvider provider(sourceClock);Gate gate;int factories=0;
        Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool& pool) {
            if(++factories==2) gate.block();
            return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::make_unique<processing::Mono8PassThroughProcessor>(pool));
        },request(),&provider);ReleaseGate release{gate};
        ASSERT_TRUE(f.initialize());f.controller.selectCamera({"SIM-LIVE"});ASSERT_TRUE(f.act(Intent::Connect));
        ASSERT_TRUE(f.act(Intent::Disconnect));ASSERT_TRUE(f.controller.dispatch(Intent::Connect).hasValue());
        ASSERT_TRUE(gate.wait());
        if(shutdown) { ASSERT_TRUE(f.pipeline.post({100,application::Shutdown{}}).hasValue()); }
        else { ASSERT_TRUE(f.controller.dispatch(Intent::Disconnect).hasValue()); }
        gate.release();
        if(shutdown) f.controller.shutdown();
        else { ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;})); }
        EXPECT_EQ(provider.creates.load(),1);
    }
}
TEST(LivePipeline, UnusableSavedRecordsNeverAuthorizeResumeOrSubstituteCamera) {
    for(int failure=0;failure<3;++failure) {
        SCOPED_TRACE(failure);auto io=std::make_unique<MemoryIo>();io->record=savedRecord();auto simulation=options();
        if(failure==0) io->failLoad=true;
        if(failure==1) simulation.capabilities.frameRate.maximum=50;
        if(failure==2) io->record->cameraId.value="MISSING-CAMERA";
        Fixture f(std::move(io),simulation);ASSERT_TRUE(f.initialize());
        ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted && !f.panel.presentation().ordinaryOperationPending;}));
        EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
        EXPECT_FALSE(f.controller.dispatch(Intent::ResumeLive).hasValue());
        EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired,0U);
        EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision.has_value());
        if(failure==1) EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
        else EXPECT_FALSE(f.pipeline.snapshot().camera->actualIdentity.has_value());
    }
}
TEST(LivePipeline, FailedSaveWarnsWithoutRevokingExplicitSessionStart) {
    auto io=std::make_unique<MemoryIo>();io->failSave=true;Fixture f(std::move(io));
    ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().startupWarning && f.panel.presentation().startupWarning->code=="save_failed";}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
    EXPECT_FALSE(f.preferences.latestStatus()->latestSavedRevision.has_value());
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
}
TEST(LivePipeline, StandardAndUnknownFactoryExceptionsFailClosed) {
    for(bool standard:{false,true}) {
        SCOPED_TRACE(standard);Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&)
            ->core::Result<std::unique_ptr<processing::IFrameProcessor>> {
            if(standard) throw std::runtime_error("factory exception");
            throw 7;
        });
        ASSERT_TRUE(f.pipeline.start().hasValue());
        ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().error.has_value();}));
        EXPECT_EQ(f.pipeline.snapshot().error->code,"pipeline_exception");
        EXPECT_FALSE(f.pipeline.post({1,application::Discover{}}).hasValue());
        f.pipeline.shutdown();EXPECT_EQ(f.pipeline.snapshot().context,nullptr);
    }
}
TEST(LivePipeline, PipelineRetainsOldContextUntilReplacementBindingAcknowledgement) {
    Fixture f;ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().context!=nullptr;}));
    auto old=f.pipeline.snapshot().context;std::weak_ptr<application::LiveSessionContext> retired=old;
    ASSERT_TRUE(f.pipeline.acknowledgeContext(old->generation).hasValue());
    ASSERT_TRUE(f.pipeline.post({1,application::Connect{{"SIM-LIVE"}}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().ordinaryOutcome.has_value();}));
    ASSERT_TRUE(f.pipeline.post({2,application::Disconnect{}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().priorityOutcome.has_value();}));
    ASSERT_TRUE(f.pipeline.post({3,application::Connect{{"SIM-LIVE"}}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().context->generation!=old->generation;}));
    old.reset();EXPECT_FALSE(retired.expired());
    ASSERT_TRUE(f.pipeline.acknowledgeContext(f.pipeline.snapshot().context->generation).hasValue());
    ASSERT_TRUE(waitUntil([&]{return retired.expired();}));
}
TEST(LivePipeline, ShutdownFromEveryStartupStageReleasesAllPresentationOwners) {
    for(int stage=0;stage<8;++stage) {
        SCOPED_TRACE(stage);Fixture f;ASSERT_TRUE(f.initialize());f.controller.selectCamera({"SIM-LIVE"});
        if(stage>=1) { ASSERT_TRUE(f.act(Intent::Connect)); }
        if(stage>=2) { ASSERT_TRUE(f.act(Intent::Apply)); }
        if(stage>=3) { ASSERT_TRUE(f.act(Intent::Confirm)); }
        if(stage>=4) { ASSERT_TRUE(f.act(Intent::Start));ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint()); }
        if(stage==5) f.controller.presenter()->pause();
        if(stage==6) { ASSERT_TRUE(f.act(Intent::Stop)); }
        if(stage==7) { ASSERT_TRUE(f.act(Intent::Disconnect)); }
        auto raw=f.pipeline.snapshot().context->rawPool;
        auto display=f.pipeline.snapshot().context->displayPool;
        auto processing=f.pipeline.snapshot().context->processingPool;
        const auto started=std::chrono::steady_clock::now();f.controller.shutdown();
        EXPECT_LT(std::chrono::steady_clock::now()-started,500ms);
        EXPECT_EQ(f.controller.presenter(),nullptr);EXPECT_EQ(f.pipeline.snapshot().context,nullptr);
        EXPECT_EQ(raw->stats().inUse,0U);EXPECT_EQ(display->stats().inUse,0U);EXPECT_EQ(processing->stats().inUse,0U);
    }
}
struct CameraObservation {
    const std::thread::id uiThread{std::this_thread::get_id()};
    std::atomic<bool> wrongThread{false};
    std::atomic<int> starts{0};std::atomic<int> creates{0};std::atomic<int> destroyed{0};
    Gate* discoveryGate{nullptr};Gate* openGate{nullptr};Gate* applyGate{nullptr};bool failOpen{false};
};
class ObservedDevice final : public camera::ICameraDevice {
public:
    ObservedDevice(std::unique_ptr<camera::ICameraDevice> device,CameraObservation& observation)
        :device_(std::move(device)),observation_(observation),owner_(std::this_thread::get_id()) {}
    ~ObservedDevice() override { check();++observation_.destroyed; }
    core::Result<void> open() override {
        check();if(observation_.openGate) observation_.openGate->block();
        if(observation_.failOpen) return core::Result<void>::failure({core::ErrorCategory::CameraConnection,"open_failed","Open failed.","",false});
        return device_->open();
    }
    core::Result<camera::CameraCapabilities> capabilities() override { check();return device_->capabilities(); }
    core::Result<camera::AppliedCameraConfiguration> applyConfiguration(const camera::CameraConfiguration& value) override {
        check();if(observation_.applyGate) observation_.applyGate->block();return device_->applyConfiguration(value);
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
TEST(LivePipeline, TerminalPostDuringDiscoveryConnectingAndErrorJoinsTheComposedOwners) {
    for(int phase=0;phase<3;++phase) {
        SCOPED_TRACE(phase);core::ManualClock sourceClock;Gate gate;CameraObservation observed;
        if(phase==0) observed.discoveryGate=&gate;
        if(phase==1) observed.openGate=&gate;
        if(phase==2) observed.failOpen=true;
        ObservedProvider provider(sourceClock,observed);
        Fixture f(std::make_unique<MemoryIo>(),options(),{},request(),&provider);ReleaseGate release{gate};
        ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(f.pipeline.start().hasValue());ASSERT_TRUE(f.controller.start().hasValue());
        if(phase>0) {
            ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
            f.controller.selectCamera({"SIM-LIVE"});ASSERT_TRUE(f.controller.dispatch(Intent::Connect).hasValue());
        }
        if(phase<2) { ASSERT_TRUE(gate.wait()); }
        const auto expected=phase==0 ? application::CameraSessionState::Discovering :
            phase==1 ? application::CameraSessionState::Connecting : application::CameraSessionState::Error;
        ASSERT_TRUE(f.wait([&]{return f.panel.presentation().cameraStatus && f.panel.presentation().cameraStatus->state==expected;}));
        auto raw=f.pipeline.snapshot().context->rawPool;auto display=f.pipeline.snapshot().context->displayPool;
        ASSERT_TRUE(f.pipeline.post({100,application::Shutdown{}}).hasValue());
        // The terminal request is issued while the device/provider call is still blocked.
        gate.release();f.controller.shutdown();
        EXPECT_EQ(raw->stats().inUse,0U);EXPECT_EQ(display->stats().inUse,0U);
        EXPECT_FALSE(observed.wrongThread.load());EXPECT_EQ(observed.creates.load(),observed.destroyed.load());
    }
}
TEST(LivePipeline, DisconnectSupersedesStopAndBlockedResumeWithoutLateStart) {
    core::ManualClock sourceClock;Gate gate;CameraObservation observed;observed.applyGate=&gate;
    ObservedProvider provider(sourceClock,observed);auto io=std::make_unique<MemoryIo>();io->record=savedRecord();
    Fixture f(std::move(io),options(),{},request(),&provider);ReleaseGate release{gate};ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable;}));
    ASSERT_TRUE(f.controller.dispatch(Intent::ResumeLive).hasValue());ASSERT_TRUE(gate.wait());
    ASSERT_TRUE(f.controller.dispatch(Intent::Stop).hasValue());
    ASSERT_TRUE(f.controller.dispatch(Intent::Stop).hasValue());
    ASSERT_TRUE(f.controller.dispatch(Intent::Disconnect).hasValue());
    gate.release();
    ASSERT_TRUE(waitUntil([&]{auto state=f.pipeline.snapshot();return state.camera &&
        state.camera->state==application::CameraSessionState::Disconnected && state.priorityOutcome.has_value();}));
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
    EXPECT_EQ(observed.starts.load(),0);EXPECT_EQ(observed.creates.load(),1);
    EXPECT_FALSE(observed.wrongThread.load());EXPECT_FALSE(f.pipeline.snapshot().camera->desiredIdentity.has_value());
    EXPECT_FALSE(f.preferences.latestStatus()->latestSavedRevision.has_value());
}
TEST(LivePipeline, RemovalRetainsContextAndRetryOpensOnlyTheSameIdentityIdle) {
    auto simulation=options();simulation.faults=camera::sim::FaultScript::create({{2,camera::sim::SimulatedFault::Disconnect}}).value();
    auto faults=simulation.faults;
    Fixture f(std::make_unique<MemoryIo>(),simulation);ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.wait([&]{return f.latest()!=nullptr;}));ASSERT_TRUE(f.paint());
    auto shown=f.controller.presenter()->presentedBundle();const auto generation=f.pipeline.snapshot().context->generation;
    f.clock.advance(34ms);
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->state==application::CameraSessionState::Reconnecting;}));
    EXPECT_EQ(f.controller.presenter()->presentedBundle(),shown);
    EXPECT_EQ(f.pipeline.snapshot().camera->desiredIdentity->value,"SIM-LIVE");
    faults->restoreConnection();ASSERT_TRUE(f.act(Intent::Retry));
    EXPECT_GT(f.pipeline.snapshot().context->generation,generation);
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_EQ(f.pipeline.snapshot().camera->actualIdentity->value,"SIM-LIVE");
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision.has_value());
    EXPECT_EQ(f.controller.presenter()->presentedBundle(),nullptr);
    shown.reset();
}
TEST(LivePipeline, DirectStartRequiresAcknowledgedGenerationAfterConfirmation) {
    Fixture f;ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().context!=nullptr;}));
    const auto generation=f.pipeline.snapshot().context->generation;
    const auto send=[&](application::CameraCommand command) {
        const auto id=command.requestId;
        if(!f.pipeline.post(std::move(command)).hasValue()) return false;
        return waitUntil([&]{auto outcome=f.pipeline.snapshot().ordinaryOutcome;return outcome && outcome->requestId==id;});
    };
    ASSERT_TRUE(send({1,application::Connect{{"SIM-LIVE"}}}));
    ASSERT_TRUE(send({2,application::ApplyConfiguration{generation,request(),1}}));
    ASSERT_TRUE(send({3,application::ConfirmConfiguration{generation,1}}));
    ASSERT_TRUE(send({4,application::StartStream{generation,1}}));
    ASSERT_TRUE(f.pipeline.snapshot().ordinaryOutcome->error.has_value());
    EXPECT_EQ(f.pipeline.snapshot().ordinaryOutcome->error->code,"context_not_bound");
    EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired,0U);
    ASSERT_TRUE(f.pipeline.acknowledgeContext(generation).hasValue());
    ASSERT_TRUE(send({5,application::StartStream{generation,1}}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
}
TEST(LivePipeline, ResolutionChangeIsRejectedBeforeMutatingTheSession) {
    Fixture f;ASSERT_TRUE(f.begin());auto changed=request();changed.roi.width=4;
    const auto before=f.pipeline.snapshot();
    auto rejected=f.pipeline.post({100,application::ApplyConfiguration{before.context->generation,changed,100}});
    EXPECT_FALSE(rejected.hasValue());
    EXPECT_TRUE(application::cameraConfigurationsEqual(f.pipeline.snapshot().camera->appliedConfiguration->actual,request()));
    EXPECT_EQ(f.pipeline.snapshot().context,before.context);
}
TEST(LivePipeline, ShutdownWhilePausedAndReconnectingReleasesTheRetiredImage) {
    auto simulation=options();simulation.faults=camera::sim::FaultScript::create({{2,camera::sim::SimulatedFault::Disconnect}}).value();
    Fixture f(std::make_unique<MemoryIo>(),simulation);ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.wait([&]{return f.latest()!=nullptr;}));ASSERT_TRUE(f.paint());f.controller.presenter()->pause();
    auto raw=f.pipeline.snapshot().context->rawPool;auto display=f.pipeline.snapshot().context->displayPool;
    f.clock.advance(34ms);ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().camera->state==application::CameraSessionState::Reconnecting;}));
    EXPECT_EQ(f.view.viewerState(),ui::ViewerState::Paused);
    f.controller.shutdown();EXPECT_EQ(raw->stats().inUse,0U);EXPECT_EQ(display->stats().inUse,0U);
}
TEST(LivePipeline, ShutdownJoinsCameraBeforeWaitingForInFlightProcessing) {
    core::ManualClock sourceClock;CameraObservation observed;ObservedProvider provider(sourceClock,observed);
    Gate gate;std::atomic<bool> stall{true};
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool& pool) {
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::make_unique<StallProcessor>(pool,gate,stall));
    },request(),&provider);ReleaseGate release{gate};ASSERT_TRUE(f.begin());ASSERT_TRUE(gate.wait());
    auto raw=f.pipeline.snapshot().context->rawPool;auto display=f.pipeline.snapshot().context->displayPool;
    ASSERT_TRUE(f.pipeline.post({100,application::Shutdown{}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{return observed.destroyed.load()==1;}));
    EXPECT_GT(raw->stats().inUse,0U);
    gate.release();f.controller.shutdown();
    EXPECT_EQ(raw->stats().inUse,0U);EXPECT_EQ(display->stats().inUse,0U);EXPECT_FALSE(observed.wrongThread.load());
}
TEST(LivePipeline, FailedReplacementPreparationRetiresPipelineOwners) {
    int constructions=0;
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool& pool) {
        if(++constructions==2) return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(
            {core::ErrorCategory::Processing,"replacement_failed","Replacement failed.","",false});
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::make_unique<processing::Mono8PassThroughProcessor>(pool));
    });
    ASSERT_TRUE(f.begin());ASSERT_TRUE(f.act(Intent::Disconnect));
    ASSERT_TRUE(f.controller.dispatch(Intent::Connect).hasValue());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().startupWarning.has_value();}));
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().context==nullptr;}));
    EXPECT_FALSE(f.pipeline.post({100,application::Discover{}}).hasValue());
}
}
