#pragma once

#include <lumora/processing/IFrameProcessor.hpp>
#include <lumora/processing/ProcessingPipeline.hpp>
#include <lumora/processing/ProcessingWorkspace.hpp>

namespace lumora::processing {

// process is single-worker; activate may run concurrently. Pools outlive the
// engine. A new source size/storage requires stopped-state engine preparation.
// Timings record executed work, not the enabled Enhanced stage list.
// shared_window_level serves both routes; original_window_level serves Original
// alone when Enhanced omits/disables WL. Terminal map timings name each route.
class FrameProcessingEngine final : public IFrameProcessor {
public:
    [[nodiscard]] static core::Result<std::unique_ptr<FrameProcessingEngine>> create(
        core::BufferPool& processingPool, core::BufferPool& displayPool,
        const core::ImageLayout& sourceLayout,
        const PipelineDefinition& definition = defaultPipeline());
    [[nodiscard]] core::Result<void, PipelineValidationError> activate(
        const PipelineDefinition& definition);
    [[nodiscard]] core::Result<std::shared_ptr<const core::FrameBundle>> process(
        std::shared_ptr<const core::RawFrame> raw) override;

private:
    FrameProcessingEngine(core::BufferPool& processingPool, core::BufferPool& displayPool);
    ProcessingPipeline pipeline_;
    ProcessingWorkspace workspace_;
};

}  // namespace lumora::processing
