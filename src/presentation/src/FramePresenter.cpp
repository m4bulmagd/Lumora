#include <lumora/presentation/FramePresenter.hpp>

#include <algorithm>
#include <utility>

namespace lumora::presentation {
namespace {
bool staleFor(std::chrono::steady_clock::duration age, double fps) noexcept {
    // Multiplication avoids overflowing a duration for tiny positive FPS.
    return age >= std::chrono::milliseconds{500} &&
        std::chrono::duration<long double>{age}.count() * static_cast<long double>(fps) >= 3.0L;
}
bool validMode(DisplayMode mode) noexcept {
    return mode == DisplayMode::Original || mode == DisplayMode::Enhanced || mode == DisplayMode::Compare;
}
}

class FramePresenter::Impl final {
public:
    Impl(core::LatestValueSlot<core::FrameBundle>& source, IPresentationSink& target,
        core::IClock& time, std::uint64_t generation)
        : slot(&source), sink(target), clock(time), sessionGeneration(generation) {}

    struct Source {
        core::LatestValueSlot<core::FrameBundle>* slot;
        std::uint64_t generation;
    };

    std::shared_ptr<const core::FrameBundle> modeSource() const noexcept {
        if (retirement || !slot) return {};
        return pending ? pending->bundle
            : retained ? retained->bundle : nullptr;
    }

    void updateStatus() {
        status = {};
        status.viewerState = state;
        if (!visible || !retained) return;
        const auto& bundle = *retained->bundle;
        const auto now = clock.steadyNow();
        const auto hostAge = std::max(now - bundle.raw->metadata.hostReceiptTime,
            std::chrono::steady_clock::duration::zero());
        status.frameAge = std::chrono::duration_cast<std::chrono::milliseconds>(hostAge);
        status.frameUtc = bundle.raw->metadata.acquisitionUtcTime;
        status.presentationOrientation = bundle.originalDisplay->presentationOrientation;
        const auto fps = bundle.raw->metadata.acquisitionSettings.actualFps;
        status.freshness = state != ViewerState::Paused &&
            (staleFor(hostAge, fps) || staleFor(now - *sourceCompletedAt, fps))
            ? FrameFreshness::Stale : FrameFreshness::Current;
    }

    bool submit(std::shared_ptr<const core::FrameBundle> bundle, DisplayMode mode) {
        PresentationSubmission submission{
            {sessionGeneration, bundle->sourceFrameId(), ++nextRevision}, std::move(bundle), mode};
        auto validation = validatePresentation(submission);
        if (!validation.hasValue()) { error = validation.error(); return false; }
        auto result = sink.submit(submission);
        if (!result.hasValue()) { error = result.error(); return false; }
        pendingRequestedMode = requestedMode;
        pending = std::move(submission);
        return true;
    }

    void handle(const PresentationReceipt& receipt) {
        if (retirement || !pending || receipt.ticket != pending->ticket) return;
        if (!countedId || receipt.ticket.sourceFrameId != *countedId) {
            ++displayedCount;
            countedId = receipt.ticket.sourceFrameId;
            sourceCompletedAt = receipt.completedAt;
        }
        // Commit fallback only if the caller has not changed intent meanwhile.
        if (requestedMode == pendingRequestedMode) requestedMode = pending->mode;
        retained = std::move(pending);
        pending.reset();
        visible = true;
        error.reset();
        if (state == ViewerState::Pausing) state = ViewerState::Paused;
    }

    void handle(const PresentationFailure& failure) {
        if (retirement) return;
        const bool admitted = pending && failure.ticket == pending->ticket;
        const bool displayed = retained && failure.ticket == retained->ticket;
        if (!admitted && !displayed) return;
        error = failure.error;
        if (!failure.previousImageRetained) visible = false;
        if (admitted) {
            pending.reset();
            acceptedId = countedId;
            if (state == ViewerState::Pausing) state = ViewerState::Paused;
        }
    }

    void handle(const PresentationRetired& event) {
        if (!retirement || event.retirementId != *retirement) return;
        pending.reset();
        retained.reset();
        visible = false;
        acceptedId.reset();
        countedId.reset();
        sourceCompletedAt.reset();
        examinedRevision = 0;
        displayedCount = 0;
        error.reset();
        retirement.reset();
        slot = replacement ? replacement->slot : nullptr;
        if (replacement) sessionGeneration = replacement->generation;
        replacement.reset();
        state = ViewerState::Live;
    }

    void drain() {
        while (auto event = sink.takeEvent()) {
            std::visit([this](const auto& value) { handle(value); }, *event);
        }
    }

    void admit() {
        if (retirement || !slot || pending || !sink.ready()) return;
        if (retained && (!visible || requestedMode != retained->mode)) {
            const auto mode = retained->bundle->enhancedDisplay ? requestedMode : DisplayMode::Original;
            (void)submit(retained->bundle, mode);
            return;
        }
        if (state != ViewerState::Live) return;
        auto publication = slot->consumeAfter(examinedRevision);
        if (!publication) return;
        examinedRevision = publication->revision;
        const auto id = publication->value->sourceFrameId();
        if (acceptedId && id <= *acceptedId) return;
        const auto mode = publication->value->enhancedDisplay ? requestedMode : DisplayMode::Original;
        if (submit(publication->value, mode)) acceptedId = id;
    }

    void beginRetirement() {
        if (retirement) return;
        retirement = ++nextRetirement;
        sink.retire(*retirement);
    }

    core::LatestValueSlot<core::FrameBundle>* slot;
    IPresentationSink& sink;
    core::IClock& clock;
    std::uint64_t sessionGeneration;
    std::uint64_t nextRevision{0};
    std::uint64_t nextRetirement{0};
    std::uint64_t examinedRevision{0};
    std::uint64_t displayedCount{0};
    std::optional<std::uint64_t> acceptedId;
    std::optional<std::uint64_t> countedId;
    std::optional<std::uint64_t> retirement;
    std::optional<Source> replacement;
    // Exactly one completed/frozen bundle plus one admitted bundle. No source
    // candidate is retained while admission is occupied or retirement pending.
    std::optional<PresentationSubmission> retained;
    std::optional<PresentationSubmission> pending;
    std::optional<std::chrono::steady_clock::time_point> sourceCompletedAt;
    DisplayMode requestedMode{DisplayMode::Enhanced};
    DisplayMode pendingRequestedMode{DisplayMode::Enhanced};
    ViewerState state{ViewerState::Live};
    bool visible{false};
    WorkstationStatus status;
    std::optional<core::Error> error;
};

FramePresenter::FramePresenter(core::LatestValueSlot<core::FrameBundle>& slot,
    IPresentationSink& sink, core::IClock& clock, std::uint64_t generation)
    : impl_(std::make_unique<Impl>(slot, sink, clock, generation)) {}
FramePresenter::~FramePresenter() = default;

void FramePresenter::refresh() {
    impl_->drain();
    impl_->admit();
    impl_->updateStatus();
}
void FramePresenter::pause() {
    if (impl_->retirement || impl_->state != ViewerState::Live) return;
    impl_->state = ViewerState::Paused;
    if (impl_->pending) {
        if (impl_->sink.cancelPending(impl_->pending->ticket)) {
            impl_->pending.reset();
            impl_->acceptedId = impl_->countedId;
            impl_->examinedRevision = 0;
            // Cancellation freezes the last completed source, but preserves
            // explicit mode intent. Automatic fallback commits only on receipt.
        } else {
            impl_->state = ViewerState::Pausing;
        }
    }
    impl_->updateStatus();
}
void FramePresenter::resume() {
    if (impl_->retirement || impl_->state != ViewerState::Paused) return;
    impl_->state = ViewerState::Live;
    impl_->acceptedId = impl_->countedId;
    impl_->examinedRevision = 0;
    refresh();
}
void FramePresenter::setDisplayMode(DisplayMode mode) {
    if (!validMode(mode)) return;
    const auto source = impl_->modeSource();
    if (!source || (mode != DisplayMode::Original && !source->enhancedDisplay)) return;
    impl_->requestedMode = mode;
    impl_->admit();
    impl_->updateStatus();
}
void FramePresenter::resetSource(core::LatestValueSlot<core::FrameBundle>& slot,
    std::uint64_t generation) {
    impl_->replacement = Impl::Source{&slot, generation};
    impl_->beginRetirement();
    impl_->updateStatus();
}
void FramePresenter::retire() {
    impl_->replacement.reset();
    if (impl_->slot) impl_->beginRetirement();
    impl_->updateStatus();
}
const WorkstationStatus& FramePresenter::status() const noexcept { return impl_->status; }
DisplayMode FramePresenter::displayMode() const noexcept {
    return impl_->visible && impl_->retained ? impl_->retained->mode : impl_->requestedMode;
}
std::uint64_t FramePresenter::displayedFrameCount() const noexcept { return impl_->displayedCount; }
std::shared_ptr<const core::FrameBundle> FramePresenter::presentedBundle() const noexcept {
    return impl_->visible && impl_->retained ? impl_->retained->bundle : nullptr;
}
bool FramePresenter::originalAvailable() const noexcept { return impl_->modeSource() != nullptr; }
bool FramePresenter::enhancedAvailable() const noexcept {
    const auto source = impl_->modeSource();
    return source && source->enhancedDisplay;
}
bool FramePresenter::compareAvailable() const noexcept { return enhancedAvailable(); }
const std::optional<core::Error>& FramePresenter::error() const noexcept { return impl_->error; }
bool FramePresenter::retirementComplete() const noexcept { return !impl_->retirement; }

}  // namespace lumora::presentation
