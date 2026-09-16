#pragma once

#include <lumora/camera/ICameraProvider.hpp>

#include <map>

namespace lumora::camera {

// Providers are borrowed and must outlive this composite. Calls, including
// discovery and creation, are serialized by the acquisition worker.
class CompositeCameraProvider final : public ICameraProvider {
public:
    explicit CompositeCameraProvider(std::vector<ICameraProvider*> providers);

    [[nodiscard]] core::Result<std::vector<CameraDescriptor>> discover(
        std::stop_token stopToken) override;
    [[nodiscard]] core::Result<std::unique_ptr<ICameraDevice>> create(
        const CameraId& id) override;

private:
    std::vector<ICameraProvider*> providers_;
    std::map<CameraId, ICameraProvider*> routes_;
};

}  // namespace lumora::camera
