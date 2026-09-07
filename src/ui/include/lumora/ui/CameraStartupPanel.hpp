#pragma once

#include <lumora/application/ApplicationState.hpp>
#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Error.hpp>

#include <QWidget>

#include <memory>
#include <optional>

namespace lumora::ui {

struct CameraStartupPanelPresentation final {
    std::shared_ptr<const application::CameraStatusSnapshot> cameraStatus;
    std::optional<camera::CameraId> selectedCameraId;
    std::optional<camera::CameraConfiguration> fixedRequestedConfiguration;
    bool controlsEnabled{true};
    bool ordinaryOperationPending{false};
    bool preferencesLoadCompleted{false};
    bool resumeLiveAvailable{false};
    std::optional<core::Error> startupWarning;
};

class CameraStartupPanel final : public QWidget {
    Q_OBJECT

public:
    explicit CameraStartupPanel(QWidget* parent = nullptr);

    [[nodiscard]] const CameraStartupPanelPresentation& presentation() const noexcept;
    void setPresentation(CameraStartupPanelPresentation presentation);

signals:
    void selectionRequested(camera::CameraId cameraId);
    void refreshRequested();
    void connectRequested();
    void applyRequested();
    void confirmRequested();
    void startRequested();
    void stopRequested();
    void disconnectRequested();
    void retryRequested();
    void resumeLiveRequested();

private:
    void updatePresentation();

    CameraStartupPanelPresentation presentation_;
};

}  // namespace lumora::ui
