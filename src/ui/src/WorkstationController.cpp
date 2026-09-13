#include <lumora/ui/WorkstationController.hpp>
#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/ProcessingPanel.hpp>
#include <lumora/ui/WorkstationView.hpp>
#include <QLabel>
#include <QTimer>

namespace lumora::ui {
namespace {
using Result=core::Result<void>;
using Intent=CameraStartupIntent;
}
struct WorkstationController::Impl {
    WorkstationView& view;
    CameraStartupPanel& panel;
    core::IClock& clock;
    presentation::WorkstationCoordinator coordinator;
    QTimer timer;
    std::unique_ptr<FramePresenter> presenter;
    std::unique_ptr<ProcessingPanel> processingPanel;
    std::unique_ptr<QLabel> processingLoadStatus;
    bool stopped{false};

    Impl(application::LivePipeline& pipeline,configuration::StartupPreferencesService& preferences,
        WorkstationView& v,CameraStartupPanel& p,core::IClock& c,camera::CameraConfiguration fixed)
        :view(v),panel(p),clock(c),coordinator(pipeline,preferences,c,std::move(fixed)) {
        processingLoadStatus=std::make_unique<QLabel>(WorkstationController::tr("Loading processing settings…"));
        processingLoadStatus->setObjectName("processingLoadStatus");
        processingLoadStatus->setTextFormat(Qt::PlainText);
        processingLoadStatus->setWordWrap(true);
        view.addSidebarPanel(processingLoadStatus.get());
    }
    void publish() {
        auto state=coordinator.state();
        state.workstationStatus=view.status();
        panel.setPresentation(std::move(state));
        const auto& processing=coordinator.processingState();
        view.setProcessingStatus(processing.processorStatus,processing.retryPending);
        view.setInstallationOrientation(coordinator.state().activeOrientation);
        if(processingPanel) processingPanel->setPersistenceWarning(processing.persistenceWarning);
        else if(processingLoadStatus && processing.loadError)
            processingLoadStatus->setText(processing.loadError->code=="processing_settings_unavailable"
                ? WorkstationController::tr("Processing settings are unavailable.")
                : QString::fromStdString(processing.loadError->operatorSummary));
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
            (void)impl_->coordinator.saveInstallationProfile(generation,std::move(id),orientation,confirmed,repair);
            impl_->publish();
        });
    connect(&panel,&CameraStartupPanel::settingsEditingStarted,this,
        [this](std::uint64_t generation,camera::CameraId id) {
            (void)impl_->coordinator.beginCameraSettingsEdit(generation,std::move(id));
            impl_->publish();
        });
    connect(&panel,&CameraStartupPanel::settingsApplyRequested,this,
        [this](std::uint64_t generation,camera::CameraId id,camera::CameraConfiguration request) {
            (void)applyCameraSettings(generation,std::move(id),std::move(request));
        });
    const auto bind=[this,&panel](auto signal,Intent intent) {
        connect(&panel,signal,this,[this,intent]{(void)dispatch(intent);});
    };
    connect(&view,&WorkstationView::processingRetryRequested,this,[this] {
        (void)impl_->coordinator.retryProcessing(); impl_->publish();
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
    auto result=impl_->coordinator.start(); impl_->publish(); return result;
}
void WorkstationController::poll() {
    auto& d=*impl_;
    if(d.stopped) return;
    d.coordinator.poll();
    if(auto handoff=d.coordinator.pendingContextHandoff()) {
        if(handoff->candidate) {
            if(d.presenter) d.presenter->resetSource(handoff->candidate->bundleSlot);
            else {
                d.presenter=std::make_unique<FramePresenter>(handoff->candidate->bundleSlot,d.view,d.clock);
                d.presenter->start();
            }
        } else {
            d.presenter.reset();
            d.view.imageViewport()->clear();
        }
        // Widgets resetSource clears its viewport and retained bundles before
        // returning, so this adapter can complete retirement synchronously.
        (void)d.coordinator.completeContextHandoff(handoff->id);
    }
    if(d.presenter) d.presenter->refresh();
    if(!d.processingPanel) {
        if(auto* model=d.coordinator.processingControls()) {
            d.processingPanel=std::make_unique<ProcessingPanel>(*model);
            d.view.addSidebarPanel(d.processingPanel.get());
            d.processingLoadStatus.reset();
            connect(d.processingPanel.get(),&ProcessingPanel::edited,this,[this]{poll();});
        }
    }
    d.publish();
}
void WorkstationController::selectCamera(camera::CameraId id) {
    impl_->coordinator.selectCamera(std::move(id)); impl_->publish();
}
Result WorkstationController::applyCameraSettings(std::uint64_t generation,camera::CameraId id,
    camera::CameraConfiguration requested) {
    auto result=impl_->coordinator.applyCameraSettings(generation,std::move(id),std::move(requested));
    impl_->publish(); return result;
}
Result WorkstationController::dispatch(Intent intent) {
    auto result=impl_->coordinator.dispatch(intent); impl_->publish(); return result;
}
FramePresenter* WorkstationController::presenter() const noexcept { return impl_->presenter.get(); }
void WorkstationController::shutdown() noexcept {
    auto& d=*impl_;
    if(d.stopped) return;
    d.stopped=true; d.timer.stop();
    d.coordinator.beginShutdown();
    if(d.processingPanel) d.processingPanel->setEnabled(false);
    d.presenter.reset();
    d.view.imageViewport()->clear();
    d.coordinator.completeRendererShutdown();
    try { d.publish(); } catch(...) {}
}
} // namespace lumora::ui
