#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
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
    if (capability.minimum.width == 0U || capability.minimum.height == 0U) {
        violations.emplace_back("capability_roi_dimensions");
    }
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
    const CameraCapabilities& capabilities, bool readback) {
    if ((exposure.mode && !contains(capabilities.exposureModes, *exposure.mode))
        || (!exposure.mode && !capabilities.exposureModes.empty())) {
        violations.emplace_back("exposure_mode_unsupported");
    }
    if (exposure.mode == ExposureMode::Manual && !exposure.requestedMicroseconds.has_value()) {
        violations.emplace_back("exposure_value_required");
    }
    if (exposure.requestedMicroseconds && (!exposure.mode
        || (exposure.mode == ExposureMode::Auto && (!readback || capabilities.exposure.access == ControlAccess::Unavailable)))) {
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
    const CameraCapabilities& capabilities, bool readback) {
    if ((gain.mode && !contains(capabilities.gainModes, *gain.mode))
        || (!gain.mode && !capabilities.gainModes.empty())) {
        violations.emplace_back("gain_mode_unsupported");
    }
    if (gain.mode == GainMode::Manual && !gain.requestedDb.has_value()) {
        violations.emplace_back("gain_value_required");
    }
    if (gain.requestedDb && (!gain.mode
        || (gain.mode == GainMode::Auto && (!readback || capabilities.gain.access == ControlAccess::Unavailable)))) {
        violations.emplace_back("gain_value_unexpected");
    }
    addRequestedNumericViolations(
        violations, gain.requestedDb, capabilities.gain, "gain", false);
}

bool validAccess(ControlAccess access) {
    return access == ControlAccess::Unavailable || access == ControlAccess::ReadOnly
        || access == ControlAccess::WritableStopped || access == ControlAccess::WritableStreaming;
}

void addAccessViolation(std::vector<std::string>& violations, ControlAccess access, std::string_view name) {
    if (!validAccess(access)) violations.emplace_back("capability_" + std::string(name) + "_access");
}

template<typename Mode>
void addModeCapabilityViolations(std::vector<std::string>& violations, const std::vector<Mode>& modes,
    ControlAccess modeAccess, const NumericCapability& numeric, std::string_view name,
    Mode manual, Mode automatic, bool positive) {
    const auto prefix = "capability_" + std::string(name);
    addAccessViolation(violations, modeAccess, std::string(name) + "_mode");
    addAccessViolation(violations, numeric.access, name);
    if (modes.empty() != (modeAccess == ControlAccess::Unavailable))
        violations.emplace_back(prefix + "_modes");
    for (auto mode = modes.begin(); mode != modes.end(); ++mode) {
        if (*mode != manual && *mode != automatic) violations.emplace_back(prefix + "_modes");
        if (std::find(mode + 1, modes.end(), *mode) != modes.end()) {
            violations.emplace_back("camera_capabilities_duplicate");
            break;
        }
    }
    if (numeric.access == ControlAccess::Unavailable) {
        if (numeric.minimum != 0.0 || numeric.maximum != 0.0 || numeric.increment != 0.0)
            violations.emplace_back(prefix + "_absent_range");
        if (std::find(modes.begin(), modes.end(), manual) != modes.end())
            violations.emplace_back(prefix + "_manual_value_unavailable");
    } else {
        if (modes.empty()) violations.emplace_back(prefix + "_modes");
        addNumericCapabilityViolations(violations, numeric, name, positive);
    }
}

void addCapabilityViolations(std::vector<std::string>& violations, const CameraCapabilities& capabilities) {
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
    addModeCapabilityViolations(violations, capabilities.exposureModes,
        capabilities.exposureModeAccess, capabilities.exposure, "exposure", ExposureMode::Manual, ExposureMode::Auto, true);
    addModeCapabilityViolations(violations, capabilities.gainModes,
        capabilities.gainModeAccess, capabilities.gain, "gain", GainMode::Manual, GainMode::Auto, false);
    addAccessViolation(violations, capabilities.pixelFormatAccess, "pixel_format");
    addAccessViolation(violations, capabilities.roi.access, "roi");
    addAccessViolation(violations, capabilities.frameRate.access, "frame_rate");
    for (auto format = capabilities.pixelFormats.begin(); format != capabilities.pixelFormats.end(); ++format) {
        if (std::any_of(format + 1, capabilities.pixelFormats.end(),
                [&](const auto& other) { return samePixelFormat(*format, other); })) {
            violations.emplace_back("camera_capabilities_duplicate");
            break;
        }
    }

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

core::Result<void> validateCameraCapabilities(const CameraCapabilities& capabilities) {
    std::vector<std::string> violations;
    addCapabilityViolations(violations, capabilities);
    if (violations.empty()) return core::Result<void>::success();
    return core::Result<void>::failure({core::ErrorCategory::CameraConfiguration,
        violations.front(), "Camera capabilities are invalid.", joinViolations(violations), true});
}

namespace {
core::Result<void> validateConfiguration(
    const CameraConfiguration& configuration,
    const CameraCapabilities& capabilities, bool readback) {
    std::vector<std::string> violations;

    addCapabilityViolations(violations, capabilities);

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
    addExposureViolations(violations, configuration.exposure, capabilities, readback);
    addGainViolations(violations, configuration.gain, capabilities, readback);
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

}  // namespace

core::Result<void> validateCameraConfiguration(
    const CameraConfiguration& configuration, const CameraCapabilities& capabilities) {
    return validateConfiguration(configuration, capabilities, false);
}

core::Result<void> validateCameraConfigurationReadback(
    const CameraConfiguration& configuration, const CameraCapabilities& capabilities) {
    if (!configuration.requestedFps || !std::isfinite(*configuration.requestedFps)
        || *configuration.requestedFps <= 0.0) {
        return core::Result<void>::failure({core::ErrorCategory::CameraConfiguration,
            "camera_actual_fps_invalid", "Camera readback must include positive finite actual FPS.", "", true});
    }
    return validateConfiguration(configuration, capabilities, true);
}

core::Result<CameraConfigurationWriteMask> planCameraConfigurationChange(
    const CameraConfiguration& requested, const CameraConfiguration& current,
    const CameraCapabilities& capabilities, bool streaming) {
    using Plan = core::Result<CameraConfigurationWriteMask>;
    auto valid = validateCameraConfiguration(requested, capabilities);
    if (!valid.hasValue()) return Plan::failure(valid.error());
    valid = validateCameraConfigurationReadback(current, capabilities);
    if (!valid.hasValue()) return Plan::failure(valid.error());
    CameraConfigurationWriteMask mask;
    std::optional<core::Error> error;
    const auto plan = [&](bool changed, ControlAccess access, bool& write, const char* field) {
        if (!changed || error) return;
        const bool writable = access == ControlAccess::WritableStreaming
            || (access == ControlAccess::WritableStopped && !streaming);
        if (writable) { write = true; return; }
        const bool stoppedOnly = access == ControlAccess::WritableStopped && streaming;
        error = core::Error{core::ErrorCategory::CameraConfiguration,
            std::string(field) + (stoppedOnly ? "_not_writable_while_streaming" : "_not_writable"),
            stoppedOnly ? "Stop the stream before changing this setting."
                        : "This camera setting is fixed; retain its current value.", "", true};
    };
    const auto roi = [](const auto& value) { return std::tie(value.x, value.y, value.width, value.height); };
    plan(!samePixelFormat(requested.pixelFormat, current.pixelFormat), capabilities.pixelFormatAccess,
        mask.pixelFormat, "pixel_format");
    plan(roi(requested.roi) != roi(current.roi), capabilities.roi.access, mask.roi, "roi");
    plan(requested.requestedFps != current.requestedFps, capabilities.frameRate.access, mask.frameRate, "frame_rate");
    plan(requested.exposure.mode != current.exposure.mode, capabilities.exposureModeAccess, mask.exposureMode, "exposure");
    // Auto clears the request's numeric representation without touching that node.
    if (requested.exposure.mode == ExposureMode::Manual)
        plan(requested.exposure.requestedMicroseconds != current.exposure.requestedMicroseconds,
            capabilities.exposure.access, mask.exposureValue, "exposure");
    plan(requested.gain.mode != current.gain.mode, capabilities.gainModeAccess, mask.gainMode, "gain");
    if (requested.gain.mode == GainMode::Manual)
        plan(requested.gain.requestedDb != current.gain.requestedDb,
            capabilities.gain.access, mask.gainValue, "gain");
    if (error) return Plan::failure(std::move(*error));
    return Plan::success(mask);
}

}  // namespace lumora::camera
