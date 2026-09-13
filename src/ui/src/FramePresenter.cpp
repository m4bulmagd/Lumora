#include <lumora/ui/FramePresenter.hpp>

#include <lumora/core/Clock.hpp>
#include <lumora/presentation/FramePresenter.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QMetaObject>
#include <QTimer>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <utility>

namespace lumora::ui {
namespace {

[[nodiscard]] std::uint64_t compatibilityGeneration() noexcept {
    static std::atomic<std::uint64_t> next{0U};
    auto generation = next.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if (generation == 0U) {
        generation = next.fetch_add(1U, std::memory_order_relaxed) + 1U;
    }
    return generation;
}

[[nodiscard]] core::Error sinkBusyError() {
    return {
        core::ErrorCategory::ResourceExhaustion,
        "widgets_presentation_busy",
        "The image viewer is still completing the previous image.",
        "WidgetsPresentationSink accepts one ticket until its terminal event is drained.",
        true,
    };
}

class WidgetsPresentationSink final : public presentation::IPresentationSink {
public:
    WidgetsPresentationSink(
        ImageViewport& imageViewport,
        core::IClock& monotonicClock)
        : viewport(imageViewport), clock(monotonicClock) {
        viewport.setCompletionObserver(
            [this](const ViewportPresentation& completed) {
                complete(completed);
            });
    }

    ~WidgetsPresentationSink() override {
        viewport.setCompletionObserver({});
    }

    [[nodiscard]] bool ready() const override {
        return !pending && eventCount == 0U;
    }

    core::Result<void> submit(
        presentation::PresentationSubmission submission) override {
        if (!ready()) {
            return core::Result<void>::failure(sinkBusyError());
        }
        const auto token = submission.ticket.presentationRevision;
        auto result = viewport.present(ViewportPresentation{
            token,
            submission.mode,
            submission.bundle->originalDisplay,
            submission.bundle->enhancedDisplay,
        });
        if (!result.hasValue()) {
            return result;
        }
        pending = std::move(submission);
        return core::Result<void>::success();
    }

    bool cancelPending(presentation::PresentationTicket ticket) override {
        if (!pending || pending->ticket != ticket) {
            return false;
        }
        viewport.discardPendingPresentation();
        pending.reset();
        return true;
    }

    void retire(std::uint64_t retirementId) override {
        viewport.clear();
        pending.reset();
        clearEvents();
        pushEvent(presentation::PresentationRetired{retirementId});
    }

    std::optional<presentation::PresentationEvent> takeEvent() override {
        if (eventCount == 0U) {
            return std::nullopt;
        }
        auto event = std::move(events[eventHead]);
        events[eventHead].reset();
        eventHead = (eventHead + 1U) % events.size();
        --eventCount;
        return event;
    }

    void setEventObserver(std::function<void()> observer) {
        eventObserver = std::move(observer);
    }

private:
    void complete(const ViewportPresentation& completed) {
        if (!pending ||
            completed.token != pending->ticket.presentationRevision ||
            completed.mode != pending->mode ||
            completed.originalDisplay != pending->bundle->originalDisplay ||
            completed.enhancedDisplay != pending->bundle->enhancedDisplay) {
            return;
        }
        const auto completedAt = clock.steadyNow();
        const auto ticket = pending->ticket;
        pending.reset();
        pushEvent(presentation::PresentationReceipt{ticket, completedAt});
        if (eventObserver) {
            eventObserver();
        }
    }

    void pushEvent(presentation::PresentationEvent event) {
        if (eventCount == events.size()) {
            return;
        }
        const auto index = (eventHead + eventCount) % events.size();
        events[index] = std::move(event);
        ++eventCount;
    }

    void clearEvents() {
        for (auto& event : events) {
            event.reset();
        }
        eventHead = 0U;
        eventCount = 0U;
    }

    ImageViewport& viewport;
    core::IClock& clock;
    std::optional<presentation::PresentationSubmission> pending;
    std::array<std::optional<presentation::PresentationEvent>, 2U> events;
    std::size_t eventHead{0U};
    std::size_t eventCount{0U};
    std::function<void()> eventObserver;
};

}  // namespace

class FramePresenter::Impl final {
public:
    Impl(core::LatestValueSlot<core::FrameBundle>& sourceSlot,
         WorkstationView& workstationView, core::IClock& sourceClock,
         std::uint64_t sessionGeneration)
        : view(workstationView), clock(sourceClock),
          sink(*workstationView.imageViewport(), sourceClock),
          presenter(std::make_unique<presentation::FramePresenter>(
              sourceSlot, sink, sourceClock, sessionGeneration)) {}

    void publish() {
        view.setDisplayModeAvailability(
            presenter->originalAvailable(), presenter->enhancedAvailable());
        view.setPresentedDisplayMode(presenter->displayMode());
        view.setStatus(presenter->status());
    }

    WorkstationView& view;
    core::IClock& clock;
    QTimer timer;
    QMetaObject::Connection pauseConnection;
    QMetaObject::Connection resumeConnection;
    QMetaObject::Connection modeConnection;
    std::optional<std::chrono::steady_clock::time_point> lastTimerDelivery;
    WidgetsPresentationSink sink;
    std::unique_ptr<presentation::FramePresenter> presenter;
};

FramePresenter::FramePresenter(
    core::LatestValueSlot<core::FrameBundle>& slot,
    WorkstationView& view,
    core::IClock& clock)
    : FramePresenter(slot, view, clock, compatibilityGeneration()) {}

FramePresenter::FramePresenter(
    core::LatestValueSlot<core::FrameBundle>& slot,
    WorkstationView& view,
    core::IClock& clock,
    std::uint64_t sessionGeneration)
    : impl_(std::make_unique<Impl>(slot, view, clock, sessionGeneration)) {
    impl_->timer.setInterval(17);
    impl_->timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&impl_->timer, &QTimer::timeout, &view, [this] {
        constexpr auto minimumCadence = std::chrono::nanoseconds{1'000'000'000 / 60};
        const auto now = impl_->clock.steadyNow();
        if (impl_->lastTimerDelivery &&
            now - *impl_->lastTimerDelivery < minimumCadence) {
            return;
        }
        impl_->lastTimerDelivery = now;
        refresh();
    });
    impl_->sink.setEventObserver([this] { refresh(); });
    impl_->pauseConnection = QObject::connect(
        &view, &WorkstationView::pauseRequested, &view, [this] { pause(); });
    impl_->resumeConnection = QObject::connect(
        &view, &WorkstationView::resumeRequested, &view, [this] { resume(); });
    impl_->modeConnection = QObject::connect(
        &view, &WorkstationView::displayModeRequested, &view,
        [this](DisplayMode mode) { setDisplayMode(mode); });
    impl_->publish();
}

FramePresenter::~FramePresenter() {
    stop();
    QObject::disconnect(impl_->pauseConnection);
    QObject::disconnect(impl_->resumeConnection);
    QObject::disconnect(impl_->modeConnection);
    impl_->sink.setEventObserver({});
    impl_->presenter->retire();
    impl_->presenter->refresh();
    impl_->presenter.reset();
}

void FramePresenter::start() {
    if (!impl_->timer.isActive()) {
        impl_->timer.start();
    }
}
void FramePresenter::stop() { impl_->timer.stop(); }
void FramePresenter::setDisplayMode(DisplayMode mode) {
    impl_->presenter->setDisplayMode(mode);
    impl_->publish();
}

DisplayMode FramePresenter::displayMode() const noexcept {
    return impl_->presenter->displayMode();
}
void FramePresenter::refresh() {
    impl_->presenter->refresh();
    impl_->publish();
}
void FramePresenter::pause() {
    impl_->presenter->pause();
    impl_->publish();
}
void FramePresenter::resume() {
    impl_->presenter->resume();
    impl_->publish();
}
void FramePresenter::resetSource(
    core::LatestValueSlot<core::FrameBundle>& freshSlot) {
    resetSource(freshSlot, compatibilityGeneration());
}
void FramePresenter::resetSource(
    core::LatestValueSlot<core::FrameBundle>& freshSlot,
    std::uint64_t sessionGeneration) {
    impl_->lastTimerDelivery.reset();
    impl_->presenter->resetSource(freshSlot, sessionGeneration);
    refresh();
}
void FramePresenter::retire() {
    impl_->presenter->retire();
    refresh();
}

std::uint64_t FramePresenter::displayedFrameCount() const noexcept {
    return impl_->presenter->displayedFrameCount();
}

std::shared_ptr<const core::FrameBundle>
FramePresenter::presentedBundle() const noexcept {
    return impl_->presenter->presentedBundle();
}

bool FramePresenter::retirementComplete() const noexcept {
    return impl_->presenter->retirementComplete();
}

}  // namespace lumora::ui
