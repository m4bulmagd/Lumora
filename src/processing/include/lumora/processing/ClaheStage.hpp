#pragma once

#include <lumora/processing/IProcessingStage.hpp>

#include <cstddef>
#include <memory>

namespace lumora::processing {
namespace detail { struct StageStorage; }

// A separate candidate may be prepared on the control thread while a prior stage
// processes. Each fixed-size prepared object belongs to one sequential worker and
// must not process concurrently. Sequential calls may use different valid strides;
// width, height, configuration, and retained capacities never mutate.
class ClaheStage final : public IProcessingStage {
public:
    // Pure sizing and validation; it has no floating-rounding-mode requirement.
    // The result is retained typed-array payload only and excludes the stage,
    // PImpl/vector control objects, allocator overhead, and process RSS.
    static core::Result<std::size_t> requiredScratchBytes(
        ClaheParameters parameters, const core::ImageLayout& layout);
    // Uses a 256 MiB standalone scratch budget. After configuration/layout
    // validation, both factories require FE_TONEAREST, as does every process
    // call; clahe_rounding_mode_unsupported precedes retained-array allocation
    // or output mutation.
    static core::Result<std::unique_ptr<ClaheStage>> create(
        ClaheParameters parameters, const core::ImageLayout& layout);
    static core::Result<std::unique_ptr<ClaheStage>> create(
        ClaheParameters parameters, const core::ImageLayout& layout,
        std::size_t scratchBudgetBytes);
    ~ClaheStage() override;
    // Same retained typed-array payload scope as requiredScratchBytes().
    [[nodiscard]] std::size_t scratchBytes() const noexcept;
    StageId id() const noexcept override;
    const StageTraits& traits() const noexcept override;
    core::Result<void> process(const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    friend struct detail::StageStorage;
    struct Impl;
    explicit ClaheStage(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::processing
