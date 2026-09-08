#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/MainWindow.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include "ViewportTestSupport.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <chrono>

namespace {

using lumora::ui::FrameFreshness;
using lumora::ui::ViewScaleMode;
using lumora::ui::ViewerState;
using lumora::ui::WorkstationView;

void sendKey(
    QWidget& target,
    Qt::Key key,
    const QString& text = {}) {
    QKeyEvent press{QEvent::KeyPress, key, Qt::NoModifier, text};
    QApplication::sendEvent(&target, &press);
    QKeyEvent release{QEvent::KeyRelease, key, Qt::NoModifier, text};
    QApplication::sendEvent(&target, &release);
    QCoreApplication::processEvents();
}

TEST(WorkstationView, EnhancementWarningCoexistsWithFreshnessAndKeepsRetryPending) {
    WorkstationView view; view.show();
    lumora::processing::ProcessorStatus processing;
    processing.mode=lumora::processing::ProcessorMode::OriginalOnlyLatched;
    processing.retrySupported=true;
    view.setProcessingStatus(processing);
    auto* warning=view.findChild<QLabel*>(QStringLiteral("processingWarning"));
    auto* retry=view.findChild<QPushButton*>(QStringLiteral("processingRetryButton"));
    ASSERT_NE(warning,nullptr); ASSERT_NE(retry,nullptr);
    EXPECT_TRUE(warning->isVisible()); EXPECT_TRUE(retry->isEnabled());
    EXPECT_EQ(warning->textFormat(),Qt::PlainText);
    int requests=0;
    QObject::connect(&view,&WorkstationView::processingRetryRequested,&view,[&]{++requests;});
    retry->click(); EXPECT_EQ(requests,1);
    for(auto freshness:{FrameFreshness::Current,FrameFreshness::Stale,FrameFreshness::WaitingForFrame}) {
        view.setStatus({ViewerState::Paused,freshness,{}, {}});
        EXPECT_TRUE(warning->isVisible());
        EXPECT_EQ(view.status().freshness,freshness);
    }
    view.setProcessingStatus(processing,true);
    EXPECT_TRUE(warning->isVisible()); EXPECT_FALSE(retry->isEnabled());
    retry->click(); EXPECT_EQ(requests,1);
    view.setProcessingStatus({}); EXPECT_FALSE(warning->isVisible()); EXPECT_FALSE(retry->isVisible());
}

TEST(WorkstationView, ImageAreaDominatesInitialLayout) {
    WorkstationView view;
    view.resize(1280, 800);
    view.show();
    QCoreApplication::processEvents();

    EXPECT_LT(view.sidebar()->width(), view.imageViewport()->width());
    EXPECT_EQ(view.viewerState(), ViewerState::Live);
    EXPECT_EQ(view.status().freshness, FrameFreshness::WaitingForFrame);
}

TEST(WorkstationView, ExposesStableNamedEvaluationComposition) {
    WorkstationView view;
    view.show();
    QCoreApplication::processEvents();

    EXPECT_EQ(view.sidebar()->objectName(), QStringLiteral("sidebar"));
    EXPECT_EQ(
        view.imageViewport()->objectName(), QStringLiteral("imageViewport"));
    for (const auto* objectName : {
             "pauseLiveButton",
             "fitAction",
             "actualPixelsAction",
             "zoomInAction",
             "zoomOutAction",
             "frameStateOverlay",
         }) {
        EXPECT_NE(
            view.findChild<QObject*>(QString::fromLatin1(objectName)), nullptr)
            << objectName;
    }

    const auto banners = view.findChildren<QLabel*>(
        QStringLiteral("releaseClassBanner"));
    ASSERT_EQ(banners.size(), 1);
    EXPECT_EQ(
        banners.front()->text(),
        QStringLiteral("EVALUATION — NOT FOR CLINICAL USE"));

    for (const auto* button : view.findChildren<QAbstractButton*>()) {
        EXPECT_NE(button->text(), QStringLiteral("Record"));
    }
}

TEST(WorkstationView, WaitingForFrameDisablesImageControls) {
    WorkstationView view;
    view.show();
    QCoreApplication::processEvents();

    const auto* pause =
        view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    const auto* overlay =
        view.findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
    ASSERT_NE(pause, nullptr);
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(pause->text(), QStringLiteral("Pause"));
    EXPECT_FALSE(pause->isEnabled());
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(overlay->text(), QStringLiteral("Waiting for image"));

    for (const auto* objectName : {
             "fitAction",
             "actualPixelsAction",
             "zoomInAction",
             "zoomOutAction",
         }) {
        const auto* action =
            view.findChild<QAction*>(QString::fromLatin1(objectName));
        ASSERT_NE(action, nullptr) << objectName;
        EXPECT_FALSE(action->isEnabled()) << objectName;
    }
}

TEST(WorkstationView, MainWindowOwnsExactlyOneSafetyComposition) {
    lumora::ui::MainWindow window;

    EXPECT_NE(qobject_cast<WorkstationView*>(window.centralWidget()), nullptr);
    EXPECT_EQ(
        window.findChildren<QLabel*>(QStringLiteral("releaseClassBanner")).size(),
        1);
}

TEST(WorkstationView, PauseAndResumeControlsEmitIntentWithoutChangingState) {
    WorkstationView view;
    view.show();
    QCoreApplication::processEvents();
    auto* pauseLive =
        view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    ASSERT_NE(pauseLive, nullptr);

    int pauseRequests = 0;
    int resumeRequests = 0;
    QObject::connect(
        &view, &WorkstationView::pauseRequested,
        [&pauseRequests] { ++pauseRequests; });
    QObject::connect(
        &view, &WorkstationView::resumeRequested,
        [&resumeRequests] { ++resumeRequests; });

    view.setStatus({
        ViewerState::Live,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    ASSERT_TRUE(pauseLive->isEnabled());
    EXPECT_EQ(pauseLive->text(), QStringLiteral("Pause"));
    pauseLive->click();
    EXPECT_EQ(pauseRequests, 1);
    EXPECT_EQ(resumeRequests, 0);
    EXPECT_EQ(view.viewerState(), ViewerState::Live);

    view.setStatus({
        ViewerState::Paused,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    ASSERT_TRUE(pauseLive->isEnabled());
    EXPECT_EQ(pauseLive->text(), QStringLiteral("Live"));
    pauseLive->click();
    EXPECT_EQ(pauseRequests, 1);
    EXPECT_EQ(resumeRequests, 1);
    EXPECT_EQ(view.viewerState(), ViewerState::Paused);
}

TEST(WorkstationView, PausedStatusShowsFrozenUtcTimestampAndSuppliedAge) {
    using namespace std::chrono;
    WorkstationView view;
    view.show();
    QCoreApplication::processEvents();
    auto* overlay =
        view.findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
    ASSERT_NE(overlay, nullptr);

    const auto frameUtc = sys_days{year{2026} / April / 25} +
                          12h + 34min + 56s + 789ms;
    view.setStatus({
        ViewerState::Paused,
        FrameFreshness::Stale,
        frameUtc,
        2345ms,
    });

    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(
        overlay->text(),
        QStringLiteral(
            "PAUSED\nFrame: 2026-04-25T12:34:56.789Z\nAge: 2345 ms"));
}

TEST(WorkstationView, PausedStatusWithoutMetadataIsExplicitlyUnavailable) {
    WorkstationView view;
    view.show();
    QCoreApplication::processEvents();
    auto* overlay =
        view.findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
    ASSERT_NE(overlay, nullptr);

    view.setStatus({
        ViewerState::Paused,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });

    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(
        overlay->text(),
        QStringLiteral(
            "PAUSED\nFrame UTC: unavailable\nAge: unavailable"));
}

TEST(WorkstationView, PausedWaitingAllowsResumeButNotImageActions) {
    using namespace std::chrono;
    WorkstationView view;
    view.resize(900, 600);
    view.show();
    view.activateWindow();
    QCoreApplication::processEvents();
    auto* pauseLive =
        view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    ASSERT_NE(pauseLive, nullptr);
    int resumeRequests = 0;
    QObject::connect(
        &view, &WorkstationView::resumeRequested,
        [&resumeRequests] { ++resumeRequests; });

    view.setStatus({
        ViewerState::Paused,
        FrameFreshness::WaitingForFrame,
        sys_days{year{2026} / April / 25},
        9876ms,
    });

    EXPECT_TRUE(pauseLive->isEnabled());
    EXPECT_EQ(pauseLive->text(), QStringLiteral("Live"));
    for (const auto* objectName : {
             "fitAction",
             "actualPixelsAction",
             "zoomInAction",
             "zoomOutAction",
         }) {
        const auto* action =
            view.findChild<QAction*>(QString::fromLatin1(objectName));
        ASSERT_NE(action, nullptr) << objectName;
        EXPECT_FALSE(action->isEnabled()) << objectName;
    }
    const auto* overlay =
        view.findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(
        overlay->text(), QStringLiteral("PAUSED\nWaiting for image"));
    EXPECT_FALSE(overlay->text().contains(QStringLiteral("2026")));
    EXPECT_FALSE(overlay->text().contains(QStringLiteral("9876")));

    pauseLive->click();
    EXPECT_EQ(resumeRequests, 1);

    view.imageViewport()->setFocus();
    ASSERT_TRUE(view.imageViewport()->hasFocus());
    sendKey(*view.imageViewport(), Qt::Key_Space, QStringLiteral(" "));
    EXPECT_EQ(resumeRequests, 2);
}

TEST(WorkstationView, LiveFreshnessTransitionsUpdatePersistentOverlay) {
    WorkstationView view;
    view.show();
    QCoreApplication::processEvents();
    auto* overlay =
        view.findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
    auto* pauseLive =
        view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    ASSERT_NE(overlay, nullptr);
    ASSERT_NE(pauseLive, nullptr);

    view.setStatus({
        ViewerState::Live,
        FrameFreshness::Stale,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(overlay->text(), QStringLiteral("STALE IMAGE / NOT LIVE"));
    EXPECT_TRUE(pauseLive->isEnabled());

    view.setStatus({
        ViewerState::Live,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    EXPECT_FALSE(overlay->isVisible());
    EXPECT_TRUE(pauseLive->isEnabled());
    for (const auto* objectName : {
             "fitAction",
             "actualPixelsAction",
             "zoomInAction",
             "zoomOutAction",
         }) {
        const auto* action =
            view.findChild<QAction*>(QString::fromLatin1(objectName));
        ASSERT_NE(action, nullptr) << objectName;
        EXPECT_TRUE(action->isEnabled()) << objectName;
    }

    view.setStatus({
        ViewerState::Live,
        FrameFreshness::WaitingForFrame,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(overlay->text(), QStringLiteral("Waiting for image"));
    EXPECT_FALSE(pauseLive->isEnabled());
    for (const auto* objectName : {
             "fitAction",
             "actualPixelsAction",
             "zoomInAction",
             "zoomOutAction",
         }) {
        const auto* action =
            view.findChild<QAction*>(QString::fromLatin1(objectName));
        ASSERT_NE(action, nullptr) << objectName;
        EXPECT_FALSE(action->isEnabled()) << objectName;
    }
}

TEST(WorkstationView, ViewerActionsRouteToViewportGeometry) {
    WorkstationView view;
    view.resize(900, 600);
    view.show();
    QCoreApplication::processEvents();
    ASSERT_TRUE(view.imageViewport()
                    ->present(lumora::test::makeDisplayFrame(1200, 800, 7))
                    .hasValue());
    view.setStatus({
        ViewerState::Live,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });

    auto* fit = view.findChild<QAction*>(QStringLiteral("fitAction"));
    auto* actual =
        view.findChild<QAction*>(QStringLiteral("actualPixelsAction"));
    auto* zoomIn = view.findChild<QAction*>(QStringLiteral("zoomInAction"));
    auto* zoomOut = view.findChild<QAction*>(QStringLiteral("zoomOutAction"));
    ASSERT_NE(fit, nullptr);
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(zoomIn, nullptr);
    ASSERT_NE(zoomOut, nullptr);

    actual->trigger();
    EXPECT_EQ(view.imageViewport()->transform().mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(view.imageViewport()->transform().scale(), 1.0);

    zoomIn->trigger();
    EXPECT_DOUBLE_EQ(view.imageViewport()->transform().scale(), 1.2);
    zoomOut->trigger();
    EXPECT_DOUBLE_EQ(view.imageViewport()->transform().scale(), 1.0);

    fit->trigger();
    EXPECT_EQ(view.imageViewport()->transform().mode(), ViewScaleMode::Fit);
    EXPECT_LT(view.imageViewport()->transform().scale(), 1.0);
}

TEST(WorkstationView, ViewerFocusedShortcutsRouteActionsAndPauseIntent) {
    WorkstationView view;
    view.resize(900, 600);
    view.show();
    view.activateWindow();
    QCoreApplication::processEvents();
    ASSERT_TRUE(view.imageViewport()
                    ->present(lumora::test::makeDisplayFrame(1200, 800, 8))
                    .hasValue());
    view.setStatus({
        ViewerState::Live,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    int pauseRequests = 0;
    int resumeRequests = 0;
    QObject::connect(
        &view, &WorkstationView::pauseRequested,
        [&pauseRequests] { ++pauseRequests; });
    QObject::connect(
        &view, &WorkstationView::resumeRequested,
        [&resumeRequests] { ++resumeRequests; });

    view.imageViewport()->setFocus();
    ASSERT_TRUE(view.imageViewport()->hasFocus());
    sendKey(*view.imageViewport(), Qt::Key_1, QStringLiteral("1"));
    EXPECT_DOUBLE_EQ(view.imageViewport()->transform().scale(), 1.0);
    sendKey(*view.imageViewport(), Qt::Key_Plus, QStringLiteral("+"));
    EXPECT_DOUBLE_EQ(view.imageViewport()->transform().scale(), 1.2);
    sendKey(*view.imageViewport(), Qt::Key_Minus, QStringLiteral("-"));
    EXPECT_DOUBLE_EQ(view.imageViewport()->transform().scale(), 1.0);
    sendKey(*view.imageViewport(), Qt::Key_F, QStringLiteral("f"));
    EXPECT_EQ(view.imageViewport()->transform().mode(), ViewScaleMode::Fit);
    sendKey(*view.imageViewport(), Qt::Key_Space, QStringLiteral(" "));
    EXPECT_EQ(pauseRequests, 1);
    EXPECT_EQ(resumeRequests, 0);

    view.setStatus({
        ViewerState::Paused,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    sendKey(*view.imageViewport(), Qt::Key_Space, QStringLiteral(" "));
    EXPECT_EQ(pauseRequests, 1);
    EXPECT_EQ(resumeRequests, 1);
}

TEST(WorkstationView, ViewerShortcutsDoNotCaptureSidebarFocus) {
    WorkstationView view;
    view.resize(900, 600);
    view.show();
    view.activateWindow();
    QCoreApplication::processEvents();
    ASSERT_TRUE(view.imageViewport()
                    ->present(lumora::test::makeDisplayFrame(1200, 800, 9))
                    .hasValue());
    view.setStatus({
        ViewerState::Live,
        FrameFreshness::Current,
        std::nullopt,
        std::chrono::milliseconds{0},
    });
    view.imageViewport()->setActualPixels();
    auto* pauseLive =
        view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    ASSERT_NE(pauseLive, nullptr);
    pauseLive->setFocus();
    ASSERT_TRUE(pauseLive->hasFocus());

    sendKey(*pauseLive, Qt::Key_F, QStringLiteral("f"));

    EXPECT_EQ(view.imageViewport()->transform().mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(view.imageViewport()->transform().scale(), 1.0);
}

TEST(WorkstationView, SidebarRemainsFixedWhileViewerResizes) {
    WorkstationView view;
    view.resize(1280, 800);
    view.show();
    QCoreApplication::processEvents();
    const int sidebarWidth = view.sidebar()->width();
    const int wideViewerWidth = view.imageViewport()->width();

    view.resize(900, 600);
    QCoreApplication::processEvents();

    EXPECT_EQ(view.sidebar()->width(), sidebarWidth);
    EXPECT_LT(view.imageViewport()->width(), wideViewerWidth);
    EXPECT_GT(view.imageViewport()->width(), view.sidebar()->width());
}

TEST(WorkstationView, SafetyIndicationsRemainVisibleWhenReparentedFullscreen) {
    using namespace std::chrono;
    QWidget fullscreenHost;
    auto* layout = new QVBoxLayout(&fullscreenHost);
    auto* view = new WorkstationView;
    layout->addWidget(view);
    view->setStatus({
        ViewerState::Paused,
        FrameFreshness::Current,
        sys_days{year{2026} / April / 25},
        1500ms,
    });

    fullscreenHost.showFullScreen();
    QCoreApplication::processEvents();

    const auto* banner =
        view->findChild<QLabel*>(QStringLiteral("releaseClassBanner"));
    const auto* overlay =
        view->findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
    ASSERT_NE(banner, nullptr);
    ASSERT_NE(overlay, nullptr);
    EXPECT_TRUE(banner->isVisible());
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_TRUE(overlay->text().startsWith(QStringLiteral("PAUSED\n")));
}

}  // namespace
