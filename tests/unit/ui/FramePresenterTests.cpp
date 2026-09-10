#include <lumora/ui/FramePresenter.hpp>

#include "ViewportTestSupport.hpp"

#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/WorkstationStatus.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QLabel>
#include <QCoreApplication>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <limits>
#include <optional>
#include <ranges>

namespace {

using namespace std::chrono_literals;
using lumora::core::FrameBundle;
using lumora::core::LatestValueSlot;
using lumora::core::ManualClock;
using lumora::ui::FrameFreshness;
using lumora::ui::FramePresenter;
using lumora::ui::ViewerState;
using lumora::ui::WorkstationView;

void paint(WorkstationView& view) {
    static_cast<void>(lumora::test::paintWidget(*view.imageViewport()));
}

std::shared_ptr<const FrameBundle> makeEnhancedBundle(
    std::uint64_t id, ManualClock& clock) {
    const auto original = lumora::test::makeBundle(64U, 32U, id, clock);
    const auto& displayLayout = original->originalDisplay->layout;
    auto displayPool = lumora::core::BufferPool::create(
        1U, displayLayout.payloadBytes()).value();
    auto displayLease = displayPool->tryAcquire();
    std::ranges::fill(displayLease->bytes(), std::byte{0xE0});
    auto display = lumora::core::DisplayFrame::create(
        id, displayLayout, std::move(*displayLease).seal(),
        lumora::core::DisplayStorage::Gray8, original->originalDisplay->mapping,
        original->originalDisplay->presentationOrientation).value();
    const auto processedLayout = lumora::core::ImageLayout::create(
        64U, 32U, 128U, lumora::core::StorageType::UInt16, 4096U).value();
    auto processedPool = lumora::core::BufferPool::create(1U, 4096U).value();
    auto processedLease = processedPool->tryAcquire();
    std::ranges::fill(processedLease->bytes(), std::byte{0xE0});
    auto processed = lumora::core::ProcessedFrame::create(
        id, processedLayout, std::move(*processedLease).seal(), {1U, 1U, 1U}, {}).value();
    return FrameBundle::create(original->raw, original->originalDisplay,
        std::move(processed), std::move(display)).value();
}

TEST(FramePresenter, EnhancedPixelsAndBundleAreAcknowledgedOnlyAfterPaint) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(900, 600);
    view.show();
    QCoreApplication::processEvents();
    FramePresenter presenter(slot, view, clock);
    const auto bundle = makeEnhancedBundle(1U, clock);
    (void)slot.publish(bundle);
    presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.displayedFrameCount(), 0U);

    const auto image = lumora::test::paintWidget(*view.imageViewport());
    EXPECT_EQ(image.pixelColor(image.width() / 2, image.height() / 2).red(), 224);
    EXPECT_EQ(presenter.presentedBundle(), bundle);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(view.status().freshness, FrameFreshness::Current);
    const auto* label = view.findChild<QLabel*>(QStringLiteral("previewModeLabel"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QStringLiteral("Enhanced"));
    EXPECT_EQ(view.imageViewport()->accessibleName(), QStringLiteral("Enhanced image viewport"));
}

TEST(FramePresenter, FallbackLabelChangesOnlyWhenOriginalIsPainted) {
    LatestValueSlot<FrameBundle> slot;
    LatestValueSlot<FrameBundle> replacement;
    WorkstationView view;
    ManualClock clock;
    view.resize(900, 600);
    view.show();
    QCoreApplication::processEvents();
    FramePresenter presenter(slot, view, clock);
    const auto* label = view.findChild<QLabel*>(QStringLiteral("previewModeLabel"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QStringLiteral("Enhanced"));
    (void)slot.publish(makeEnhancedBundle(1U, clock));
    presenter.refresh();
    paint(view);

    const auto fallback = lumora::test::makeBundle(64U, 32U, 2U, clock);
    (void)slot.publish(fallback);
    presenter.refresh();
    EXPECT_EQ(label->text(), QStringLiteral("Enhanced"));
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 1U);
    const auto image = lumora::test::paintWidget(*view.imageViewport());
    EXPECT_EQ(image.pixelColor(image.width() / 2, image.height() / 2).red(), 128);
    EXPECT_EQ(presenter.presentedBundle(), fallback);
    EXPECT_EQ(label->text(), QStringLiteral("Original (fallback)"));
    EXPECT_EQ(view.imageViewport()->accessibleName(), QStringLiteral("Original image viewport"));

    presenter.resetSource(replacement);
    EXPECT_EQ(label->text(), QStringLiteral("Enhanced"));
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(view.status().freshness, FrameFreshness::WaitingForFrame);
}

TEST(FramePresenter, PauseKeepsPaintedEnhancedPixelsAndLabelAcrossPendingFallback) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(900, 600);
    view.show();
    QCoreApplication::processEvents();
    FramePresenter presenter(slot, view, clock);
    const auto enhanced = makeEnhancedBundle(1U, clock);
    (void)slot.publish(enhanced);
    presenter.refresh();
    paint(view);
    (void)slot.publish(lumora::test::makeBundle(64U, 32U, 2U, clock));
    presenter.refresh();
    presenter.pause();
    clock.advance(501ms);
    presenter.refresh();

    const auto image = lumora::test::paintWidget(*view.imageViewport());
    EXPECT_EQ(image.pixelColor(image.width() / 2, image.height() / 2).red(), 224);
    EXPECT_EQ(presenter.presentedBundle(), enhanced);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(view.viewerState(), ViewerState::Paused);
    EXPECT_EQ(view.status().frameAge, 501ms);
    const auto* label = view.findChild<QLabel*>(QStringLiteral("previewModeLabel"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QStringLiteral("Enhanced"));

    presenter.resume();
    paint(view);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 2U);
    EXPECT_EQ(label->text(), QStringLiteral("Original (fallback)"));
    EXPECT_EQ(view.status().freshness, FrameFreshness::Stale);
}

std::shared_ptr<const FrameBundle> makeUnsupportedBundle(
    std::uint64_t id, ManualClock& clock) {
    constexpr std::uint32_t width = 16U;
    constexpr std::uint32_t height = 8U;
    const auto stride = static_cast<std::size_t>(width) * 2U;
    const auto layout = lumora::core::ImageLayout::create(
        width, height, stride, lumora::core::StorageType::UInt16,
        stride * static_cast<std::size_t>(height)).value();
    auto pool = lumora::core::BufferPool::create(1U, layout.payloadBytes()).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), std::byte{0});
    const auto pixels = std::move(*lease).seal();
    auto settings = lumora::core::AcquisitionSettingsSnapshot::create(
        {"Lumora", "Fixture", "SIM-TEST", "Simulator", std::nullopt},
        {"Mono16", 0x01100007U, 16U, 65535U,
         lumora::core::SourcePacking::Unpacked,
         lumora::core::BitAlignment::LeastSignificant,
         lumora::core::StorageType::UInt16},
        {0U, 0U, width, height}, 30.0, 30.0, std::nullopt, std::nullopt).value();
    auto raw = lumora::core::RawFrame::create(
        id, layout, pixels,
        {std::nullopt, clock.steadyNow(), clock.utcNow(), std::nullopt,
         std::move(settings)}).value();
    auto display = lumora::core::DisplayFrame::create(
        id, layout, pixels, lumora::core::DisplayStorage::Gray16,
        {0U, 65535U, 65535U, 1U},
        {false, false, lumora::core::Rotation::Degrees0}).value();
    return FrameBundle::create(raw, display, nullptr, nullptr).value();
}

TEST(FramePresenter, ResumeShowsNewestBundleNotBacklog) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(1280, 800);
    view.show();
    FramePresenter presenter(slot, view, clock);
    for (auto id : {1U, 2U, 3U}) {
        (void)slot.publish(lumora::test::makeBundle(64, 32, id, clock));
    }
    presenter.refresh();
    paint(view);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 3U);
    presenter.pause();
    for (auto id : {4U, 5U}) {
        (void)slot.publish(lumora::test::makeBundle(64, 32, id, clock));
    }
    presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 3U);
    presenter.resume();
    presenter.refresh();
    paint(view);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 5U);
}

TEST(FramePresenter, DoesNotCountOrOwnFrameUntilPaintCompletes) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();

    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.displayedFrameCount(), 0U);
    EXPECT_EQ(view.status().freshness, FrameFreshness::WaitingForFrame);
    paint(view);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}

TEST(FramePresenter, RejectionDoesNotAdvanceAcceptedIdOrFreshness) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(makeUnsupportedBundle(7U, clock));
    presenter.refresh();
    paint(view);
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(view.status().freshness, FrameFreshness::WaitingForFrame);

    (void)slot.publish(lumora::test::makeBundle(64, 32, 6U, clock));
    presenter.refresh();
    paint(view);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 6U);
}

TEST(FramePresenter, RepeatedAndOutOfOrderIdsDoNotReplaceCompletedFrame) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 5U, clock));
    presenter.refresh();
    paint(view);
    ASSERT_EQ(presenter.displayedFrameCount(), 1U);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 5U, clock));
    presenter.refresh();
    (void)slot.publish(lumora::test::makeBundle(64, 32, 4U, clock));
    presenter.refresh();
    paint(view);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 5U);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}

TEST(FramePresenter, ZeroIsAValidFirstFrameId) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 0U, clock));
    presenter.refresh();
    paint(view);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 0U);
}

TEST(FramePresenter, PauseBetweenStageAndPaintDiscardsReplacement) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    paint(view);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 2U, clock));
    presenter.refresh();
    presenter.pause();
    paint(view);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 1U);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    EXPECT_EQ(view.viewerState(), ViewerState::Paused);
}

TEST(FramePresenter, ResumeRecoversConsumedUnpaintedFrameWithoutPublication) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    presenter.pause();
    presenter.resume();
    paint(view);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 1U);
}

TEST(FramePresenter, ThirtyFpsBecomesStaleAtFiveHundredMilliseconds) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    paint(view);
    clock.advance(499ms);
    presenter.refresh();
    EXPECT_EQ(view.status().freshness, FrameFreshness::Current);
    clock.advance(1ms);
    presenter.refresh();
    EXPECT_EQ(view.status().freshness, FrameFreshness::Stale);
}

TEST(FramePresenter, OneFpsBecomesStaleAtThreeSeconds) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock, 1.0));
    presenter.refresh();
    paint(view);
    clock.advance(2999ms);
    presenter.refresh();
    EXPECT_EQ(view.status().freshness, FrameFreshness::Current);
    clock.advance(1ms);
    presenter.refresh();
    EXPECT_EQ(view.status().freshness, FrameFreshness::Stale);
}

TEST(FramePresenter, InvalidActualFpsCannotReplaceCompletedFrame) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    paint(view);
    ASSERT_EQ(presenter.presentedBundle()->sourceFrameId(), 1U);

    for (const double invalidFps : {
             0.0,
             -1.0,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        (void)slot.publish(lumora::test::makeBundle(64, 32, 2U, clock, invalidFps));
        presenter.refresh();
        paint(view);
        EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 1U);
        EXPECT_EQ(presenter.displayedFrameCount(), 1U);
    }
}

TEST(FramePresenter, TinyPositiveFpsDoesNotOverflowStaleDeadline) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock, 1e-300));
    presenter.refresh();
    paint(view);

    clock.advance(500ms);
    presenter.refresh();

    EXPECT_EQ(view.status().freshness, FrameFreshness::Current);
}

TEST(FramePresenter, DelayedFrameIsStaleImmediatelyAfterResumeAndPaint) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    presenter.pause();
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    clock.advance(500ms);
    presenter.resume();
    paint(view);
    EXPECT_EQ(view.status().freshness, FrameFreshness::Stale);
}

TEST(FramePresenter, WallClockJumpDoesNotChangeFreshnessAge) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    paint(view);
    clock.setUtc(std::chrono::system_clock::time_point{24h});
    presenter.refresh();
    EXPECT_EQ(view.status().freshness, FrameFreshness::Current);
    EXPECT_EQ(view.status().frameAge, 0ms);
}

TEST(FramePresenter, UnsupportedReplacementCannotPaintWhileHeartbeatBecomesStale) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    paint(view);
    clock.advance(499ms);
    (void)slot.publish(makeUnsupportedBundle(2U, clock));
    presenter.refresh();
    paint(view);
    EXPECT_EQ(view.status().freshness, FrameFreshness::Current);

    clock.advance(1ms);
    presenter.refresh();

    EXPECT_EQ(view.status().freshness, FrameFreshness::Stale);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}

TEST(FramePresenter, EventLoopStallBecomesStaleOnFirstRecoveryRefresh) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.refresh();
    paint(view);
    ASSERT_EQ(view.status().freshness, FrameFreshness::Current);

    clock.advance(500ms);
    EXPECT_EQ(view.status().freshness, FrameFreshness::Current);
    presenter.refresh();

    EXPECT_EQ(view.status().freshness, FrameFreshness::Stale);
}

TEST(FramePresenter, DestructionClearsObserverAndDisconnectsViewIntents) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    {
        FramePresenter presenter(slot, view, clock);
        (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
        presenter.refresh();
    }

    paint(view);
    view.pauseRequested();
    view.resumeRequested();

    EXPECT_EQ(view.imageViewport()->presentedFrameId(), 1U);
}

TEST(FramePresenter, ResetSourceAcceptsLowerIdAndResetsSessionState) {
    LatestValueSlot<FrameBundle> oldSlot;
    LatestValueSlot<FrameBundle> freshSlot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(oldSlot, view, clock);
    (void)oldSlot.publish(lumora::test::makeBundle(64, 32, 100U, clock));
    presenter.refresh();
    paint(view);
    ASSERT_EQ(presenter.displayedFrameCount(), 1U);
    (void)freshSlot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.resetSource(freshSlot);
    EXPECT_EQ(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.displayedFrameCount(), 0U);
    presenter.refresh();
    paint(view);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 1U);
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);
}

TEST(FramePresenter, ResetSourceReleasesOldPresenterAndViewportOwnership) {
    auto oldSlot = std::make_unique<LatestValueSlot<FrameBundle>>();
    LatestValueSlot<FrameBundle> freshSlot;
    WorkstationView view;
    ManualClock clock;
    view.resize(640, 480);
    FramePresenter presenter(*oldSlot, view, clock);
    auto bundle = lumora::test::makeBundle(64, 32, 1U, clock);
    std::weak_ptr<const FrameBundle> weakBundle = bundle;
    (void)oldSlot->publish(bundle);
    presenter.refresh();
    paint(view);
    bundle.reset();
    presenter.resetSource(freshSlot);
    oldSlot.reset();
    EXPECT_TRUE(weakBundle.expired());
}

}  // namespace
