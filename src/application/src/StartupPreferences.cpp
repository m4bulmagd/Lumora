#include <lumora/application/StartupPreferences.hpp>

#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace lumora::application {
namespace {

[[nodiscard]] core::Error preferenceError(
    std::string code,
    std::string detail) {
    return {core::ErrorCategory::Configuration, std::move(code),
        "Saved startup preferences are invalid.", std::move(detail), true};
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

[[nodiscard]] bool regionsEqual(
    const core::RegionOfInterest& left,
    const core::RegionOfInterest& right) {
    return std::tie(left.x, left.y, left.width, left.height)
        == std::tie(right.x, right.y, right.width, right.height);
}

[[nodiscard]] bool numericCapabilitiesEqual(
    const camera::NumericCapability& left,
    const camera::NumericCapability& right) {
    return std::tie(left.minimum, left.maximum, left.increment,
               left.writableWhileStreaming)
        == std::tie(right.minimum, right.maximum, right.increment,
            right.writableWhileStreaming);
}

template<typename T, typename Equal>
[[nodiscard]] bool unorderedUniqueEqual(
    const std::vector<T>& left,
    const std::vector<T>& right,
    Equal equal) {
    if (left.size() != right.size()) {
        return false;
    }
    return std::all_of(left.begin(), left.end(), [&](const auto& value) {
        return std::count_if(left.begin(), left.end(),
                   [&](const auto& candidate) { return equal(value, candidate); })
                == 1
            && std::count_if(right.begin(), right.end(),
                   [&](const auto& candidate) { return equal(value, candidate); })
                == 1;
    });
}

template<typename T>
[[nodiscard]] bool unorderedUniqueEqual(
    const std::vector<T>& left,
    const std::vector<T>& right) {
    return unorderedUniqueEqual(left, right,
        [](const auto& first, const auto& second) { return first == second; });
}

[[nodiscard]] bool validExposureMode(camera::ExposureMode mode) {
    return mode == camera::ExposureMode::Manual || mode == camera::ExposureMode::Auto;
}

[[nodiscard]] bool validGainMode(camera::GainMode mode) {
    return mode == camera::GainMode::Manual || mode == camera::GainMode::Auto;
}

[[nodiscard]] bool validAcquisitionMode(camera::AcquisitionMode mode) {
    return mode == camera::AcquisitionMode::Continuous
        || mode == camera::AcquisitionMode::Triggered;
}

}  // namespace

core::Result<void> validateStartupPreferences(
    const StartupPreferences& preferences) {
    if (preferences.recordVersion != 1U) {
        return core::Result<void>::failure(preferenceError(
            "startup_record_version_unsupported",
            "Only startup preference record version 1 is supported."));
    }
    if (preferences.cameraId.value.empty() || preferences.identity.manufacturer.empty()
        || preferences.identity.model.empty() || preferences.identity.serial.empty()) {
        return core::Result<void>::failure(preferenceError(
            "startup_identity_invalid",
            "The stable camera ID and manufacturer/model/serial identity must be present."));
    }

    if (preferences.capabilityFingerprintVersion != 1U) {
        return core::Result<void>::failure(preferenceError(
            "startup_capability_fingerprint_version_unsupported",
            "Only capability fingerprint version 1 is supported."));
    }
    const auto capabilitiesResult =
        validateCameraCapabilities(preferences.confirmedCapabilities);
    if (!capabilitiesResult.hasValue()) {
        return core::Result<void>::failure(preferenceError(
            capabilitiesResult.error().code == "camera_capabilities_duplicate"
                ? "startup_capabilities_duplicate"
                : "startup_capabilities_invalid",
            capabilitiesResult.error().diagnosticDetail));
    }
    if (preferences.installationProfile) {
        const auto& reference = *preferences.installationProfile;
        const auto validRotation = reference.orientation.rotation == core::Rotation::Degrees0
            || reference.orientation.rotation == core::Rotation::Degrees90
            || reference.orientation.rotation == core::Rotation::Degrees180
            || reference.orientation.rotation == core::Rotation::Degrees270;
        if (reference.recordVersion != 1U || reference.revision == 0U
            || !validRotation) {
            return core::Result<void>::failure(preferenceError(
                "startup_installation_reference_invalid",
                "The installation profile reference version, revision or orientation is invalid."));
        }
    }

    const auto& capabilities = preferences.confirmedCapabilities;
    if (!validExposureMode(preferences.requested.exposure.mode)
        || !validGainMode(preferences.requested.gain.mode)
        || !validAcquisitionMode(preferences.requested.acquisitionMode)
        || !validExposureMode(preferences.lastApplied.exposure.mode)
        || !validGainMode(preferences.lastApplied.gain.mode)
        || !validAcquisitionMode(preferences.lastApplied.acquisitionMode)) {
        return core::Result<void>::failure(preferenceError(
            "startup_configuration_invalid",
            "Startup camera configuration mode enumerators must be valid."));
    }
    if (!preferences.lastApplied.requestedFps
        || !std::isfinite(*preferences.lastApplied.requestedFps)
        || *preferences.lastApplied.requestedFps <= 0.0) {
        return core::Result<void>::failure(preferenceError(
            "startup_applied_frame_rate_invalid",
            "The confirmed actual frame rate must be positive and finite."));
    }

    const auto requested = camera::validateCameraConfiguration(
        preferences.requested, capabilities);
    const auto lastApplied = camera::validateCameraConfiguration(
        preferences.lastApplied, capabilities);
    if (!requested.hasValue() || !lastApplied.hasValue()) {
        const auto& error = !requested.hasValue() ? requested.error() : lastApplied.error();
        return core::Result<void>::failure(preferenceError(
            "startup_configuration_invalid", error.diagnosticDetail));
    }
    return core::Result<void>::success();
}

bool cameraCapabilitiesEqual(
    const camera::CameraCapabilities& left,
    const camera::CameraCapabilities& right) {
    return unorderedUniqueEqual(left.pixelFormats, right.pixelFormats, pixelFormatsEqual)
        && regionsEqual(left.roi.minimum, right.roi.minimum)
        && regionsEqual(left.roi.maximum, right.roi.maximum)
        && regionsEqual(left.roi.increment, right.roi.increment)
        && numericCapabilitiesEqual(left.frameRate, right.frameRate)
        && numericCapabilitiesEqual(left.exposure, right.exposure)
        && unorderedUniqueEqual(left.exposureModes, right.exposureModes)
        && numericCapabilitiesEqual(left.gain, right.gain)
        && unorderedUniqueEqual(left.gainModes, right.gainModes);
}

bool cameraConfigurationsEqual(
    const camera::CameraConfiguration& left,
    const camera::CameraConfiguration& right) {
    return pixelFormatsEqual(left.pixelFormat, right.pixelFormat)
        && regionsEqual(left.roi, right.roi)
        && left.requestedFps == right.requestedFps
        && left.exposure.mode == right.exposure.mode
        && left.exposure.requestedMicroseconds == right.exposure.requestedMicroseconds
        && left.gain.mode == right.gain.mode
        && left.gain.requestedDb == right.gain.requestedDb
        && left.acquisitionMode == right.acquisitionMode;
}

bool isStartupResumeEligible(
    const StartupPreferences& preferences,
    const camera::CameraId& cameraId,
    const core::CameraIdentity& identity,
    const camera::CameraCapabilities& capabilities) {
    return preferences.confirmed
        && validateStartupPreferences(preferences).hasValue()
        && preferences.cameraId == cameraId
        && cameraIdentityKeysEqual(preferences.identity, identity)
        && cameraCapabilitiesEqual(preferences.confirmedCapabilities, capabilities)
        && camera::validateCameraConfiguration(preferences.requested, capabilities).hasValue();
}

}  // namespace lumora::application
