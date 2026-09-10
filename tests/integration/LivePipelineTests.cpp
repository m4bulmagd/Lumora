#include "ViewportTestSupport.hpp"
#include "SimulatorComposition.hpp"
#include "FrameEngineTestAccess.hpp"
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <lumora/configuration/PresetCodec.hpp>
#include <lumora/ui/ProcessingPanel.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/CameraSettingsDialog.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/WorkstationController.hpp>
#include <lumora/ui/WorkstationView.hpp>
#include <lumora/processing/Mono8PassThroughProcessor.hpp>
#include <lumora/camera/sim/FaultScript.hpp>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <gtest/gtest.h>
#include <cstring>
#include <limits>
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
    bool next(core::ManualClock* alternateCameraClock=nullptr) {
        auto previous=latest();
        clock.advance(34ms);
        if(alternateCameraClock) {
            alternateCameraClock->advance(clock.steadyNow()-alternateCameraClock->steadyNow());
        }
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
bool nextAfter(Fixture& fixture, std::chrono::milliseconds elapsed) {
    const auto previous=fixture.latest();
    fixture.clock.advance(elapsed);
    return fixture.wait([&] {
        const auto value=fixture.latest();
        return value && (!previous || value->sourceFrameId()>previous->sourceFrameId())
            && value->raw->metadata.hostReceiptTime==fixture.clock.steadyNow();
    });
}
camera::CameraConfiguration controlsRequest() { auto value=request(); value.roi={0,0,64,48}; return value; }
camera::sim::SimulatedCameraOptions controlsOptions() { auto value=options(); value.capabilities.roi.maximum={0,0,64,48}; return value; }
std::unique_ptr<MemoryIo> invertedPresetsIo() {
    auto io=std::make_unique<MemoryIo>(); io->presetState.selectedId={"custom"};
    io->presetState.activePipeline.stages.back().enabled=true; return io;
}
std::string processingFailureDetail(Fixture& fixture) {
    const auto snapshot=fixture.pipeline.snapshot();
    std::string detail;
    if(snapshot.processingConfigurationOutcome && snapshot.processingConfigurationOutcome->error) {
        const auto& error=*snapshot.processingConfigurationOutcome->error; detail=error.code;
        for(const auto& violation:error.violations) detail+=" / "+violation.detail;
        if(error.preparationError) detail+=" / "+error.preparationError->diagnosticDetail;
    }
    if(auto* label=fixture.view.findChild<QLabel*>("processingSettingsStatus")) detail+=" / "+label->text().toStdString();
    if(const auto preferences=fixture.preferences.latestStatus();preferences->warning) detail+=" / "+preferences->warning->diagnosticDetail;
    return detail;
}
TEST(LivePipeline, LoadedPresetActivatesWithoutStartingCameraAndSavesAcknowledgedState) {
    auto io=std::make_unique<MemoryIo>(); auto* memory=io.get();
    auto repository=configuration::PresetCodec::loadDefaultRepository().value();
    ASSERT_TRUE(repository.apply({"standard"}).hasValue()); io->presetState=repository.snapshot();
    Fixture f(std::move(io),controlsOptions(),{},controlsRequest()); ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision.has_value();})) << processingFailureDetail(f);
    auto snapshot=f.pipeline.snapshot(); ASSERT_TRUE(snapshot.processingConfigurationOutcome);
    EXPECT_FALSE(snapshot.processingConfigurationOutcome->error);
    EXPECT_EQ(snapshot.camera->state,application::CameraSessionState::Disconnected);
    EXPECT_EQ(snapshot.camera->acquisitionCounters.acquired,0U);
    auto* selector=f.view.findChild<QComboBox*>("presetSelector"); ASSERT_NE(selector,nullptr);
    EXPECT_EQ(selector->currentData().toString(),QStringLiteral("standard"));
    std::lock_guard lock(memory->mutex); ASSERT_TRUE(memory->savedPresets);
    EXPECT_EQ(memory->savedPresets->selectedId.value,"standard");
}
TEST(LivePipeline, ProcessingControlRevisionReachesFramesAndResetPreservesPausedCameraAndViewport) {
    Fixture f(std::make_unique<MemoryIo>(),controlsOptions(),{},controlsRequest()); ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision.has_value();})) << processingFailureDetail(f);
    auto* selector=f.view.findChild<QComboBox*>("presetSelector"); ASSERT_NE(selector,nullptr);
    const auto previous=f.preferences.latestStatus()->latestSavedPresetRevision;
    selector->setCurrentIndex(selector->findData("standard"));
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision>previous;})) << processingFailureDetail(f);
    const auto applied=f.pipeline.snapshot().processingConfigurationOutcome; ASSERT_TRUE(applied);
    ASSERT_TRUE(f.next()); ASSERT_TRUE(f.latest()->enhanced);
    EXPECT_EQ(f.latest()->enhanced->pipelineVersion.configurationRevision,applied->configurationRevision);
    ASSERT_TRUE(f.paint()); f.controller.presenter()->pause();
    auto frozen=f.controller.presenter()->presentedBundle();
    f.view.imageViewport()->setActualPixels();
    const auto camera=f.pipeline.snapshot().camera;
    auto* reset=f.view.findChild<QPushButton*>("resetProcessing"); ASSERT_NE(reset,nullptr); reset->click();
    ASSERT_TRUE(f.wait([&]{auto result=f.pipeline.snapshot().processingConfigurationOutcome;
        return result && result->configurationRevision>applied->configurationRevision;}));
    EXPECT_EQ(f.controller.presenter()->presentedBundle(),frozen);
    EXPECT_EQ(f.view.viewerState(),ui::ViewerState::Paused);
    EXPECT_EQ(f.view.imageViewport()->transform().mode(),ui::ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(f.view.imageViewport()->transform().scale(),1.0);
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
    EXPECT_EQ(f.pipeline.snapshot().camera->sessionGeneration,camera->sessionGeneration);
    EXPECT_EQ(f.pipeline.snapshot().camera->confirmedRevision,camera->confirmedRevision);
    EXPECT_EQ(selector->currentData().toString(),QStringLiteral("original"));
}
TEST(LivePipeline, RejectedPresetRestoresAcceptedSettingsWithoutStoppingCameraOrSavingFailure) {
    Fixture f; ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision.has_value();}));
    const auto saved=f.preferences.latestStatus()->latestSavedPresetRevision;
    auto* selector=f.view.findChild<QComboBox*>("presetSelector"); ASSERT_NE(selector,nullptr);
    selector->setCurrentIndex(selector->findData("standard"));
    ASSERT_TRUE(f.wait([&]{auto outcome=f.pipeline.snapshot().processingConfigurationOutcome;return outcome && outcome->error;}));
    f.controller.poll();
    EXPECT_EQ(f.pipeline.snapshot().processingConfigurationOutcome->error->code,"clahe_image_too_small");
    EXPECT_EQ(selector->currentData().toString(),QStringLiteral("original"));
    EXPECT_EQ(f.preferences.latestStatus()->latestSavedPresetRevision,saved);
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
    EXPECT_FALSE(f.pipeline.snapshot().processingConfigurationPending);
    auto* status=f.view.findChild<QLabel*>("processingSettingsStatus"); ASSERT_NE(status,nullptr);
    EXPECT_NE(status->text(),QStringLiteral("Changes pending…"));
}
class LiveEnhancementFault final : public processing::detail::EngineHooks {
public:
    std::atomic<bool> fail{true};
    core::Result<void> before(processing::ProcessingOperation operation,std::span<std::byte>) override {
        if(operation==processing::ProcessingOperation::Invert && fail.load())
            return core::Result<void>::failure({core::ErrorCategory::Processing,"live_enhancement_failure","Enhancement failed.","Persistent diagnostic",true});
        return core::Result<void>::success();
    }
};
TEST(LivePipeline, ProcessingRetryIsGenerationCheckedPendingWithoutFramesAndNeverReconnects) {
    auto hook=std::make_shared<LiveEnhancementFault>();
    Fixture f(invertedPresetsIo(),options(),[hook](core::BufferPool& p,core::BufferPool& d,const core::ImageLayout& source) {
        auto definition=processing::defaultPipeline(); definition.stages.back().enabled=true;
        auto made=processing::detail::FrameEngineTestAccess::create(p,d,source,definition,{},hook);
        if(!made.hasValue()) return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(made.error());
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::move(made).value());
    });
    f.view.show(); ASSERT_TRUE(f.begin());
    for(std::uint64_t failures=1;failures<=3;++failures) {
        f.clock.advance(34ms);
        ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().processing.processorStatus.enhancementFailures>=failures;}));
    }
    auto* warning=f.view.findChild<QLabel*>(QStringLiteral("processingWarning"));
    auto* retry=f.view.findChild<QPushButton*>(QStringLiteral("processingRetryButton"));
    ASSERT_NE(warning,nullptr); ASSERT_NE(retry,nullptr);
    // The control snapshot may advance just after the controller's poll. Wait
    // for the rendered warning and completed publication before clicking Retry.
    ASSERT_TRUE(f.wait([&]{return warning->isVisible() && retry->isEnabled() && f.latest();}));
    const auto before=f.pipeline.snapshot();
    ASSERT_TRUE(before.context); EXPECT_EQ(f.latest()->enhanced,nullptr);
    auto stale=f.pipeline.requestProcessingRetry(before.context->generation+1);
    ASSERT_FALSE(stale.hasValue()); EXPECT_EQ(stale.error().code,"stale_processing_session");
    const auto frame=f.latest()->sourceFrameId();
    retry->click();
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().processingRetryPending;}));
    EXPECT_FALSE(retry->isEnabled()); EXPECT_TRUE(warning->isVisible());
    EXPECT_EQ(f.latest()->sourceFrameId(),frame);
    EXPECT_EQ(f.pipeline.snapshot().camera->sessionGeneration,before.camera->sessionGeneration);
    // Admission is visible immediately, before the control thread delivers it.
    // Do not spend the sole recovery frame before that dispatch has completed.
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().processing.processorStatus.retryPending;}));
    hook->fail=false;
    ASSERT_TRUE(f.next());
    ASSERT_TRUE(f.wait([&]{
        const auto snapshot=f.pipeline.snapshot();
        return snapshot.processingAvailable && !snapshot.processingRetryPending
            && snapshot.processing.processorStatus.retriesConsumed==1U
            && f.latest()->enhanced && !warning->isVisible();
    }));
    const auto recovered=f.pipeline.snapshot();
    SCOPED_TRACE(::testing::Message() << "available=" << recovered.processingAvailable
        << " pending=" << recovered.processingRetryPending
        << " enginePending=" << recovered.processing.processorStatus.retryPending
        << " error=" << (recovered.error ? recovered.error->code : "none")
        << " cameraReplacement=" << recovered.camera->sourceReplacementRequired);
    EXPECT_NE(f.latest()->enhanced,nullptr);
    EXPECT_EQ(recovered.context,before.context);
    EXPECT_EQ(recovered.processing.processorStatus.retriesConsumed,1U);
}
TEST(LivePipeline, SessionReplacementClearsWarningAndRejectsTheOldProcessingRetryGeneration) {
    auto hook=std::make_shared<LiveEnhancementFault>();
    Fixture f(invertedPresetsIo(),options(),[hook](core::BufferPool& p,core::BufferPool& d,const core::ImageLayout& source) {
        auto definition=processing::defaultPipeline(); definition.stages.back().enabled=true;
        auto made=processing::detail::FrameEngineTestAccess::create(p,d,source,definition,{},hook);
        if(!made.hasValue()) return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(made.error());
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::move(made).value());
    });
    f.view.show(); ASSERT_TRUE(f.begin());
    for(std::uint64_t failures=1;failures<=3;++failures) {
        f.clock.advance(34ms); ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().processing.processorStatus.enhancementFailures>=failures;}));
    }
    auto* warning=f.view.findChild<QLabel*>(QStringLiteral("processingWarning")); ASSERT_NE(warning,nullptr);
    ASSERT_TRUE(f.wait([&]{return warning->isVisible();}));
    const auto generation=f.pipeline.snapshot().context->generation;
    ASSERT_TRUE(f.act(Intent::Stop)); ASSERT_TRUE(f.pipeline.requestProcessingRetry(generation).hasValue());
    ASSERT_TRUE(f.act(Intent::Disconnect)); ASSERT_TRUE(f.act(Intent::Connect));
    ASSERT_GT(f.pipeline.snapshot().context->generation,generation);
    ASSERT_TRUE(f.wait([&]{return !warning->isVisible();}));
    auto rejected=f.pipeline.requestProcessingRetry(generation); ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code,"stale_processing_session");
    EXPECT_FALSE(f.pipeline.snapshot().processingRetryPending);
    EXPECT_EQ(f.pipeline.snapshot().processing.processorStatus.enhancementFailures,0U);
    EXPECT_EQ(f.pipeline.snapshot().processing.processorStatus.retriesConsumed,0U);
}
TEST(LivePipeline, DefaultAdmissionIncludesTheRawPoolAndCompletePreparedEngineCoverage) {
    Fixture f; ASSERT_TRUE(f.initialize());
    auto resources=f.pipeline.snapshot().resources; ASSERT_TRUE(resources);
    EXPECT_FALSE(resources->customProcessorStorageUnknown);
    EXPECT_EQ(resources->externalSessionBytes,core::BufferPool::plan(10,48).value().requiredStorageBytes);
    EXPECT_GT(resources->frameObjectBytes,0U);
    EXPECT_EQ(resources->activationReserveBytes,3U*resources->activationEnvelopeBytes);
    EXPECT_EQ(resources->requiredStorageBytes,resources->fixedStorageBytes+resources->activationReserveBytes+resources->gammaCacheReserveBytes);
    EXPECT_LE(resources->requiredStorageBytes,resources->storageBudgetBytes);
    EXPECT_FALSE(f.pipeline.requestProcessingRetry(f.pipeline.snapshot().context->generation+1).hasValue());
}

TEST(LivePipeline, ResourceRejectionExposesRequestedStorageBeforeFactoryOrContext) {
    core::ManualClock clock; camera::sim::SimulatedCameraProvider provider(options(),clock);
    processing::ProcessingPreparationOptions preparation; preparation.storageBudgetBytes=1;
    std::atomic<int> factories{};
    application::LivePipeline pipeline(provider,clock,request(),[&](core::BufferPool&,core::BufferPool&,const core::ImageLayout&) {
        ++factories; return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success({});
    },preparation);
    auto started=pipeline.start(); EXPECT_FALSE(started.hasValue());
    EXPECT_EQ(factories.load(),0);
    const auto snapshot=pipeline.snapshot(); EXPECT_EQ(snapshot.context,nullptr);
    ASSERT_TRUE(snapshot.resources.has_value());
    EXPECT_GT(snapshot.resources->requiredStorageBytes,1U);
    EXPECT_EQ(snapshot.resources->storageBudgetBytes,1U);
    EXPECT_TRUE(snapshot.resources->customProcessorStorageUnknown);
}

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
        // Binding acknowledgement permits control-thread retirement; command
        // completion does not imply that destruction of the old slots finished.
        ASSERT_TRUE(f.wait([&]{return raw->stats().inUse==0U && display->stats().inUse==0U &&
            u16->stats().inUse==0U;}));
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
    for(const bool changedFrameRate : {false,true}) {
        SCOPED_TRACE(changedFrameRate ? "frame rate readback" : "exposure readback");
        auto io=std::make_unique<MemoryIo>();io->record=savedRecord();
        if(changedFrameRate) {
            io->record->requested.requestedFps=1.25;
            io->record->lastApplied.requestedFps=2.0;
        } else {
            io->record->lastApplied.exposure.requestedMicroseconds=101.0;
        }
        Fixture f(std::move(io));ASSERT_TRUE(f.initialize());
        ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable;}));
        ASSERT_TRUE(f.act(Intent::ResumeLive));
        const auto camera=f.pipeline.snapshot().camera;
        EXPECT_EQ(camera->state,application::CameraSessionState::ConnectedIdle);
        EXPECT_FALSE(camera->confirmedRevision.has_value());
        EXPECT_EQ(camera->acquisitionCounters.acquired,0U);
        ASSERT_TRUE(camera->appliedConfiguration);
        if(changedFrameRate) {
            EXPECT_DOUBLE_EQ(*camera->appliedConfiguration->requested.requestedFps,1.25);
            EXPECT_DOUBLE_EQ(*camera->appliedConfiguration->actual.requestedFps,1.0);
        }
        ASSERT_TRUE(f.panel.presentation().startupWarning.has_value());
        EXPECT_EQ(f.panel.presentation().startupWarning->code,"startup_readback_changed");
        ASSERT_TRUE(f.act(Intent::Confirm));ASSERT_TRUE(f.act(Intent::Start));
        EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
    }
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
TEST(LivePipeline, ConfirmationDuringSlowLoadSurvivesImagingAndDisconnect) {
    Gate gate;auto io=std::make_unique<MemoryIo>();
    io->beforeLoad=[&]{gate.block();};
    Fixture f(std::move(io));ReleaseGate release{gate};
    ASSERT_TRUE(f.begin());ASSERT_TRUE(gate.wait());
    EXPECT_FALSE(f.preferences.latestStatus()->loadCompleted);
    for(int i=0;i<3;++i) { ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint()); }
    ASSERT_TRUE(f.act(Intent::Disconnect));
    EXPECT_FALSE(f.preferences.latestStatus()->latestAttemptedSaveRevision.has_value());
    gate.release();
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedRevision.has_value();}));
    EXPECT_EQ(f.preferences.latestStatus()->latestSavedRevision,1U);
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
}
TEST(LivePipeline, CloseDuringSlowLoadDrainsTheAlreadyConfirmedPreferences) {
    Gate gate;auto io=std::make_unique<MemoryIo>();
    io->beforeLoad=[&]{gate.block();};
    Fixture f(std::move(io));ReleaseGate release{gate};
    ASSERT_TRUE(f.begin());ASSERT_TRUE(gate.wait());
    ASSERT_TRUE(f.next());ASSERT_TRUE(f.paint());
    f.controller.shutdown();
    f.preferences.requestStop();
    EXPECT_FALSE(f.preferences.latestStatus()->loadCompleted);
    gate.release();f.preferences.join();
    EXPECT_EQ(f.preferences.latestStatus()->latestSavedRevision,1U);
    EXPECT_EQ(f.controller.presenter(),nullptr);
    EXPECT_FALSE(f.pipeline.snapshot().context);
}
TEST(LivePipeline, PermanentSaveAdmissionRejectionIsNotRetriedByOrdinaryPolling) {
    Fixture f;
    ASSERT_TRUE(f.pipeline.start().hasValue());ASSERT_TRUE(f.controller.start().hasValue());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().cameraStatus &&
        !f.panel.presentation().cameraStatus->discoveredDescriptors.empty() &&
        !f.panel.presentation().ordinaryOperationPending;}));
    f.controller.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(f.act(Intent::Connect));ASSERT_TRUE(f.act(Intent::Apply));ASSERT_TRUE(f.act(Intent::Confirm));
    ASSERT_TRUE(f.panel.presentation().startupWarning.has_value());
    EXPECT_EQ(f.panel.presentation().startupWarning->code,"startup_service_not_started");
    for(int i=0;i<20;++i) f.controller.poll();
    ASSERT_TRUE(f.preferences.start().hasValue());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->loadCompleted;}));
    for(int i=0;i<20;++i) f.controller.poll();
    EXPECT_FALSE(f.preferences.latestStatus()->latestAttemptedSaveRevision.has_value());
    ASSERT_TRUE(f.act(Intent::Apply));ASSERT_TRUE(f.act(Intent::Confirm));
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedRevision.has_value();}));
    EXPECT_EQ(f.preferences.latestStatus()->latestSavedRevision,2U);
}
TEST(LivePipeline, DuplicatePendingDisconnectRetainsItsCompletionCorrelation) {
    Gate gate;
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool& pool, const core::ImageLayout&) {
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
TEST(LivePipeline, ShutdownCapturesConfirmationCompletedSinceTheLastUiPoll) {
    Gate gate;auto io=std::make_unique<MemoryIo>();auto* observer=io.get();
    io->beforeLoad=[&]{gate.block();};
    Fixture f(std::move(io));ReleaseGate release{gate};
    ASSERT_TRUE(f.initialize());ASSERT_TRUE(gate.wait());
    f.controller.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(f.act(Intent::Connect));ASSERT_TRUE(f.act(Intent::Apply));
    ASSERT_TRUE(f.controller.dispatch(Intent::Confirm).hasValue());
    // This helper deliberately does not poll the controller or dispatch Qt events.
    ASSERT_TRUE(waitUntil([&]{auto camera=f.pipeline.snapshot().camera;
        return camera && camera->confirmedRevision.has_value();}));
    EXPECT_TRUE(f.panel.presentation().ordinaryOperationPending);
    EXPECT_FALSE(f.preferences.latestStatus()->latestAttemptedSaveRevision.has_value());
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    f.controller.shutdown();f.preferences.requestStop();gate.release();f.preferences.join();
    EXPECT_EQ(f.preferences.latestStatus()->latestSavedRevision,1U);
    ASSERT_TRUE(observer->saved.has_value());
    EXPECT_TRUE(observer->saved->confirmed);
    EXPECT_TRUE(application::cameraConfigurationsEqual(observer->saved->lastApplied,request()));
    EXPECT_EQ(f.controller.presenter(),nullptr);
    EXPECT_FALSE(f.pipeline.snapshot().context);
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
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool&, const core::ImageLayout&) {
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
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool&, const core::ImageLayout&) {
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
    EXPECT_EQ(bundle->raw->metadata.acquisitionSettings.sourceFormat.sampleMaximum,4095U);
    auto context=f.pipeline.snapshot().context;
    EXPECT_EQ(context->rawPool->stats().capacity,10U);EXPECT_EQ(context->processingPool->stats().capacity,9U);
    EXPECT_EQ(context->displayPool->stats().capacity,16U);
    EXPECT_EQ(context->rawPool->stats().bytesPerBuffer,614400U);
    EXPECT_EQ(context->processingPool->stats().bytesPerBuffer,614400U);
    EXPECT_EQ(context->displayPool->stats().bytesPerBuffer,307200U);
    EXPECT_EQ(bundle->raw->layout.storage(), core::StorageType::UInt16);
    ASSERT_NE(bundle->enhanced, nullptr); ASSERT_NE(bundle->enhancedDisplay, nullptr);
    EXPECT_EQ(bundle->enhanced->sourceFrameId, bundle->raw->frameId);
    EXPECT_EQ(bundle->originalDisplay->sourceFrameId, bundle->raw->frameId);
    EXPECT_EQ(bundle->enhancedDisplay->sourceFrameId, bundle->raw->frameId);
    EXPECT_EQ(bundle->originalDisplay->storage, core::DisplayStorage::Gray8);
    EXPECT_EQ(bundle->enhancedDisplay->storage, core::DisplayStorage::Gray8);
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
        Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool& pool, const core::ImageLayout&) {
            return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::make_unique<StallProcessor>(pool,gate,stall));
        },request(),nullptr,boundary==0 ? &sourceClock : nullptr);
        ReleaseGate release{gate};ASSERT_TRUE(f.begin());
        ASSERT_TRUE(f.next(boundary==0 ? &sourceClock : nullptr));
        ASSERT_TRUE(f.wait([&]{auto frame=f.latest();return frame && frame->raw->metadata.hostReceiptTime==f.clock.steadyNow();}));
        ASSERT_TRUE(f.paint());
        auto contextual=f.controller.presenter()->presentedBundle();const auto paintedAt=f.clock.steadyNow();
        ASSERT_TRUE(f.wait([&]{auto camera=f.pipeline.snapshot().camera;
            return camera && camera->acquisitionCounters.acquired>=contextual->sourceFrameId();}));
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
        if(boundary==0) {
            ASSERT_TRUE(f.next(&sourceClock));
        } else {
            ASSERT_TRUE(f.wait([&]{auto frame=f.latest();
                return frame && frame->sourceFrameId()>contextual->sourceFrameId();}));
        }
        ASSERT_TRUE(f.paint());
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
        Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool& pool, const core::ImageLayout&) {
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
        SCOPED_TRACE(standard);Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool&, const core::ImageLayout&)
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
    const auto disconnected=waitUntil([&]{return f.pipeline.snapshot().priorityOutcome.has_value();});
    if(!disconnected) {
        const auto stalled=f.pipeline.snapshot();
        FAIL() << "cameraState=" << static_cast<int>(stalled.camera->state)
            << " cameraOutcome=" << (stalled.camera->latestOutcome ? stalled.camera->latestOutcome->requestId : 0U)
            << " cameraOutcomeError=" << (stalled.camera->latestOutcome && stalled.camera->latestOutcome->error
                ? stalled.camera->latestOutcome->error->code : "none")
            << " ordinaryOutcome=" << (stalled.ordinaryOutcome ? stalled.ordinaryOutcome->requestId : 0U)
            << " ordinaryOutcomeError=" << (stalled.ordinaryOutcome && stalled.ordinaryOutcome->error
                ? stalled.ordinaryOutcome->error->code : "none")
            << " priorityOutcome=" << (stalled.priorityOutcome ? stalled.priorityOutcome->requestId : 0U)
            << " priorityOutcomeError=" << (stalled.priorityOutcome && stalled.priorityOutcome->error
                ? stalled.priorityOutcome->error->code : "none")
            << " replacementRequired=" << stalled.camera->sourceReplacementRequired
            << " contextBound=" << stalled.contextBound
            << " contextGeneration=" << (stalled.context ? stalled.context->generation : 0U)
            << " pipelineError=" << (stalled.error ? stalled.error->code : "none");
    }
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
TEST(LivePipeline, ShutdownCapturesResumeConfirmationWithoutAdvancingToStart) {
    core::ManualClock sourceClock;CameraObservation observed;ObservedProvider provider(sourceClock,observed);
    auto io=std::make_unique<MemoryIo>();io->record=savedRecord();
    Fixture f(std::move(io),options(),{},request(),&provider);
    ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable &&
        !f.panel.presentation().ordinaryOperationPending;}));
    ASSERT_TRUE(f.controller.dispatch(Intent::ResumeLive).hasValue());
    ASSERT_TRUE(waitUntil([&]{auto camera=f.pipeline.snapshot().camera;return camera && camera->appliedRevision>0;}));
    f.controller.poll(); // Advance only the completed Apply to Confirm.
    ASSERT_TRUE(waitUntil([&]{auto camera=f.pipeline.snapshot().camera;return camera && camera->confirmedRevision.has_value();}));
    f.controller.shutdown();f.preferences.requestStop();f.preferences.join();
    EXPECT_EQ(f.preferences.latestStatus()->latestSavedRevision,1U);
    EXPECT_EQ(observed.starts.load(),0);
    EXPECT_EQ(observed.destroyed.load(),observed.creates.load());
    EXPECT_FALSE(observed.wrongThread.load());
}
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
TEST(LivePipeline, DirectFrameRateApplyPreservesResourcesAndRequiresExplicitPositiveFiniteRequest) {
    Fixture f; ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(waitUntil([&]{return f.pipeline.snapshot().context!=nullptr;}));
    const auto context=f.pipeline.snapshot().context;
    const auto raw=context->rawPool;
    const auto processing=context->processingPool;
    const auto display=context->displayPool;
    ASSERT_TRUE(f.pipeline.post({1U,application::Connect{{"SIM-LIVE"}}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{const auto outcome=f.pipeline.snapshot().ordinaryOutcome;
        return outcome && outcome->requestId==1U;}));
    ASSERT_FALSE(f.pipeline.snapshot().ordinaryOutcome->error);
    std::uint64_t revision=0U;
    for(const double fps : {1.25,1.0,60.0}) {
        SCOPED_TRACE(fps);
        auto edited=request(); edited.requestedFps=fps;
        const auto id=++revision+1U;
        ASSERT_TRUE(f.pipeline.post({id,application::ApplyConfiguration{
            context->generation,edited,revision}}).hasValue());
        ASSERT_TRUE(waitUntil([&]{const auto outcome=f.pipeline.snapshot().ordinaryOutcome;
            return outcome && outcome->requestId==id;}));
        const auto applied=f.pipeline.snapshot();
        ASSERT_FALSE(applied.ordinaryOutcome->error);
        ASSERT_TRUE(applied.camera->requestedConfiguration);
        ASSERT_TRUE(applied.camera->appliedConfiguration);
        EXPECT_DOUBLE_EQ(*applied.camera->requestedConfiguration->requestedFps,fps);
        EXPECT_DOUBLE_EQ(*applied.camera->appliedConfiguration->actual.requestedFps,fps==1.25 ? 1.0 : fps);
        EXPECT_EQ(applied.camera->state,application::CameraSessionState::ConnectedIdle);
        EXPECT_EQ(applied.camera->sessionGeneration,context->generation);
        EXPECT_EQ(applied.context,context);
        EXPECT_EQ(applied.context->rawPool,raw);
        EXPECT_EQ(applied.context->processingPool,processing);
        EXPECT_EQ(applied.context->displayPool,display);
    }
    for(const auto fps : {std::optional<double>{},std::optional<double>{0.0},std::optional<double>{-1.0},
             std::optional<double>{std::numeric_limits<double>::quiet_NaN()},
             std::optional<double>{std::numeric_limits<double>::infinity()},
             std::optional<double>{-std::numeric_limits<double>::infinity()}}) {
        SCOPED_TRACE(fps.value_or(-2.0));
        auto invalid=request(); invalid.requestedFps=fps;
        const auto rejected=f.pipeline.post({100U,application::ApplyConfiguration{
            context->generation,invalid,revision+1U}});
        ASSERT_FALSE(rejected.hasValue());
        EXPECT_EQ(rejected.error().code,"unsupported_pipeline_request");
        EXPECT_EQ(f.pipeline.snapshot().camera->requestedRevision,revision);
    }
    // A numeric request can retain prepared resources while failing camera capability validation.
    auto outOfRange=request(); outOfRange.requestedFps=61.0;
    ASSERT_TRUE(f.pipeline.post({101U,application::ApplyConfiguration{
        context->generation,outOfRange,revision+1U}}).hasValue());
    ASSERT_TRUE(waitUntil([&]{const auto outcome=f.pipeline.snapshot().ordinaryOutcome;
        return outcome && outcome->requestId==101U;}));
    const auto rejected=f.pipeline.snapshot();
    ASSERT_TRUE(rejected.ordinaryOutcome->error);
    EXPECT_EQ(rejected.ordinaryOutcome->error->category,core::ErrorCategory::CameraConfiguration);
    EXPECT_EQ(rejected.camera->requestedRevision,revision+1U);
    EXPECT_EQ(rejected.camera->appliedRevision,revision);
    EXPECT_DOUBLE_EQ(*rejected.camera->appliedConfiguration->actual.requestedFps,60.0);
    EXPECT_EQ(rejected.context,context);
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
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool& pool, const core::ImageLayout&) {
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
    Fixture f(std::make_unique<MemoryIo>(),options(),[&](core::BufferPool&, core::BufferPool& pool, const core::ImageLayout&) {
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
camera::CameraConfiguration depthRequest(unsigned bits) {
    auto fixed = request();
    fixed.pixelFormat = {"Mono" + std::to_string(bits), bits == 8 ? 0x01080001U :
        (bits == 10 ? 0x01100003U : (bits == 12 ? 0x01100005U : 0x01100007U)),
        static_cast<std::uint8_t>(bits), static_cast<std::uint16_t>((1U << bits) - 1U),
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        bits == 8 ? core::StorageType::UInt8 : core::StorageType::UInt16};
    return fixed;
}
camera::sim::SimulatedCameraOptions depthOptions(unsigned bits) {
    auto simulation = options();
    simulation.capabilities.pixelFormats = {depthRequest(bits).pixelFormat};
    simulation.pattern = camera::sim::SimulationPattern::Ramp;
    return simulation;
}
std::vector<std::byte> retainedBytes(const core::SharedBuffer& buffer) {
    return {buffer.bytes().begin(), buffer.bytes().end()};
}
TEST(LivePipeline, NativeDepthsPublishPairedImmutableProductsAndReleaseAllPools) {
    for (unsigned bits : {8U,10U,12U,16U}) {
        SCOPED_TRACE(bits);
        Fixture f(std::make_unique<MemoryIo>(), depthOptions(bits), {}, depthRequest(bits));
        ASSERT_TRUE(f.begin()); ASSERT_TRUE(f.next()); ASSERT_TRUE(f.paint());
        auto context = f.pipeline.snapshot().context;
        auto rawPool = context->rawPool; auto u16Pool = context->processingPool; auto displayPool = context->displayPool;
        const auto sampleBytes = bits == 8 ? 1U : 2U;
        EXPECT_EQ(rawPool->stats().bytesPerBuffer, 48U * sampleBytes);
        EXPECT_EQ(u16Pool->stats().bytesPerBuffer, 96U);
        EXPECT_EQ(displayPool->stats().bytesPerBuffer, 48U);
        auto bundle = f.latest();
        ASSERT_NE(bundle, nullptr); ASSERT_NE(bundle->enhanced, nullptr); ASSERT_NE(bundle->enhancedDisplay, nullptr);
        auto retainedRaw = bundle->raw;
        const auto before = retainedBytes(retainedRaw->pixels);
        const auto processed = retainedBytes(bundle->enhanced->pixels);
        const auto original = retainedBytes(bundle->originalDisplay->pixels);
        const auto enhanced = retainedBytes(bundle->enhancedDisplay->pixels);
        EXPECT_EQ(retainedRaw->layout.storage(), depthRequest(bits).pixelFormat.applicationStorage);
        EXPECT_EQ(bundle->enhanced->layout.storage(), core::StorageType::UInt16);
        EXPECT_EQ(bundle->originalDisplay->storage, core::DisplayStorage::Gray8);
        EXPECT_EQ(bundle->enhancedDisplay->storage, core::DisplayStorage::Gray8);
        EXPECT_EQ(bundle->originalDisplay->sourceFrameId, retainedRaw->frameId);
        EXPECT_EQ(bundle->enhanced->sourceFrameId, retainedRaw->frameId);
        EXPECT_EQ(bundle->enhancedDisplay->sourceFrameId, retainedRaw->frameId);
        EXPECT_EQ(bundle->originalDisplay->mapping, bundle->enhancedDisplay->mapping);
        EXPECT_EQ(bundle->enhanced->pipelineVersion.configurationRevision,
            bundle->enhancedDisplay->mapping.configurationRevision);
        const auto maximum = depthRequest(bits).pixelFormat.sampleMaximum;
        for (std::size_t x = 0; x < 8; ++x) {
            std::uint16_t sample = 0;
            if (bits == 8) sample = std::to_integer<std::uint8_t>(before[x]);
            else std::memcpy(&sample, before.data() + x * 2, 2);
            EXPECT_EQ(sample, static_cast<std::uint16_t>(x * maximum / 7U));
            const auto canonical = (static_cast<std::uint64_t>(sample) * 65535U + maximum / 2U) / maximum;
            std::uint16_t actual; std::memcpy(&actual, processed.data() + x * 2, 2);
            EXPECT_EQ(actual, canonical);
            EXPECT_EQ(std::to_integer<unsigned>(original[x]), (canonical + 128U) / 257U);
        }
        for (int next = 0; next < 4; ++next) ASSERT_TRUE(f.next());
        EXPECT_EQ(retainedBytes(retainedRaw->pixels), before);
        EXPECT_EQ(retainedBytes(bundle->enhanced->pixels), processed);
        EXPECT_EQ(retainedBytes(bundle->originalDisplay->pixels), original);
        EXPECT_EQ(retainedBytes(bundle->enhancedDisplay->pixels), enhanced);
        ASSERT_TRUE(f.act(Intent::Disconnect));
        ASSERT_TRUE(f.act(Intent::Connect));
        ASSERT_NE(f.pipeline.snapshot().context->generation, context->generation);
        EXPECT_EQ(retainedBytes(retainedRaw->pixels), before);
        bundle.reset(); retainedRaw.reset(); context.reset();
        ASSERT_TRUE(f.wait([&] { return rawPool->stats().inUse == 0 && u16Pool->stats().inUse == 0 && displayPool->stats().inUse == 0; }));
        auto last = f.pipeline.snapshot().context;
        rawPool = last->rawPool; u16Pool = last->processingPool; displayPool = last->displayPool;
        ASSERT_TRUE(f.act(Intent::Apply)); ASSERT_TRUE(f.act(Intent::Confirm)); ASSERT_TRUE(f.act(Intent::Start));
        ASSERT_TRUE(f.next()); ASSERT_TRUE(f.paint());
        f.controller.shutdown(); last.reset();
        EXPECT_EQ(rawPool->stats().inUse, 0U); EXPECT_EQ(u16Pool->stats().inUse, 0U); EXPECT_EQ(displayPool->stats().inUse, 0U);
    }
}
TEST(LivePipeline, HighDepthFactoryReceivesCoherentPlanAndActivationRevisions) {
    core::BufferPool* receivedU16 = nullptr; core::BufferPool* receivedGray = nullptr;
    std::optional<core::ImageLayout> receivedLayout;
    std::atomic<processing::FrameProcessingEngine*> engine{nullptr};
    Fixture f(std::make_unique<MemoryIo>(), depthOptions(12),
        [&](core::BufferPool& u16, core::BufferPool& gray, const core::ImageLayout& layout) {
            receivedU16 = &u16; receivedGray = &gray; receivedLayout = layout;
            auto made = processing::FrameProcessingEngine::create(u16, gray, layout);
            if (!made.hasValue()) return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(made.error());
            engine.store(made.value().get());
            return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::move(made).value());
        }, depthRequest(12));
    ASSERT_TRUE(f.begin());
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->latestSavedPresetRevision.has_value();}));
    const auto initialRevision=f.pipeline.snapshot().processingConfigurationOutcome->configurationRevision;
    ASSERT_TRUE(f.next());
    auto context = f.pipeline.snapshot().context;
    EXPECT_EQ(receivedU16, context->processingPool.get()); EXPECT_EQ(receivedGray, context->displayPool.get());
    ASSERT_TRUE(receivedLayout); EXPECT_EQ(receivedLayout->storage(), core::StorageType::UInt16);
    EXPECT_EQ(receivedLayout->strideBytes(), 16U); EXPECT_EQ(receivedLayout->payloadBytes(), 96U);
    auto previous = f.latest(); ASSERT_NE(previous->enhanced, nullptr);
    auto definition = processing::defaultPipeline(); definition.version.configurationRevision = 7;
    ASSERT_TRUE(engine.load()->activate(definition).hasValue());
    ASSERT_TRUE(f.next());
    auto current = f.latest(); ASSERT_NE(current->enhanced, nullptr);
    EXPECT_EQ(previous->enhanced->pipelineVersion.configurationRevision, initialRevision);
    EXPECT_EQ(current->enhanced->pipelineVersion.configurationRevision, 7U);
    EXPECT_EQ(current->originalDisplay->mapping.configurationRevision, 7U);
    EXPECT_EQ(current->enhancedDisplay->mapping.configurationRevision, 7U);
}
TEST(LivePipeline, CheckedNativeResourcePlanRejectsInvalidModesBeforeFactoryOrContext) {
    for (int invalid = 0; invalid < 12; ++invalid) {
        SCOPED_TRACE(invalid);
        core::ManualClock clock; camera::sim::SimulatedCameraProvider provider(options(), clock);
        auto fixed = depthRequest(12);
        if (invalid == 0) fixed.pixelFormat.applicationStorage = static_cast<core::StorageType>(99);
        if (invalid == 1) fixed.pixelFormat.sampleMaximum = 65535;
        if (invalid == 2) fixed.roi.width = 0;
        if (invalid == 3) fixed.roi.height = 0;
        if (invalid == 4) fixed.requestedFps = 0;
        if (invalid == 5) fixed.requestedFps = std::numeric_limits<double>::infinity();
        if (invalid == 6) fixed.requestedFps = std::numeric_limits<double>::quiet_NaN();
        if (invalid == 7) fixed.acquisitionMode = camera::AcquisitionMode::Triggered;
        if (invalid == 8) fixed.requestedFps.reset();
        if (invalid == 9) fixed.roi = {0,0,1U << 30U,1U << 30U}; // blocks fit size_t; total does not
        if (invalid >= 10) {
            if (invalid == 11) fixed = depthRequest(8);
            fixed.roi = {0,0,3U << 28U,1U << 29U}; // each pool fits size_t; their sum does not
        }
        std::atomic<int> calls{0};
        application::LivePipeline pipeline(provider, clock, fixed,
            [&](core::BufferPool&, core::BufferPool& display, const core::ImageLayout&) {
                ++calls;
                return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(
                    std::make_unique<processing::Mono8PassThroughProcessor>(display));
            });
        auto started = pipeline.start();
        ASSERT_FALSE(started.hasValue());
        if (invalid >= 9) { EXPECT_EQ(started.error().category, core::ErrorCategory::ResourceExhaustion); }
        if (invalid >= 10 && sizeof(std::size_t) == 8) { EXPECT_EQ(started.error().code, "size_addition_overflow"); }
        EXPECT_EQ(calls.load(), 0); EXPECT_EQ(pipeline.snapshot().context, nullptr);
    }
}
TEST(LivePipeline, OldMono8SavedCapabilitiesRequireReviewForShippingMono12) {
    auto io = std::make_unique<MemoryIo>();
    auto old = savedRecord();
    old.requested = app::simulatorConfiguration(); old.lastApplied = old.requested;
    old.requested.pixelFormat = request().pixelFormat; old.lastApplied.pixelFormat = request().pixelFormat;
    old.confirmedCapabilities = app::simulatorOptions().capabilities;
    old.confirmedCapabilities.pixelFormats = {request().pixelFormat};
    io->record = old;
    Fixture f(std::move(io), app::simulatorOptions(), {}, app::simulatorConfiguration());
    ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&] { return f.panel.presentation().preferencesLoadCompleted
        && !f.panel.presentation().ordinaryOperationPending; }));
    EXPECT_EQ(f.pipeline.snapshot().camera->state, application::CameraSessionState::Disconnected);
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
    EXPECT_FALSE(f.controller.dispatch(Intent::ResumeLive).hasValue());
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
    EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired, 0U);
    f.controller.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(f.act(Intent::Connect)); ASSERT_TRUE(f.act(Intent::Apply));
    EXPECT_FALSE(f.controller.dispatch(Intent::Start).hasValue());
    EXPECT_EQ(f.pipeline.snapshot().camera->appliedConfiguration->actual.pixelFormat.sampleMaximum, 4095U);
    ASSERT_TRUE(f.act(Intent::Confirm)); ASSERT_TRUE(f.act(Intent::Start));
    ASSERT_TRUE(f.next());
}

TEST(LivePipeline, StoppedDialogEditsKeepTheSourceAndRequireFreshConfirmation) {
    Fixture f; ASSERT_TRUE(f.begin()); ASSERT_TRUE(f.next()); ASSERT_TRUE(f.paint());
    f.controller.presenter()->pause();
    f.view.imageViewport()->setActualPixels(); f.view.imageViewport()->zoomIn();
    const auto scale=f.view.imageViewport()->transform().scale();
    const auto before=f.pipeline.snapshot();
    const auto rawPool=before.context->rawPool;
    const auto processingPool=before.context->processingPool;
    const auto displayPool=before.context->displayPool;
    const auto presenter=f.controller.presenter();
    const auto oldFrame=f.controller.presenter()->presentedBundle();
    ASSERT_TRUE(f.act(Intent::Stop));
    auto* settings=f.panel.findChild<QPushButton*>("cameraSettingsButton");
    ASSERT_NE(settings,nullptr); ASSERT_TRUE(settings->isEnabled()); settings->click();
    auto* dialog=f.panel.findChild<ui::CameraSettingsDialog*>(); ASSERT_NE(dialog,nullptr);
    auto* exposure=dialog->findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* gain=dialog->findChild<QDoubleSpinBox*>("cameraGainValue");
    auto* frameRate=dialog->findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* apply=dialog->findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure,nullptr); ASSERT_NE(gain,nullptr); ASSERT_NE(frameRate,nullptr); ASSERT_NE(apply,nullptr);
    exposure->setValue(123.25); gain->setValue(2.25); frameRate->setValue(1.25); apply->click();
    ASSERT_TRUE(f.panel.presentation().ordinaryOperationPending);
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
    EXPECT_TRUE(apply->isEnabled());
    EXPECT_TRUE(exposure->isEnabled());
    EXPECT_TRUE(gain->isEnabled());
    EXPECT_TRUE(frameRate->isEnabled());
    const auto edited=f.pipeline.snapshot();
    ASSERT_TRUE(edited.camera->requestedConfiguration); ASSERT_TRUE(edited.camera->appliedConfiguration);
    EXPECT_DOUBLE_EQ(*edited.camera->requestedConfiguration->exposure.requestedMicroseconds,123.25);
    EXPECT_DOUBLE_EQ(*edited.camera->requestedConfiguration->gain.requestedDb,2.25);
    EXPECT_DOUBLE_EQ(*edited.camera->requestedConfiguration->requestedFps,1.25);
    EXPECT_DOUBLE_EQ(*edited.camera->appliedConfiguration->actual.exposure.requestedMicroseconds,123.0);
    EXPECT_DOUBLE_EQ(*edited.camera->appliedConfiguration->actual.gain.requestedDb,2.0);
    EXPECT_DOUBLE_EQ(*edited.camera->appliedConfiguration->actual.requestedFps,1.0);
    EXPECT_EQ(edited.context,before.context);
    EXPECT_EQ(edited.context->rawPool,rawPool);
    EXPECT_EQ(edited.context->processingPool,processingPool);
    EXPECT_EQ(edited.context->displayPool,displayPool);
    EXPECT_EQ(edited.camera->sessionGeneration,before.camera->sessionGeneration);
    EXPECT_FALSE(edited.camera->confirmedRevision);
    EXPECT_FALSE(f.controller.dispatch(Intent::Start).hasValue());
    EXPECT_EQ(f.controller.presenter(),presenter);
    EXPECT_EQ(f.controller.presenter()->presentedBundle(),oldFrame);
    EXPECT_EQ(f.view.viewerState(),ui::ViewerState::Paused);
    EXPECT_DOUBLE_EQ(f.view.imageViewport()->transform().scale(),scale);
    ASSERT_TRUE(f.act(Intent::Confirm)); ASSERT_TRUE(f.act(Intent::Start));
    ASSERT_TRUE(nextAfter(f,1s));
    const auto current=f.latest();
    EXPECT_GT(current->sourceFrameId(),oldFrame->sourceFrameId());
    const auto& metadata=current->raw->metadata.acquisitionSettings;
    EXPECT_DOUBLE_EQ(metadata.requestedFps,1.25);
    EXPECT_DOUBLE_EQ(metadata.actualFps,1.0);
    EXPECT_DOUBLE_EQ(*metadata.exposureMicroseconds,123.0);
    EXPECT_DOUBLE_EQ(*metadata.gainDb,2.0);
    EXPECT_EQ(f.pipeline.snapshot().context,before.context);
    EXPECT_EQ(f.controller.presenter()->presentedBundle(),oldFrame);
    EXPECT_EQ(f.view.viewerState(),ui::ViewerState::Paused);
    EXPECT_DOUBLE_EQ(f.view.imageViewport()->transform().scale(),scale);
    // The ordinary Apply button must retain the edited request too.
    ASSERT_TRUE(f.act(Intent::Stop)); ASSERT_TRUE(f.act(Intent::Apply));
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->exposure.requestedMicroseconds,123.25);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->gain.requestedDb,2.25);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->requestedFps,1.25);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->appliedConfiguration->actual.requestedFps,1.0);
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
}

TEST(LivePipeline, CameraSettingsRejectStaleInvalidAndStreamingRequestsBeforeAdmission) {
    Fixture f; ASSERT_TRUE(f.begin());
    auto camera=f.pipeline.snapshot().camera; auto edited=request();
    edited.exposure.requestedMicroseconds=200.0;
    edited.requestedFps=1.25;
    f.controller.presenter()->pause();
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
    ASSERT_TRUE(f.act(Intent::Stop)); camera=f.pipeline.snapshot().camera;
    const auto revision=camera->requestedRevision;
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration+1,{"SIM-LIVE"},edited).hasValue());
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"other"},edited).hasValue());
    auto invalid=edited; invalid.exposure.requestedMicroseconds=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},invalid).hasValue());
    invalid=edited; invalid.gain.requestedDb=11.0;
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},invalid).hasValue());
    for(const auto fps : {std::optional<double>{},std::optional<double>{0.0},std::optional<double>{-1.0},
             std::optional<double>{0.5},std::optional<double>{61.0},
             std::optional<double>{std::numeric_limits<double>::quiet_NaN()},
             std::optional<double>{std::numeric_limits<double>::infinity()},
             std::optional<double>{-std::numeric_limits<double>::infinity()}}) {
        SCOPED_TRACE(fps.value_or(-2.0));
        invalid=edited; invalid.requestedFps=fps;
        EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},invalid).hasValue());
    }
    invalid=edited; invalid.roi.width=4U;
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},invalid).hasValue());
    EXPECT_EQ(f.pipeline.snapshot().camera->requestedRevision,revision);
    EXPECT_DOUBLE_EQ(*f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,100.0);
    EXPECT_DOUBLE_EQ(*f.panel.presentation().requestedConfiguration->requestedFps,30.0);
    ASSERT_TRUE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->exposure.requestedMicroseconds,200.0);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->requestedFps,1.25);
}

TEST(LivePipeline, ConfirmedEditedCameraSettingsPersistAndResumeOnNextLaunch) {
    std::optional<application::StartupPreferences> saved;
    {
        auto io=std::make_unique<MemoryIo>(); auto* memory=io.get(); Fixture f(std::move(io));
        ASSERT_TRUE(f.begin()); ASSERT_TRUE(f.act(Intent::Stop));
        const auto camera=f.pipeline.snapshot().camera;
        auto edited=request(); edited.exposure.requestedMicroseconds=246.25; edited.gain.requestedDb=3.25;
        edited.requestedFps=1.25;
        ASSERT_TRUE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
        ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
        ASSERT_TRUE(f.act(Intent::Confirm));
        ASSERT_TRUE(f.wait([&]{std::lock_guard lock(memory->mutex); return memory->saved &&
            memory->saved->requested.exposure.requestedMicroseconds==246.25 &&
            memory->saved->requested.requestedFps==1.25;}));
        std::lock_guard lock(memory->mutex); saved=memory->saved;
    }
    ASSERT_TRUE(saved); EXPECT_DOUBLE_EQ(*saved->lastApplied.exposure.requestedMicroseconds,246.0);
    EXPECT_DOUBLE_EQ(*saved->requested.requestedFps,1.25);
    EXPECT_DOUBLE_EQ(*saved->lastApplied.requestedFps,1.0);
    auto io=std::make_unique<MemoryIo>(); io->record=saved; Fixture f(std::move(io));
    ASSERT_TRUE(f.initialize());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable;}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired,0U);
    EXPECT_DOUBLE_EQ(*f.panel.presentation().requestedConfiguration->gain.requestedDb,3.25);
    EXPECT_DOUBLE_EQ(*f.panel.presentation().requestedConfiguration->requestedFps,1.25);
    ASSERT_TRUE(f.act(Intent::ResumeLive));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->appliedConfiguration->actual.gain.requestedDb,3.0);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->appliedConfiguration->actual.requestedFps,1.0);
    ASSERT_TRUE(nextAfter(f,1s));
    EXPECT_DOUBLE_EQ(f.latest()->raw->metadata.acquisitionSettings.requestedFps,1.25);
    EXPECT_DOUBLE_EQ(f.latest()->raw->metadata.acquisitionSettings.actualFps,1.0);
}

TEST(LivePipeline, SavedResumeCannotBypassNewUnconfirmedSettings) {
    for(const bool editFrameRate : {false,true}) {
        SCOPED_TRACE(editFrameRate ? "frame rate edit" : "exposure edit");
        auto io=std::make_unique<MemoryIo>(); io->record=savedRecord(); Fixture f(std::move(io));
        ASSERT_TRUE(f.initialize()); ASSERT_TRUE(f.wait([&]{return f.panel.presentation().resumeLiveAvailable;}));
        auto edited=request();
        if(editFrameRate) edited.requestedFps=1.25;
        else edited.exposure.requestedMicroseconds=300.0;
        ASSERT_TRUE(f.controller.applyCameraSettings(f.pipeline.snapshot().camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
        ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
        EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
        EXPECT_FALSE(f.controller.dispatch(Intent::ResumeLive).hasValue());
        EXPECT_FALSE(f.controller.dispatch(Intent::Start).hasValue());
        EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
        EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->exposure.requestedMicroseconds,
            *edited.exposure.requestedMicroseconds);
        EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->requestedFps,*edited.requestedFps);
    }
}

TEST(LivePipeline, LatePreferencesCannotOverwriteSubmittedCameraSettings) {
    Gate gate; auto io=std::make_unique<MemoryIo>(); io->record=savedRecord();
    io->record->requested.exposure.requestedMicroseconds=400.0;
    io->record->lastApplied.exposure.requestedMicroseconds=400.0;
    io->record->requested.requestedFps=2.25;
    io->record->lastApplied.requestedFps=2.0;
    io->beforeLoad=[&]{gate.block();};
    Fixture f(std::move(io)); ReleaseGate release{gate};
    ASSERT_TRUE(f.initialize()); ASSERT_TRUE(gate.wait());
    f.controller.selectCamera({"SIM-LIVE"}); ASSERT_TRUE(f.act(Intent::Connect));
    auto edited=request(); edited.exposure.requestedMicroseconds=200.0;
    edited.requestedFps=1.25;
    ASSERT_TRUE(f.controller.applyCameraSettings(f.pipeline.snapshot().camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
    ASSERT_TRUE(f.act(Intent::Confirm)); gate.release();
    ASSERT_TRUE(f.wait([&]{return f.preferences.latestStatus()->loadCompleted;}));
    EXPECT_DOUBLE_EQ(*f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,200.0);
    EXPECT_DOUBLE_EQ(*f.panel.presentation().requestedConfiguration->requestedFps,1.25);
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
    ASSERT_TRUE(f.act(Intent::Apply));
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->exposure.requestedMicroseconds,200.0);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->requestedFps,1.25);
    EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->appliedConfiguration->actual.requestedFps,1.0);
}

TEST(LivePipeline, SelectedOtherCameraAndFailedApplyCannotConfirmOrStartOldReadback) {
    Fixture f; ASSERT_TRUE(f.begin()); ASSERT_TRUE(f.act(Intent::Stop));
    const auto camera=f.pipeline.snapshot().camera;
    f.controller.selectCamera({"another-camera"});
    EXPECT_FALSE(f.controller.dispatch(Intent::Apply).hasValue());
    EXPECT_FALSE(f.controller.dispatch(Intent::Confirm).hasValue());
    EXPECT_FALSE(f.controller.dispatch(Intent::Start).hasValue());
    EXPECT_FALSE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},request()).hasValue());
    f.controller.selectCamera({"SIM-LIVE"});
    auto invalid=request(); invalid.exposure.requestedMicroseconds=2000.0;
    ASSERT_TRUE(f.pipeline.post({900U,application::ApplyConfiguration{
        camera->sessionGeneration,invalid,camera->requestedRevision+1U}}).hasValue());
    ASSERT_TRUE(f.wait([&]{const auto status=f.pipeline.snapshot().camera;
        return status->latestOutcome && status->latestOutcome->requestId==900U;}));
    EXPECT_NE(f.pipeline.snapshot().camera->requestedRevision,f.pipeline.snapshot().camera->appliedRevision);
    EXPECT_FALSE(f.controller.dispatch(Intent::Confirm).hasValue());
    EXPECT_FALSE(f.controller.dispatch(Intent::Start).hasValue());
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
}

TEST(LivePipeline, ReturningToEditedCameraRequiresApplyingTheDisplayedDefaults) {
    for(const auto intent : {Intent::Confirm,Intent::Start}) {
        SCOPED_TRACE(static_cast<int>(intent));
        Fixture f; ASSERT_TRUE(f.begin()); ASSERT_TRUE(f.act(Intent::Stop));
        const auto camera=f.pipeline.snapshot().camera;
        auto edited=request(); edited.exposure.requestedMicroseconds=250.0;
        ASSERT_TRUE(f.controller.applyCameraSettings(camera->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
        ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
        if(intent==Intent::Start) { ASSERT_TRUE(f.act(Intent::Confirm)); }
        f.controller.selectCamera({"another-camera"}); f.controller.selectCamera({"SIM-LIVE"});
        EXPECT_DOUBLE_EQ(*f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,100.0);
        EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->exposure.requestedMicroseconds,250.0);
        EXPECT_FALSE(f.panel.findChild<QPushButton*>("confirmCameraButton")->isEnabled());
        EXPECT_FALSE(f.panel.findChild<QPushButton*>("startCameraButton")->isEnabled());
        EXPECT_TRUE(f.panel.findChild<QPushButton*>("applyCameraButton")->isEnabled());
        ASSERT_FALSE(f.controller.dispatch(intent).hasValue());
        ASSERT_TRUE(f.act(Intent::Apply));
        EXPECT_DOUBLE_EQ(*f.pipeline.snapshot().camera->requestedConfiguration->exposure.requestedMicroseconds,100.0);
        EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
        ASSERT_FALSE(f.controller.dispatch(Intent::Start).hasValue());
        ASSERT_TRUE(f.act(Intent::Confirm)); ASSERT_TRUE(f.act(Intent::Start));
    }
}

TEST(LivePipeline, PendingSettingsDisconnectNeverStartsAndRetiredDialogCannotApply) {
    Fixture f; ASSERT_TRUE(f.begin()); ASSERT_TRUE(f.act(Intent::Stop));
    const auto old=f.pipeline.snapshot().camera;
    auto edited=request(); edited.gain.requestedDb=5.0;
    ASSERT_TRUE(f.controller.applyCameraSettings(old->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
    ASSERT_TRUE(f.act(Intent::Disconnect)); ASSERT_TRUE(f.act(Intent::Refresh)); ASSERT_TRUE(f.act(Intent::Connect));
    const auto current=f.pipeline.snapshot().camera;
    EXPECT_NE(current->sessionGeneration,old->sessionGeneration);
    EXPECT_FALSE(f.controller.applyCameraSettings(old->sessionGeneration,{"SIM-LIVE"},edited).hasValue());
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_EQ(f.pipeline.snapshot().camera->acquisitionCounters.acquired,0U);
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
}

}
