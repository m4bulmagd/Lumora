#pragma once

#include <QMainWindow>

namespace lumora::ui {
class WorkstationView;
class CameraStartupPanel;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    [[nodiscard]] WorkstationView& workstationView() const noexcept;
    [[nodiscard]] CameraStartupPanel& cameraStartupPanel() const noexcept;
private:
    WorkstationView* view_;
    CameraStartupPanel* panel_;
};

}  // namespace lumora::ui
