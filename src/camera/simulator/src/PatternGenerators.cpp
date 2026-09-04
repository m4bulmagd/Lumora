#include <lumora/camera/sim/IPatternGenerator.hpp>

#include <lumora/core/CheckedMath.hpp>
#include <lumora/core/Error.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>

namespace lumora::camera::sim {
namespace {

[[nodiscard]] core::Error invalidFrame(std::string detail) {
    return {
        .category = core::ErrorCategory::InvalidFrame,
        .code = "invalid_frame",
        .operatorSummary = "The simulator could not generate a valid frame.",
        .diagnosticDetail = std::move(detail),
        .recoverable = true,
    };
}

[[nodiscard]] core::Error cancelled() {
    return {
        .category = core::ErrorCategory::Cancelled,
        .code = "cancelled",
        .operatorSummary = "Simulated frame generation was cancelled.",
        .diagnosticDetail = "The stop token was requested before generation completed.",
        .recoverable = true,
    };
}

[[nodiscard]] std::size_t bytesPerSample(core::StorageType storage) noexcept {
    switch (storage) {
    case core::StorageType::UInt8:
        return sizeof(std::uint8_t);
    case core::StorageType::UInt16:
        return sizeof(std::uint16_t);
    }
    return 0U;
}

[[nodiscard]] std::uint16_t scaled(
    std::uint64_t numerator,
    std::uint64_t denominator,
    std::uint16_t maximum) noexcept {
    if (denominator == 0U) {
        return 0U;
    }
    return static_cast<std::uint16_t>(
        (numerator * static_cast<std::uint64_t>(maximum)) / denominator);
}

[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

class GeneratedPattern final : public IPatternGenerator {
public:
    GeneratedPattern(SimulationPattern pattern, std::uint64_t seed) noexcept
        : pattern_(pattern), seed_(seed) {}

    core::Result<void> fill(
        std::span<std::byte> destination,
        std::uint32_t width,
        std::uint32_t height,
        std::size_t strideBytes,
        const core::SourcePixelFormat& format,
        std::uint64_t frameId,
        std::stop_token stopToken) const noexcept override {
        const auto sampleBytes = bytesPerSample(format.applicationStorage);
        if (sampleBytes == 0U || width == 0U || height == 0U) {
            return core::Result<void>::failure(invalidFrame(
                "The destination buffer is smaller than the generated image layout."));
        }
        const auto rowBytes = core::checkedMultiply(
            static_cast<std::size_t>(width), sampleBytes);
        const auto requiredBytes = core::checkedMultiply(
            strideBytes, static_cast<std::size_t>(height));
        if (!rowBytes.hasValue() || !requiredBytes.hasValue()) {
            return core::Result<void>::failure(invalidFrame(
                "Computing the generated frame size overflowed size_t."));
        }
        if (strideBytes < rowBytes.value() ||
            destination.size() < requiredBytes.value()) {
            return core::Result<void>::failure(invalidFrame(
                "The destination buffer is smaller than the generated image layout."));
        }

        constexpr std::size_t cancellationChunk = 4096U;
        for (std::size_t offset = 0U; offset < destination.size();
             offset += std::min(cancellationChunk, destination.size() - offset)) {
            if (stopToken.stop_requested()) {
                return core::Result<void>::failure(cancelled());
            }
            const auto chunk = std::min(
                cancellationChunk, destination.size() - offset);
            std::fill_n(
                destination.data() + offset, chunk, std::byte{0U});
        }
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                if (x % 1024U == 0U && stopToken.stop_requested()) {
                    return core::Result<void>::failure(cancelled());
                }
                const auto sample = valueAt(x, y, width, height, format.sampleMaximum, frameId);
                const auto offset = static_cast<std::size_t>(y) * strideBytes +
                                    static_cast<std::size_t>(x) * sampleBytes;
                if (format.applicationStorage == core::StorageType::UInt8) {
                    destination[offset] = static_cast<std::byte>(sample);
                } else {
                    std::memcpy(destination.data() + offset, &sample, sizeof(sample));
                }
            }
        }
        return core::Result<void>::success();
    }

private:
    [[nodiscard]] std::uint16_t valueAt(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t width,
        std::uint32_t height,
        std::uint16_t maximum,
        std::uint64_t frameId) const noexcept {
        switch (pattern_) {
        case SimulationPattern::Ramp:
            return scaled(x, width > 1U ? width - 1U : 1U, maximum);
        case SimulationPattern::Gradient:
            return scaled(
                static_cast<std::uint64_t>(x) + y,
                static_cast<std::uint64_t>(width > 1U ? width - 1U : 0U) +
                    (height > 1U ? height - 1U : 0U),
                maximum);
        case SimulationPattern::Checkerboard: {
            const auto tileSize = std::min(
                8U, std::max(1U, std::min(width, height) / 4U));
            return (((x / tileSize) + (y / tileSize)) % 2U == 0U)
                       ? 0U
                       : maximum;
        }
        case SimulationPattern::ImpulseNoise: {
            const auto pixelIndex = static_cast<std::uint64_t>(y) * width + x;
            const auto random = mix(seed_ ^ mix(frameId) ^ mix(pixelIndex));
            return random % 20U == 0U ? maximum : 0U;
        }
        case SimulationPattern::MovingBar: {
            const auto barWidth = std::max(1U, width / 8U);
            const auto barStart = static_cast<std::uint32_t>((frameId - 1U) % width);
            const auto distance = (x + width - barStart) % width;
            return distance < barWidth ? maximum : 0U;
        }
        }
        return 0U;
    }

    SimulationPattern pattern_;
    std::uint64_t seed_;
};

}  // namespace

std::unique_ptr<IPatternGenerator> makePatternGenerator(
    SimulationPattern pattern,
    std::uint64_t seed) {
    return std::make_unique<GeneratedPattern>(pattern, seed);
}

}  // namespace lumora::camera::sim
