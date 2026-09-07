#include <lumora/processing/DisplayMapper.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace lumora::processing {
namespace {

[[nodiscard]] core::Result<core::DisplayMapping> mappingFailure(
    std::string code,
    std::string detail) {
    return core::Result<core::DisplayMapping>::failure({
        core::ErrorCategory::Processing,
        std::move(code),
        "Display mapping failed.",
        std::move(detail),
        false,
    });
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

[[nodiscard]] std::uint16_t readU16(
    std::span<const std::byte> row,
    std::size_t byteOffset) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, row.data() + byteOffset, sizeof(value));
    return value;
}

}  // namespace

core::Result<core::DisplayMapping> DisplayMapper::map(
    const ImageView& source,
    const core::ImageLayout& destinationLayout,
    core::DisplayStorage destinationStorage,
    std::span<std::byte> destinationBytes,
    std::uint64_t configurationRevision) const {
    if (destinationStorage != core::DisplayStorage::Gray8) {
        return mappingFailure(
            "unsupported_display_storage",
            "Only Gray8 terminal display storage is supported.");
    }
    if (source.layout().storage() != core::StorageType::UInt16) {
        return mappingFailure(
            "source_storage_mismatch",
            "Display mapping input must use unsigned 16-bit storage.");
    }
    if (destinationLayout.storage() != core::StorageType::UInt8) {
        return mappingFailure(
            "destination_storage_mismatch",
            "Gray8 display output requires unsigned 8-bit storage.");
    }
    if (source.domain() != ImageDomain::CanonicalU16) {
        return mappingFailure(
            "image_domain_mismatch",
            "Display mapping input must use the CanonicalU16 domain.");
    }
    if (source.layout().width() != destinationLayout.width()
        || source.layout().height() != destinationLayout.height()) {
        return mappingFailure(
            "image_extent_mismatch",
            "Display mapping input and output extents must match.");
    }
    if (destinationBytes.size() < destinationLayout.payloadBytes()) {
        return mappingFailure(
            "destination_span_too_small",
            "The destination span must cover the complete declared payload.");
    }

    const auto boundedDestination = destinationBytes.first(destinationLayout.payloadBytes());
    if (byteRangesOverlap(source.bytes(), boundedDestination)) {
        return mappingFailure(
            "image_views_overlap",
            "Display mapping input and output storage must not overlap.");
    }

    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        const auto sourceRow = source.row(y);
        auto destinationRow = boundedDestination.subspan(
            static_cast<std::size_t>(y) * destinationLayout.strideBytes(),
            destinationLayout.rowBytes());
        for (std::uint32_t x = 0U; x < source.layout().width(); ++x) {
            const auto value = readU16(sourceRow, static_cast<std::size_t>(x) * 2U);
            destinationRow[x] = static_cast<std::byte>(
                (static_cast<std::uint32_t>(value) + 128U) / 257U);
        }
    }

    return core::Result<core::DisplayMapping>::success(
        {0U, 65535U, 255U, configurationRevision});
}

}  // namespace lumora::processing
