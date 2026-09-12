#include <lumora/ui/MainWindow.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>

#include <gtest/gtest.h>

namespace {

TEST(MainWindowSmoke, HasStableIdentityAndCanClose) {
    lumora::ui::MainWindow window;
    EXPECT_EQ(window.objectName(), QStringLiteral("mainWindow"));
    EXPECT_FALSE(window.windowTitle().isEmpty());

    window.show();
    QCoreApplication::processEvents();
    EXPECT_TRUE(window.isVisible());

    window.close();
    EXPECT_FALSE(window.isVisible());
}

TEST(MainWindowSmoke, ShowsMandatoryEvaluationWarning) {
    lumora::ui::MainWindow window;
    const auto* banner = window.findChild<QLabel*>(QStringLiteral("releaseClassBanner"));

    ASSERT_NE(banner, nullptr);
    EXPECT_EQ(banner->text(), QStringLiteral("EVALUATION — NOT FOR CLINICAL USE"));
}

TEST(MainWindowSmoke, CameraContentDoesNotDisplacePersistentProcessingWarning) {
    lumora::ui::MainWindow window;
    window.resize(900, 600);
    auto& view = window.workstationView();
    lumora::processing::ProcessorStatus processing;
    processing.mode = lumora::processing::ProcessorMode::OriginalOnlyLatched;
    processing.retrySupported = true;
    view.setProcessingStatus(processing);
    window.show();
    QCoreApplication::processEvents();

    const auto scrolls = view.sidebar()->findChildren<QScrollArea*>();
    ASSERT_EQ(scrolls.size(), 1);
    EXPECT_TRUE(scrolls.front()->widget()->isAncestorOf(&window.cameraStartupPanel())
        || scrolls.front()->widget() == &window.cameraStartupPanel());
    auto* warning = view.findChild<QLabel*>(QStringLiteral("processingWarning"));
    auto* retry = view.findChild<QPushButton*>(QStringLiteral("processingRetryButton"));
    auto* pause = view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    ASSERT_NE(warning, nullptr);
    ASSERT_NE(retry, nullptr);
    ASSERT_NE(pause, nullptr);
    EXPECT_GE(view.sidebar()->layout()->indexOf(warning), 0);
    EXPECT_GE(view.sidebar()->layout()->indexOf(retry), 0);
    EXPECT_GE(view.sidebar()->layout()->indexOf(pause), 0);
    EXPECT_FALSE(scrolls.front()->widget()->isAncestorOf(warning));
    EXPECT_TRUE(warning->isVisible());
    EXPECT_TRUE(retry->isVisible());
    EXPECT_TRUE(pause->isVisible());
}

}  // namespace
