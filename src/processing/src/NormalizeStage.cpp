#include <lumora/processing/NormalizeStage.hpp>

#include "ImageRowCopy.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace lumora::processing {
namespace {

[[nodiscard]] core::Result<void> processingFailure(
    std::string code,
    std::string detail) {
    return core::Result<void>::failure({
        core::ErrorCategory::Processing,
        std::move(code),
        "Image normalization failed.",
        std::move(detail),
        false,
    });
}

[[nodiscard]] std::uint16_t readU16(
    std::span<const std::byte> row,
    std::size_t byteOffset) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, row.data() + byteOffset, sizeof(value));
    return value;
}

void writeU16(
    std::span<std::byte> row,
    std::size_t byteOffset,
    std::uint16_t value) noexcept {
    std::memcpy(row.data() + byteOffset, &value, sizeof(value));
}

[[nodiscard]] std::uint16_t normalizeSample(
    std::uint16_t value,
    std::uint16_t sourceMaximum) noexcept {
    const auto numerator = static_cast<std::uint64_t>(value) * 65535U
        + static_cast<std::uint64_t>(sourceMaximum) / 2U;
    return static_cast<std::uint16_t>(numerator / sourceMaximum);
}

}  // namespace

StageId NormalizeStage::id() const noexcept {
    return StageId::Normalize;
}

const StageTraits& NormalizeStage::traits() const noexcept {
    static constexpr StageTraits normalizeTraits{
        StageId::Normalize,
        ImageDomain::SensorNative,
        ImageDomain::CanonicalU16,
        false,
        0U,
        0U,
        false,
        ExecutionBackend::Cpu,
    };
    return normalizeTraits;
}

core::Result<void> NormalizeStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat& sourceFormat) const {
    const auto formatValidation = core::validateSourcePixelFormat(sourceFormat);
    if (!formatValidation.hasValue())
        return core::Result<void>::failure(formatValidation.error());

    if (source.layout().storage() != sourceFormat.applicationStorage) {
        return processingFailure(
            "source_storage_mismatch",
            "The source view storage does not match the validated source pixel format.");
    }
    if (destination.layout().storage() != core::StorageType::UInt16) {
        return processingFailure(
            "destination_storage_mismatch",
            "The normalization destination must use unsigned 16-bit storage.");
    }
    if (source.domain() != ImageDomain::SensorNative
        || destination.domain() != ImageDomain::CanonicalU16) {
        return processingFailure(
            "image_domain_mismatch",
            "Normalization requires SensorNative input and CanonicalU16 output.");
    }
    if (source.layout().width() != destination.layout().width()
        || source.layout().height() != destination.layout().height()) {
        return processingFailure(
            "image_extent_mismatch",
            "Normalization requires identical source and destination extents.");
    }
    if (overlaps(source, destination)) {
        return processingFailure(
            "image_views_overlap",
            "Normalization requires non-overlapping source and destination storage.");
    }

    if (sourceFormat.applicationStorage == core::StorageType::UInt16
        && sourceFormat.sampleMaximum == 65535U) {
        detail::copyActiveRows(source, destination);
        return core::Result<void>::success();
    }

    const auto width = source.layout().width();
    const auto height = source.layout().height();
    const auto maximum = sourceFormat.sampleMaximum;
    const bool isU8 = sourceFormat.applicationStorage == core::StorageType::UInt8;
    for (std::uint32_t y = 0U; y < height; ++y) {
        const auto sourceRow = source.row(y);
        const auto destinationRow = destination.row(y);
        for (std::uint32_t x = 0U; x < width; ++x) {
            const auto sourceOffset = static_cast<std::size_t>(x) * (isU8 ? 1U : 2U);
            const auto value = isU8
                ? static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(sourceRow[sourceOffset]))
                : readU16(sourceRow, sourceOffset);
            if (value > maximum) {
                return processingFailure(
                    "sample_exceeds_source_maximum",
                    "The first invalid sample is at x=" + std::to_string(x)
                        + ", y=" + std::to_string(y)
                        + ", value=" + std::to_string(value)
                        + ", maximum=" + std::to_string(maximum) + ".");
            }
            writeU16(destinationRow, static_cast<std::size_t>(x) * 2U,
                normalizeSample(value, maximum));
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
