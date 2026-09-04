#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/camera/ICameraDevice.hpp>
#include <lumora/core/Result.hpp>

#include <memory>
#include <stop_token>
#include <vector>

namespace lumora::camera {

class ICameraProvider {
public:
    virtual ~ICameraProvider() = default;

    [[nodiscard]] virtual core::Result<std::vector<CameraDescriptor>> discover(
        std::stop_token stopToken) = 0;
    [[nodiscard]] virtual core::Result<std::unique_ptr<ICameraDevice>> create(
        const CameraId& id) = 0;
};

}  // namespace lumora::camera
