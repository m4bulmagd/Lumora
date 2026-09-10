#include <lumora/ui/WorkstationController.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/WorkstationView.hpp>
#include <QTimer>
#include <QLabel>
#include <limits>
#include <lumora/configuration/PresetCodec.hpp>
#include <lumora/ui/ProcessingControlsModel.hpp>
#include <lumora/ui/ProcessingPanel.hpp>
#include <algorithm>

namespace lumora::ui {
namespace {
using Result=core::Result<void>;
using Intent=CameraStartupIntent;
Result rejected() { return Result::failure({core::ErrorCategory::CameraConfiguration,
    "startup_action_unavailable","The camera action is unavailable.","Wait for the current operation or review settings.",false}); }
}
struct WorkstationController::Impl {
    application::LivePipeline& pipeline;
    configuration::StartupPreferencesService& preferences;
    WorkstationView& view;
    CameraStartupPanel& panel;
    core::IClock& clock;
    camera::CameraConfiguration fixed;
    QTimer timer;
    std::unique_ptr<ProcessingControlsModel> processingModel;
    std::unique_ptr<ProcessingPanel> processingPanel;
    std::unique_ptr<QLabel> processingLoadStatus;
    bool presetsLoaded{false};
    std::uint64_t presetSaveRevision{0};
    std::optional<core::Error> presetSaveWarning;
    void loadPresets(WorkstationController& owner,const application::StartupPreferencesStatus& status) {
        if(presetsLoaded || !status.loadCompleted) return;
        presetsLoaded=true;
        auto showError=[&](const core::Error& error) {
            processingLoadStatus->setText(QString::fromStdString(error.operatorSummary));
        };
        if(!status.loadedPresets) {
            if(status.warning) showError(*status.warning);
            else processingLoadStatus->setText(WorkstationController::tr("Processing settings are unavailable."));
            return;
        }
        auto repository=configuration::PresetCodec::loadDefaultRepository();
        if(!repository.hasValue()) { showError(repository.error()); return; }
        auto restored=repository.value().restore(*status.loadedPresets);
        if(!restored.hasValue()) { showError(restored.error()); return; }
        processingModel=std::make_unique<ProcessingControlsModel>(std::move(repository).value(),clock);
        processingPanel=std::make_unique<ProcessingPanel>(*processingModel);
        view.addSidebarPanel(processingPanel.get()); processingLoadStatus.reset();
        QObject::connect(processingPanel.get(),&ProcessingPanel::edited,&owner,[&owner]{owner.poll();});
    }
    void captureProcessing(const application::LivePipelineSnapshot& snapshot) {
        if(!processingModel) return;
        processingModel->bindSession(snapshot.context && snapshot.processingAvailable ? snapshot.context->generation : 0U);
        if(snapshot.processingConfigurationOutcome) {
            const auto& outcome=*snapshot.processingConfigurationOutcome;
            std::optional<core::Error> error;
            if(outcome.error) {
                error=outcome.error->preparationError;
                if(!error) error=core::Error{core::ErrorCategory::Processing,outcome.error->code,
                    "Processing settings were not applied.",outcome.error->violations.empty()?"":outcome.error->violations.front().detail,true};
            }
            if(processingModel->complete(outcome.sessionGeneration,outcome.configurationRevision,std::move(error))) {
                if(presetSaveRevision==std::numeric_limits<std::uint64_t>::max()) {
                    presetSaveWarning=core::Error{core::ErrorCategory::Configuration,"preset_save_revision_exhausted",
                        "Restart the application before saving more settings.","Save revision overflow.",true};
                } else {
                    auto saved=preferences.postPresetSave(++presetSaveRevision,*processingModel->acknowledged());
                    presetSaveWarning=saved.hasValue()?std::nullopt:std::optional<core::Error>{saved.error()};
                }
            }
        }
    }
    void updateProcessing(const application::LivePipelineSnapshot& snapshot) {
        if(!processingModel) return;
        captureProcessing(snapshot);
        if(auto submission=processingModel->takeSubmission()) {
            auto result=pipeline.setProcessingConfiguration({submission->sessionGeneration,submission->state.activePipeline});
            if(!result.hasValue()) processingModel->rejectAdmission(submission->sessionGeneration,
                submission->state.activePipeline.version.configurationRevision,result.error());
        }
        const auto saved=preferences.latestStatus();
        auto warning=presetSaveWarning;
        if(!warning && saved->latestAttemptedPresetSaveRevision &&
            saved->latestSavedPresetRevision<saved->latestAttemptedPresetSaveRevision && saved->warning) warning=saved->warning;
        processingPanel->setPersistenceWarning(std::move(warning));
    }

    std::unique_ptr<FramePresenter> presenter;
    std::shared_ptr<application::LiveSessionContext> context;
    CameraStartupPanelPresentation presentation;
    std::optional<std::uint64_t> pending;
    std::optional<std::uint64_t> barrier;
    std::optional<Intent> barrierIntent;
    std::uint64_t nextRequest{0};
    std::uint64_t revision{0};
    std::uint64_t saveRevision{0};
    std::optional<std::pair<std::uint64_t,std::uint64_t>> submittedConfirmation;
    std::optional<std::pair<std::uint64_t,std::uint64_t>> rejectedConfirmation;
    bool stopped{false};
    bool probeAttempted{false};
    bool manuallyDisconnected{false};
    std::optional<Intent> resumePhase;
    std::uint64_t resumeGeneration{0};
    std::optional<application::StartupPreferences> loaded;
    void captureConfirmation(const std::shared_ptr<const application::CameraStatusSnapshot>& camera) {
        if(!camera || !camera->confirmedRevision || !camera->actualIdentity || !camera->capabilities ||
            !camera->requestedConfiguration || !camera->appliedConfiguration) return;
        const auto confirmation=std::pair{camera->sessionGeneration,*camera->confirmedRevision};
        if(submittedConfirmation==confirmation || rejectedConfirmation==confirmation) return;
        const auto descriptor=std::find_if(camera->discoveredDescriptors.begin(),camera->discoveredDescriptors.end(),
            [&](const auto& value){return value.id==*camera->actualIdentity;});
        if(descriptor==camera->discoveredDescriptors.end()) return;
        application::StartupPreferences record{1,*camera->actualIdentity,descriptor->identity,*camera->capabilities,
            *camera->requestedConfiguration,camera->appliedConfiguration->actual,true};
        auto saved=preferences.postSave(++saveRevision,std::move(record));
        if(saved.hasValue()) submittedConfirmation=confirmation;
        else {
            // Permanent admission failure is not a submission or a polling retry.
            rejectedConfirmation=confirmation;presentation.startupWarning=saved.error();
        }
    }
    bool eligible(const application::CameraStatusSnapshot& camera) const {
        if(!loaded || manuallyDisconnected || !camera.actualIdentity || !camera.capabilities ||
            camera.state!=application::CameraSessionState::ConnectedIdle ||
            !application::cameraConfigurationsEqual(loaded->requested,fixed)) return false;
        const auto descriptor=std::find_if(camera.discoveredDescriptors.begin(),camera.discoveredDescriptors.end(),
            [&](const auto& value){return value.id==*camera.actualIdentity;});
        return descriptor!=camera.discoveredDescriptors.end() &&
            application::isStartupResumeEligible(*loaded,*camera.actualIdentity,descriptor->identity,*camera.capabilities);
    }
    Impl(application::LivePipeline& p,configuration::StartupPreferencesService& preferencesService,
        WorkstationView& v,CameraStartupPanel& panelWidget,core::IClock& c,camera::CameraConfiguration request)
        :pipeline(p),preferences(preferencesService),view(v),panel(panelWidget),clock(c),fixed(std::move(request)) {
        presentation.fixedRequestedConfiguration=fixed;
        processingLoadStatus=std::make_unique<QLabel>(WorkstationController::tr("Loading processing settings…"));
        processingLoadStatus->setObjectName("processingLoadStatus");
        processingLoadStatus->setTextFormat(Qt::PlainText); processingLoadStatus->setWordWrap(true);
        view.addSidebarPanel(processingLoadStatus.get());
    }
};
WorkstationController::WorkstationController(application::LivePipeline& pipeline,
    configuration::StartupPreferencesService& preferences,WorkstationView& view,
    CameraStartupPanel& panel,core::IClock& clock,camera::CameraConfiguration fixed)
    :impl_(std::make_unique<Impl>(pipeline,preferences,view,panel,clock,std::move(fixed))) {
    connect(&impl_->timer,&QTimer::timeout,this,[this]{poll();});
    connect(&panel,&CameraStartupPanel::selectionRequested,this,[this](camera::CameraId id){selectCamera(std::move(id));});
    const auto bind=[this,&panel](auto signal,Intent intent) {
        connect(&panel,signal,this,[this,intent]{(void)dispatch(intent);});
    };
    connect(&view,&WorkstationView::processingRetryRequested,this,[this] {
        auto& d=*impl_;
        if(d.stopped || !d.context) return;
        if(d.pipeline.requestProcessingRetry(d.context->generation).hasValue()) {
            auto snapshot=d.pipeline.snapshot();
            d.view.setProcessingStatus(snapshot.processing.processorStatus,snapshot.processingRetryPending);
        }
    });
    bind(&CameraStartupPanel::refreshRequested,Intent::Refresh);
    bind(&CameraStartupPanel::connectRequested,Intent::Connect);
    bind(&CameraStartupPanel::applyRequested,Intent::Apply);
    bind(&CameraStartupPanel::confirmRequested,Intent::Confirm);
    bind(&CameraStartupPanel::startRequested,Intent::Start);
    bind(&CameraStartupPanel::stopRequested,Intent::Stop);
    bind(&CameraStartupPanel::disconnectRequested,Intent::Disconnect);
    bind(&CameraStartupPanel::retryRequested,Intent::Retry);
    bind(&CameraStartupPanel::resumeLiveRequested,Intent::ResumeLive);
}
WorkstationController::~WorkstationController() { shutdown(); }
Result WorkstationController::start() {
    impl_->timer.start(16);
    return dispatch(Intent::Refresh);
}
void WorkstationController::poll() {
    auto& d=*impl_;
    if(d.stopped) return;
    auto snapshot=d.pipeline.snapshot();
    if(snapshot.context && snapshot.context!=d.context) {
        if(d.presenter) d.presenter->resetSource(snapshot.context->bundleSlot);
        else { d.presenter=std::make_unique<FramePresenter>(snapshot.context->bundleSlot,d.view,d.clock);d.presenter->start(); }
        d.context=snapshot.context;
        (void)d.pipeline.acknowledgeContext(d.context->generation);
    }
    if(d.presenter) d.presenter->refresh();
    d.view.setProcessingStatus(snapshot.processing.processorStatus,snapshot.processingRetryPending);
    d.presentation.cameraStatus=std::move(snapshot.camera);
    std::optional<Intent> continueResume;
    if(snapshot.ordinaryOutcome && d.pending==snapshot.ordinaryOutcome->requestId) {
        d.pending.reset();d.presentation.startupWarning=snapshot.ordinaryOutcome->error;
        if(d.resumePhase && !snapshot.ordinaryOutcome->error && d.presentation.cameraStatus &&
            d.presentation.cameraStatus->sessionGeneration==d.resumeGeneration && d.eligible(*d.presentation.cameraStatus)) {
            if(*d.resumePhase==Intent::Apply && d.presentation.cameraStatus->appliedConfiguration &&
                application::cameraConfigurationsEqual(d.presentation.cameraStatus->appliedConfiguration->actual,d.loaded->lastApplied))
                continueResume=Intent::Confirm;
            else if(*d.resumePhase==Intent::Confirm) continueResume=Intent::Start;
            else if(*d.resumePhase==Intent::Apply) d.presentation.startupWarning=core::Error{
                core::ErrorCategory::CameraConfiguration,"startup_readback_changed",
                "Camera readback changed. Review and confirm settings before Start.","",false};
        }
        d.resumePhase.reset();
    }
    if(snapshot.priorityOutcome && d.barrier==snapshot.priorityOutcome->requestId) { d.barrier.reset();d.barrierIntent.reset(); }
    auto preferences=d.preferences.latestStatus();
    if(preferences) {
        d.loadPresets(*this,*preferences);
        d.presentation.preferencesLoadCompleted=preferences->loadCompleted;
        d.loaded=preferences->warning ? std::nullopt : preferences->loadedPreferences;
        if(preferences->warning) d.presentation.startupWarning=preferences->warning;
    }
    d.updateProcessing(snapshot);
    if(snapshot.error) {
        d.presentation.startupWarning=snapshot.error;d.pending.reset();d.barrier.reset();d.resumePhase.reset();
        d.presentation.controlsEnabled=false;
    }
    const auto& camera=d.presentation.cameraStatus;
    d.captureConfirmation(camera);
    d.presentation.ordinaryOperationPending=d.pending.has_value() || d.barrier.has_value();
    d.presentation.resumeLiveAvailable=camera && d.eligible(*camera) && !d.presentation.startupWarning;
    d.panel.setPresentation(d.presentation);
    if(continueResume) {
        auto admitted=dispatch(*continueResume);
        if(admitted.hasValue()) d.resumePhase=continueResume;
    } else if(d.loaded && camera && !d.probeAttempted && !d.manuallyDisconnected && !d.pending && !d.barrier &&
        camera->state==application::CameraSessionState::Disconnected &&
        application::validateStartupPreferences(*d.loaded).hasValue() && d.loaded->confirmed &&
        application::cameraConfigurationsEqual(d.loaded->requested,d.fixed)) {
        const auto descriptor=std::find_if(camera->discoveredDescriptors.begin(),camera->discoveredDescriptors.end(),
            [&](const auto& value){return value.id==d.loaded->cameraId && value.available;});
        if(descriptor!=camera->discoveredDescriptors.end() && descriptor->identity.manufacturer==d.loaded->identity.manufacturer &&
            descriptor->identity.model==d.loaded->identity.model && descriptor->identity.serial==d.loaded->identity.serial) {
            d.probeAttempted=true;d.presentation.selectedCameraId=d.loaded->cameraId;(void)dispatch(Intent::Connect);
        }
    }
}
void WorkstationController::selectCamera(camera::CameraId id) {
    impl_->presentation.selectedCameraId=std::move(id);poll();
}
Result WorkstationController::dispatch(Intent intent) {
    auto& d=*impl_;
    if(d.stopped) return rejected();
    const bool priority=intent==Intent::Stop || intent==Intent::Disconnect;
    if(priority && d.barrier && (d.barrierIntent==intent || d.barrierIntent==Intent::Disconnect)) return Result::success();
    if(!priority && (d.pending || d.barrier)) return rejected();
    auto camera=d.pipeline.snapshot().camera;
    application::CameraCommand command{++d.nextRequest,application::Discover{}};
    switch(intent) {
    case Intent::Refresh: break;
    case Intent::Connect:
        if(!d.presentation.selectedCameraId) return rejected();
        d.manuallyDisconnected=false;
        command.payload=application::Connect{*d.presentation.selectedCameraId};break;
    case Intent::Apply:
        if(!camera) return rejected();
        command.payload=application::ApplyConfiguration{camera->sessionGeneration,d.fixed,++d.revision};break;
    case Intent::Confirm:
        if(!camera) return rejected();
        command.payload=application::ConfirmConfiguration{camera->sessionGeneration,camera->appliedRevision};break;
    case Intent::Start:
        if(!camera || !camera->confirmedRevision) return rejected();
        command.payload=application::StartStream{camera->sessionGeneration,*camera->confirmedRevision};break;
    case Intent::Stop: command.payload=application::StopStream{};break;
    case Intent::Disconnect:command.payload=application::Disconnect{};break;
    case Intent::Retry:command.payload=application::Retry{};break;
    case Intent::ResumeLive:
        if(!camera || !d.eligible(*camera) || d.presentation.startupWarning) return rejected();
        d.resumeGeneration=camera->sessionGeneration;
        command.payload=application::ApplyConfiguration{camera->sessionGeneration,d.loaded->requested,++d.revision};break;
    }
    auto result=d.pipeline.post(command);
    if(result.hasValue()) {
        if(priority) { d.pending.reset();d.resumePhase.reset();d.barrier=command.requestId;d.barrierIntent=intent;
            if(intent==Intent::Disconnect) { d.manuallyDisconnected=true;d.probeAttempted=true; } }
        else d.pending=command.requestId;
        if(intent==Intent::ResumeLive) d.resumePhase=Intent::Apply;
    } else d.presentation.startupWarning=result.error();
    d.presentation.ordinaryOperationPending=d.pending.has_value() || d.barrier.has_value();
    d.panel.setPresentation(d.presentation);
    return result;
}
FramePresenter* WorkstationController::presenter() const noexcept { return impl_->presenter.get(); }
void WorkstationController::shutdown() noexcept {
    auto& d=*impl_;if(d.stopped) return;d.stopped=true;d.timer.stop();
    d.pending.reset();d.barrier.reset();d.resumePhase.reset();
    d.presentation.controlsEnabled=false;d.presentation.ordinaryOperationPending=false;
    try {
        // Capture only already-confirmed facts, without advancing poll's startup
        // or Resume continuations, before camera shutdown clears those facts.
        const auto snapshot=d.pipeline.snapshot();
        d.captureConfirmation(snapshot.camera);
        d.captureProcessing(snapshot);
    } catch(...) {
        try {
            d.presentation.startupWarning=core::Error{core::ErrorCategory::Configuration,
                "startup_save_capture_failed","Startup preferences could not be captured before closing.",
                "An exception occurred while capturing the final confirmed configuration.",true};
        } catch(...) { /* Best-effort warning must not prevent teardown. */ }
    }
    try { d.panel.setPresentation(d.presentation); } catch(...) {}
    if(d.processingPanel) d.processingPanel->setEnabled(false);
    d.pipeline.shutdown();
    d.presenter.reset();d.view.imageViewport()->clear();d.context.reset();
    d.presentation.cameraStatus.reset();
    try { d.panel.setPresentation(d.presentation); } catch(...) {}
}
}
