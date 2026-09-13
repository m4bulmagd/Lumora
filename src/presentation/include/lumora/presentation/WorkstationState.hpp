#pragma once

#include <lumora/application/ApplicationState.hpp>
#include <lumora/application/InstallationProfiles.hpp>
#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Error.hpp>
#include <lumora/presentation/WorkstationStatus.hpp>

#include <memory>
#include <optional>

namespace lumora::presentation {

enum class CameraStartupIntent { Refresh, Connect, Apply, Confirm, Start, Stop, Disconnect, Retry, ResumeLive };

struct WorkstationState final {
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
    bool contextBound{false};
    WorkstationStatus workstationStatus;
};

}  // namespace lumora::presentation
