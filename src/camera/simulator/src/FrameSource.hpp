#pragma once

#include <lumora/camera/sim/SimulatedCameraOptions.hpp>

#include <memory>
#include <span>
#include <stop_token>

namespace lumora::camera::sim {

// Private source seam: fill prepares a candidate; only publication commits it.
class FrameSource {
public:
    virtual ~FrameSource() = default;
    [[nodiscard]] virtual core::Result<bool> fill(
        std::span<std::byte> destination, const CameraConfiguration& actual,
        std::size_t strideBytes, std::uint64_t frameId, std::stop_token stopToken) = 0;
    virtual void commit() noexcept = 0;
    [[nodiscard]] virtual const char* model() const noexcept = 0;
};

// Replay derives format/sensor capabilities; both sources use the shared
// configuration validator after factory construction.
[[nodiscard]] core::Result<std::unique_ptr<FrameSource>> makeFrameSource(
    SimulatedCameraOptions& options);

}  // namespace lumora::camera::sim
