#include <lumora/ui/FramePresenter.hpp>

#include <lumora/core/Clock.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace lumora::ui {
namespace {

[[nodiscard]] bool staleFor(
    std::chrono::steady_clock::duration age,
    double actualFps) noexcept {
    if (age < std::chrono::milliseconds{500}) {
        return false;
    }
    const auto seconds = std::chrono::duration<long double>{age}.count();
    return seconds * static_cast<long double>(actualFps) >= 3.0L;
}

}  // namespace

class FramePresenter::Impl final {
public:
    Impl(core::LatestValueSlot<core::FrameBundle>& sourceSlot,
         WorkstationView& workstationView, core::IClock& sourceClock)
        : slot(&sourceSlot), view(&workstationView), clock(&sourceClock) {}

    [[nodiscard]] std::shared_ptr<const core::FrameBundle> modeSource() const {
        return paused || !pending ? completed : pending;
    }

    void updateAvailability() {
        const auto source = modeSource();
        view->setDisplayModeAvailability(source != nullptr,
            source && source->enhancedDisplay != nullptr);
    }

    [[nodiscard]] bool submit(
        std::shared_ptr<const core::FrameBundle> bundle, DisplayMode mode) {
        const auto token = ++nextToken;
        auto result = view->imageViewport()->present(ViewportPresentation{
            token, mode, bundle->originalDisplay, bundle->enhancedDisplay});
        if (!result.hasValue()) {
            return false;
        }
        pending = std::move(bundle);
        pendingMode = mode;
        pendingToken = token;
        requestedMode = mode;
        updateAvailability();
        return true;
    }

    void updateStatus() {
        WorkstationStatus status;
        status.viewerState = paused ? ViewerState::Paused : ViewerState::Live;
        if (!completed) {
            status.freshness = FrameFreshness::WaitingForFrame;
            view->setStatus(status);
            return;
        }

        const auto now = clock->steadyNow();
        const auto hostAge = std::max(
            now - completed->raw->metadata.hostReceiptTime,
            std::chrono::steady_clock::duration::zero());
        status.frameUtc = completed->raw->metadata.acquisitionUtcTime;
        status.frameAge = std::chrono::duration_cast<std::chrono::milliseconds>(hostAge);
        if (paused) {
            status.freshness = FrameFreshness::Current;
        } else {
            const auto paintAge = now - *completedAt;
            const auto actualFps =
                completed->raw->metadata.acquisitionSettings.actualFps;
            status.freshness =
                staleFor(hostAge, actualFps) || staleFor(paintAge, actualFps)
                    ? FrameFreshness::Stale
                    : FrameFreshness::Current;
        }
        view->setStatus(status);
    }

    void accept(const core::PublishedValue<core::FrameBundle>& publication) {
        examinedRevision = publication.revision;
        if (paused) {
            return;
        }
        const auto id = publication.value->sourceFrameId();
        const auto actualFps =
            publication.value->raw->metadata.acquisitionSettings.actualFps;
        if (!std::isfinite(actualFps) || actualFps <= 0.0) {
            return;
        }
        if (acceptedId && id <= *acceptedId) {
            return;
        }
        const auto mode = publication.value->enhancedDisplay
            ? requestedMode : DisplayMode::Original;
        if (!submit(publication.value, mode)) {
            return;
        }
        acceptedId = id;
    }

    void readLatest(std::uint64_t afterRevision) {
        if (auto publication = slot->consumeAfter(afterRevision)) {
            accept(*publication);
        }
    }

    core::LatestValueSlot<core::FrameBundle>* slot;
    WorkstationView* view;
    core::IClock* clock;
    QTimer timer;
    QMetaObject::Connection pauseConnection;
    QMetaObject::Connection resumeConnection;
    QMetaObject::Connection modeConnection;
    std::shared_ptr<const core::FrameBundle> completed;
    std::shared_ptr<const core::FrameBundle> pending;
    DisplayMode requestedMode{DisplayMode::Enhanced};
    DisplayMode pendingMode{DisplayMode::Enhanced};
    DisplayMode completedMode{DisplayMode::Enhanced};
    std::uint64_t nextToken{0U};
    std::uint64_t pendingToken{0U};
    std::optional<std::uint64_t> acceptedId;
    std::optional<std::chrono::steady_clock::time_point> completedAt;
    std::optional<std::chrono::steady_clock::time_point> lastTimerDelivery;
    std::uint64_t examinedRevision{0U};
    std::uint64_t displayedCount{0U};
    bool paused{false};
};

FramePresenter::FramePresenter(
    core::LatestValueSlot<core::FrameBundle>& slot,
    WorkstationView& view,
    core::IClock& clock)
    : impl_(std::make_unique<Impl>(slot, view, clock)) {
    impl_->timer.setInterval(17);
    impl_->timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&impl_->timer, &QTimer::timeout, &view, [this] {
        constexpr auto minimumCadence = std::chrono::nanoseconds{1'000'000'000 / 60};
        const auto now = impl_->clock->steadyNow();
        if (impl_->lastTimerDelivery &&
            now - *impl_->lastTimerDelivery < minimumCadence) {
            return;
        }
        impl_->lastTimerDelivery = now;
        refresh();
    });
    view.imageViewport()->setCompletionObserver(
        [this](const ViewportPresentation& receipt) {
            if (!impl_->pending || receipt.token != impl_->pendingToken ||
                receipt.mode != impl_->pendingMode ||
                receipt.originalDisplay != impl_->pending->originalDisplay ||
                receipt.enhancedDisplay != impl_->pending->enhancedDisplay) {
                return;
            }
            if (impl_->pending != impl_->completed) {
                impl_->completedAt = impl_->clock->steadyNow();
                ++impl_->displayedCount;
            }
            impl_->completed = std::move(impl_->pending);
            impl_->pending.reset();
            impl_->completedMode = receipt.mode;
            impl_->pendingToken = 0U;
            impl_->updateAvailability();
            impl_->view->setPresentedDisplayMode(impl_->completedMode);
            impl_->updateStatus();
        });
    impl_->pauseConnection = QObject::connect(
        &view, &WorkstationView::pauseRequested, &view, [this] { pause(); });
    impl_->resumeConnection = QObject::connect(
        &view, &WorkstationView::resumeRequested, &view, [this] { resume(); });
    impl_->modeConnection = QObject::connect(
        &view, &WorkstationView::displayModeRequested, &view,
        [this](DisplayMode mode) { setDisplayMode(mode); });
    impl_->updateAvailability();
}

FramePresenter::~FramePresenter() {
    stop();
    QObject::disconnect(impl_->pauseConnection);
    QObject::disconnect(impl_->resumeConnection);
    QObject::disconnect(impl_->modeConnection);
    impl_->view->imageViewport()->setCompletionObserver({});
}

void FramePresenter::start() {
    if (!impl_->timer.isActive()) {
        impl_->timer.start();
    }
}
void FramePresenter::stop() { impl_->timer.stop(); }
void FramePresenter::setDisplayMode(DisplayMode mode) {
    if (mode != DisplayMode::Original && mode != DisplayMode::Enhanced &&
        mode != DisplayMode::Compare) {
        return;
    }
    const auto source = impl_->modeSource();
    if (!source || (mode != DisplayMode::Original && !source->enhancedDisplay)) {
        return;
    }
    if (impl_->requestedMode == mode &&
        ((impl_->pending && impl_->pendingMode == mode) ||
         (!impl_->pending && impl_->completedMode == mode))) {
        return;
    }
    static_cast<void>(impl_->submit(source, mode));
}

DisplayMode FramePresenter::displayMode() const noexcept {
    return impl_->completed ? impl_->completedMode : impl_->requestedMode;
}
void FramePresenter::refresh() {
    impl_->readLatest(impl_->examinedRevision);
    impl_->updateStatus();
}
void FramePresenter::pause() {
    if (impl_->paused) {
        return;
    }
    impl_->paused = true;
    impl_->view->imageViewport()->discardPendingPresentation();
    impl_->pending.reset();
    impl_->pendingToken = 0U;
    impl_->requestedMode = impl_->completed ? impl_->completedMode : DisplayMode::Enhanced;
    impl_->updateAvailability();
    impl_->view->setPresentedDisplayMode(impl_->requestedMode);
    impl_->updateStatus();
}
void FramePresenter::resume() {
    if (!impl_->paused) {
        return;
    }
    impl_->paused = false;
    impl_->acceptedId = impl_->completed
        ? std::optional<std::uint64_t>{impl_->completed->sourceFrameId()}
        : std::nullopt;
    impl_->readLatest(0U);
    impl_->updateAvailability();
    impl_->updateStatus();
}
void FramePresenter::resetSource(
    core::LatestValueSlot<core::FrameBundle>& freshSlot) {
    impl_->view->imageViewport()->clear();
    impl_->slot = &freshSlot;
    impl_->pending.reset();
    impl_->completed.reset();
    impl_->requestedMode = DisplayMode::Enhanced;
    impl_->completedMode = DisplayMode::Enhanced;
    impl_->pendingMode = DisplayMode::Enhanced;
    impl_->pendingToken = 0U;
    impl_->acceptedId.reset();
    impl_->completedAt.reset();
    impl_->lastTimerDelivery.reset();
    impl_->examinedRevision = 0U;
    impl_->displayedCount = 0U;
    impl_->paused = false;
    impl_->updateAvailability();
    impl_->view->setPresentedDisplayMode(DisplayMode::Enhanced);
    impl_->updateStatus();
}

std::uint64_t FramePresenter::displayedFrameCount() const noexcept {
    return impl_->displayedCount;
}

std::shared_ptr<const core::FrameBundle>
FramePresenter::presentedBundle() const noexcept {
    return impl_->completed;
}

}  // namespace lumora::ui
