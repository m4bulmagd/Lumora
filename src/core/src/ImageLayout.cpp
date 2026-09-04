#include <lumora/core/ImageLayout.hpp>

#include <lumora/core/CheckedMath.hpp>
#include <lumora/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace lumora::core {
namespace {

[[nodiscard]] Error invalidFrameError(std::string code, std::string detail) {
    return Error{
        ErrorCategory::InvalidFrame,
        std::move(code),
        "The image data is invalid.",
        std::move(detail),
        false,
    };
}

struct StorageTraits final {
    std::size_t bytesPerPixel;
    std::uint8_t bits;
};

[[nodiscard]] constexpr std::optional<StorageTraits> storageTraits(
    StorageType storage) noexcept {
    switch (storage) {
    case StorageType::UInt8:
        return StorageTraits{1U, 8U};
    case StorageType::UInt16:
        return StorageTraits{2U, 16U};
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool isSupportedPacking(SourcePacking packing) noexcept {
    switch (packing) {
    case SourcePacking::Unpacked:
    case SourcePacking::Packed:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool isSupportedAlignment(
    BitAlignment alignment) noexcept {
    switch (alignment) {
    case BitAlignment::LeastSignificant:
    case BitAlignment::MostSignificant:
        return true;
    }
    return false;
}

}  // namespace

Result<void> validateSourcePixelFormat(const SourcePixelFormat& format) {
    if (format.canonicalName.empty()) {
        return Result<void>::failure(invalidFrameError(
            "pixel_format_name_empty",
            "The source pixel format must have a stable canonical name."));
    }

    const auto traits = storageTraits(format.applicationStorage);
    if (!traits.has_value()) {
        return Result<void>::failure(invalidFrameError(
            "unsupported_storage_type",
            "The source pixel format declares an unknown application storage type."));
    }
    if (!isSupportedPacking(format.packing)) {
        return Result<void>::failure(invalidFrameError(
            "unsupported_source_packing",
            "The source pixel format declares an unknown packing mode."));
    }
    if (!isSupportedAlignment(format.alignment)) {
        return Result<void>::failure(invalidFrameError(
            "unsupported_bit_alignment",
            "The source pixel format declares an unknown bit alignment."));
    }

    constexpr std::uint8_t maximumSupportedBits = 16U;
    if (format.validBits == 0U || format.validBits > maximumSupportedBits) {
        return Result<void>::failure(invalidFrameError(
            "invalid_valid_bits",
            "The source pixel format valid-bit count must be between 1 and 16."));
    }
    if (format.validBits > traits->bits) {
        return Result<void>::failure(invalidFrameError(
            "valid_bits_exceed_storage",
            "The valid-bit count exceeds the application storage width."));
    }
    if (format.sampleMaximum == 0U) {
        return Result<void>::failure(invalidFrameError(
            "invalid_sample_maximum",
            "The declared sample maximum must be greater than zero."));
    }

    const auto maximumForBits =
        (std::uint32_t{1U} << format.validBits) - std::uint32_t{1U};
    if (format.sampleMaximum > maximumForBits) {
        return Result<void>::failure(invalidFrameError(
            "sample_maximum_exceeds_valid_bits",
            "The declared sample maximum exceeds the valid-bit range."));
    }
    return Result<void>::success();
}

Result<ImageLayout> ImageLayout::create(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t strideBytes,
    StorageType storage,
    std::size_t payloadBytes) {
    if (width == 0U || height == 0U) {
        return Result<ImageLayout>::failure(invalidFrameError(
            "zero_dimensions",
            "Image width and height must both be greater than zero."));
    }

    const auto traits = storageTraits(storage);
    if (!traits.has_value()) {
        return Result<ImageLayout>::failure(invalidFrameError(
            "unsupported_storage_type",
            "The image layout declares an unknown application storage type."));
    }

    const auto rowBytes = checkedMultiply(
        static_cast<std::size_t>(width), traits->bytesPerPixel);
    if (!rowBytes.hasValue()) {
        return Result<ImageLayout>::failure(invalidFrameError(
            "layout_size_overflow",
            "Computing the image row size overflowed size_t."));
    }
    if (strideBytes < rowBytes.value()) {
        return Result<ImageLayout>::failure(invalidFrameError(
            "stride_too_small",
            "The row stride is smaller than the pixel bytes in one row."));
    }

    const auto requiredBytes = checkedMultiply(strideBytes, static_cast<std::size_t>(height));
    if (!requiredBytes.hasValue()) {
        return Result<ImageLayout>::failure(invalidFrameError(
            "layout_size_overflow",
            "Computing stride times height overflowed size_t."));
    }
    if (payloadBytes < requiredBytes.value()) {
        return Result<ImageLayout>::failure(invalidFrameError(
            "payload_too_small",
            "The payload is shorter than stride times height."));
    }

    return Result<ImageLayout>::success(ImageLayout(
        width,
        height,
        rowBytes.value(),
        strideBytes,
        requiredBytes.value(),
        payloadBytes,
        storage));
}

ImageLayout::ImageLayout(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t rowBytes,
    std::size_t strideBytes,
    std::size_t requiredBytes,
    std::size_t payloadBytes,
    StorageType storage) noexcept
    : width_(width),
      height_(height),
      rowBytes_(rowBytes),
      strideBytes_(strideBytes),
      requiredBytes_(requiredBytes),
      payloadBytes_(payloadBytes),
      storage_(storage) {}

std::uint32_t ImageLayout::width() const noexcept {
    return width_;
}

std::uint32_t ImageLayout::height() const noexcept {
    return height_;
}

std::size_t ImageLayout::rowBytes() const noexcept {
    return rowBytes_;
}

std::size_t ImageLayout::strideBytes() const noexcept {
    return strideBytes_;
}

std::size_t ImageLayout::requiredBytes() const noexcept {
    return requiredBytes_;
}

std::size_t ImageLayout::payloadBytes() const noexcept {
    return payloadBytes_;
}

StorageType ImageLayout::storage() const noexcept {
    return storage_;
}

}  // namespace lumora::core
