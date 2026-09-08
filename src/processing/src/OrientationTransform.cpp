#include <lumora/processing/OrientationTransform.hpp>

#include <lumora/core/CheckedMath.hpp>
#include <lumora/core/Error.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace lumora::processing {
namespace {

[[nodiscard]] core::Error orientationError(std::string code, std::string detail) {
    return {
        core::ErrorCategory::Processing,
        std::move(code),
        "Image orientation failed.",
        std::move(detail),
        false,
    };
}

struct StorageTraits final {
    core::StorageType storageType;
    std::size_t bytesPerPixel;
};

[[nodiscard]] bool storageTraits(
    core::DisplayStorage storage,
    StorageTraits& traits) noexcept {
    switch (storage) {
    case core::DisplayStorage::Gray8:
        traits = {core::StorageType::UInt8, 1U};
        return true;
    case core::DisplayStorage::Gray16:
        traits = {core::StorageType::UInt16, 2U};
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isSupportedRotation(core::Rotation rotation) noexcept {
    switch (rotation) {
    case core::Rotation::Degrees0:
    case core::Rotation::Degrees90:
    case core::Rotation::Degrees180:
    case core::Rotation::Degrees270:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool swapsAxes(core::Rotation rotation) noexcept {
    return rotation == core::Rotation::Degrees90
        || rotation == core::Rotation::Degrees270;
}

[[nodiscard]] bool byteRangesOverlap(
    std::span<const std::byte> source,
    std::span<std::byte> destination) noexcept {
    const auto sourceStart = reinterpret_cast<std::uintptr_t>(source.data());
    const auto destinationStart = reinterpret_cast<std::uintptr_t>(destination.data());
    return sourceStart <= destinationStart
        ? destinationStart - sourceStart < source.size()
        : sourceStart - destinationStart < destination.size();
}

struct SourceCoordinate final {
    std::uint32_t x;
    std::uint32_t y;
};

[[nodiscard]] SourceCoordinate sourceCoordinate(
    std::uint32_t destinationX,
    std::uint32_t destinationY,
    const core::ImageLayout& sourceLayout,
    core::Orientation orientation) noexcept {
    std::uint32_t flippedX = 0U;
    std::uint32_t flippedY = 0U;
    switch (orientation.rotation) {
    case core::Rotation::Degrees0:
        flippedX = destinationX;
        flippedY = destinationY;
        break;
    case core::Rotation::Degrees90:
        flippedX = destinationY;
        flippedY = sourceLayout.height() - 1U - destinationX;
        break;
    case core::Rotation::Degrees180:
        flippedX = sourceLayout.width() - 1U - destinationX;
        flippedY = sourceLayout.height() - 1U - destinationY;
        break;
    case core::Rotation::Degrees270:
        flippedX = sourceLayout.width() - 1U - destinationY;
        flippedY = destinationX;
        break;
    }

    return {
        orientation.flipHorizontal
            ? sourceLayout.width() - 1U - flippedX
            : flippedX,
        orientation.flipVertical
            ? sourceLayout.height() - 1U - flippedY
            : flippedY,
    };
}

}  // namespace

core::Result<core::ImageLayout> OrientationTransform::outputLayout(
    const core::ImageLayout& sourceLayout,
    core::DisplayStorage storage,
    core::Orientation orientation) {
    StorageTraits traits{};
    if (!storageTraits(storage, traits)) {
        return core::Result<core::ImageLayout>::failure(orientationError(
            "orientation_invalid_display_storage",
            "The display storage value is not supported."));
    }
    if (sourceLayout.storage() != traits.storageType) {
        return core::Result<core::ImageLayout>::failure(orientationError(
            "orientation_source_storage_mismatch",
            "The source layout storage does not match the display storage."));
    }
    if (!isSupportedRotation(orientation.rotation)) {
        return core::Result<core::ImageLayout>::failure(orientationError(
            "orientation_invalid_rotation",
            "The rotation value is not supported."));
    }

    const auto width = swapsAxes(orientation.rotation)
        ? sourceLayout.height()
        : sourceLayout.width();
    const auto height = swapsAxes(orientation.rotation)
        ? sourceLayout.width()
        : sourceLayout.height();
    const auto stride = core::checkedMultiply(
        static_cast<std::size_t>(width), traits.bytesPerPixel);
    if (!stride.hasValue()) {
        return core::Result<core::ImageLayout>::failure(orientationError(
            "orientation_output_layout_overflow",
            "Computing the oriented row size overflowed size_t."));
    }
    const auto payload = core::checkedMultiply(
        stride.value(), static_cast<std::size_t>(height));
    if (!payload.hasValue()) {
        return core::Result<core::ImageLayout>::failure(orientationError(
            "orientation_output_layout_overflow",
            "Computing the oriented payload size overflowed size_t."));
    }
    const auto layout = core::ImageLayout::create(
        width, height, stride.value(), traits.storageType, payload.value());
    if (!layout.hasValue()) {
        return core::Result<core::ImageLayout>::failure(orientationError(
            "orientation_output_layout_invalid",
            layout.error().diagnosticDetail));
    }
    return core::Result<core::ImageLayout>::success(layout.value());
}

core::Result<void> OrientationTransform::apply(
    const core::ImageLayout& sourceLayout,
    core::DisplayStorage storage,
    std::span<const std::byte> sourceBytes,
    const core::ImageLayout& destinationLayout,
    std::span<std::byte> destinationBytes,
    core::Orientation orientation) const {
    StorageTraits traits{};
    if (!storageTraits(storage, traits)) {
        return core::Result<void>::failure(orientationError(
            "orientation_invalid_display_storage",
            "The display storage value is not supported."));
    }
    if (!isSupportedRotation(orientation.rotation)) {
        return core::Result<void>::failure(orientationError(
            "orientation_invalid_rotation",
            "The rotation value is not supported."));
    }
    if (sourceLayout.storage() != traits.storageType) {
        return core::Result<void>::failure(orientationError(
            "orientation_source_storage_mismatch",
            "The source layout storage does not match the display storage."));
    }
    if (destinationLayout.storage() != traits.storageType) {
        return core::Result<void>::failure(orientationError(
            "orientation_destination_storage_mismatch",
            "The destination layout storage does not match the display storage."));
    }
    const auto expectedWidth = swapsAxes(orientation.rotation)
        ? sourceLayout.height()
        : sourceLayout.width();
    const auto expectedHeight = swapsAxes(orientation.rotation)
        ? sourceLayout.width()
        : sourceLayout.height();
    if (destinationLayout.width() != expectedWidth
        || destinationLayout.height() != expectedHeight) {
        return core::Result<void>::failure(orientationError(
            "orientation_destination_extent_mismatch",
            "The destination extent does not match the oriented source extent."));
    }
    if (sourceBytes.size() < sourceLayout.payloadBytes()) {
        return core::Result<void>::failure(orientationError(
            "orientation_source_span_too_small",
            "The source span must cover the complete declared payload."));
    }
    if (destinationBytes.size() < destinationLayout.payloadBytes()) {
        return core::Result<void>::failure(orientationError(
            "orientation_destination_span_too_small",
            "The destination span must cover the complete declared payload."));
    }

    const auto boundedSource = sourceBytes.first(sourceLayout.payloadBytes());
    auto boundedDestination = destinationBytes.first(destinationLayout.payloadBytes());
    if (byteRangesOverlap(boundedSource, boundedDestination)) {
        return core::Result<void>::failure(orientationError(
            "orientation_views_overlap",
            "The complete source and destination payloads must not overlap."));
    }

    const auto sampleBytes = traits.bytesPerPixel;
    for (std::uint32_t y = 0U; y < destinationLayout.height(); ++y) {
        for (std::uint32_t x = 0U; x < destinationLayout.width(); ++x) {
            const auto source = sourceCoordinate(x, y, sourceLayout, orientation);
            const auto sourceOffset = static_cast<std::size_t>(source.y)
                    * sourceLayout.strideBytes()
                + static_cast<std::size_t>(source.x) * sampleBytes;
            const auto destinationOffset = static_cast<std::size_t>(y)
                    * destinationLayout.strideBytes()
                + static_cast<std::size_t>(x) * sampleBytes;
            std::memcpy(boundedDestination.data() + destinationOffset,
                boundedSource.data() + sourceOffset, sampleBytes);
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
