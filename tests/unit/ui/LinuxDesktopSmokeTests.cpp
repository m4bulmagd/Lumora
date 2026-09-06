#include <lumora/ui/MainWindow.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QThread>
#include <QWindow>

#include <gtest/gtest.h>

namespace {

TEST(LinuxDesktopSmoke, ExposesEvaluationWindowOnXcbAndCloses) {
    ASSERT_EQ(QGuiApplication::platformName(), QStringLiteral("xcb"));

    lumora::ui::MainWindow window;
    window.show();
    ASSERT_NE(window.windowHandle(), nullptr);

    QElapsedTimer timer;
    timer.start();
    while (!window.windowHandle()->isExposed() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    ASSERT_TRUE(window.windowHandle()->isExposed());

    const auto* banner = window.findChild<QLabel*>(QStringLiteral("releaseClassBanner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_TRUE(banner->isVisible());
    EXPECT_EQ(banner->text(), QStringLiteral("EVALUATION — NOT FOR CLINICAL USE"));

    EXPECT_TRUE(window.close());
    EXPECT_FALSE(window.isVisible());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
