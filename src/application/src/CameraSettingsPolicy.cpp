#include <lumora/application/CameraSettingsPolicy.hpp>

#include <cmath>

namespace lumora::application {

bool isSupportedLiveCameraConfiguration(const camera::CameraConfiguration& requested) {
    return core::validateSourcePixelFormat(requested.pixelFormat).hasValue()
        && requested.roi.width > 0 && requested.roi.height > 0
        && requested.requestedFps && std::isfinite(*requested.requestedFps)
        && *requested.requestedFps > 0.0
        && requested.acquisitionMode == camera::AcquisitionMode::Continuous;
}

bool isCameraSettingsCompatible(
    const camera::CameraConfiguration& requested,
    const camera::CameraConfiguration& prepared) {
    const auto& requestedFormat = requested.pixelFormat;
    const auto& preparedFormat = prepared.pixelFormat;
    const auto& requestedRoi = requested.roi;
    const auto& preparedRoi = prepared.roi;

    return requestedFormat.canonicalName == preparedFormat.canonicalName
        && requestedFormat.canonicalEncoding == preparedFormat.canonicalEncoding
        && requestedFormat.validBits == preparedFormat.validBits
        && requestedFormat.sampleMaximum == preparedFormat.sampleMaximum
        && requestedFormat.packing == preparedFormat.packing
        && requestedFormat.alignment == preparedFormat.alignment
        && requestedFormat.applicationStorage == preparedFormat.applicationStorage
        && requestedRoi.x == preparedRoi.x
        && requestedRoi.y == preparedRoi.y
        && requestedRoi.width == preparedRoi.width
        && requestedRoi.height == preparedRoi.height
        && requested.requestedFps && prepared.requestedFps
        && std::isfinite(*requested.requestedFps) && *requested.requestedFps > 0.0
        && std::isfinite(*prepared.requestedFps) && *prepared.requestedFps > 0.0
        && requested.acquisitionMode == prepared.acquisitionMode;
}

}  // namespace lumora::application
