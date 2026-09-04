#include <lumora/core/Frame.hpp>

#include <lumora/core/Error.hpp>
#include <lumora/core/PixelFormat.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace lumora::core {
namespace {

[[nodiscard]] Error invalidFrameError(std::string code, std::string detail) {
    return Error{
        ErrorCategory::InvalidFrame,
        std::move(code),
        "The frame description is inconsistent.",
        std::move(detail),
        false,
    };
}

[[nodiscard]] Error frameAllocationError() {
    return Error{
        ErrorCategory::ResourceExhaustion,
        "frame_allocation_failed",
        "Frame metadata could not be allocated.",
        "Allocating immutable frame ownership failed.",
        true,
    };
}

[[nodiscard]] Result<void> validateBuffer(
    const ImageLayout& layout,
    const SharedBuffer& pixels) {
    if (!pixels) {
        return Result<void>::failure(invalidFrameError(
            "frame_buffer_missing", "A frame requires a sealed pixel buffer."));
    }
    if (pixels.size() < layout.payloadBytes()) {
        return Result<void>::failure(invalidFrameError(
            "frame_buffer_too_small",
            "The sealed buffer is shorter than the validated payload size."));
    }
    return Result<void>::success();
}

[[nodiscard]] bool isSupportedRotation(Rotation rotation) noexcept {
    switch (rotation) {
    case Rotation::Degrees0:
    case Rotation::Degrees90:
    case Rotation::Degrees180:
    case Rotation::Degrees270:
        return true;
    }
    return false;
}

[[nodiscard]] bool swapsDimensions(Rotation rotation) noexcept {
    return rotation == Rotation::Degrees90 || rotation == Rotation::Degrees270;
}

[[nodiscard]] StorageType storageType(DisplayStorage storage) noexcept {
    switch (storage) {
    case DisplayStorage::Gray8:
        return StorageType::UInt8;
    case DisplayStorage::Gray16:
        return StorageType::UInt16;
    }
    return StorageType::UInt8;
}

[[nodiscard]] std::uint16_t maximumOutput(DisplayStorage storage) noexcept {
    switch (storage) {
    case DisplayStorage::Gray8:
        return 255U;
    case DisplayStorage::Gray16:
        return 65'535U;
    }
    return 0U;
}

[[nodiscard]] Result<void> validatePresentationDimensions(
    const ImageLayout& rawLayout,
    const DisplayFrame& display) {
    if (!isSupportedRotation(display.presentationOrientation.rotation)) {
        return Result<void>::failure(invalidFrameError(
            "unsupported_presentation_rotation",
            "Presentation rotation must be 0, 90, 180, or 270 degrees."));
    }

    const bool swapped = swapsDimensions(display.presentationOrientation.rotation);
    const auto expectedWidth = swapped ? rawLayout.height() : rawLayout.width();
    const auto expectedHeight = swapped ? rawLayout.width() : rawLayout.height();
    if (display.layout.width() != expectedWidth ||
        display.layout.height() != expectedHeight) {
        return Result<void>::failure(invalidFrameError(
            "presentation_dimensions_mismatch",
            "Display dimensions do not match the source after the declared orientation."));
    }
    return Result<void>::success();
}

}  // namespace

RawFrame::RawFrame(
    std::uint64_t frameIdValue,
    ImageLayout layoutValue,
    SharedBuffer pixelsValue,
    FrameMetadata metadataValue)
    : frameId(frameIdValue),
      layout(std::move(layoutValue)),
      pixels(std::move(pixelsValue)),
      metadata(std::move(metadataValue)) {}

Result<std::shared_ptr<const RawFrame>> RawFrame::create(
    std::uint64_t frameId,
    ImageLayout layout,
    SharedBuffer pixels,
    FrameMetadata metadata) {
    const auto bufferValidation = validateBuffer(layout, pixels);
    if (!bufferValidation.hasValue()) {
        return Result<std::shared_ptr<const RawFrame>>::failure(
            bufferValidation.error());
    }

    const auto formatValidation =
        validateSourcePixelFormat(metadata.acquisitionSettings.sourceFormat);
    if (!formatValidation.hasValue()) {
        return Result<std::shared_ptr<const RawFrame>>::failure(
            formatValidation.error());
    }
    if (metadata.acquisitionSettings.sourceFormat.applicationStorage !=
        layout.storage()) {
        return Result<std::shared_ptr<const RawFrame>>::failure(invalidFrameError(
            "frame_storage_mismatch",
            "The layout storage differs from the applied source-format storage."));
    }
    if (metadata.acquisitionSettings.roi.width != layout.width() ||
        metadata.acquisitionSettings.roi.height != layout.height()) {
        return Result<std::shared_ptr<const RawFrame>>::failure(invalidFrameError(
            "frame_roi_mismatch",
            "The validated frame dimensions differ from the applied camera ROI."));
    }

    try {
        return Result<std::shared_ptr<const RawFrame>>::success(
            std::shared_ptr<const RawFrame>(new RawFrame(
                frameId,
                std::move(layout),
                std::move(pixels),
                std::move(metadata))));
    } catch (const std::bad_alloc&) {
        return Result<std::shared_ptr<const RawFrame>>::failure(
            frameAllocationError());
    }
}

ProcessedFrame::ProcessedFrame(
    std::uint64_t frameIdValue,
    ImageLayout layoutValue,
    SharedBuffer pixelsValue,
    PipelineVersion pipelineVersionValue,
    ProcessingTimings timingsValue)
    : frameId(frameIdValue),
      layout(std::move(layoutValue)),
      pixels(std::move(pixelsValue)),
      pipelineVersion(pipelineVersionValue),
      timings(std::move(timingsValue)) {}

Result<std::shared_ptr<const ProcessedFrame>> ProcessedFrame::create(
    std::uint64_t frameId,
    ImageLayout layout,
    SharedBuffer pixels,
    PipelineVersion pipelineVersion,
    ProcessingTimings timings) {
    const auto bufferValidation = validateBuffer(layout, pixels);
    if (!bufferValidation.hasValue()) {
        return Result<std::shared_ptr<const ProcessedFrame>>::failure(
            bufferValidation.error());
    }
    if (layout.storage() != StorageType::UInt16) {
        return Result<std::shared_ptr<const ProcessedFrame>>::failure(
            invalidFrameError(
                "processed_storage_not_u16",
                "Enhanced native-orientation pixels must use unsigned 16-bit storage."));
    }
    if (pipelineVersion.schemaVersion == 0U || pipelineVersion.orderVersion == 0U) {
        return Result<std::shared_ptr<const ProcessedFrame>>::failure(
            invalidFrameError(
                "invalid_pipeline_version",
                "Pipeline schema and order versions must be nonzero."));
    }
    if (timings.total < std::chrono::nanoseconds::zero()) {
        return Result<std::shared_ptr<const ProcessedFrame>>::failure(
            invalidFrameError(
                "negative_processing_timing",
                "Total processing duration cannot be negative."));
    }
    for (const auto& timing : timings.stages) {
        if (timing.stageId.empty()) {
            return Result<std::shared_ptr<const ProcessedFrame>>::failure(
                invalidFrameError(
                    "processing_stage_id_empty",
                    "Every recorded processing timing requires a stable stage ID."));
        }
        if (timing.elapsed < std::chrono::nanoseconds::zero()) {
            return Result<std::shared_ptr<const ProcessedFrame>>::failure(
                invalidFrameError(
                    "negative_processing_timing",
                    "Stage processing duration cannot be negative."));
        }
    }

    try {
        return Result<std::shared_ptr<const ProcessedFrame>>::success(
            std::shared_ptr<const ProcessedFrame>(new ProcessedFrame(
                frameId,
                std::move(layout),
                std::move(pixels),
                pipelineVersion,
                std::move(timings))));
    } catch (const std::bad_alloc&) {
        return Result<std::shared_ptr<const ProcessedFrame>>::failure(
            frameAllocationError());
    }
}

DisplayFrame::DisplayFrame(
    std::uint64_t frameIdValue,
    ImageLayout layoutValue,
    SharedBuffer pixelsValue,
    DisplayStorage storageValue,
    DisplayMapping mappingValue,
    Orientation presentationOrientationValue)
    : frameId(frameIdValue),
      layout(std::move(layoutValue)),
      pixels(std::move(pixelsValue)),
      storage(storageValue),
      mapping(mappingValue),
      presentationOrientation(presentationOrientationValue) {}

Result<std::shared_ptr<const DisplayFrame>> DisplayFrame::create(
    std::uint64_t frameId,
    ImageLayout layout,
    SharedBuffer pixels,
    DisplayStorage storage,
    DisplayMapping mapping,
    Orientation presentationOrientation) {
    const auto bufferValidation = validateBuffer(layout, pixels);
    if (!bufferValidation.hasValue()) {
        return Result<std::shared_ptr<const DisplayFrame>>::failure(
            bufferValidation.error());
    }
    if (layout.storage() != storageType(storage)) {
        return Result<std::shared_ptr<const DisplayFrame>>::failure(
            invalidFrameError(
                "display_storage_mismatch",
                "The image layout does not match the declared display storage."));
    }
    if (mapping.inputMinimum >= mapping.inputMaximum ||
        mapping.outputMaximum == 0U ||
        mapping.outputMaximum > maximumOutput(storage)) {
        return Result<std::shared_ptr<const DisplayFrame>>::failure(
            invalidFrameError(
                "invalid_display_mapping",
                "Display mapping bounds are invalid for the declared storage."));
    }
    if (!isSupportedRotation(presentationOrientation.rotation)) {
        return Result<std::shared_ptr<const DisplayFrame>>::failure(
            invalidFrameError(
                "unsupported_presentation_rotation",
                "Presentation rotation must be 0, 90, 180, or 270 degrees."));
    }

    try {
        return Result<std::shared_ptr<const DisplayFrame>>::success(
            std::shared_ptr<const DisplayFrame>(new DisplayFrame(
                frameId,
                std::move(layout),
                std::move(pixels),
                storage,
                mapping,
                presentationOrientation)));
    } catch (const std::bad_alloc&) {
        return Result<std::shared_ptr<const DisplayFrame>>::failure(
            frameAllocationError());
    }
}

FrameBundle::FrameBundle(
    std::shared_ptr<const RawFrame> rawValue,
    std::shared_ptr<const DisplayFrame> originalDisplayValue,
    std::shared_ptr<const ProcessedFrame> processedValue,
    std::shared_ptr<const DisplayFrame> enhancedDisplayValue)
    : raw(std::move(rawValue)),
      originalDisplay(std::move(originalDisplayValue)),
      processed(std::move(processedValue)),
      enhancedDisplay(std::move(enhancedDisplayValue)) {}

Result<std::shared_ptr<const FrameBundle>> FrameBundle::create(
    std::shared_ptr<const RawFrame> raw,
    std::shared_ptr<const DisplayFrame> originalDisplay,
    std::shared_ptr<const ProcessedFrame> processed,
    std::shared_ptr<const DisplayFrame> enhancedDisplay) {
    if (!raw || !originalDisplay) {
        return Result<std::shared_ptr<const FrameBundle>>::failure(
            invalidFrameError(
                "bundle_required_frame_missing",
                "A bundle requires raw and original-display frames."));
    }
    if (static_cast<bool>(processed) != static_cast<bool>(enhancedDisplay)) {
        return Result<std::shared_ptr<const FrameBundle>>::failure(
            invalidFrameError(
                "incomplete_enhanced_pair",
                "Processed and enhanced-display frames must be supplied together."));
    }
    if (originalDisplay->frameId != raw->frameId ||
        (processed && processed->frameId != raw->frameId) ||
        (enhancedDisplay && enhancedDisplay->frameId != raw->frameId)) {
        return Result<std::shared_ptr<const FrameBundle>>::failure(
            invalidFrameError(
                "frame_id_mismatch",
                "Every frame in a bundle must originate from the same acquisition."));
    }

    const auto originalDimensions =
        validatePresentationDimensions(raw->layout, *originalDisplay);
    if (!originalDimensions.hasValue()) {
        return Result<std::shared_ptr<const FrameBundle>>::failure(
            originalDimensions.error());
    }
    if (processed &&
        (processed->layout.width() != raw->layout.width() ||
         processed->layout.height() != raw->layout.height())) {
        return Result<std::shared_ptr<const FrameBundle>>::failure(
            invalidFrameError(
                "processed_dimensions_mismatch",
                "Processed pixels must remain in native sensor dimensions."));
    }
    if (enhancedDisplay) {
        const auto enhancedDimensions =
            validatePresentationDimensions(raw->layout, *enhancedDisplay);
        if (!enhancedDimensions.hasValue()) {
            return Result<std::shared_ptr<const FrameBundle>>::failure(
                enhancedDimensions.error());
        }
        if (enhancedDisplay->presentationOrientation !=
            originalDisplay->presentationOrientation) {
            return Result<std::shared_ptr<const FrameBundle>>::failure(
                invalidFrameError(
                    "presentation_orientation_mismatch",
                    "Original and Enhanced displays must share one installation orientation."));
        }
        if (enhancedDisplay->mapping != originalDisplay->mapping ||
            enhancedDisplay->mapping.configurationRevision !=
                processed->pipelineVersion.configurationRevision) {
            return Result<std::shared_ptr<const FrameBundle>>::failure(
                invalidFrameError(
                    "display_mapping_mismatch",
                    "Original and Enhanced displays must use the processed pipeline revision and mapping."));
        }
    }

    try {
        return Result<std::shared_ptr<const FrameBundle>>::success(
            std::shared_ptr<const FrameBundle>(new FrameBundle(
                std::move(raw),
                std::move(originalDisplay),
                std::move(processed),
                std::move(enhancedDisplay))));
    } catch (const std::bad_alloc&) {
        return Result<std::shared_ptr<const FrameBundle>>::failure(
            frameAllocationError());
    }
}

}  // namespace lumora::core
