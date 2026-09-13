#include <QGuiApplication>
#include <QQuickWindow>
#include <QDebug>
#include <gtest/gtest.h>
int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    qInfo() << "Renderer experiment Qt" << qVersion() << "platform" << QGuiApplication::platformName()
        << "requested loop" << qgetenv("QSG_RENDER_LOOP") << "requested backend" << qgetenv("QT_QUICK_BACKEND");
    ::testing::InitGoogleTest(&argc,argv);
    return RUN_ALL_TESTS();
}
