#include <lumora/processing/ToneStages.hpp>

#include "ToneStageSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lumora::processing {
namespace {

[[nodiscard]] std::uint16_t adjustSample(
    std::uint16_t value,
    std::int64_t brightnessOffset,
    double contrast) noexcept {
    const auto brightened = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(value) + brightnessOffset, 0, 65535);
    const auto scaled = (static_cast<double>(brightened) - 32767.5) * contrast
        + 32767.5;
    if (scaled <= 0.0) return 0U;
    if (scaled >= 65535.0) return 65535U;
    return static_cast<std::uint16_t>(std::floor(scaled + 0.5));
}

}  // namespace

BrightnessContrastStage::BrightnessContrastStage(BrightnessContrastParameters parameters)
    : parameters_(parameters) {}

StageId BrightnessContrastStage::id() const noexcept {
    return StageId::BrightnessContrast;
}

const StageTraits& BrightnessContrastStage::traits() const noexcept {
    static constexpr StageTraits traits{StageId::BrightnessContrast,
        ImageDomain::CanonicalU16, ImageDomain::CanonicalU16, false, 0U, 0U, false,
        ExecutionBackend::Cpu};
    return traits;
}

core::Result<void> BrightnessContrastStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat&) const {
    if (!std::isfinite(parameters_.brightness)
        || parameters_.brightness < -1.0 || parameters_.brightness > 1.0
        || !std::isfinite(parameters_.contrast)
        || parameters_.contrast < 0.0 || parameters_.contrast > 4.0) {
        return detail::toneFailure("brightness_contrast", "_invalid_parameters",
            "Brightness/contrast adjustment failed.",
            "Brightness must be finite in [-1, 1] and contrast finite in [0, 4].");
    }
    const auto validation = detail::validateToneViews(source, destination,
        "brightness_contrast", "Brightness/contrast adjustment");
    if (!validation.hasValue()) return validation;

    const auto brightnessOffset = static_cast<std::int64_t>(
        std::llround(parameters_.brightness * 65535.0));
    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        const auto sourceRow = source.row(y);
        const auto destinationRow = destination.row(y);
        for (std::uint32_t x = 0U; x < source.layout().width(); ++x) {
            const auto byteOffset = static_cast<std::size_t>(x) * 2U;
            detail::writeU16(destinationRow, byteOffset,
                adjustSample(detail::readU16(sourceRow, byteOffset),
                    brightnessOffset, parameters_.contrast));
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
