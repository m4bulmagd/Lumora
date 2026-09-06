#include <lumora/ui/MainWindow.hpp>

#include <lumora/ui/WorkstationView.hpp>

namespace lumora::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Lumora"));
    resize(1280, 800);
    setMinimumSize(900, 600);

    setCentralWidget(new WorkstationView(this));
}

}  // namespace lumora::ui
