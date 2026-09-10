#pragma once

#include <lumora/processing/IFrameProcessor.hpp>
#include <lumora/processing/ProcessingPreparation.hpp>
#include <lumora/core/FrameObjectPool.hpp>
#include <lumora/processing/ProcessingPipeline.hpp>
#include <lumora/processing/ProcessingWorkspace.hpp>

namespace lumora::processing {
namespace detail { struct EngineState; class EngineHooks; struct FrameEngineTestAccess; }

// process is single-worker; activate may run concurrently. Pools outlive the
// engine. A new source size/storage requires stopped-state engine preparation.
// Timings record executed work, not the enabled Enhanced stage list.
// shared_window_level serves both routes; original_window_level serves Original
// alone when Enhanced omits/disables WL. Terminal map timings name each route.
class FrameProcessingEngine final : public IFrameProcessor {
public:
    [[nodiscard]] static ProcessingPreparationAssessment plan(
        ProcessingPoolSpecification processingPool, ProcessingPoolSpecification displayPool,
        const core::ImageLayout& sourceLayout,
        const PipelineDefinition& definition = defaultPipeline(),
        ProcessingPreparationOptions options = {});
    [[nodiscard]] static core::Result<std::unique_ptr<FrameProcessingEngine>> create(
        core::BufferPool& processingPool, core::BufferPool& displayPool,
        const core::ImageLayout& sourceLayout,
        const PipelineDefinition& definition = defaultPipeline(),
        ProcessingPreparationOptions options = {});
    [[nodiscard]] core::Result<void, PipelineValidationError> activate(
        const PipelineDefinition& definition) override;
    [[nodiscard]] core::Result<std::shared_ptr<const core::FrameBundle>> process(
        std::shared_ptr<const core::RawFrame> raw) override;

    [[nodiscard]] static core::Result<std::unique_ptr<FrameProcessingEngine>> create(
        core::BufferPool& processingPool, core::BufferPool& displayPool,
        const ProcessingPreparationPlan& plan);
    ~FrameProcessingEngine() override;
    [[nodiscard]] ProcessorStatus status() const noexcept override;
    [[nodiscard]] bool requestRetry() noexcept override;
    [[nodiscard]] ProcessingResources resources() const noexcept;
private:
    friend struct detail::FrameEngineTestAccess;
    static core::Result<std::unique_ptr<FrameProcessingEngine>> createPrepared(
        core::BufferPool&, core::BufferPool&, const ProcessingPreparationPlan&, std::shared_ptr<detail::EngineHooks>);
    FrameProcessingEngine(core::BufferPool&, core::BufferPool&, std::shared_ptr<const detail::EnginePlan>);
    std::unique_ptr<detail::EngineState> state_;
};

}  // namespace lumora::processing
