#include "FrameSource.hpp"

#include <lumora/camera/sim/IPatternGenerator.hpp>
#include <lumora/core/CheckedMath.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace lumora::camera::sim {
namespace {

class GeneratedSource final : public FrameSource {
public:
    explicit GeneratedSource(std::unique_ptr<IPatternGenerator> generator)
        : generator_(std::move(generator)) {}

    core::Result<bool> fill(
        std::span<std::byte> destination, const CameraConfiguration& actual,
        std::size_t strideBytes, std::uint64_t frameId, std::stop_token stopToken) override {
        auto filled = generator_->fill(destination, actual.roi.width, actual.roi.height,
            strideBytes, actual.pixelFormat, frameId, stopToken);
        return filled.hasValue() ? core::Result<bool>::success(true)
                                 : core::Result<bool>::failure(filled.error());
    }
    void commit() noexcept override {}
    const char* model() const noexcept override { return "Generated Camera"; }

private:
    std::unique_ptr<IPatternGenerator> generator_;
};

class ReplaySource final : public FrameSource {
public:
    explicit ReplaySource(SequenceSource sequence) : sequence_(std::move(sequence)) {}

    core::Result<bool> fill(
        std::span<std::byte> destination, const CameraConfiguration& actual,
        std::size_t strideBytes, std::uint64_t frameId, std::stop_token stopToken) override {
        static_cast<void>(frameId);
        const auto next = sequence_.peek();
        if (!next.hasValue()) {
            return core::Result<bool>::failure(next.error());
        }
        if (!next.value().has_value()) {
            return core::Result<bool>::success(false);
        }
        const auto required = core::checkedMultiply(strideBytes, actual.roi.height);
        if (!required.hasValue() || destination.size() < required.value()) {
            return core::Result<bool>::failure({core::ErrorCategory::InvalidFrame,
                "invalid_frame", "The replay destination is too small.",
                "The caller's pool block must hold the applied ROI.", true});
        }
        const auto& source = **next.value();
        const std::size_t sampleBytes = source.format.applicationStorage == core::StorageType::UInt8 ? 1U : 2U;
        const auto sourceStride = static_cast<std::size_t>(source.width) * sampleBytes;
        // Bounded chunks keep cancellation responsive even for very wide rows.
        constexpr std::size_t chunkBytes = 64U * 1024U;
        for (std::size_t offset = required.value(); offset < destination.size();) {
            if (stopToken.stop_requested()) {
                return core::Result<bool>::failure({core::ErrorCategory::Cancelled,
                    "cancelled", "Frame retrieval was cancelled.", "Replay padding clear was cancelled before publication.", true});
            }
            const auto count = std::min(chunkBytes, destination.size() - offset);
            std::fill_n(destination.data() + offset, count, std::byte{0U});
            offset += count;
        }
        for (std::uint32_t row = 0U; row < actual.roi.height; ++row) {
            const auto sourceOffset = static_cast<std::size_t>(row + actual.roi.y) * sourceStride +
                                      static_cast<std::size_t>(actual.roi.x) * sampleBytes;
            const auto destinationOffset = static_cast<std::size_t>(row) * strideBytes;
            for (std::size_t offset = 0U; offset < strideBytes;) {
                if (stopToken.stop_requested()) {
                    return core::Result<bool>::failure({core::ErrorCategory::Cancelled,
                        "cancelled", "Frame retrieval was cancelled.", "Replay copy was cancelled before publication.", true});
                }
                const auto count = std::min(chunkBytes, strideBytes - offset);
                std::memcpy(destination.data() + destinationOffset + offset,
                            source.pixels.data() + sourceOffset + offset, count);
                offset += count;
            }
        }
        return core::Result<bool>::success(true);
    }
    void commit() noexcept override { sequence_.commit(); }
    const char* model() const noexcept override { return "PGM Replay Camera"; }

private:
    SequenceSource sequence_;
};

}  // namespace

core::Result<std::unique_ptr<FrameSource>> makeFrameSource(SimulatedCameraOptions& options) {
    using SourceResult = core::Result<std::unique_ptr<FrameSource>>;
    if (!options.sequence) {
        return SourceResult::success(std::make_unique<GeneratedSource>(
            makePatternGenerator(options.pattern, options.seed)));
    }
    auto sequence = SequenceSource::openDirectory(options.sequence->directory, options.sequence->end);
    if (!sequence.hasValue()) {
        return SourceResult::failure(sequence.error());
    }
    const auto& descriptor = sequence.value().descriptor();
    options.capabilities.pixelFormats = {descriptor.format};
    options.capabilities.roi = {
        .minimum = {.x = 0U, .y = 0U, .width = 1U, .height = 1U},
        .maximum = {.x = descriptor.width - 1U, .y = descriptor.height - 1U,
                    .width = descriptor.width, .height = descriptor.height},
        .increment = {.x = 1U, .y = 1U, .width = 1U, .height = 1U}};
    return SourceResult::success(std::make_unique<ReplaySource>(std::move(sequence).value()));
}

}  // namespace lumora::camera::sim
