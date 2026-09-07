#pragma once

#include <lumora/processing/IProcessingStage.hpp>

namespace lumora::processing {

class WindowLevelStage final : public IProcessingStage {
public:
    explicit WindowLevelStage(WindowLevelParameters parameters = {});

    [[nodiscard]] StageId id() const noexcept override;
    [[nodiscard]] const StageTraits& traits() const noexcept override;
    [[nodiscard]] core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    WindowLevelParameters parameters_;
};

}  // namespace lumora::processing
