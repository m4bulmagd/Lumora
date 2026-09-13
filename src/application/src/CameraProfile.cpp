#include <lumora/application/CameraProfile.hpp>

#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <string>
#include <tuple>
#include <utility>

namespace lumora::application {
namespace {

[[nodiscard]] core::Error capabilityError(std::string code, std::string detail) {
    return {core::ErrorCategory::Configuration, std::move(code),
        "Saved camera capabilities are invalid.", std::move(detail), true};
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
    const auto validated = camera::validateCameraCapabilities(capabilities);
    if (validated.hasValue()) return core::Result<void>::success();

    const auto& source = validated.error();
    const auto incomplete = source.code == "camera_capabilities_incomplete"
        || source.code == "capability_pixel_format_missing";
    const auto duplicate = source.code == "camera_capabilities_duplicate";
    return core::Result<void>::failure(capabilityError(
        incomplete ? "camera_capabilities_incomplete"
                   : duplicate ? "camera_capabilities_duplicate"
                               : "camera_capabilities_invalid",
        source.diagnosticDetail));
}

}  // namespace lumora::application
