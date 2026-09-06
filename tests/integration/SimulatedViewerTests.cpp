#include "SimulatorFeed.hpp"
#include "ViewportTestSupport.hpp"
#include "ViewerHarnessUi.hpp"

#include <lumora/core/Clock.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTimer>
#include <QTranslator>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool waitForQtCondition(
    const std::function<bool()>& condition,
    std::chrono::milliseconds timeout = 2s) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (condition()) {
            return true;
        }
        std::this_thread::yield();
    }
    QCoreApplication::processEvents();
    return condition();
}

class HarnessTranslator final : public QTranslator {
public:
    QString translate(
        const char* context,
        const char* sourceText,
        const char*,
        int) const override {
        if (QString::fromLatin1(context) !=
            QStringLiteral("LumoraViewerHarness")) {
            return {};
        }
        const auto source = QString::fromUtf8(sourceText);
        if (source == QStringLiteral("Lumora Simulated Viewer")) {
            return QStringLiteral("translated viewer title");
        }
        if (source == QStringLiteral(
                "Lumora Simulated Viewer — RETRIEVAL TIMEOUT OBSERVED")) {
            return QStringLiteral("translated timeout title");
        }
        if (source == QStringLiteral("Simulated viewer error")) {
            return QStringLiteral("translated error title");
        }
        if (source == QStringLiteral("EVALUATION — NOT FOR CLINICAL USE")) {
            return QStringLiteral("translated evaluation banner");
        }
        if (source == QStringLiteral(
                "acquisition_timeout: transient retrieval timeout; feed continues")) {
            return QStringLiteral("translated timeout notice");
        }
        if (source == QStringLiteral(
                "The simulated frame could not be published.")) {
            return QStringLiteral("translated publication failure");
        }
        if (source == QStringLiteral(
                "The simulated viewer encountered an error.")) {
            return QStringLiteral("translated generic viewer error");
        }
        return {};
    }
};

void verifyHarnessOperatorTextUsesQtTranslationBoundary() {
    HarnessTranslator translator;
    EXPECT_TRUE(QCoreApplication::installTranslator(&translator));
    const lumora::core::Error publicationError{
        lumora::core::ErrorCategory::Internal,
        "viewer_slot_closed", "Runtime-only operator summary",
        "technical diagnostic", false};
    const lumora::core::Error unknownError{
        lumora::core::ErrorCategory::Internal,
        "unmapped_fixture", "Another runtime-only summary",
        "technical diagnostic", false};

    EXPECT_EQ(
        lumora::tools::ui_detail::viewerWindowTitle(),
        QStringLiteral("translated viewer title"));
    EXPECT_EQ(
        lumora::tools::ui_detail::timeoutWindowTitle(),
        QStringLiteral("translated timeout title"));
    EXPECT_EQ(
        lumora::tools::ui_detail::errorDialogTitle(),
        QStringLiteral("translated error title"));
    EXPECT_EQ(
        lumora::tools::ui_detail::evaluationBanner(),
        QStringLiteral("translated evaluation banner"));
    EXPECT_EQ(
        lumora::tools::ui_detail::timeoutConsoleNotice(),
        QStringLiteral("translated timeout notice"));
    EXPECT_EQ(
        lumora::tools::ui_detail::translatedOperatorSummary(publicationError),
        QStringLiteral("translated publication failure"));
    EXPECT_EQ(
        lumora::tools::ui_detail::translatedOperatorSummary(unknownError),
        QStringLiteral("translated generic viewer error"));

    QCoreApplication::removeTranslator(&translator);
}

void verifyPresenterTimerStartStopAndCadence() {
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    lumora::core::ManualClock clock;
    lumora::ui::WorkstationView view;
    view.resize(640, 480);
    view.show();
    lumora::ui::FramePresenter presenter(slot, view, clock);
    (void)slot.publish(lumora::test::makeBundle(64, 32, 1U, clock));
    presenter.start();
    presenter.start();
    ASSERT_TRUE(waitForQtCondition([&] {
        return presenter.displayedFrameCount() == 1U;
    }));

    QTimer witnessTimer;
    witnessTimer.setInterval(17);
    int witnessTicks = 0;
    QObject::connect(&witnessTimer, &QTimer::timeout, [&] { ++witnessTicks; });
    (void)slot.publish(lumora::test::makeBundle(64, 32, 2U, clock));
    clock.advance(16ms);
    witnessTimer.start();
    ASSERT_TRUE(waitForQtCondition([&] { return witnessTicks >= 2; }));
    witnessTimer.stop();
    EXPECT_EQ(presenter.displayedFrameCount(), 1U);

    clock.advance(1ms);
    ASSERT_TRUE(waitForQtCondition([&] {
        return presenter.displayedFrameCount() == 2U;
    }));

    presenter.stop();
    presenter.stop();
    (void)slot.publish(lumora::test::makeBundle(64, 32, 3U, clock));
    clock.advance(100ms);
    witnessTicks = 0;
    witnessTimer.start();
    ASSERT_TRUE(waitForQtCondition([&] { return witnessTicks >= 2; }));
    witnessTimer.stop();
    EXPECT_EQ(presenter.displayedFrameCount(), 2U);

    presenter.start();
    ASSERT_TRUE(waitForQtCondition([&] {
        return presenter.displayedFrameCount() == 3U;
    }));
    presenter.stop();
}

void verifyTimeoutPredicateRequiresExactTypedRecoverableError() {
    const lumora::core::Error exact{
        lumora::core::ErrorCategory::Acquisition,
        "acquisition_timeout", "timeout", "exact transient", true};
    const lumora::core::Error wrongCategory{
        lumora::core::ErrorCategory::Internal,
        "acquisition_timeout", "timeout", "wrong category", true};
    const lumora::core::Error nonrecoverable{
        lumora::core::ErrorCategory::Acquisition,
        "acquisition_timeout", "timeout", "not recoverable", false};
    const lumora::core::Error wrongCode{
        lumora::core::ErrorCategory::Acquisition,
        "other", "timeout", "wrong code", true};

    EXPECT_TRUE(lumora::tools::detail::isRecoverableAcquisitionTimeout(exact));
    EXPECT_FALSE(
        lumora::tools::detail::isRecoverableAcquisitionTimeout(wrongCategory));
    EXPECT_FALSE(
        lumora::tools::detail::isRecoverableAcquisitionTimeout(nonrecoverable));
    EXPECT_FALSE(
        lumora::tools::detail::isRecoverableAcquisitionTimeout(wrongCode));
}

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
            lumora::tools::detail::isRecoverableAcquisitionTimeout(
                snapshot.error())) {
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
    EXPECT_TRUE(lumora::tools::detail::isRecoverableAcquisitionTimeout(
        transientSnapshot.error()));
    EXPECT_GE(feed.timeoutCount(), 1U);

    slot.close();
    const auto terminalDeadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < terminalDeadline &&
           lumora::tools::detail::isRecoverableAcquisitionTimeout(
               feed.result().error())) {
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

    // Retain accumulated evidence even if a final fatal assertion returns early.
    std::cout << "observed publications=" << observedPublications
              << ", completed paints=" << presenter.displayedFrameCount()
              << ", max retained bundles=" << maximumRetainedBundles
              << ", retrieval timeouts="
              << feed.timeoutCount() << '\n';

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
        EXPECT_TRUE(lumora::tools::detail::isRecoverableAcquisitionTimeout(
            feedResult.error()));
        EXPECT_GT(feed.timeoutCount(), 0U);
    }
    const auto stoppedRevision = latest->revision;
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(slot.consumeAfter(stoppedRevision).has_value());
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
    verifyHarnessOperatorTextUsesQtTranslationBoundary();
    verifyPresenterTimerStartStopAndCadence();
    verifyTimeoutPredicateRequiresExactTypedRecoverableError();
    runResponsiveViewer(10s);
    verifyTimeoutIsObservableAndRecovers();
    verifySuppressedPreparedPublicationKeepsAcquiringAndBecomesStale();
}

TEST(SimulatedViewer, HarnessOperatorTextUsesQtTranslationBoundary) {
    verifyHarnessOperatorTextUsesQtTranslationBoundary();
}

TEST(SimulatedViewer, PresenterTimerStartStopAndCadence) {
    verifyPresenterTimerStartStopAndCadence();
}

TEST(SimulatedViewer, TimeoutPredicateRequiresExactTypedRecoverableError) {
    verifyTimeoutPredicateRequiresExactTypedRecoverableError();
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
    const auto stoppedValue = slot.consumeAfter(0U);
    ASSERT_TRUE(stoppedValue.has_value());
    const auto stoppedRevision = stoppedValue->revision;
    std::this_thread::sleep_for(100ms);

    EXPECT_FALSE(slot.consumeAfter(stoppedRevision).has_value());
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
