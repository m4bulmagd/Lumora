#pragma once
#include <lumora/ui/CameraStartupPanel.hpp>
#include <QDialog>
#include <memory>
namespace lumora::ui {
class InstallationSettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit InstallationSettingsDialog(QWidget* parent = nullptr);
    ~InstallationSettingsDialog() override;
    void setPresentation(CameraStartupPanelPresentation presentation);
signals:
    void installationSaveRequested(std::uint64_t sessionGeneration,
        camera::CameraId cameraId, core::Orientation orientation, bool confirmed, bool repairInvalid);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
