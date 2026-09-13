#include <lumora/presentation/FramePresenter.hpp>
#include "ControlledPresentationSink.hpp"
#include "PresentationTestFrames.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace std::chrono_literals;
using namespace lumora::presentation;
class SharedFramePresenter : public ::testing::Test {
protected:
    lumora::core::ManualClock clock;
    lumora::test::PresentationTestFrames frames;
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    lumora::test::ControlledPresentationSink sink;
    FramePresenter presenter{slot, sink, clock, 1};
    auto frame(std::uint64_t id, bool enhanced = true, double fps = 30.0) {
        return frames.frame(id, clock, enhanced, fps);
    }
    void publish(std::uint64_t id, bool enhanced = true, double fps = 30.0) {
        (void)slot.publish(frame(id, enhanced, fps));
        presenter.refresh();
    }
    void complete() {
        sink.consume();
        sink.complete(clock.steadyNow());
        sink.deliver();
        presenter.refresh();
    }
};

// Using GUI delivery time as completion time incorrectly renews freshness.
TEST_F(SharedFramePresenter, DelayedGuiDeliveryCannotRenewCompletionFreshness) {
    publish(1);
    EXPECT_EQ(presenter.displayedFrameCount(), 0U);
    sink.consume();
    clock.advance(20ms);
    sink.complete(clock.steadyNow());
    clock.advance(700ms);
    sink.deliver();
    presenter.refresh();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
}
}
namespace {
TEST_F(SharedFramePresenter, RenderDelayDoesNotPretendToComplete) {
    publish(0); sink.consume(); clock.advance(900ms); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.displayedFrameCount(), 0U);
    complete();
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 0U);
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
}
TEST_F(SharedFramePresenter, ExactReceiptRejectsWrongGenerationRevisionAndDuplicates) {
    publish(1); ASSERT_TRUE(sink.pending);
    const auto ticket = sink.pending->ticket;
    for (auto wrong : {PresentationTicket{2, 1, ticket.presentationRevision},
                       PresentationTicket{1, 1, ticket.presentationRevision + 1},
                       PresentationTicket{1, 2, ticket.presentationRevision}}) {
        sink.inject(PresentationReceipt{wrong, clock.steadyNow()}); presenter.refresh();
        EXPECT_EQ(presenter.displayedFrameCount(), 0U);
    }
    complete(); clock.advance(501ms);
    sink.inject(PresentationReceipt{ticket, clock.steadyNow()}); presenter.refresh();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
}
TEST_F(SharedFramePresenter, RepeatedAndOutOfOrderSourceIdsDoNotReplaceCompletedImage) {
    publish(3); complete(); auto completed = presenter.presentedBundle();
    for (auto id : {3U, 2U, 0U}) { publish(id); EXPECT_FALSE(sink.pending); }
    EXPECT_EQ(presenter.presentedBundle(), completed);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}
TEST_F(SharedFramePresenter, RejectedSubmissionDoesNotAdvanceAcceptedSourceIdentity) {
    publish(1); complete(); sink.reject = true; publish(9);
    EXPECT_TRUE(presenter.error());
    sink.reject = false; publish(2); complete();
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
    EXPECT_FALSE(presenter.error());
}
TEST_F(SharedFramePresenter, InvalidFpsAndUnsupportedPlanesDoNotAdvanceAcceptedIdentity) {
    publish(1); complete();
    for (double fps : {0., -1., std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::quiet_NaN()}) {
        publish(9, true, fps); EXPECT_FALSE(sink.pending);
    }
    (void)slot.publish(frames.frame(9, clock, true, 30,
        lumora::core::DisplayStorage::Gray8, lumora::core::DisplayStorage::Gray16));
    presenter.refresh(); EXPECT_FALSE(sink.pending);
    publish(2); complete();
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
}
TEST_F(SharedFramePresenter, ReadsNewestOnlyWhenReadyWithoutOwningCandidateBacklog) {
    publish(1); complete(); publish(2); sink.consume();
    auto intermediate = frame(3); std::weak_ptr weak = intermediate;
    (void)slot.publish(intermediate); intermediate.reset(); presenter.refresh();
    publish(4); EXPECT_TRUE(weak.expired());
    ASSERT_TRUE(sink.pending); EXPECT_EQ(sink.pending->ticket.sourceFrameId, 2U);
    complete(); ASSERT_TRUE(sink.pending); EXPECT_EQ(sink.pending->ticket.sourceFrameId, 4U);
    EXPECT_EQ(presenter.displayedFrameCount(), 2U);
    complete(); EXPECT_EQ(presenter.displayedFrameCount(), 3U);
}
TEST_F(SharedFramePresenter, PauseBeforeConsumeCancelsAndResumeReadsNewest) {
    publish(1); complete(); auto frozen = presenter.presentedBundle();
    publish(2); ASSERT_TRUE(sink.pending); auto cancelled = sink.pending->ticket;
    presenter.pause(); EXPECT_EQ(presenter.status().viewerState, ViewerState::Paused);
    EXPECT_FALSE(sink.pending); publish(3);
    sink.inject(PresentationReceipt{cancelled, clock.steadyNow()}); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), frozen);
    presenter.resume(); complete();
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 3U);
}
TEST_F(SharedFramePresenter, ResumeRecoversCancelledSubmissionWithoutRepublishing) {
    publish(0); presenter.pause(); presenter.resume(); complete();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}
TEST_F(SharedFramePresenter, PauseAfterConsumeWaitsForExactTerminalAndIgnoresResume) {
    publish(1); complete(); publish(2); sink.consume(); presenter.pause();
    EXPECT_EQ(presenter.status().viewerState, ViewerState::Pausing);
    presenter.resume(); publish(3);
    EXPECT_EQ(presenter.status().viewerState, ViewerState::Pausing);
    complete(); EXPECT_EQ(presenter.status().viewerState, ViewerState::Paused);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
    EXPECT_FALSE(sink.pending);
}
TEST_F(SharedFramePresenter, PauseAfterRenderWaitsForGuiReceiptAndFreezesRenderedImage) {
    publish(1); complete(); publish(2); sink.consume(); sink.complete(clock.steadyNow());
    presenter.pause(); EXPECT_EQ(presenter.status().viewerState, ViewerState::Pausing);
    sink.deliver(); presenter.refresh();
    EXPECT_EQ(presenter.status().viewerState, ViewerState::Paused);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
}
TEST_F(SharedFramePresenter, ConsumedFailureWithRetainedImageSettlesPauseOnOldImage) {
    publish(1); complete(); auto frozen = presenter.presentedBundle();
    publish(2); sink.consume(); presenter.pause(); sink.fail(true); sink.deliver();
    presenter.refresh(); EXPECT_EQ(presenter.status().viewerState, ViewerState::Paused);
    EXPECT_EQ(presenter.presentedBundle(), frozen); EXPECT_TRUE(presenter.error());
}
TEST_F(SharedFramePresenter, FailedConsumedWorkWithoutRetainedImageRecoversFrozenBundle) {
    publish(1); complete(); auto frozen = presenter.presentedBundle();
    publish(2); sink.consume(); presenter.pause(); sink.fail(false); sink.deliver();
    presenter.refresh(); EXPECT_EQ(presenter.status().viewerState, ViewerState::Paused);
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    ASSERT_TRUE(sink.pending); EXPECT_EQ(sink.pending->bundle, frozen);
    complete(); EXPECT_EQ(presenter.presentedBundle(), frozen);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}
TEST_F(SharedFramePresenter, ModeIntentCoalescesWithoutReplacingExactAdmittedTicket) {
    publish(1); complete(); publish(2); ASSERT_TRUE(sink.pending);
    auto admitted = sink.pending->ticket; sink.consume();
    presenter.setDisplayMode(DisplayMode::Compare);
    presenter.setDisplayMode(DisplayMode::Original);
    EXPECT_EQ(sink.pending->ticket, admitted);
    EXPECT_EQ(sink.pending->mode, DisplayMode::Enhanced);
    complete(); ASSERT_TRUE(sink.pending);
    EXPECT_EQ(sink.pending->ticket.sourceFrameId, 2U);
    EXPECT_GT(sink.pending->ticket.presentationRevision, admitted.presentationRevision);
    EXPECT_EQ(sink.pending->mode, DisplayMode::Original);
    EXPECT_EQ(presenter.displayMode(), DisplayMode::Enhanced);
    complete(); EXPECT_EQ(presenter.displayMode(), DisplayMode::Original);
    EXPECT_EQ(presenter.displayedFrameCount(), 2U);
}
TEST_F(SharedFramePresenter, PausedModeChangeUsesFrozenPairAndDoesNotCountAgain) {
    publish(1); complete(); presenter.pause(); publish(2, false);
    presenter.setDisplayMode(DisplayMode::Compare); complete();
    EXPECT_EQ(presenter.displayMode(), DisplayMode::Compare);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 1U);
    EXPECT_TRUE(presenter.compareAvailable());
}
TEST_F(SharedFramePresenter, FallbackChangesModeOnlyOnCompletionAndKeepsOriginalIntent) {
    publish(1); complete(); publish(2, false);
    EXPECT_EQ(presenter.displayMode(), DisplayMode::Enhanced);
    complete(); EXPECT_EQ(presenter.displayMode(), DisplayMode::Original);
    EXPECT_TRUE(presenter.originalAvailable()); EXPECT_FALSE(presenter.enhancedAvailable());
    presenter.setDisplayMode(DisplayMode::Compare); EXPECT_FALSE(sink.pending);
    publish(3); complete(); EXPECT_EQ(presenter.displayMode(), DisplayMode::Original);
}
TEST_F(SharedFramePresenter, CancellingFallbackDoesNotChangeCompletedModeOrIntent) {
    publish(1); complete(); publish(2, false); presenter.pause();
    EXPECT_EQ(presenter.displayMode(), DisplayMode::Enhanced);
    publish(3); presenter.resume(); complete();
    EXPECT_EQ(presenter.displayMode(), DisplayMode::Enhanced);
}
TEST_F(SharedFramePresenter, OrientationComesFromCompletedImageAndStaysFrozen) {
    const lumora::core::Orientation orientation{true, false, lumora::core::Rotation::Degrees180};
    (void)slot.publish(frames.frame(1, clock, true, 30,
        lumora::core::DisplayStorage::Gray8, lumora::core::DisplayStorage::Gray8, orientation));
    presenter.refresh(); EXPECT_FALSE(presenter.status().presentationOrientation); complete();
    publish(2); presenter.pause();
    EXPECT_EQ(presenter.status().presentationOrientation, orientation);
}
TEST_F(SharedFramePresenter, FpsThresholdAndWallClockJumpUseSteadyHostAge) {
    publish(1, true, 30); complete(); clock.advance(499ms); presenter.refresh();
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Current);
    clock.setUtc(std::chrono::system_clock::time_point{} + 100000h);
    clock.advance(1ms); presenter.refresh();
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
    EXPECT_EQ(presenter.status().frameAge, 500ms);
    publish(2, true, 1); complete(); clock.advance(2999ms); presenter.refresh();
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Current);
    clock.advance(1ms); presenter.refresh(); EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
    publish(3, true, 60); complete(); clock.advance(500ms); presenter.refresh();
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
}
TEST_F(SharedFramePresenter, TinyPositiveFpsDoesNotOverflowDeadline) {
    publish(1, true, 1e-300); complete(); clock.advance(100000h); presenter.refresh();
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Current);
}
TEST_F(SharedFramePresenter, ModeOnlyRepaintCannotRenewSourceCompletionDeadline) {
    // Future host timestamp isolates the independent source completion deadline.
    lumora::core::ManualClock sourceClock{clock.steadyNow() + 10s};
    (void)slot.publish(frames.frame(1, sourceClock)); presenter.refresh(); complete();
    clock.advance(499ms); presenter.setDisplayMode(DisplayMode::Original); complete();
    clock.advance(1ms); presenter.refresh();
    EXPECT_EQ(presenter.status().frameAge, 0ms);
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}
TEST_F(SharedFramePresenter, SurfaceLossRecoveryDoesNotRecountOrRenewDeadline) {
    lumora::core::ManualClock sourceClock{clock.steadyNow() + 10s};
    (void)slot.publish(frames.frame(1, sourceClock)); presenter.refresh(); complete();
    clock.advance(499ms); sink.fail(false); sink.deliver(); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), nullptr); complete();
    clock.advance(1ms); presenter.refresh();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
}
TEST_F(SharedFramePresenter, PausedSurfaceLossCanRecoverWithoutPublication) {
    publish(1); complete(); presenter.pause(); auto frozen = presenter.presentedBundle();
    sink.fail(false); sink.deliver(); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), nullptr); complete();
    EXPECT_EQ(presenter.presentedBundle(), frozen);
    EXPECT_EQ(presenter.status().viewerState, ViewerState::Paused);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}
TEST_F(SharedFramePresenter, LateFailureForSupersededCompletedTicketCannotInvalidateNewImage) {
    publish(1); complete(); ASSERT_TRUE(sink.visible); auto old = sink.visible->ticket;
    publish(2); complete(); auto current = presenter.presentedBundle();
    sink.inject(PresentationFailure{old, sink.failureError(), false}); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), current); EXPECT_FALSE(sink.pending);
}
TEST_F(SharedFramePresenter, RetirementRetainsOldOwnersUntilExactReceiptAndBindsLatestReset) {
    auto oldSlot = std::make_unique<lumora::core::LatestValueSlot<lumora::core::FrameBundle>>();
    FramePresenter p{*oldSlot, sink, clock, 10};
    auto old = frame(9); std::weak_ptr weak = old;
    (void)oldSlot->publish(old); p.refresh(); sink.consume(); sink.complete(clock.steadyNow()); sink.deliver(); p.refresh();
    old.reset(); oldSlot.reset();
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> first, latest;
    (void)first.publish(frame(0)); (void)latest.publish(frame(1));
    p.resetSource(first, 11); ASSERT_TRUE(sink.retiring); auto retirement = *sink.retiring;
    p.resetSource(latest, 12); EXPECT_EQ(*sink.retiring, retirement);
    EXPECT_FALSE(p.retirementComplete()); EXPECT_FALSE(weak.expired());
    sink.inject(PresentationRetired{retirement + 1}); p.refresh();
    EXPECT_FALSE(p.retirementComplete()); EXPECT_FALSE(weak.expired());
    sink.finishRetirement(); p.refresh();
    EXPECT_TRUE(p.retirementComplete()); EXPECT_TRUE(weak.expired());
    EXPECT_EQ(p.displayedFrameCount(), 0U); ASSERT_TRUE(sink.pending);
    EXPECT_EQ(sink.pending->ticket.sessionGeneration, 12U);
    EXPECT_EQ(sink.pending->ticket.sourceFrameId, 1U);
}
TEST_F(SharedFramePresenter, ResetFromAllAdmissionStatesRejectsLateReceiptsAndAcceptsLowId) {
    for (int phase = 0; phase != 5; ++phase) {
        lumora::core::LatestValueSlot<lumora::core::FrameBundle> original, replacement;
        lumora::test::ControlledPresentationSink localSink;
        FramePresenter p{original, localSink, clock, 4};
        (void)original.publish(frame(9)); p.refresh(); ASSERT_TRUE(localSink.pending);
        auto oldTicket = localSink.pending->ticket;
        if (phase >= 1) localSink.consume();
        if (phase >= 2) localSink.complete(clock.steadyNow());
        if (phase >= 3) { localSink.deliver(); p.refresh(); }
        if (phase == 1 || phase == 4) p.pause();
        (void)replacement.publish(frame(0)); p.resetSource(replacement, 5);
        localSink.inject(PresentationReceipt{oldTicket, clock.steadyNow()}); p.refresh();
        EXPECT_FALSE(p.retirementComplete());
        localSink.finishRetirement(); p.refresh(); ASSERT_TRUE(localSink.pending);
        EXPECT_EQ(localSink.pending->ticket.sessionGeneration, 5U);
        localSink.consume(); localSink.complete(clock.steadyNow()); localSink.deliver(); p.refresh();
        EXPECT_EQ(p.displayedFrameCount(), 1U);
        EXPECT_EQ(p.status().viewerState, ViewerState::Live);
        localSink.inject(PresentationReceipt{oldTicket, clock.steadyNow()}); p.refresh();
        ASSERT_NE(p.presentedBundle(), nullptr); EXPECT_EQ(p.presentedBundle()->sourceFrameId(), 0U);
    }
}
TEST_F(SharedFramePresenter, RetireStopsAdmissionAndClearsOwnersOnlyWhenFinished) {
    publish(1); complete(); presenter.retire(); EXPECT_FALSE(presenter.retirementComplete());
    publish(2); EXPECT_FALSE(sink.pending);
    sink.finishRetirement(); presenter.refresh();
    EXPECT_TRUE(presenter.retirementComplete()); EXPECT_EQ(presenter.presentedBundle(), nullptr);
    presenter.refresh(); EXPECT_FALSE(sink.pending);
}
}
namespace {
TEST_F(SharedFramePresenter, HiddenSinkDefersSourceReadsAndPausedRecoveryUntilReady) {
    sink.available = false; publish(1);
    EXPECT_FALSE(sink.pending); EXPECT_FALSE(presenter.error());
    publish(2); sink.available = true; presenter.refresh(); complete();
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
    presenter.pause(); sink.available = false; sink.fail(false); sink.deliver(); presenter.refresh();
    EXPECT_FALSE(sink.pending); EXPECT_EQ(presenter.presentedBundle(), nullptr);
    sink.available = true; presenter.refresh(); complete();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
}
}
namespace {
TEST_F(SharedFramePresenter, ReceiptThenInvalidationBeforeRefreshReportsNoVisibleImage) {
    publish(1); sink.consume(); sink.complete(clock.steadyNow()); sink.deliver();
    sink.fail(false); sink.deliver(); presenter.refresh();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    ASSERT_TRUE(sink.pending); EXPECT_EQ(sink.pending->ticket.sourceFrameId, 1U);
    complete(); EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_NE(presenter.presentedBundle(), nullptr);
}
TEST_F(SharedFramePresenter, FailureOfDisplayedTicketDoesNotTerminateDifferentAdmittedTicket) {
    publish(1); complete(); ASSERT_TRUE(sink.visible); auto old = sink.visible->ticket;
    publish(2); sink.consume();
    sink.inject(PresentationFailure{old, sink.failureError(), false}); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    ASSERT_TRUE(sink.pending); EXPECT_EQ(sink.pending->ticket.sourceFrameId, 2U);
    complete(); EXPECT_EQ(presenter.displayedFrameCount(), 2U);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
}
}
namespace {
TEST_F(SharedFramePresenter, RenderCompletionThenLossBeforeAnyDeliveryKeepsBothEvents) {
    publish(1); sink.consume(); sink.complete(clock.steadyNow()); sink.fail(false);
    sink.deliver(); presenter.refresh();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    complete(); EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_NE(presenter.presentedBundle(), nullptr);
}
TEST_F(SharedFramePresenter, PauseBeforeFirstFrameWaitsWithoutOwningNewestSource) {
    presenter.pause(); publish(1);
    EXPECT_EQ(presenter.status().viewerState, ViewerState::Paused);
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::WaitingForFrame);
    EXPECT_FALSE(sink.pending); EXPECT_FALSE(presenter.originalAvailable());
    clock.advance(600ms); presenter.resume(); complete();
    EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
}
TEST_F(SharedFramePresenter, RetirementReleasesBothFrozenAndAdmittedOwnersTogether) {
    auto frozen = frame(1); std::weak_ptr frozenWeak = frozen;
    (void)slot.publish(frozen); frozen.reset(); presenter.refresh(); complete();
    auto admitted = frame(2); std::weak_ptr admittedWeak = admitted;
    (void)slot.publish(admitted); admitted.reset(); presenter.refresh(); sink.consume();
    publish(3); // slot alone owns newest; presenter has only frozen + admitted.
    EXPECT_EQ(frames.objects->stats().inUse.bundles, 3U);
    presenter.retire(); EXPECT_FALSE(frozenWeak.expired()); EXPECT_FALSE(admittedWeak.expired());
    sink.finishRetirement();
    EXPECT_FALSE(frozenWeak.expired()); EXPECT_FALSE(admittedWeak.expired());
    presenter.refresh(); EXPECT_TRUE(frozenWeak.expired()); EXPECT_TRUE(admittedWeak.expired());
    EXPECT_EQ(frames.objects->stats().inUse.bundles, 1U);
}
TEST_F(SharedFramePresenter, StaleRetirementAfterBindingCannotClearReplacement) {
    publish(1); complete();
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> replacement;
    (void)replacement.publish(frame(0)); presenter.resetSource(replacement, 2);
    ASSERT_TRUE(sink.retiring); const auto retired = *sink.retiring;
    sink.finishRetirement(); presenter.refresh(); complete(); auto current = presenter.presentedBundle();
    sink.inject(PresentationRetired{retired}); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), current); EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}
TEST_F(SharedFramePresenter, ResetPreservesCompletedOriginalFallbackIntent) {
    publish(1, false); complete();
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> replacement;
    (void)replacement.publish(frame(0)); presenter.resetSource(replacement, 2);
    sink.finishRetirement(); presenter.refresh(); complete();
    EXPECT_EQ(presenter.displayMode(), DisplayMode::Original);
}
TEST_F(SharedFramePresenter, InvalidModeRequestCannotCreatePresentationWork) {
    publish(1); complete(); presenter.setDisplayMode(static_cast<DisplayMode>(99));
    EXPECT_FALSE(sink.pending); EXPECT_EQ(presenter.displayMode(), DisplayMode::Enhanced);
}
}
