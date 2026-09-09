#include <lumora/processing/OrientationTransform.hpp>

#include <lumora/core/CheckedMath.hpp>
#include <lumora/core/Error.hpp>

#include <algorithm>
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

template<std::size_t PixelBytes>
inline void copyPixel(
    std::byte* destination,
    const std::byte* source) noexcept {
    std::memcpy(destination, source, PixelBytes);
}

template<std::size_t PixelBytes, bool ReverseX, bool ReverseY>
void copySameAxes(
    const core::ImageLayout& sourceLayout,
    std::span<const std::byte> source,
    const core::ImageLayout& destinationLayout,
    std::span<std::byte> destination) noexcept {
    [[maybe_unused]] const auto sourceWidth = sourceLayout.width();
    const auto sourceHeight = sourceLayout.height();
    const auto sourceStrideBytes = sourceLayout.strideBytes();
    [[maybe_unused]] const auto sourceRowBytes = sourceLayout.rowBytes();
    [[maybe_unused]] const auto destinationWidth = destinationLayout.width();
    const auto destinationHeight = destinationLayout.height();
    const auto destinationStrideBytes = destinationLayout.strideBytes();
    for (std::uint32_t destinationY = 0U;
         destinationY < destinationHeight; ++destinationY) {
        const auto sourceY = ReverseY
            ? sourceHeight - 1U - destinationY
            : destinationY;
        const auto* sourceRow = source.data()
            + static_cast<std::size_t>(sourceY) * sourceStrideBytes;
        auto* destinationRow = destination.data()
            + static_cast<std::size_t>(destinationY)
                * destinationStrideBytes;
        if constexpr (!ReverseX) {
            std::memcpy(destinationRow, sourceRow, sourceRowBytes);
        } else {
            for (std::uint32_t destinationX = 0U;
                 destinationX < destinationWidth; ++destinationX) {
                const auto sourceX = sourceWidth - 1U - destinationX;
                copyPixel<PixelBytes>(
                    destinationRow
                        + static_cast<std::size_t>(destinationX) * PixelBytes,
                    sourceRow + static_cast<std::size_t>(sourceX) * PixelBytes);
            }
        }
    }
}

template<std::size_t PixelBytes,
    bool ReverseSourceXFromDestinationY,
    bool ReverseSourceYFromDestinationX>
void copySwappedAxes32(
    const core::ImageLayout& sourceLayout,
    std::span<const std::byte> source,
    const core::ImageLayout& destinationLayout,
    std::span<std::byte> destination) noexcept {
    constexpr std::uint32_t tileSize = 32U;
    const auto sourceWidth = sourceLayout.width();
    const auto sourceHeight = sourceLayout.height();
    const auto sourceStrideBytes = sourceLayout.strideBytes();
    const auto destinationWidth = destinationLayout.width();
    const auto destinationHeight = destinationLayout.height();
    const auto destinationStrideBytes = destinationLayout.strideBytes();
    for (std::uint32_t tileY = 0U; tileY < destinationHeight;) {
        const auto tileHeight = std::min(
            tileSize, destinationHeight - tileY);
        const auto tileEndY = tileY + tileHeight;
        for (std::uint32_t tileX = 0U; tileX < destinationWidth;) {
            const auto tileWidth = std::min(
                tileSize, destinationWidth - tileX);
            const auto tileEndX = tileX + tileWidth;
            for (auto destinationY = tileY;
                 destinationY < tileEndY; ++destinationY) {
                const auto sourceX = ReverseSourceXFromDestinationY
                    ? sourceWidth - 1U - destinationY
                    : destinationY;
                const auto sourceColumnOffset =
                    static_cast<std::size_t>(sourceX) * PixelBytes;
                auto* destinationPixel = destination.data()
                    + static_cast<std::size_t>(destinationY)
                        * destinationStrideBytes
                    + static_cast<std::size_t>(tileX) * PixelBytes;
                for (auto destinationX = tileX;
                     destinationX < tileEndX; ++destinationX) {
                    const auto sourceY = ReverseSourceYFromDestinationX
                        ? sourceHeight - 1U - destinationX
                        : destinationX;
                    const auto* sourcePixel = source.data()
                        + static_cast<std::size_t>(sourceY)
                            * sourceStrideBytes
                        + sourceColumnOffset;
                    copyPixel<PixelBytes>(destinationPixel, sourcePixel);
                    destinationPixel += PixelBytes;
                }
            }
            tileX += tileWidth;
        }
        tileY += tileHeight;
    }
}

template<std::size_t PixelBytes>
void dispatchSameAxes(
    const core::ImageLayout& sourceLayout,
    std::span<const std::byte> source,
    const core::ImageLayout& destinationLayout,
    std::span<std::byte> destination,
    bool reverseX,
    bool reverseY) noexcept {
    if (reverseX) {
        if (reverseY) {
            copySameAxes<PixelBytes, true, true>(
                sourceLayout, source, destinationLayout, destination);
        } else {
            copySameAxes<PixelBytes, true, false>(
                sourceLayout, source, destinationLayout, destination);
        }
    } else if (reverseY) {
        copySameAxes<PixelBytes, false, true>(
            sourceLayout, source, destinationLayout, destination);
    } else {
        copySameAxes<PixelBytes, false, false>(
            sourceLayout, source, destinationLayout, destination);
    }
}

template<std::size_t PixelBytes>
void dispatchSwappedAxes(
    const core::ImageLayout& sourceLayout,
    std::span<const std::byte> source,
    const core::ImageLayout& destinationLayout,
    std::span<std::byte> destination,
    bool reverseSourceXFromDestinationY,
    bool reverseSourceYFromDestinationX) noexcept {
    if (reverseSourceXFromDestinationY) {
        if (reverseSourceYFromDestinationX) {
            copySwappedAxes32<PixelBytes, true, true>(
                sourceLayout, source, destinationLayout, destination);
        } else {
            copySwappedAxes32<PixelBytes, true, false>(
                sourceLayout, source, destinationLayout, destination);
        }
    } else if (reverseSourceYFromDestinationX) {
        copySwappedAxes32<PixelBytes, false, true>(
            sourceLayout, source, destinationLayout, destination);
    } else {
        copySwappedAxes32<PixelBytes, false, false>(
            sourceLayout, source, destinationLayout, destination);
    }
}

template<std::size_t PixelBytes>
void copyOriented(
    const core::ImageLayout& sourceLayout,
    std::span<const std::byte> source,
    const core::ImageLayout& destinationLayout,
    std::span<std::byte> destination,
    core::Orientation orientation) noexcept {
    switch (orientation.rotation) {
    case core::Rotation::Degrees0:
        dispatchSameAxes<PixelBytes>(sourceLayout, source,
            destinationLayout, destination,
            orientation.flipHorizontal, orientation.flipVertical);
        break;
    case core::Rotation::Degrees90:
        dispatchSwappedAxes<PixelBytes>(sourceLayout, source,
            destinationLayout, destination,
            orientation.flipHorizontal, !orientation.flipVertical);
        break;
    case core::Rotation::Degrees180:
        dispatchSameAxes<PixelBytes>(sourceLayout, source,
            destinationLayout, destination,
            !orientation.flipHorizontal, !orientation.flipVertical);
        break;
    case core::Rotation::Degrees270:
        dispatchSwappedAxes<PixelBytes>(sourceLayout, source,
            destinationLayout, destination,
            !orientation.flipHorizontal, orientation.flipVertical);
        break;
    }
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

    switch (storage) {
    case core::DisplayStorage::Gray8:
        copyOriented<1U>(sourceLayout, boundedSource,
            destinationLayout, boundedDestination, orientation);
        break;
    case core::DisplayStorage::Gray16:
        copyOriented<2U>(sourceLayout, boundedSource,
            destinationLayout, boundedDestination, orientation);
        break;
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
