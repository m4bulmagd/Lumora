#include <lumora/ui/MainWindow.hpp>

#include <lumora/ui/WorkstationView.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <QScrollArea>
#include <QVBoxLayout>

namespace lumora::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Lumora"));
    resize(1280, 800);
    setMinimumSize(900, 600);

    view_=new WorkstationView(this);
    setCentralWidget(view_);
    auto* scroll=new QScrollArea(view_->sidebar());
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    panel_=new CameraStartupPanel;
    scroll->setWidget(panel_);
    auto* layout=qobject_cast<QVBoxLayout*>(view_->sidebar()->layout());
    delete layout->takeAt(1);
    layout->insertWidget(1,scroll,1);
}
WorkstationView& MainWindow::workstationView() const noexcept { return *view_; }
CameraStartupPanel& MainWindow::cameraStartupPanel() const noexcept { return *panel_; }

}  // namespace lumora::ui
