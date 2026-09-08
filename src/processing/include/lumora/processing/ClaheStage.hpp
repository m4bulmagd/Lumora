#pragma once

#include <lumora/processing/IProcessingStage.hpp>

#include <memory>

namespace lumora::processing {

// Created and replaced only while processing is stopped. A stage is bound to
// one width/height, while each process call may supply different valid strides.
// Each instance belongs to one processing worker: process is logically const,
// but mutates private OpenCV caches and must not execute concurrently.
class ClaheStage final : public IProcessingStage {
public:
    static core::Result<std::unique_ptr<ClaheStage>> create(
        ClaheParameters parameters, const core::ImageLayout& layout);
    ~ClaheStage() override;
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
