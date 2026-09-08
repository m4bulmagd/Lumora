#pragma once

#include <lumora/processing/IProcessingStage.hpp>

#include <cstddef>
#include <memory>

namespace lumora::processing {

// A separate candidate may be prepared on the control thread while a prior stage
// processes. Each fixed-size prepared object belongs to one sequential worker and
// must not process concurrently. Sequential calls may use different valid strides;
// width, height, configuration, and retained capacities never mutate.
class ClaheStage final : public IProcessingStage {
public:
    static core::Result<std::size_t> requiredScratchBytes(
        ClaheParameters parameters, const core::ImageLayout& layout);
    static core::Result<std::unique_ptr<ClaheStage>> create(
        ClaheParameters parameters, const core::ImageLayout& layout);
    static core::Result<std::unique_ptr<ClaheStage>> create(
        ClaheParameters parameters, const core::ImageLayout& layout,
        std::size_t scratchBudgetBytes);
    ~ClaheStage() override;
    [[nodiscard]] std::size_t scratchBytes() const noexcept;
    StageId id() const noexcept override;
    const StageTraits& traits() const noexcept override;
    core::Result<void> process(const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    struct Impl;
    explicit ClaheStage(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::processing
