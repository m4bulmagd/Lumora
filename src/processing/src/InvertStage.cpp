#include <lumora/processing/ToneStages.hpp>

#include "ToneStageSupport.hpp"

#include <cstddef>
#include <cstdint>

namespace lumora::processing {

InvertStage::InvertStage(InvertParameters parameters)
    : parameters_(parameters) {}

StageId InvertStage::id() const noexcept {
    return StageId::Invert;
}

const StageTraits& InvertStage::traits() const noexcept {
    static constexpr StageTraits traits{StageId::Invert,
        ImageDomain::CanonicalU16, ImageDomain::CanonicalU16, false, 0U, 0U, false,
        ExecutionBackend::Cpu};
    return traits;
}

core::Result<void> InvertStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat&) const {
    const auto validation = detail::validateToneViews(
        source, destination, "invert", "Inversion");
    if (!validation.hasValue()) return validation;

    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        const auto sourceRow = source.row(y);
        const auto destinationRow = destination.row(y);
        for (std::uint32_t x = 0U; x < source.layout().width(); ++x) {
            const auto byteOffset = static_cast<std::size_t>(x) * 2U;
            detail::writeU16(destinationRow, byteOffset, static_cast<std::uint16_t>(
                65535U - detail::readU16(sourceRow, byteOffset)));
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
