#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Result.hpp>

#include <QJsonObject>

#include <cstdint>

namespace lumora::configuration {

[[nodiscard]] QJsonObject encodeCameraIdentity(
    const core::CameraIdentity& identity);
[[nodiscard]] core::Result<core::CameraIdentity> decodeCameraIdentity(
    const QJsonObject& object);
[[nodiscard]] core::Result<QJsonObject> encodeCameraCapabilities(
    camera::CameraCapabilities capabilities,
    std::uint32_t fingerprintVersion = 2U);
[[nodiscard]] core::Result<camera::CameraCapabilities> decodeCameraCapabilities(
    const QJsonObject& object,
    std::uint32_t fingerprintVersion = 2U);

}  // namespace lumora::configuration
