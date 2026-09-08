#include <lumora/processing/WindowLevelStage.hpp>

#include <algorithm>
#include <cmath>
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
        "Window/level mapping failed.",
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

[[nodiscard]] std::uint16_t mapSample(
    std::uint16_t value,
    double lower,
    double upper) noexcept {
    const auto sample = static_cast<double>(value);
    if (sample <= lower) return 0U;
    if (sample >= upper) return 65535U;

    // Keep this order stable: calculate the positive linear position, scale it,
    // then round to nearest by adding one half before floor.
    const auto scaled = ((sample - lower) * 65535.0) / (upper - lower);
    return static_cast<std::uint16_t>(std::floor(scaled + 0.5));
}

}  // namespace

WindowLevelStage::WindowLevelStage(WindowLevelParameters parameters)
    : parameters_(parameters) {}

StageId WindowLevelStage::id() const noexcept {
    return StageId::WindowLevel;
}

const StageTraits& WindowLevelStage::traits() const noexcept {
    static constexpr StageTraits windowLevelTraits{
        StageId::WindowLevel,
        ImageDomain::CanonicalU16,
        ImageDomain::CanonicalU16,
        false,
        0U,
        0U,
        false,
        ExecutionBackend::Cpu,
    };
    return windowLevelTraits;
}

core::Result<void> WindowLevelStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat&) const {
    if (!std::isfinite(parameters_.window) || parameters_.window <= 0.0
        || !std::isfinite(parameters_.level)) {
        return processingFailure(
            "invalid_window_level_parameters",
            "Window must be finite and positive, and level must be finite.");
    }
    if (source.layout().storage() != core::StorageType::UInt16) {
        return processingFailure(
            "source_storage_mismatch",
            "Window/level input must use unsigned 16-bit storage.");
    }
    if (destination.layout().storage() != core::StorageType::UInt16) {
        return processingFailure(
            "destination_storage_mismatch",
            "Window/level output must use unsigned 16-bit storage.");
    }
    if (source.domain() != ImageDomain::CanonicalU16
        || destination.domain() != ImageDomain::CanonicalU16) {
        return processingFailure(
            "image_domain_mismatch",
            "Window/level mapping requires CanonicalU16 input and output.");
    }
    if (source.layout().width() != destination.layout().width()
        || source.layout().height() != destination.layout().height()) {
        return processingFailure(
            "image_extent_mismatch",
            "Window/level input and output extents must match.");
    }
    if (overlaps(source, destination)) {
        return processingFailure(
            "image_views_overlap",
            "Window/level input and output storage must not overlap.");
    }

    const auto halfWindow = parameters_.window / 2.0;
    const auto lower = std::clamp(parameters_.level - halfWindow, 0.0, 65535.0);
    const auto upper = std::clamp(parameters_.level + halfWindow, 0.0, 65535.0);
    if (!(upper > lower)) {
        return processingFailure(
            "invalid_window_level_parameters",
            "The clipped window endpoints must describe a positive interval.");
    }

    if (lower == 0.0 && upper == 65535.0) {
        for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
            const auto sourceRow = source.row(y);
            const auto destinationRow = destination.row(y);
            std::memcpy(destinationRow.data(), sourceRow.data(), sourceRow.size());
        }
        return core::Result<void>::success();
    }

    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        const auto sourceRow = source.row(y);
        const auto destinationRow = destination.row(y);
        for (std::uint32_t x = 0U; x < source.layout().width(); ++x) {
            const auto byteOffset = static_cast<std::size_t>(x) * 2U;
            writeU16(destinationRow, byteOffset,
                mapSample(readU16(sourceRow, byteOffset), lower, upper));
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
