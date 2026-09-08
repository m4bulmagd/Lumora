#include <lumora/processing/ToneStages.hpp>

#include "ToneStageSupport.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lumora::processing {
namespace {

[[nodiscard]] bool isValidGamma(double gamma) noexcept {
    return std::isfinite(gamma) && gamma >= 0.1 && gamma <= 5.0;
}

[[nodiscard]] std::array<std::uint16_t, 65536U> buildGammaLut(double gamma) {
    std::array<std::uint16_t, 65536U> lut{};
    if (!isValidGamma(gamma)) return lut;

    lut.front() = 0U;
    const auto exponent = 1.0 / gamma;
    for (std::uint32_t value = 1U; value < 65535U; ++value) {
        const auto normalized = static_cast<double>(value) / 65535.0;
        const auto scaled = std::pow(normalized, exponent) * 65535.0;
        lut[value] = static_cast<std::uint16_t>(std::floor(scaled + 0.5));
    }
    lut.back() = 65535U;
    return lut;
}

}  // namespace

GammaStage::GammaStage(GammaParameters parameters)
    : parameters_(parameters), lut_(buildGammaLut(parameters.gamma)) {}

StageId GammaStage::id() const noexcept {
    return StageId::Gamma;
}

const StageTraits& GammaStage::traits() const noexcept {
    static constexpr StageTraits traits{StageId::Gamma,
        ImageDomain::CanonicalU16, ImageDomain::CanonicalU16, false, 0U, 0U, false,
        ExecutionBackend::Cpu};
    return traits;
}

core::Result<void> GammaStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat&) const {
    if (!isValidGamma(parameters_.gamma)) {
        return detail::toneFailure("gamma", "_invalid_parameters",
            "Gamma correction failed.", "Gamma must be finite in [0.1, 5].");
    }
    const auto validation = detail::validateToneViews(
        source, destination, "gamma", "Gamma correction");
    if (!validation.hasValue()) return validation;

    if (parameters_.gamma == 1.0) {
        detail::copyActiveRows(source, destination);
        return core::Result<void>::success();
    }

    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        const auto sourceRow = source.row(y);
        const auto destinationRow = destination.row(y);
        for (std::uint32_t x = 0U; x < source.layout().width(); ++x) {
            const auto byteOffset = static_cast<std::size_t>(x) * 2U;
            detail::writeU16(destinationRow, byteOffset,
                lut_[detail::readU16(sourceRow, byteOffset)]);
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
