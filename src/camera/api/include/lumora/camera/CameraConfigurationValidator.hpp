#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Result.hpp>

namespace lumora::camera {

struct CameraConfigurationWriteMask final {
    bool pixelFormat{false};
    bool roi{false};
    bool frameRate{false};
    bool exposureMode{false};
    bool exposureValue{false};
    bool gainMode{false};
    bool gainValue{false};
};

[[nodiscard]] core::Result<void> validateCameraCapabilities(const CameraCapabilities& capabilities);

// Actual FPS is required. Readable Auto values are measurements, not requests.
[[nodiscard]] core::Result<void> validateCameraConfigurationReadback(
    const CameraConfiguration& configuration, const CameraCapabilities& capabilities);

// Fixed fields must equal fresh actual facts; an unchanged field has no write bit.
[[nodiscard]] core::Result<CameraConfigurationWriteMask> planCameraConfigurationChange(
    const CameraConfiguration& requested, const CameraConfiguration& current,
    const CameraCapabilities& capabilities, bool streaming);

[[nodiscard]] core::Result<void> validateCameraConfiguration(
    const CameraConfiguration& configuration,
    const CameraCapabilities& capabilities);

}  // namespace lumora::camera
