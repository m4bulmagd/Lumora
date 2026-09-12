#include <lumora/ui/MainWindow.hpp>

#include <lumora/ui/WorkstationView.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>

namespace lumora::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Lumora"));
    resize(1280, 800);
    setMinimumSize(900, 600);

    view_=new WorkstationView(this);
    setCentralWidget(view_);
    panel_=new CameraStartupPanel;
    view_->addSidebarPanel(panel_);
}
WorkstationView& MainWindow::workstationView() const noexcept { return *view_; }
CameraStartupPanel& MainWindow::cameraStartupPanel() const noexcept { return *panel_; }

}  // namespace lumora::ui
