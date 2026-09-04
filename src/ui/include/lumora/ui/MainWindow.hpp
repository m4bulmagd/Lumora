#pragma once

#include <QMainWindow>

namespace lumora::ui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};

}  // namespace lumora::ui
