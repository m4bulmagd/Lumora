#include <lumora/ui/MainWindow.hpp>

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    lumora::ui::MainWindow window;
    window.show();
    return application.exec();
}
