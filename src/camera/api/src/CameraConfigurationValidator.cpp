#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::camera {
namespace {

bool isValidNumericCapability(const NumericCapability& capability) {
    return std::isfinite(capability.minimum) &&
           std::isfinite(capability.maximum) &&
           std::isfinite(capability.increment) &&
           capability.minimum <= capability.maximum && capability.increment > 0.0;
}

bool contains(const std::vector<ExposureMode>& modes, ExposureMode mode) {
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

bool contains(const std::vector<GainMode>& modes, GainMode mode) {
    return std::find(modes.begin(), modes.end(), mode) != modes.end();
}

bool samePixelFormat(
    const core::SourcePixelFormat& left,
    const core::SourcePixelFormat& right) {
    return left.canonicalName == right.canonicalName &&
           left.canonicalEncoding == right.canonicalEncoding &&
           left.validBits == right.validBits &&
           left.sampleMaximum == right.sampleMaximum && left.packing == right.packing &&
           left.alignment == right.alignment &&
           left.applicationStorage == right.applicationStorage;
}

bool isAligned(std::uint32_t value, std::uint32_t minimum, std::uint32_t increment) {
    return value >= minimum && (value - minimum) % increment == 0U;
}

bool isInRange(double value, const NumericCapability& capability) {
    return std::isfinite(value) && value >= capability.minimum &&
           value <= capability.maximum;
}

void addNumericCapabilityViolations(
    std::vector<std::string>& violations,
    const NumericCapability& capability,
    std::string_view name,
    bool requiresPositiveMinimum) {
    if (!std::isfinite(capability.minimum) || !std::isfinite(capability.maximum) ||
        capability.minimum > capability.maximum ||
        (requiresPositiveMinimum && capability.minimum <= 0.0)) {
        violations.emplace_back("capability_" + std::string(name) + "_range");
    }
    if (!std::isfinite(capability.increment) || capability.increment <= 0.0) {
        violations.emplace_back("capability_" + std::string(name) + "_increment");
    }
}

void addRoiCapabilityViolations(
    std::vector<std::string>& violations,
    const RegionOfInterestCapability& capability) {
    const auto validateDimension = [&violations](
                                       std::uint32_t minimum,
                                       std::uint32_t maximum,
                                       std::uint32_t increment,
                                       std::string_view name) {
        if (minimum > maximum) {
            violations.emplace_back("capability_roi_" + std::string(name) + "_range");
        }
        if (increment == 0U) {
            violations.emplace_back("capability_roi_" + std::string(name) + "_increment");
        }
    };

    validateDimension(capability.minimum.x, capability.maximum.x, capability.increment.x, "x");
    validateDimension(capability.minimum.y, capability.maximum.y, capability.increment.y, "y");
    validateDimension(
        capability.minimum.width, capability.maximum.width, capability.increment.width, "width");
    validateDimension(
        capability.minimum.height, capability.maximum.height, capability.increment.height, "height");
}

void addRoiValueViolations(
    std::vector<std::string>& violations,
    const core::RegionOfInterest& roi,
    const RegionOfInterestCapability& capability) {
    const auto validateDimension = [&violations](
                                       std::uint32_t value,
                                       std::uint32_t minimum,
                                       std::uint32_t maximum,
                                       std::uint32_t increment,
                                       std::string_view name) {
        if (value < minimum || value > maximum) {
            violations.emplace_back("roi_" + std::string(name) + "_range");
            return;
        }
        if (increment != 0U && !isAligned(value, minimum, increment)) {
            violations.emplace_back("roi_" + std::string(name) + "_increment");
        }
    };

    validateDimension(roi.x, capability.minimum.x, capability.maximum.x, capability.increment.x, "x");
    validateDimension(roi.y, capability.minimum.y, capability.maximum.y, capability.increment.y, "y");
    validateDimension(
        roi.width, capability.minimum.width, capability.maximum.width, capability.increment.width, "width");
    validateDimension(
        roi.height, capability.minimum.height, capability.maximum.height, capability.increment.height, "height");

    const auto exceedsSensor = [](std::uint32_t offset, std::uint32_t size, std::uint32_t maximum) {
        return static_cast<std::uint64_t>(offset) + size > maximum;
    };
    if (exceedsSensor(roi.x, roi.width, capability.maximum.width) ||
        exceedsSensor(roi.y, roi.height, capability.maximum.height)) {
        violations.emplace_back("roi_containment");
    }
}

void addRequestedNumericViolations(
    std::vector<std::string>& violations,
    std::optional<double> value,
    const NumericCapability& capability,
    std::string_view name,
    bool requiresPositiveValue) {
    if (!value.has_value()) {
        return;
    }
    if (!std::isfinite(*value)) {
        violations.emplace_back(std::string(name) + "_finite");
        return;
    }
    if (requiresPositiveValue && *value <= 0.0) {
        violations.emplace_back(std::string(name) + "_positive");
        return;
    }
    if (isValidNumericCapability(capability) && !isInRange(*value, capability)) {
        violations.emplace_back(std::string(name) + "_range");
    }
}

void addExposureViolations(
    std::vector<std::string>& violations,
    const ExposureConfiguration& exposure,
    const CameraCapabilities& capabilities) {
    if (!contains(capabilities.exposureModes, exposure.mode)) {
        violations.emplace_back("exposure_mode_unsupported");
    }
    if (exposure.mode == ExposureMode::Manual && !exposure.requestedMicroseconds.has_value()) {
        violations.emplace_back("exposure_value_required");
    }
    if (exposure.mode == ExposureMode::Auto && exposure.requestedMicroseconds.has_value()) {
        violations.emplace_back("exposure_value_unexpected");
    }
    addRequestedNumericViolations(
        violations,
        exposure.requestedMicroseconds,
        capabilities.exposure,
        "exposure",
        true);
}

void addGainViolations(
    std::vector<std::string>& violations,
    const GainConfiguration& gain,
    const CameraCapabilities& capabilities) {
    if (!contains(capabilities.gainModes, gain.mode)) {
        violations.emplace_back("gain_mode_unsupported");
    }
    if (gain.mode == GainMode::Manual && !gain.requestedDb.has_value()) {
        violations.emplace_back("gain_value_required");
    }
    if (gain.mode == GainMode::Auto && gain.requestedDb.has_value()) {
        violations.emplace_back("gain_value_unexpected");
    }
    addRequestedNumericViolations(
        violations, gain.requestedDb, capabilities.gain, "gain", false);
}

std::string joinViolations(const std::vector<std::string>& violations) {
    std::string detail;
    for (const auto& violation : violations) {
        if (!detail.empty()) {
            detail.push_back('\n');
        }
        detail += violation;
    }
    return detail;
}

}  // namespace

core::Result<void> validateCameraConfiguration(
    const CameraConfiguration& configuration,
    const CameraCapabilities& capabilities) {
    std::vector<std::string> violations;

    for (const auto& format : capabilities.pixelFormats) {
        if (!core::validateSourcePixelFormat(format).hasValue()) {
            violations.emplace_back("capability_pixel_format_invalid");
        }
    }
    if (capabilities.pixelFormats.empty()) {
        violations.emplace_back("capability_pixel_format_missing");
    }
    addRoiCapabilityViolations(violations, capabilities.roi);
    addNumericCapabilityViolations(violations, capabilities.frameRate, "frame_rate", true);
    addNumericCapabilityViolations(violations, capabilities.exposure, "exposure", true);
    addNumericCapabilityViolations(violations, capabilities.gain, "gain", false);
    if (capabilities.exposureModes.empty()) {
        violations.emplace_back("capability_exposure_modes");
    }
    if (capabilities.gainModes.empty()) {
        violations.emplace_back("capability_gain_modes");
    }

    const auto formatIsSupported = std::any_of(
        capabilities.pixelFormats.begin(),
        capabilities.pixelFormats.end(),
        [&configuration](const core::SourcePixelFormat& format) {
            return samePixelFormat(configuration.pixelFormat, format);
        });
    if (!formatIsSupported) {
        violations.emplace_back("pixel_format_unsupported");
    }
    addRoiValueViolations(violations, configuration.roi, capabilities.roi);
    addRequestedNumericViolations(
        violations, configuration.requestedFps, capabilities.frameRate, "frame_rate", true);
    addExposureViolations(violations, configuration.exposure, capabilities);
    addGainViolations(violations, configuration.gain, capabilities);
    if (configuration.acquisitionMode != AcquisitionMode::Continuous) {
        violations.emplace_back("acquisition_mode_unsupported");
    }

    if (violations.empty()) {
        return core::Result<void>::success();
    }

    return core::Result<void>::failure({
        .category = core::ErrorCategory::CameraConfiguration,
        .code = violations.front(),
        .operatorSummary = "Camera configuration is invalid.",
        .diagnosticDetail = joinViolations(violations),
        .recoverable = true,
    });
}

}  // namespace lumora::camera
