#pragma once

#include <lumora/camera/CameraTypes.hpp>

namespace lumora::application {

// Resource compatibility requires identical source format, full ROI and acquisition
// mode, and explicit positive finite FPS in both configurations. FPS, exposure and
// gain may differ; admission must still validate the camera's capabilities.
[[nodiscard]] bool isCameraSettingsCompatible(
    const camera::CameraConfiguration& requested,
    const camera::CameraConfiguration& prepared);

}  // namespace lumora::application
