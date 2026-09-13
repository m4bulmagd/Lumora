#pragma once

#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <utility>

namespace lumora::ui {

[[nodiscard]] inline bool isWritableCameraControl(camera::ControlAccess access) noexcept {
    return access == camera::ControlAccess::WritableStopped
        || access == camera::ControlAccess::WritableStreaming;
}

[[nodiscard]] inline bool cameraSettingsFixedFieldsMatchCurrent(
    const camera::CameraConfiguration& requested,
    const camera::CameraConfiguration& current,
    const camera::CameraCapabilities& capabilities) noexcept {
    const auto sameFormat = [](const auto& left, const auto& right) {
        return left.canonicalName == right.canonicalName
            && left.canonicalEncoding == right.canonicalEncoding
            && left.validBits == right.validBits
            && left.sampleMaximum == right.sampleMaximum
            && left.packing == right.packing
            && left.alignment == right.alignment
            && left.applicationStorage == right.applicationStorage;
    };
    const auto sameRoi = [](const auto& left, const auto& right) {
        return left.x == right.x && left.y == right.y
            && left.width == right.width && left.height == right.height;
    };
    return (isWritableCameraControl(capabilities.pixelFormatAccess)
            || sameFormat(requested.pixelFormat, current.pixelFormat))
        && (isWritableCameraControl(capabilities.roi.access)
            || sameRoi(requested.roi, current.roi))
        && (isWritableCameraControl(capabilities.frameRate.access)
            || requested.requestedFps == current.requestedFps)
        && (isWritableCameraControl(capabilities.exposureModeAccess)
            || requested.exposure.mode == current.exposure.mode)
        && (isWritableCameraControl(capabilities.exposure.access)
            || requested.exposure.mode != camera::ExposureMode::Manual
            || requested.exposure.requestedMicroseconds
                == current.exposure.requestedMicroseconds)
        && (isWritableCameraControl(capabilities.gainModeAccess)
            || requested.gain.mode == current.gain.mode)
        && (isWritableCameraControl(capabilities.gain.access)
            || requested.gain.mode != camera::GainMode::Manual
            || requested.gain.requestedDb == current.gain.requestedDb)
        && requested.acquisitionMode == current.acquisitionMode;
}

// Reconciles a prepared or saved draft with fresh device facts. Only fields
// that the camera reports as fixed are replaced; writable operator choices are
// retained. The result remains a request, so automatic/absent modes cannot
// carry an actual numeric measurement.
[[nodiscard]] inline core::Result<camera::CameraConfiguration> normalizeCameraSettingsDraft(
    const camera::CameraConfiguration& preferred,
    const camera::CameraConfiguration& current,
    const camera::CameraCapabilities& capabilities) {
    using Result = core::Result<camera::CameraConfiguration>;
    if (auto valid = camera::validateCameraCapabilities(capabilities); !valid.hasValue()) {
        return Result::failure(valid.error());
    }
    if (auto valid = camera::validateCameraConfigurationReadback(current, capabilities);
        !valid.hasValue()) {
        return Result::failure(valid.error());
    }

    auto normalized = preferred;
    if (!isWritableCameraControl(capabilities.pixelFormatAccess)) {
        normalized.pixelFormat = current.pixelFormat;
    }
    if (!isWritableCameraControl(capabilities.roi.access)) normalized.roi = current.roi;
    if (!isWritableCameraControl(capabilities.frameRate.access)) {
        normalized.requestedFps = current.requestedFps;
    }
    if (!isWritableCameraControl(capabilities.exposureModeAccess)) {
        normalized.exposure.mode = current.exposure.mode;
    }
    if (normalized.exposure.mode != camera::ExposureMode::Manual) {
        normalized.exposure.requestedMicroseconds.reset();
    } else if (!isWritableCameraControl(capabilities.exposure.access)) {
        normalized.exposure.requestedMicroseconds = current.exposure.requestedMicroseconds;
    }
    if (!isWritableCameraControl(capabilities.gainModeAccess)) {
        normalized.gain.mode = current.gain.mode;
    }
    if (normalized.gain.mode != camera::GainMode::Manual) {
        normalized.gain.requestedDb.reset();
    } else if (!isWritableCameraControl(capabilities.gain.access)) {
        normalized.gain.requestedDb = current.gain.requestedDb;
    }
    normalized.acquisitionMode = current.acquisitionMode;

    return Result::success(std::move(normalized));
}

}  // namespace lumora::ui
