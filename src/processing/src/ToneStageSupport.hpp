#pragma once

#include <lumora/processing/ImageView.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace lumora::processing::detail {

[[nodiscard]] inline core::Result<void> toneFailure(
    std::string_view stagePrefix,
    std::string_view suffix,
    std::string userMessage,
    std::string detail) {
    auto code = std::string(stagePrefix);
    code += suffix;
    return core::Result<void>::failure({core::ErrorCategory::Processing,
        std::move(code), std::move(userMessage), std::move(detail), false});
}

[[nodiscard]] inline core::Result<void> validateToneViews(
    const ImageView& source,
    const MutableImageView& destination,
    std::string_view stagePrefix,
    std::string_view operationName) {
    const auto failure = [stagePrefix, operationName](
                             std::string_view suffix,
                             std::string_view detail) {
        return toneFailure(stagePrefix, suffix,
            std::string(operationName) + " failed.",
            std::string(operationName) + std::string(detail));
    };
    if (source.layout().storage() != core::StorageType::UInt16) {
        return failure("_source_storage_mismatch",
            " input must use unsigned 16-bit storage.");
    }
    if (destination.layout().storage() != core::StorageType::UInt16) {
        return failure("_destination_storage_mismatch",
            " output must use unsigned 16-bit storage.");
    }
    if (source.domain() != ImageDomain::CanonicalU16
        || destination.domain() != ImageDomain::CanonicalU16) {
        return failure("_image_domain_mismatch",
            " requires CanonicalU16 input and output.");
    }
    if (source.layout().width() != destination.layout().width()
        || source.layout().height() != destination.layout().height()) {
        return failure("_image_extent_mismatch",
            " input and output extents must match.");
    }
    if (overlaps(source, destination)) {
        return failure("_image_views_overlap",
            " input and output storage must not overlap.");
    }
    return core::Result<void>::success();
}

[[nodiscard]] inline std::uint16_t readU16(
    std::span<const std::byte> row,
    std::size_t byteOffset) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, row.data() + byteOffset, sizeof(value));
    return value;
}

inline void copyActiveRows(
    const ImageView& source,
    MutableImageView destination) noexcept {
    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        const auto sourceRow = source.row(y);
        const auto destinationRow = destination.row(y);
        std::memcpy(destinationRow.data(), sourceRow.data(), sourceRow.size());
    }
}

inline void writeU16(
    std::span<std::byte> row,
    std::size_t byteOffset,
    std::uint16_t value) noexcept {
    std::memcpy(row.data() + byteOffset, &value, sizeof(value));
}

}  // namespace lumora::processing::detail
