#pragma once

#include <lumora/camera/sim/SimulatedCameraOptions.hpp>
#include <lumora/core/PixelFormat.hpp>
#include <lumora/core/Result.hpp>

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>

namespace lumora::camera::sim {

class IPatternGenerator {
public:
    virtual ~IPatternGenerator() = default;

    // Allocation failures in typed error construction may propagate to the
    // camera/worker boundary. The optional deadline uses the real steady clock.
    [[nodiscard]] virtual core::Result<void> fill(
        std::span<std::byte> destination,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t strideBytes,
        const core::SourcePixelFormat& format,
        std::uint64_t frameId,
        std::stop_token stopToken = {},
        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::time_point::max()) const = 0;
};

[[nodiscard]] std::unique_ptr<IPatternGenerator> makePatternGenerator(
    SimulationPattern pattern,
    std::uint64_t seed);

}  // namespace lumora::camera::sim
