#pragma once

#include <lumora/camera/ICameraProvider.hpp>
#include <lumora/camera/sim/SimulatedCameraOptions.hpp>
#include <lumora/core/Clock.hpp>

#include <stop_token>

namespace lumora::camera::sim {

// The injected clock is non-owning and must outlive devices created by this
// provider.
class SimulatedCameraProvider final : public ICameraProvider {
public:
    SimulatedCameraProvider(
        SimulatedCameraOptions options,
        core::IClock& clock);

    [[nodiscard]] core::Result<std::vector<CameraDescriptor>> discover(
        std::stop_token stopToken = {}) override;
    [[nodiscard]] core::Result<std::unique_ptr<ICameraDevice>> create(
        const CameraId& id) override;

private:
    SimulatedCameraOptions options_;
    core::IClock* clock_;
};

}  // namespace lumora::camera::sim
