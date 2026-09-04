#include <lumora/ui/MainWindow.hpp>

#include <QApplication>
#include <QLabel>

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

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
