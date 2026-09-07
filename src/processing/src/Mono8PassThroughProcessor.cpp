#include <lumora/processing/Mono8PassThroughProcessor.hpp>

#include <lumora/core/Error.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace lumora::processing {
namespace {

[[nodiscard]] core::Error processingError(
    core::ErrorCategory category,
    std::string code,
    std::string detail,
    bool recoverable = false) {
    return {
        category,
        std::move(code),
        "The frame could not be processed.",
        std::move(detail),
        recoverable,
    };
}

[[nodiscard]] bool isSupportedMono8(const core::RawFrame& raw) {
    const auto& format = raw.metadata.acquisitionSettings.sourceFormat;
    return format.canonicalName == "Mono8" &&
           format.canonicalEncoding == 0x01080001U &&
           format.validBits == 8U && format.sampleMaximum == 255U &&
           format.packing == core::SourcePacking::Unpacked &&
           format.alignment == core::BitAlignment::LeastSignificant &&
           format.applicationStorage == core::StorageType::UInt8 &&
           raw.layout.storage() == core::StorageType::UInt8;
}

}  // namespace

Mono8PassThroughProcessor::Mono8PassThroughProcessor(
    core::BufferPool& displayPool) noexcept
    : displayPool_(&displayPool) {}

core::Result<std::shared_ptr<const core::FrameBundle>>
Mono8PassThroughProcessor::process(std::shared_ptr<const core::RawFrame> raw) {
    if (!raw || !isSupportedMono8(*raw)) {
        return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
            processingError(
                core::ErrorCategory::Processing,
                "processing_format_not_available",
                "The M5 processor accepts only the complete full-range Mono8 descriptor."));
    }
    if (!std::isfinite(raw->metadata.acquisitionSettings.actualFps) ||
        raw->metadata.acquisitionSettings.actualFps <= 0.0) {
        return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
            processingError(
                core::ErrorCategory::Processing,
                "processing_metadata_invalid",
                "Actual acquisition FPS must be positive and finite before presentation."));
    }

    auto displayLease = displayPool_->tryAcquire();
    if (!displayLease) {
        return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
            processingError(
                core::ErrorCategory::ResourceExhaustion,
                std::string(displayBufferPoolExhaustedCode),
                "No pooled Gray8 display buffer is available.",
                true));
    }

    const auto displayLayout = core::ImageLayout::create(
        raw->layout.width(), raw->layout.height(), raw->layout.rowBytes(),
        core::StorageType::UInt8,
        raw->layout.rowBytes() * static_cast<std::size_t>(raw->layout.height()));
    if (!displayLayout.hasValue()) {
        return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
            displayLayout.error());
    }

    const auto source = raw->pixels.bytes();
    auto destination = displayLease->bytes();
    if (destination.size() < displayLayout.value().payloadBytes()) {
        return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
            processingError(
                core::ErrorCategory::ResourceExhaustion,
                "display_buffer_too_small",
                "The pooled display buffer is smaller than the active Gray8 image."));
    }
    for (std::size_t row = 0U; row < raw->layout.height(); ++row) {
        const auto sourceOffset = row * raw->layout.strideBytes();
        const auto destinationOffset = row * displayLayout.value().strideBytes();
        std::ranges::copy(
            source.subspan(sourceOffset, raw->layout.rowBytes()),
            destination.subspan(destinationOffset, displayLayout.value().rowBytes())
                .begin());
    }

    auto display = core::DisplayFrame::create(
        raw->frameId, displayLayout.value(), std::move(*displayLease).seal(),
        core::DisplayStorage::Gray8, core::DisplayMapping{0U, 255U, 255U, 1U},
        core::Orientation{false, false, core::Rotation::Degrees0});
    if (!display.hasValue()) {
        return core::Result<std::shared_ptr<const core::FrameBundle>>::failure(
            display.error());
    }
    return core::FrameBundle::create(
        std::move(raw), std::move(display).value(), nullptr, nullptr);
}

}  // namespace lumora::processing
