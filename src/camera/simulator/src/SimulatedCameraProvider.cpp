#include <lumora/camera/sim/SimulatedCameraProvider.hpp>

#include <lumora/core/Error.hpp>

#include <memory>
#include <new>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace lumora::camera::sim {

[[nodiscard]] core::Result<std::unique_ptr<ICameraDevice>>
makeSimulatedCameraDevice(SimulatedCameraOptions options, core::IClock& clock);

namespace {

[[nodiscard]] core::Error cancelledError() {
    return {
        .category = core::ErrorCategory::Cancelled,
        .code = "cancelled",
        .operatorSummary = "Camera discovery was cancelled.",
        .diagnosticDetail = "The simulator discovery stop token was requested.",
        .recoverable = true,
    };
}

[[nodiscard]] core::Error cameraNotFoundError(const CameraId& id) {
    return {
        .category = core::ErrorCategory::CameraDiscovery,
        .code = "camera_not_found",
        .operatorSummary = "The requested simulated camera was not found.",
        .diagnosticDetail = "No configured simulator has camera ID '" + id.value + "'.",
        .recoverable = true,
    };
}

}  // namespace

SimulatedCameraProvider::SimulatedCameraProvider(
    SimulatedCameraOptions options,
    core::IClock& clock)
    : options_(std::move(options)), clock_(&clock) {}

core::Result<std::vector<CameraDescriptor>> SimulatedCameraProvider::discover(
    std::stop_token stopToken) {
    if (stopToken.stop_requested()) {
        return core::Result<std::vector<CameraDescriptor>>::failure(cancelledError());
    }

    return core::Result<std::vector<CameraDescriptor>>::success({CameraDescriptor{
        .id = options_.id,
        .identity = {
            .manufacturer = "Lumora",
            .model = options_.sequence ? "PGM Replay Camera" : "Generated Camera",
            .serial = options_.id.value,
            .transport = "Simulator",
            .firmware = std::nullopt,
        },
        .available = true,
    }});
}

core::Result<std::unique_ptr<ICameraDevice>> SimulatedCameraProvider::create(
    const CameraId& id) {
    if (id != options_.id) {
        return core::Result<std::unique_ptr<ICameraDevice>>::failure(
            cameraNotFoundError(id));
    }
    return makeSimulatedCameraDevice(options_, *clock_);
}

}  // namespace lumora::camera::sim
