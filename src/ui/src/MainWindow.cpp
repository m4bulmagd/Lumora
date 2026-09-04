#include <lumora/ui/MainWindow.hpp>

#include <QColor>
#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

namespace lumora::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Lumora"));
    resize(1280, 800);
    setMinimumSize(900, 600);

    auto* workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("workspace"));
    workspace->setAutoFillBackground(true);

    auto palette = workspace->palette();
    palette.setColor(QPalette::Window, QColor(28, 31, 36));
    palette.setColor(QPalette::WindowText, QColor(238, 240, 243));
    workspace->setPalette(palette);

    auto* layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(16, 12, 16, 12);

    auto* releaseClassBanner = new QLabel(
        tr("EVALUATION — NOT FOR CLINICAL USE"), workspace);
    releaseClassBanner->setObjectName(QStringLiteral("releaseClassBanner"));
    releaseClassBanner->setAlignment(Qt::AlignCenter);
    releaseClassBanner->setStyleSheet(
        QStringLiteral("QLabel { color: #ffcc66; font-weight: 700; }"));
    layout->addWidget(releaseClassBanner);
    layout->addStretch(1);

    setCentralWidget(workspace);
}

}  // namespace lumora::ui
