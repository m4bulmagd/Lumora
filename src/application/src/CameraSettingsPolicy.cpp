#include <lumora/application/CameraSettingsPolicy.hpp>

namespace lumora::application {

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
        && requested.requestedFps == prepared.requestedFps
        && requested.acquisitionMode == prepared.acquisitionMode;
}

}  // namespace lumora::application
