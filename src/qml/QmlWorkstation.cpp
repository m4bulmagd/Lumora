#include "QmlWorkstation.hpp"
#include "QuickImageItem.hpp"
#include <lumora/presentation/FramePresenter.hpp>
#include <QTimer>
#include <limits>

namespace lumora::qml {
namespace {
core::Error reserveError() {
    return {core::ErrorCategory::ResourceExhaustion, "qml_renderer_storage_overflow",
        "The image viewer storage reservation is too large.",
        "The maximum SIM Compare image and texture reservation overflowed size_t.", false};
}
}
core::Result<void> reserveSimulatorRendererStorage(processing::ProcessingPreparationOptions& options,
    std::size_t width, std::size_t height) {
    const auto assessed = QuickImageItem::assessStorage(width, height, presentation::DisplayMode::Compare);
    if (!assessed.hasValue()) return core::Result<void>::failure(assessed.error());
    auto total = options.externalSessionStorageBytes;
    for (const auto bytes : {assessed.value().currentImageBytes,
            assessed.value().replacementImageBytes, assessed.value().nominalTextureBytes}) {
        if (bytes > std::numeric_limits<std::size_t>::max() - total)
            return core::Result<void>::failure(reserveError());
        total += bytes;
    }
    options.externalSessionStorageBytes = total;
    return core::Result<void>::success();
}

struct QmlWorkstation::Impl {
    presentation::WorkstationCoordinator coordinator;
    core::IClock& clock;
    QuickImageItem item;
    CameraAdapter camera;
    ViewerAdapter viewer;
    ProcessingAdapter processing;
    QTimer timer;
    std::shared_ptr<application::LiveSessionContext> boundContext;
    std::optional<presentation::ContextHandoff> handoff;
    std::unique_ptr<presentation::FramePresenter> presenter;
    bool started{};
    bool closing{};
    bool closed{};
    QString error;

    Impl(application::LivePipeline& pipeline, configuration::StartupPreferencesService& preferences,
        core::IClock& sourceClock, camera::CameraConfiguration request, QmlWorkstation& owner)
        : coordinator(pipeline, preferences, sourceClock, std::move(request)), clock(sourceClock),
          item(sourceClock), camera(coordinator, &owner), viewer(item, &owner), processing(coordinator, &owner) {
        timer.setInterval(17);
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, &owner, &QmlWorkstation::poll);
    }
    void beginHandoff(const presentation::ContextHandoff& next) {
        handoff = next;
        if (presenter) {
            if (next.candidate)
                presenter->resetSource(next.candidate->bundleSlot, next.candidate->generation);
            else presenter->retire();
        } else if (next.candidate) {
            presenter = std::make_unique<presentation::FramePresenter>(
                next.candidate->bundleSlot, item, clock, next.candidate->generation);
            viewer.bindPresenter(presenter.get());
        }
    }
    void refreshAdapters() {
        camera.refresh();
        processing.refresh();
        viewer.refresh();
    }
};
QmlWorkstation::QmlWorkstation(application::LivePipeline& pipeline,
    configuration::StartupPreferencesService& preferences, core::IClock& clock,
    camera::CameraConfiguration request, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(pipeline, preferences, clock, std::move(request), *this)) {}
QmlWorkstation::~QmlWorkstation() {
    // A destructor cannot drain an asynchronous render job. The application
    // intercepts window close/quit and keeps this owner alive until the signal.
    Q_ASSERT_X(!impl_->started || impl_->closed, "QmlWorkstation", "Wait for shutdownComplete before destruction");
    impl_->timer.stop();
}
core::Result<void> QmlWorkstation::start() {
    auto& d = *impl_;
    if (d.started || d.closing) {
        return core::Result<void>::failure({core::ErrorCategory::Configuration,
            "qml_runtime_already_started", "The workstation cannot start again.", "", false});
    }
    auto result = d.coordinator.start();
    if (!result.hasValue()) {
        d.error = QString::fromStdString(result.error().operatorSummary);
        emit stateChanged();
        return result;
    }
    d.started = true;
    d.timer.start();
    poll();
    return result;
}
void QmlWorkstation::poll() {
    auto& d = *impl_;
    if (d.closed) return;
    if (d.closing) {
        if (d.presenter) d.presenter->refresh();
        d.viewer.refresh();
        if (d.presenter && !d.presenter->retirementComplete()) return;
        d.viewer.bindPresenter(nullptr);
        d.presenter.reset();
        d.handoff.reset();
        d.boundContext.reset();
        d.coordinator.completeRendererShutdown();
        d.closed = true;
        d.timer.stop();
        emit stateChanged();
        // The receiver may release the workstation after this terminal signal.
        emit shutdownComplete();
        return;
    }
    if (!d.started) return;
    d.coordinator.poll();
    if (const auto next = d.coordinator.pendingContextHandoff();
        next && (!d.handoff || next->id != d.handoff->id)) d.beginHandoff(*next);
    if (d.presenter) d.presenter->refresh();
    if (d.handoff && (!d.presenter || d.presenter->retirementComplete())) {
        // The presenter now borrows this candidate even if acknowledgement
        // races another replacement. Retain it before attempting the exact ID.
        d.boundContext = d.handoff->candidate;
        const auto completed = d.coordinator.completeContextHandoff(d.handoff->id);
        if (completed.hasValue()) {
            d.handoff.reset();
            if (!d.boundContext) {
                d.viewer.bindPresenter(nullptr);
                d.presenter.reset();
            }
            if (!d.error.isEmpty()) { d.error.clear(); emit stateChanged(); }
        } else {
            const auto message = QString::fromStdString(completed.error().operatorSummary);
            if (d.error != message) { d.error = message; emit stateChanged(); }
        }
    }
    d.refreshAdapters();
}
void QmlWorkstation::requestShutdown() {
    auto& d = *impl_;
    if (d.closing) return;
    d.closing = true;
    d.coordinator.beginShutdown();
    d.viewer.setClosing();
    d.camera.setClosing();
    d.processing.refresh();
    if (d.presenter) d.presenter->retire();
    // Keep draining with the event loop, including hidden/deleted/never-shown
    // visual hosts. Only a real retirement receipt permits the second phase.
    d.timer.start();
    emit stateChanged();
    poll();
}
CameraAdapter* QmlWorkstation::camera() const noexcept { return &impl_->camera; }
ViewerAdapter* QmlWorkstation::viewer() const noexcept { return &impl_->viewer; }
ProcessingAdapter* QmlWorkstation::processing() const noexcept { return &impl_->processing; }
presentation::WorkstationCoordinator& QmlWorkstation::coordinator() noexcept { return impl_->coordinator; }
bool QmlWorkstation::closing() const noexcept { return impl_->closing; }
bool QmlWorkstation::closed() const noexcept { return impl_->closed; }
QString QmlWorkstation::error() const { return impl_->error; }
}
