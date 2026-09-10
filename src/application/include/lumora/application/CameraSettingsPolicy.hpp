#pragma once

#include <lumora/camera/CameraTypes.hpp>

namespace lumora::application {

[[nodiscard]] bool isCameraSettingsCompatible(
    const camera::CameraConfiguration& requested,
    const camera::CameraConfiguration& prepared);

}  // namespace lumora::application
