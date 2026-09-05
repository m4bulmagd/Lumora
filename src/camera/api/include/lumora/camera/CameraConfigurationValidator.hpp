#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Result.hpp>

namespace lumora::camera {

[[nodiscard]] core::Result<void> validateCameraConfiguration(
    const CameraConfiguration& configuration,
    const CameraCapabilities& capabilities);

}  // namespace lumora::camera
