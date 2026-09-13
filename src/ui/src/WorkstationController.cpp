#include <lumora/ui/WorkstationController.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/application/CameraSettingsPolicy.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/CameraSettingsModel.hpp>
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
    const camera::CameraConfiguration fixed;
    camera::CameraConfiguration desired;
    std::optional<camera::CameraId> desiredCameraId;
    bool requestChosen{false};
    std::optional<std::pair<std::uint64_t, camera::CameraId>> settingsEditSource;
    bool initialRequestConsidered{false};
    bool savedRequestReconciliationPending{true};
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
    std::uint64_t installationRequest{0};
    std::optional<std::pair<std::uint64_t,std::uint64_t>> submittedConfirmation;
    std::optional<std::pair<std::uint64_t,std::uint64_t>> rejectedConfirmation;
    bool stopped{false};
    bool probeAttempted{false};
    bool manuallyDisconnected{false};
    std::optional<Intent> resumePhase;
    std::uint64_t resumeGeneration{0};
    std::uint64_t resumeApplyRevision{0};
    application::CameraPreferences initialProfiles;
    std::vector<application::StartupPreferences> currentProfiles;
    std::optional<application::StartupPreferences> loaded;
    std::optional<application::StartupPreferences> profileFor(const camera::CameraId& id,
        const std::shared_ptr<const application::CameraStatusSnapshot>& camera) const {
        const camera::CameraDescriptor* descriptor=nullptr;
        if(camera) {
            const auto found=std::find_if(camera->discoveredDescriptors.begin(),camera->discoveredDescriptors.end(),
                [&](const auto& value){return value.id==id;});
            if(found!=camera->discoveredDescriptors.end()) descriptor=&*found;
        }
        const auto matches=[&](const auto& record) {
            return descriptor?application::cameraIdentityKeysEqual(record.identity,descriptor->identity):record.cameraId==id;
        };
        for(const auto* records:{&currentProfiles,&initialProfiles.profiles}) {
            const auto found=std::find_if(records->begin(),records->end(),matches);
            if(found!=records->end()) return *found;
        }
        return std::nullopt;
    }
    void refreshSelectedProfile() {
        loaded=presentation.selectedCameraId?profileFor(*presentation.selectedCameraId,presentation.cameraStatus):std::nullopt;
    }
    void reconcileSavedRequest() {
        const auto& camera=presentation.cameraStatus;
        if(!savedRequestReconciliationPending || !initialRequestConsidered ||
            !presentation.selectedCameraId || !camera) return;
        const auto descriptor=std::find_if(camera->discoveredDescriptors.begin(),camera->discoveredDescriptors.end(),
            [&](const auto& value){return value.id==*presentation.selectedCameraId;});
        if(descriptor==camera->discoveredDescriptors.end()) return;
        // Both initial provenance and the selected stable identity are now
        // known. An accepted Apply consumes this opportunity before late data
        // can replace an operator's submitted request.
        savedRequestReconciliationPending=false;
        desired=fixed;
        if(loaded && loaded->confirmed && application::validateStartupPreferences(*loaded).hasValue() &&
            application::isSupportedLiveCameraConfiguration(loaded->requested) &&
            (!camera->actualIdentity || camera->actualIdentity!=presentation.selectedCameraId || !camera->capabilities ||
                application::cameraCapabilitiesEqual(loaded->confirmedCapabilities,*camera->capabilities)))
            desired=loaded->requested;
        desiredCameraId=presentation.selectedCameraId;
        presentation.requestedConfiguration=desired;
    }
    core::Result<camera::CameraConfiguration> normalizedDesiredForCurrentSource() const {
        using ConfigurationResult=core::Result<camera::CameraConfiguration>;
        const auto& camera=presentation.cameraStatus;
        if(!camera || !camera->actualIdentity || !camera->capabilities || !camera->currentConfiguration ||
            presentation.selectedCameraId!=camera->actualIdentity) {
            return ConfigurationResult::failure(rejected().error());
        }
        return normalizeCameraSettingsDraft(desired,*camera->currentConfiguration,*camera->capabilities);
    }
    void remember(const application::StartupPreferences& record) {
        const auto found=std::find_if(currentProfiles.begin(),currentProfiles.end(),[&](const auto& value) {
            return application::cameraIdentityKeysEqual(value.identity,record.identity);
        });
        if(found!=currentProfiles.end()) *found=record;
        else if(currentProfiles.size()<application::CameraPreferences::MaximumProfiles) currentProfiles.push_back(record);
    }
    void captureConfirmation(const application::LivePipelineSnapshot& snapshot) {
        const auto& camera=snapshot.camera;
        if(!camera || !camera->confirmedRevision || !camera->actualIdentity || !camera->capabilities ||
            !camera->requestedConfiguration || !camera->appliedConfiguration ||
            camera->requestedRevision!=camera->appliedRevision ||
            *camera->confirmedRevision!=camera->appliedRevision) return;
        const auto confirmation=std::pair{camera->sessionGeneration,*camera->confirmedRevision};
        if(submittedConfirmation==confirmation || rejectedConfirmation==confirmation) return;
        const auto descriptor=std::find_if(camera->discoveredDescriptors.begin(),camera->discoveredDescriptors.end(),
            [&](const auto& value){return value.id==*camera->actualIdentity;});
        if(descriptor==camera->discoveredDescriptors.end()) return;
        application::StartupPreferences record{1,*camera->actualIdentity,descriptor->identity,*camera->capabilities,
            *camera->requestedConfiguration,camera->appliedConfiguration->actual,true};
        record.installationProfile=snapshot.activeInstallationProfile;
        if(saveRevision==std::numeric_limits<std::uint64_t>::max()) {
            rejectedConfirmation=confirmation;presentation.startupWarning=rejected().error();return;
        }
        auto saved=preferences.postSave(++saveRevision,record,false);
        if(saved.hasValue()) remember(record);
        if(saved.hasValue()) submittedConfirmation=confirmation;
        else {
            // Permanent admission failure is not a submission or a polling retry.
            rejectedConfirmation=confirmation;presentation.startupWarning=saved.error();
        }
    }
    bool eligible(const application::CameraStatusSnapshot& camera) const {
        if(!loaded || manuallyDisconnected || !camera.actualIdentity || !camera.capabilities ||
            !camera.currentConfiguration ||
            camera.state!=application::CameraSessionState::ConnectedIdle ||
            !application::isSupportedLiveCameraConfiguration(loaded->requested) ||
            !application::cameraConfigurationsEqual(loaded->requested,desired) ||
            presentation.selectedCameraId!=camera.actualIdentity) return false;
        if(!camera::planCameraConfigurationChange(
            loaded->requested,*camera.currentConfiguration,*camera.capabilities,false).hasValue()) return false;
        const auto descriptor=std::find_if(camera.discoveredDescriptors.begin(),camera.discoveredDescriptors.end(),
            [&](const auto& value){return value.id==*camera.actualIdentity;});
        if(descriptor==camera.discoveredDescriptors.end() ||
            !application::isStartupResumeEligible(*loaded,*camera.actualIdentity,descriptor->identity,*camera.capabilities)) return false;
        const auto snapshot=pipeline.snapshot();
        if(snapshot.installationProfilePending) return false;
        if(!snapshot.installationProfiles) return !loaded->installationProfile;
        const auto resolved=application::resolveInstallationProfile(*snapshot.installationProfiles,descriptor->identity,*camera.capabilities);
        if(!resolved.hasValue()) return false;
        const auto reference=resolved.value()?std::optional{application::installationProfileReference(*resolved.value())}:std::nullopt;
        return reference==loaded->installationProfile;
    }
    bool installationBindingCurrent(const application::LivePipelineSnapshot& snapshot) const {
        if(!snapshot.installationProfiles) return true;
        if(snapshot.installationProfilePending || !snapshot.camera || !snapshot.camera->actualIdentity || !snapshot.camera->capabilities) return false;
        const auto& camera=*snapshot.camera;
        const auto descriptor=std::find_if(camera.discoveredDescriptors.begin(),camera.discoveredDescriptors.end(),
            [&](const auto& value){return value.id==*camera.actualIdentity;});
        if(descriptor==camera.discoveredDescriptors.end()) return false;
        const auto resolved=application::resolveInstallationProfile(*snapshot.installationProfiles,descriptor->identity,*camera.capabilities);
        if(!resolved.hasValue()) return false;
        const auto reference=resolved.value()?std::optional{application::installationProfileReference(*resolved.value())}:std::nullopt;
        return reference==snapshot.activeInstallationProfile;
    }
    bool installationBusy(const application::LivePipelineSnapshot& snapshot) const {
        return snapshot.installationProfilePending || (snapshot.installationProfiles && snapshot.installationProfiles->savePending);
    }
    bool hasSuccessfulDesiredRequest(const application::CameraStatusSnapshot& camera) const {
        return camera.requestedConfiguration && camera.appliedConfiguration &&
            camera.appliedRevision!=0U && camera.requestedRevision==camera.appliedRevision &&
            application::cameraConfigurationsEqual(*camera.requestedConfiguration,desired) &&
            application::cameraConfigurationsEqual(camera.appliedConfiguration->requested,desired);
    }
    bool isOwnResumeReconfiguration(const application::LivePipelineSnapshot& snapshot) const {
        const auto& camera=snapshot.camera;
        return resumePhase==Intent::Apply && pending && snapshot.ordinaryOutcome &&
            snapshot.ordinaryOutcome->requestId==*pending && !snapshot.ordinaryOutcome->error &&
            camera && camera->latestOutcome && camera->latestOutcome->requestId==*pending &&
            snapshot.context && snapshot.context->generation==camera->sessionGeneration &&
            resumeGeneration!=std::numeric_limits<std::uint64_t>::max() &&
            camera->sessionGeneration==resumeGeneration+1U &&
            camera->appliedRevision==resumeApplyRevision &&
            hasSuccessfulDesiredRequest(*camera) && eligible(*camera);
    }
    Result post(const application::CameraCommand& command, Intent intent) {
        const bool priority=intent==Intent::Stop || intent==Intent::Disconnect;
        auto result=pipeline.post(command);
        if(result.hasValue()) {
            if(priority) {
                pending.reset(); resumePhase.reset(); barrier=command.requestId; barrierIntent=intent;
                if(intent==Intent::Disconnect) { manuallyDisconnected=true; probeAttempted=true; }
            } else pending=command.requestId;
            if(intent==Intent::Apply || intent==Intent::ResumeLive) savedRequestReconciliationPending=false;
            if(intent==Intent::ResumeLive) resumePhase=Intent::Apply;
        } else presentation.startupWarning=result.error();
        presentation.ordinaryOperationPending=pending.has_value() || barrier.has_value();
        presentation.resumeLiveAvailable=false;
        return result;
    }
    Impl(application::LivePipeline& p,configuration::StartupPreferencesService& preferencesService,
        WorkstationView& v,CameraStartupPanel& panelWidget,core::IClock& c,camera::CameraConfiguration request)
        :pipeline(p),preferences(preferencesService),view(v),panel(panelWidget),clock(c),fixed(std::move(request)),desired(fixed) {
        presentation.requestedConfiguration=desired;
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
    connect(&panel,&CameraStartupPanel::installationSaveRequested,this,
        [this](std::uint64_t generation,camera::CameraId id,core::Orientation orientation,bool confirmed,bool repair) {
            auto& d=*impl_;
            const auto snapshot=d.pipeline.snapshot();
            if(d.installationBusy(snapshot) || d.presentation.installationProfilePending ||
                d.installationRequest==std::numeric_limits<std::uint64_t>::max()) return;
            const auto requestId=++d.installationRequest;
            const auto& camera=snapshot.camera;
            auto result=rejected();
            if(!d.stopped && d.presentation.controlsEnabled && !d.pending && !d.barrier && !d.installationBusy(snapshot) &&
                camera && camera->state==application::CameraSessionState::ConnectedIdle && camera->sessionGeneration==generation &&
                camera->actualIdentity==id && d.presentation.selectedCameraId==id &&
                snapshot.installationProfiles && snapshot.installationProfiles->administratorMode && confirmed) {
                result=d.pipeline.saveInstallationProfile({requestId,generation,std::move(id),orientation,confirmed,repair});
            }
            d.resumePhase.reset();d.presentation.resumeLiveAvailable=false;
            d.presentation.installationProfilePending=result.hasValue() || d.installationBusy(snapshot);
            d.presentation.ordinaryOperationPending=d.pending.has_value() || d.barrier.has_value() || d.presentation.installationProfilePending;
            if(!result.hasValue()) {
                d.presentation.installationProfileOutcome=application::InstallationSaveOutcome{requestId,{},result.error()};
                d.presentation.installationProfileError=result.error();
            }
            d.panel.setPresentation(d.presentation);
        });
    connect(&panel,&CameraStartupPanel::settingsEditingStarted,this,
        [this](std::uint64_t generation,camera::CameraId id) {
            auto& d=*impl_;
            const auto snapshot=d.pipeline.snapshot();
            const auto& camera=snapshot.camera;
            if(d.stopped || !d.presentation.controlsEnabled || d.pending || d.barrier || d.installationBusy(snapshot) ||
                !camera || camera->state!=application::CameraSessionState::ConnectedIdle ||
                camera->sessionGeneration!=generation || camera->actualIdentity!=id || d.presentation.selectedCameraId!=id) return;
            d.savedRequestReconciliationPending=false;
            d.requestChosen=true;
            d.settingsEditSource=std::pair{generation,std::move(id)};
        });
    connect(&panel,&CameraStartupPanel::settingsApplyRequested,this,
        [this](std::uint64_t generation,camera::CameraId id,camera::CameraConfiguration request) {
            (void)applyCameraSettings(generation,std::move(id),std::move(request));
        });
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
    d.presentation.workstationStatus=d.view.status();
    d.view.setProcessingStatus(snapshot.processing.processorStatus,snapshot.processingRetryPending);
    d.presentation.installationProfiles=snapshot.installationProfiles;
    d.presentation.activeInstallationProfile=snapshot.activeInstallationProfile;
    d.presentation.activeOrientation=snapshot.context && snapshot.camera && snapshot.camera->actualIdentity
        ?std::optional{snapshot.activeOrientation}:std::nullopt;
    d.view.setInstallationOrientation(d.presentation.activeOrientation);
    d.presentation.installationProfilePending=d.installationBusy(snapshot);
    d.presentation.installationBindingCurrent=d.installationBindingCurrent(snapshot);
    if(snapshot.installationProfileOutcome && (!d.presentation.installationProfileOutcome ||
        snapshot.installationProfileOutcome->requestId>=d.presentation.installationProfileOutcome->requestId)) {
        d.presentation.installationProfileOutcome=snapshot.installationProfileOutcome;
    }
    d.presentation.installationProfileError=snapshot.installationProfileError;
    const bool ownResumeReconfiguration=d.isOwnResumeReconfiguration(snapshot);
    d.presentation.cameraStatus=snapshot.camera;
    const auto& currentCamera=d.presentation.cameraStatus;
    if(d.resumePhase && currentCamera && currentCamera->sessionGeneration!=d.resumeGeneration &&
        !ownResumeReconfiguration) {
        d.resumePhase.reset();
        d.pending.reset();
    }
    std::optional<Intent> continueResume;
    if(snapshot.ordinaryOutcome && d.pending==snapshot.ordinaryOutcome->requestId) {
        d.pending.reset();d.presentation.startupWarning=snapshot.ordinaryOutcome->error;
        if(d.resumePhase && !snapshot.ordinaryOutcome->error && currentCamera &&
            (currentCamera->sessionGeneration==d.resumeGeneration || ownResumeReconfiguration) &&
            d.eligible(*currentCamera) && d.hasSuccessfulDesiredRequest(*currentCamera) &&
            currentCamera->appliedRevision==d.resumeApplyRevision) {
            if(!application::cameraConfigurationsEqual(currentCamera->appliedConfiguration->actual,d.loaded->lastApplied)) {
                d.presentation.startupWarning=core::Error{
                    core::ErrorCategory::CameraConfiguration,"startup_readback_changed",
                    "Camera readback changed. Review and confirm settings before Start.","",false};
            } else if(*d.resumePhase==Intent::Apply) {
                d.resumeGeneration=currentCamera->sessionGeneration;
                continueResume=Intent::Confirm;
            } else if(*d.resumePhase==Intent::Confirm) continueResume=Intent::Start;
        }
        d.resumePhase.reset();
    }
    if(snapshot.priorityOutcome && d.barrier==snapshot.priorityOutcome->requestId) { d.barrier.reset();d.barrierIntent.reset(); }
    auto preferences=d.preferences.latestStatus();
    if(preferences) {
        d.loadPresets(*this,*preferences);
        d.presentation.preferencesLoadCompleted=preferences->loadCompleted;
        if(preferences->warning) d.presentation.startupWarning=preferences->warning;
        if(preferences->loadCompleted && !d.initialRequestConsidered) {
            d.initialRequestConsidered=true;
            if(preferences->loadedCameraPreferences) d.initialProfiles=*preferences->loadedCameraPreferences;
            else if(preferences->loadedPreferences) {
                d.initialProfiles.lastSelectedCameraId=preferences->loadedPreferences->cameraId;
                d.initialProfiles.profiles.push_back(*preferences->loadedPreferences);
            }
            if(!d.requestChosen) {
                d.presentation.selectedCameraId=d.initialProfiles.lastSelectedCameraId;
                d.refreshSelectedProfile();
                if(d.loaded && d.loaded->confirmed && application::validateStartupPreferences(*d.loaded).hasValue() &&
                    application::isSupportedLiveCameraConfiguration(d.loaded->requested)) {
                    d.desired=d.loaded->requested;d.desiredCameraId=d.presentation.selectedCameraId;
                    d.presentation.requestedConfiguration=d.desired;
                }
            }
        }
    }
    d.updateProcessing(snapshot);
    if(snapshot.error) {
        d.presentation.startupWarning=snapshot.error;d.pending.reset();d.barrier.reset();d.resumePhase.reset();
        d.presentation.controlsEnabled=false;
    }
    const auto& camera=d.presentation.cameraStatus;
    d.captureConfirmation(snapshot);
    d.refreshSelectedProfile();
    d.reconcileSavedRequest();
    if(d.settingsEditSource && (!camera || !camera->actualIdentity ||
        camera->sessionGeneration!=d.settingsEditSource->first ||
        *camera->actualIdentity!=d.settingsEditSource->second)) d.settingsEditSource.reset();
    if(!d.settingsEditSource && camera && camera->currentConfiguration && camera->capabilities &&
        camera->actualIdentity==d.presentation.selectedCameraId) {
        auto normalized=d.normalizedDesiredForCurrentSource();
        if(normalized.hasValue()) {
            d.desired=std::move(normalized).value();
            d.desiredCameraId=camera->actualIdentity;
            d.presentation.requestedConfiguration=d.desired;
        }
    }
    d.presentation.ordinaryOperationPending=d.pending.has_value() || d.barrier.has_value() || d.installationBusy(snapshot);
    d.presentation.resumeLiveAvailable=camera && d.eligible(*camera) && !d.presentation.startupWarning;
    d.panel.setPresentation(d.presentation);
    if(continueResume) {
        auto admitted=dispatch(*continueResume);
        if(admitted.hasValue()) d.resumePhase=continueResume;
    } else if(d.loaded && camera && !d.probeAttempted && !d.manuallyDisconnected && !d.pending && !d.barrier &&
        camera->state==application::CameraSessionState::Disconnected &&
        application::validateStartupPreferences(*d.loaded).hasValue() && d.loaded->confirmed &&
        application::isSupportedLiveCameraConfiguration(d.loaded->requested) &&
        application::cameraConfigurationsEqual(d.loaded->requested,d.desired)) {
        const auto descriptor=std::find_if(camera->discoveredDescriptors.begin(),camera->discoveredDescriptors.end(),
            [&](const auto& value){return value.id==d.loaded->cameraId && value.available;});
        if(descriptor!=camera->discoveredDescriptors.end() && descriptor->identity.manufacturer==d.loaded->identity.manufacturer &&
            descriptor->identity.model==d.loaded->identity.model && descriptor->identity.serial==d.loaded->identity.serial) {
            d.probeAttempted=true;(void)dispatch(Intent::Connect);
        }
    }
}
void WorkstationController::selectCamera(camera::CameraId id) {
    auto& d=*impl_;
    if(d.stopped) return;
    const auto camera=d.pipeline.snapshot().camera;
    const bool desiredUnsupported=camera && camera->actualIdentity==id && camera->capabilities &&
        !camera::validateCameraConfiguration(d.desired,*camera->capabilities).hasValue();
    if(!d.desiredCameraId || *d.desiredCameraId!=id || desiredUnsupported) {
        d.savedRequestReconciliationPending=true;
        d.desired=d.fixed;
        const auto record=d.profileFor(id,camera);
        if(record && record->confirmed && application::validateStartupPreferences(*record).hasValue() &&
            application::isSupportedLiveCameraConfiguration(record->requested) &&
            (!camera || camera->actualIdentity!=id || !camera->capabilities ||
                application::cameraCapabilitiesEqual(record->confirmedCapabilities,*camera->capabilities)))
            d.desired=record->requested;
        d.presentation.requestedConfiguration=d.desired;
    }
    d.desiredCameraId=id;
    d.settingsEditSource.reset();
    d.requestChosen=true; d.probeAttempted=true; d.resumePhase.reset();
    d.presentation.selectedCameraId=id;
    if(d.saveRevision==std::numeric_limits<std::uint64_t>::max()) d.presentation.startupWarning=rejected().error();
    else if(auto saved=d.preferences.postSelection(++d.saveRevision,std::move(id));!saved.hasValue())
        d.presentation.startupWarning=saved.error();
    poll();
}
Result WorkstationController::applyCameraSettings(
    std::uint64_t generation, camera::CameraId id, camera::CameraConfiguration requested) {
    auto& d=*impl_;
    const auto fail=[&d](core::Error error) {
        d.presentation.startupWarning=error;
        d.presentation.resumeLiveAvailable=false;
        d.panel.setPresentation(d.presentation);
        return Result::failure(std::move(error));
    };
    if(d.stopped || !d.presentation.controlsEnabled || d.pending || d.barrier)
        return fail(rejected().error());
    const auto snapshot=d.pipeline.snapshot();
    const auto camera=snapshot.camera;
    if(d.installationBusy(snapshot) || !camera || camera->state!=application::CameraSessionState::ConnectedIdle ||
        camera->sessionGeneration!=generation || !camera->actualIdentity ||
        *camera->actualIdentity!=id || d.presentation.selectedCameraId!=camera->actualIdentity ||
        !camera->capabilities || !camera->currentConfiguration ||
        !application::isSupportedLiveCameraConfiguration(requested))
        return fail(rejected().error());
    auto valid=camera::validateCameraConfiguration(requested,*camera->capabilities);
    if(!valid.hasValue()) return fail(valid.error());
    auto plan=camera::planCameraConfigurationChange(
        requested,*camera->currentConfiguration,*camera->capabilities,false);
    if(!plan.hasValue()) return fail(plan.error());
    if(d.revision==std::numeric_limits<std::uint64_t>::max() ||
        d.nextRequest==std::numeric_limits<std::uint64_t>::max())
        return fail(rejected().error());
    auto admitted=d.post({++d.nextRequest,application::ApplyConfiguration{
        generation,requested,++d.revision}},Intent::Apply);
    if(admitted.hasValue()) {
        d.desired=std::move(requested); d.desiredCameraId=std::move(id); d.requestChosen=true;
        d.settingsEditSource.reset();
        d.resumePhase.reset();
        d.presentation.requestedConfiguration=d.desired;
    }
    // Publish admission and its desired request together so the dialog can
    // distinguish its own submission from an external request change.
    d.panel.setPresentation(d.presentation);
    return admitted;
}
Result WorkstationController::dispatch(Intent intent) {
    auto& d=*impl_;
    if(d.stopped || !d.presentation.controlsEnabled) return rejected();
    const bool priority=intent==Intent::Stop || intent==Intent::Disconnect;
    if(priority && d.barrier && (d.barrierIntent==intent || d.barrierIntent==Intent::Disconnect)) return Result::success();
    if(!priority && (d.pending || d.barrier)) return rejected();
    const auto snapshot=d.pipeline.snapshot();
    if(!priority && d.installationBusy(snapshot)) return rejected();
    auto camera=snapshot.camera;
    const bool settingsIntent=intent==Intent::Apply || intent==Intent::Confirm || intent==Intent::Start;
    if(settingsIntent && (!camera || !camera->actualIdentity ||
        d.presentation.selectedCameraId!=camera->actualIdentity)) return rejected();
    if((intent==Intent::Confirm || intent==Intent::Start) &&
        (!d.hasSuccessfulDesiredRequest(*camera) || !d.installationBindingCurrent(snapshot))) return rejected();
    if(intent==Intent::Start && (!camera->confirmedRevision ||
        *camera->confirmedRevision!=camera->appliedRevision)) return rejected();
    if(intent==Intent::Apply) {
        const auto failPreflight=[&d](core::Error error) {
            d.presentation.startupWarning=error;
            d.presentation.resumeLiveAvailable=false;
            d.panel.setPresentation(d.presentation);
            return Result::failure(std::move(error));
        };
        if(!camera->currentConfiguration || !camera->capabilities)
            return failPreflight(rejected().error());
        auto normalized=normalizeCameraSettingsDraft(
            d.desired,*camera->currentConfiguration,*camera->capabilities);
        if(!normalized.hasValue()) return failPreflight(normalized.error());
        auto plan=camera::planCameraConfigurationChange(
            normalized.value(),*camera->currentConfiguration,*camera->capabilities,false);
        if(!plan.hasValue()) return failPreflight(plan.error());
        d.desired=std::move(normalized).value();
        d.desiredCameraId=camera->actualIdentity;
        d.presentation.requestedConfiguration=d.desired;
    }
    if(d.nextRequest==std::numeric_limits<std::uint64_t>::max() ||
        ((intent==Intent::Apply || intent==Intent::ResumeLive) &&
         d.revision==std::numeric_limits<std::uint64_t>::max())) return rejected();
    application::CameraCommand command{++d.nextRequest,application::Discover{}};
    switch(intent) {
    case Intent::Refresh: break;
    case Intent::Connect:
        if(!d.presentation.selectedCameraId) return rejected();
        d.manuallyDisconnected=false;
        command.payload=application::Connect{*d.presentation.selectedCameraId};break;
    case Intent::Apply:
        if(!camera) return rejected();
        command.payload=application::ApplyConfiguration{camera->sessionGeneration,d.desired,++d.revision};break;
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
        d.resumeApplyRevision=++d.revision;
        command.payload=application::ApplyConfiguration{camera->sessionGeneration,d.loaded->requested,d.resumeApplyRevision};break;
    }
    auto result=d.post(command,intent);
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
        d.captureConfirmation(snapshot);
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
