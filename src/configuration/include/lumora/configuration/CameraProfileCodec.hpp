#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Result.hpp>

#include <QJsonObject>

namespace lumora::configuration {

[[nodiscard]] QJsonObject encodeCameraIdentity(
    const core::CameraIdentity& identity);
[[nodiscard]] core::Result<core::CameraIdentity> decodeCameraIdentity(
    const QJsonObject& object);
[[nodiscard]] QJsonObject encodeCameraCapabilities(
    camera::CameraCapabilities capabilities);
[[nodiscard]] core::Result<camera::CameraCapabilities> decodeCameraCapabilities(
    const QJsonObject& object);

}  // namespace lumora::configuration
