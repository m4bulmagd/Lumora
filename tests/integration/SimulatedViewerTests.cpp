#include "SimulatorFeed.hpp"

#include <lumora/core/Clock.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

class OneTimeoutClock final : public lumora::core::IClock {
public:
    std::chrono::steady_clock::time_point steadyNow() const noexcept override {
        return systemClock_.steadyNow();
    }

    std::chrono::system_clock::time_point utcNow() const noexcept override {
        return systemClock_.utcNow();
    }

    lumora::core::ClockWaitOutcome waitUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token stopToken,
        std::chrono::milliseconds maximumRealWait) const override {
        if (!timeoutDelivered_.exchange(true)) {
            return lumora::core::ClockWaitOutcome::MaximumWaitElapsed;
        }
        return systemClock_.waitUntil(deadline, stopToken, maximumRealWait);
    }

    [[nodiscard]] bool timeoutDelivered() const noexcept {
        return timeoutDelivered_.load();
    }

private:
    mutable std::atomic_bool timeoutDelivered_{false};
    lumora::core::SystemClock systemClock_;
};

void verifyTimeoutIsObservableAndRecovers() {
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    OneTimeoutClock clock;
    lumora::tools::SimulatorFeed feed(slot, clock);
    feed.start();
    std::uint64_t revision = 0U;
    std::uint64_t revisionAtTimeout = 0U;
    bool sawTimeout = false;
    bool recovered = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline && !recovered) {
        if (auto value = slot.consumeAfter(revision)) {
            revision = value->revision;
        }
        const auto snapshot = feed.result();
        if (!snapshot.hasValue() &&
            snapshot.error().category == lumora::core::ErrorCategory::Acquisition &&
            snapshot.error().code == "acquisition_timeout" &&
            snapshot.error().recoverable) {
            if (!sawTimeout) {
                revisionAtTimeout = revision;
            }
            sawTimeout = true;
        }
        recovered = sawTimeout && revision > revisionAtTimeout;
        std::this_thread::sleep_for(2ms);
    }
    ASSERT_TRUE(sawTimeout);
    EXPECT_TRUE(clock.timeoutDelivered());
    EXPECT_TRUE(recovered);
    const auto transientSnapshot = feed.result();
    ASSERT_FALSE(transientSnapshot.hasValue());
    EXPECT_EQ(transientSnapshot.error().code, "acquisition_timeout");
    EXPECT_GE(feed.timeoutCount(), 1U);

    slot.close();
    const auto terminalDeadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < terminalDeadline &&
           feed.result().error().code == "acquisition_timeout") {
        std::this_thread::sleep_for(2ms);
    }
    feed.stop();
    const auto terminalSnapshot = feed.result();
    ASSERT_FALSE(terminalSnapshot.hasValue());
    EXPECT_EQ(terminalSnapshot.error().code, "viewer_slot_closed");
}

void runResponsiveViewer(std::chrono::milliseconds duration) {
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    lumora::core::SystemClock clock;
    lumora::ui::WorkstationView view;
    view.resize(1024, 720);
    view.show();
    lumora::ui::FramePresenter presenter(slot, view, clock);
    lumora::tools::SimulatorFeed feed(slot, clock);
    presenter.start();
    feed.start();

    std::uint64_t observedRevision = 0U;
    std::uint64_t observedPublications = 0U;
    std::uint64_t publicationAtPause = 0U;
    std::uint64_t revisionAtHalfway = 0U;
    std::uint64_t paintsAtHalfway = 0U;
    qint64 lastPublicationElapsed = 0;
    bool capturedHalfway = false;
    bool publicationContinuedDuringPause = false;
    std::size_t maximumRetainedBundles = 0U;
    std::vector<std::weak_ptr<const lumora::core::FrameBundle>> observedBundles;
    qint64 lastActionBucket = -1;
    QElapsedTimer elapsed;
    elapsed.start();
    bool paused = false;
    while (elapsed.elapsed() < duration.count()) {
        QCoreApplication::processEvents();
        if (auto value = slot.consumeAfter(observedRevision)) {
            observedRevision = value->revision;
            ++observedPublications;
            lastPublicationElapsed = elapsed.elapsed();
            std::erase_if(observedBundles, [](const auto& weak) {
                return weak.expired();
            });
            observedBundles.emplace_back(value->value);
        }
        const auto retained = observedBundles.size();
        maximumRetainedBundles = std::max(maximumRetainedBundles, retained);
        if (paused && observedRevision > publicationAtPause) {
            publicationContinuedDuringPause = true;
        }
        const auto phase = elapsed.elapsed() % 1000;
        if (!capturedHalfway && elapsed.elapsed() >= duration.count() / 2) {
            revisionAtHalfway = observedRevision;
            paintsAtHalfway = presenter.displayedFrameCount();
            capturedHalfway = true;
        }
        if (!paused && phase >= 300 && phase < 600) {
            publicationAtPause = observedRevision;
            presenter.pause();
            paused = true;
        } else if (paused && phase >= 600) {
            presenter.resume();
            paused = false;
        }
        const auto actionBucket = elapsed.elapsed() / 100;
        if (actionBucket != lastActionBucket) {
            lastActionBucket = actionBucket;
            if (phase < 100) {
                view.imageViewport()->zoomIn();
            } else if (phase < 200) {
                view.imageViewport()->zoomOut();
            } else if (phase < 300) {
                auto* viewport = view.imageViewport();
                QMouseEvent press(
                    QEvent::MouseButtonPress, QPointF{50.0, 50.0},
                    QPointF{50.0, 50.0}, Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
                QApplication::sendEvent(viewport, &press);
                QMouseEvent move(
                    QEvent::MouseMove, QPointF{60.0, 55.0},
                    QPointF{60.0, 55.0}, Qt::NoButton, Qt::LeftButton,
                    Qt::NoModifier);
                QApplication::sendEvent(viewport, &move);
                QMouseEvent release(
                    QEvent::MouseButtonRelease, QPointF{60.0, 55.0},
                    QPointF{60.0, 55.0}, Qt::LeftButton, Qt::NoButton,
                    Qt::NoModifier);
                QApplication::sendEvent(viewport, &release);
            } else if (phase >= 700 && phase < 800) {
                view.resize(960 + static_cast<int>(observedRevision % 64U), 680);
            }
        }
        std::this_thread::sleep_for(2ms);
    }

    if (paused) {
        presenter.resume();
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(2ms);
    }
    feed.stop();
    presenter.refresh();
    QCoreApplication::processEvents();
    presenter.stop();

    EXPECT_GT(observedPublications, 0U);
    EXPECT_TRUE(publicationContinuedDuringPause);
    EXPECT_GT(presenter.displayedFrameCount(), 0U);
    EXPECT_GT(observedRevision, revisionAtHalfway);
    EXPECT_GT(presenter.displayedFrameCount(), paintsAtHalfway);
    EXPECT_GE(lastPublicationElapsed, duration.count() - 2000);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    const auto latest = slot.consumeAfter(0U);
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(
        presenter.presentedBundle()->sourceFrameId(),
        latest->value->sourceFrameId());
    EXPECT_LE(maximumRetainedBundles, 3U);
    const auto feedResult = feed.result();
    if (!feedResult.hasValue()) {
        EXPECT_EQ(feedResult.error().category, lumora::core::ErrorCategory::Acquisition);
        EXPECT_EQ(feedResult.error().code, "acquisition_timeout");
        EXPECT_TRUE(feedResult.error().recoverable);
        EXPECT_GT(feed.timeoutCount(), 0U);
    }
    const auto stoppedRevision = latest->revision;
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(slot.consumeAfter(stoppedRevision).has_value());
    std::cout << "observed publications=" << observedPublications
              << ", completed paints=" << presenter.displayedFrameCount()
              << ", max retained bundles=" << maximumRetainedBundles
              << ", retrieval timeouts="
              << feed.timeoutCount() << '\n';
    slot.close();
}

void verifySuppressedPreparedPublicationKeepsAcquiringAndBecomesStale() {
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> sourceSlot;
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> viewerSlot;
    lumora::core::SystemClock clock;
    lumora::ui::WorkstationView view;
    view.resize(800, 600);
    view.show();
    lumora::ui::FramePresenter presenter(viewerSlot, view, clock);
    lumora::tools::SimulatorFeed feed(sourceSlot, clock);
    presenter.start();
    feed.start();

    std::uint64_t sourceRevision = 0U;
    const auto firstPaintDeadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < firstPaintDeadline &&
           presenter.displayedFrameCount() == 0U) {
        if (auto prepared = sourceSlot.consumeAfter(sourceRevision)) {
            sourceRevision = prepared->revision;
            (void)viewerSlot.publish(prepared->value);
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(2ms);
    }
    ASSERT_GT(presenter.displayedFrameCount(), 0U);
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    const auto displayedId = presenter.presentedBundle()->sourceFrameId();
    const auto sourceRevisionAtSuppression = sourceRevision;

    const auto suppressionDeadline = std::chrono::steady_clock::now() + 700ms;
    while (std::chrono::steady_clock::now() < suppressionDeadline) {
        if (auto prepared = sourceSlot.consumeAfter(sourceRevision)) {
            sourceRevision = prepared->revision;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(2ms);
    }

    feed.stop();
    presenter.stop();
    sourceSlot.close();
    viewerSlot.close();
    EXPECT_GT(sourceRevision, sourceRevisionAtSuppression);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), displayedId);
    EXPECT_EQ(view.status().freshness, lumora::ui::FrameFreshness::Stale);
}

TEST(SimulatedViewer, ResponsiveTenSeconds) {
    runResponsiveViewer(10s);
    verifyTimeoutIsObservableAndRecovers();
    verifySuppressedPreparedPublicationKeepsAcquiringAndBecomesStale();
}

TEST(SimulatedViewer, TimeoutIsObservableAndRecovers) {
    verifyTimeoutIsObservableAndRecovers();
}

TEST(SimulatedViewer, StressTenMinutes) {
    runResponsiveViewer(600s);
}

TEST(SimulatedViewer, StoppedProducerDoesNotPublish) {
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    lumora::core::SystemClock clock;
    lumora::tools::SimulatorFeed feed(slot, clock);
    feed.start();
    std::uint64_t revision = 0U;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline && revision == 0U) {
        if (auto value = slot.consumeAfter(revision)) {
            revision = value->revision;
        }
        std::this_thread::sleep_for(2ms);
    }
    ASSERT_GT(revision, 0U);

    feed.stop();
    std::this_thread::sleep_for(100ms);

    EXPECT_FALSE(slot.consumeAfter(revision).has_value());
    slot.close();
}

TEST(SimulatedViewer, SuppressedPreparedPublicationKeepsAcquiringAndBecomesStale) {
    verifySuppressedPreparedPublicationKeepsAcquiringAndBecomesStale();
}

TEST(SimulatedViewer, ClosedSlotSuppressesPreparedPublicationAfterAcquisition) {
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    lumora::core::SystemClock clock;
    slot.close();
    lumora::tools::SimulatorFeed feed(slot, clock);
    feed.start();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = feed.result();
        if (!snapshot.hasValue() && snapshot.error().code == "viewer_slot_closed") {
            break;
        }
        std::this_thread::sleep_for(2ms);
    }
    feed.stop();
    const auto snapshot = feed.result();
    ASSERT_FALSE(snapshot.hasValue());
    EXPECT_EQ(snapshot.error().code, "viewer_slot_closed");
    EXPECT_FALSE(slot.consumeAfter(0U).has_value());
}

}  // namespace
