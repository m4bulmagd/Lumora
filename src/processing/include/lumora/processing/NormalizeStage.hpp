#pragma once

#include <lumora/processing/IProcessingStage.hpp>

namespace lumora::processing {

class NormalizeStage final : public IProcessingStage {
public:
    [[nodiscard]] StageId id() const noexcept override;
    [[nodiscard]] const StageTraits& traits() const noexcept override;
    // A failure may leave destination partially written; the caller must
    // discard destination unless process returns success.
    [[nodiscard]] core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;
};

}  // namespace lumora::processing
