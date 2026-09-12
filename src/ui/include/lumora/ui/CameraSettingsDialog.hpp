#pragma once

#include <lumora/ui/CameraStartupPanel.hpp>

#include <QDialog>

#include <memory>

namespace lumora::ui {

class CameraSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CameraSettingsDialog(QWidget* parent = nullptr);
    ~CameraSettingsDialog() override;
    void setPresentation(CameraStartupPanelPresentation presentation);

signals:
    void settingsApplyRequested(std::uint64_t sessionGeneration,
        camera::CameraId cameraId, camera::CameraConfiguration requested);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::ui
