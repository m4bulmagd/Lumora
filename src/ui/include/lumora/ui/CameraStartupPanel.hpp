#pragma once

#include <lumora/application/ApplicationState.hpp>
#include <lumora/application/InstallationProfiles.hpp>
#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Error.hpp>

#include <QWidget>
#include <QPointer>

#include <memory>
#include <optional>

namespace lumora::ui {

class CameraSettingsDialog;
class InstallationSettingsDialog;

struct CameraStartupPanelPresentation final {
    std::shared_ptr<const application::CameraStatusSnapshot> cameraStatus;
    std::optional<camera::CameraId> selectedCameraId;
    std::optional<camera::CameraConfiguration> requestedConfiguration;
    bool controlsEnabled{true};
    bool ordinaryOperationPending{false};
    bool preferencesLoadCompleted{false};
    bool resumeLiveAvailable{false};
    std::optional<core::Error> startupWarning;
    std::shared_ptr<const application::InstallationProfilesSnapshot> installationProfiles;
    std::optional<application::InstallationProfileReference> activeInstallationProfile;
    std::optional<core::Orientation> activeOrientation;
    bool installationProfilePending{false};
    bool installationBindingCurrent{true};
    std::optional<application::InstallationSaveOutcome> installationProfileOutcome;
    std::optional<core::Error> installationProfileError;
};

class CameraStartupPanel final : public QWidget {
    Q_OBJECT

public:
    explicit CameraStartupPanel(QWidget* parent = nullptr);

    [[nodiscard]] const CameraStartupPanelPresentation& presentation() const noexcept;
    void setPresentation(CameraStartupPanelPresentation presentation);

signals:
    void settingsEditingStarted(std::uint64_t sessionGeneration, camera::CameraId cameraId);
    void installationSaveRequested(std::uint64_t sessionGeneration, camera::CameraId cameraId,
        core::Orientation orientation, bool confirmed, bool repairInvalid);
    void settingsApplyRequested(std::uint64_t sessionGeneration,
        camera::CameraId cameraId, camera::CameraConfiguration requested);
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
    QPointer<CameraSettingsDialog> settingsDialog_;
    QPointer<InstallationSettingsDialog> installationDialog_;
};

}  // namespace lumora::ui
