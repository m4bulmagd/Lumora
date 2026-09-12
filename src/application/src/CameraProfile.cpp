#include <lumora/application/CameraProfile.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

namespace lumora::application {
namespace {

[[nodiscard]] core::Error capabilityError(std::string code, std::string detail) {
    return {core::ErrorCategory::Configuration, std::move(code),
        "Saved camera capabilities are invalid.", std::move(detail), true};
}

[[nodiscard]] bool pixelFormatsEqual(
    const core::SourcePixelFormat& left,
    const core::SourcePixelFormat& right) {
    return std::tie(left.canonicalName, left.canonicalEncoding, left.validBits,
               left.sampleMaximum, left.packing, left.alignment,
               left.applicationStorage)
        == std::tie(right.canonicalName, right.canonicalEncoding, right.validBits,
            right.sampleMaximum, right.packing, right.alignment,
            right.applicationStorage);
}

template<typename T>
[[nodiscard]] bool hasDuplicates(const std::vector<T>& values) {
    for (auto current = values.begin(); current != values.end(); ++current) {
        if (std::find(std::next(current), values.end(), *current) != values.end()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasDuplicatePixelFormats(
    const std::vector<core::SourcePixelFormat>& formats) {
    for (auto current = formats.begin(); current != formats.end(); ++current) {
        if (std::any_of(std::next(current), formats.end(), [&](const auto& candidate) {
                return pixelFormatsEqual(*current, candidate);
            })) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool validExposureMode(camera::ExposureMode mode) {
    return mode == camera::ExposureMode::Manual || mode == camera::ExposureMode::Auto;
}

[[nodiscard]] bool validGainMode(camera::GainMode mode) {
    return mode == camera::GainMode::Manual || mode == camera::GainMode::Auto;
}

[[nodiscard]] bool validNumeric(
    const camera::NumericCapability& value,
    bool positiveMinimum) {
    return std::isfinite(value.minimum) && std::isfinite(value.maximum)
        && std::isfinite(value.increment) && value.minimum <= value.maximum
        && value.increment > 0.0 && (!positiveMinimum || value.minimum > 0.0);
}

[[nodiscard]] bool validRoi(
    const camera::RegionOfInterestCapability& value) {
    const auto dimensionValid = [](std::uint32_t minimum, std::uint32_t maximum,
                                    std::uint32_t increment, bool positive) {
        return minimum <= maximum && increment > 0U && (!positive || minimum > 0U);
    };
    return dimensionValid(value.minimum.x, value.maximum.x, value.increment.x, false)
        && dimensionValid(value.minimum.y, value.maximum.y, value.increment.y, false)
        && dimensionValid(value.minimum.width, value.maximum.width,
            value.increment.width, true)
        && dimensionValid(value.minimum.height, value.maximum.height,
            value.increment.height, true);
}

}  // namespace

bool cameraIdentityKeysEqual(
    const core::CameraIdentity& left,
    const core::CameraIdentity& right) {
    return std::tie(left.manufacturer, left.model, left.serial)
        == std::tie(right.manufacturer, right.model, right.serial);
}

core::Result<void> validateCameraCapabilities(
    const camera::CameraCapabilities& capabilities) {
    if (capabilities.pixelFormats.empty() || capabilities.exposureModes.empty()
        || capabilities.gainModes.empty()) {
        return core::Result<void>::failure(capabilityError(
            "camera_capabilities_incomplete",
            "Pixel formats, exposure modes and gain modes must be present."));
    }
    if (hasDuplicatePixelFormats(capabilities.pixelFormats)
        || hasDuplicates(capabilities.exposureModes)
        || hasDuplicates(capabilities.gainModes)) {
        return core::Result<void>::failure(capabilityError(
            "camera_capabilities_duplicate",
            "Capability sets must not contain duplicate values."));
    }
    if (!std::all_of(capabilities.pixelFormats.begin(), capabilities.pixelFormats.end(),
            [](const auto& format) {
                return core::validateSourcePixelFormat(format).hasValue();
            })
        || !std::all_of(capabilities.exposureModes.begin(),
            capabilities.exposureModes.end(), validExposureMode)
        || !std::all_of(capabilities.gainModes.begin(), capabilities.gainModes.end(),
            validGainMode)
        || !validRoi(capabilities.roi)
        || !validNumeric(capabilities.frameRate, true)
        || !validNumeric(capabilities.exposure, true)
        || !validNumeric(capabilities.gain, false)) {
        return core::Result<void>::failure(capabilityError(
            "camera_capabilities_invalid",
            "A pixel format, ROI range, numeric range or mode is invalid."));
    }
    return core::Result<void>::success();
}

}  // namespace lumora::application
