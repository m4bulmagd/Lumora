#pragma once

#include <lumora/processing/IProcessingStage.hpp>

#include <cstddef>
#include <memory>

namespace lumora::processing {

// Prepared instances own fixed scratch and belong to one processing worker.
// Parameters and extents are immutable; process mutates scratch and is not concurrent.
class DenoiseStage final : public IProcessingStage {
public:
    [[nodiscard]] static core::Result<std::size_t> requiredScratchBytes(
        DenoiseParameters parameters, const core::ImageLayout& layout);
    [[nodiscard]] static core::Result<std::unique_ptr<DenoiseStage>> create(
        DenoiseParameters parameters,
        const core::ImageLayout& layout,
        std::size_t scratchBudgetBytes = 256U * 1024U * 1024U);
    ~DenoiseStage() override;

    [[nodiscard]] std::size_t scratchBytes() const noexcept;
    [[nodiscard]] StageId id() const noexcept override;
    [[nodiscard]] const StageTraits& traits() const noexcept override;
    [[nodiscard]] core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    struct Impl;
    explicit DenoiseStage(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::processing
