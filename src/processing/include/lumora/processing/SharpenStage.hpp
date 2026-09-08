#pragma once

#include <lumora/processing/IProcessingStage.hpp>

#include <cstddef>
#include <memory>

namespace lumora::processing {
namespace detail { struct StageStorage; }

// Prepared instances own fixed scratch and belong to one processing worker.
// Parameters and extents are immutable; process mutates scratch and is not concurrent.
class SharpenStage final : public IProcessingStage {
public:
    [[nodiscard]] static core::Result<std::size_t> requiredScratchBytes(
        SharpenParameters parameters, const core::ImageLayout& layout);
    [[nodiscard]] static core::Result<std::unique_ptr<SharpenStage>> create(
        SharpenParameters parameters,
        const core::ImageLayout& layout,
        std::size_t scratchBudgetBytes = 256U * 1024U * 1024U);
    ~SharpenStage() override;

    [[nodiscard]] std::size_t scratchBytes() const noexcept;
    [[nodiscard]] StageId id() const noexcept override;
    [[nodiscard]] const StageTraits& traits() const noexcept override;
    [[nodiscard]] core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    friend struct detail::StageStorage;
    struct Impl;
    explicit SharpenStage(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::processing
