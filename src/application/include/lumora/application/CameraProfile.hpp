#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/Result.hpp>

#include <cstdint>

namespace lumora::application {

struct InstallationProfileReference final {
    std::uint32_t recordVersion{1};
    std::uint64_t revision;
    core::Orientation orientation;

    [[nodiscard]] bool operator==(
        const InstallationProfileReference&) const noexcept = default;
};

[[nodiscard]] bool cameraIdentityKeysEqual(
    const core::CameraIdentity& left,
    const core::CameraIdentity& right);
[[nodiscard]] core::Result<void> validateCameraCapabilities(
    const camera::CameraCapabilities& capabilities);

}  // namespace lumora::application
