#pragma once

#include <lumora/camera/sim/SimulatedCameraOptions.hpp>
#include <lumora/core/PixelFormat.hpp>
#include <lumora/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace lumora::camera::sim {

class IPatternGenerator {
public:
    virtual ~IPatternGenerator() = default;

    [[nodiscard]] virtual core::Result<void> fill(
        std::span<std::byte> destination,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t strideBytes,
        const core::SourcePixelFormat& format,
        std::uint64_t frameId) const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IPatternGenerator> makePatternGenerator(
    SimulationPattern pattern,
    std::uint64_t seed);

}  // namespace lumora::camera::sim
