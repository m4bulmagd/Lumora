#pragma once
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <lumora/processing/IProcessingStage.hpp>
#include <span>
namespace lumora::processing::detail {
// Private dependency, never installed. Tests own their synchronized context via
// this shared owner; callbacks must not retain a cycle back to the engine.
class EngineHooks {
public:
    virtual ~EngineHooks() = default;
    virtual core::Result<std::shared_ptr<const IProcessingStage>> prepare(
        const StageDefinition&, const core::ImageLayout&, std::size_t scratchBytes);
    virtual core::Result<void> before(ProcessingOperation,std::span<std::byte>) { return core::Result<void>::success(); }
};
struct FrameEngineTestAccess final {
    static core::Result<std::unique_ptr<FrameProcessingEngine>> create(
        core::BufferPool&,core::BufferPool&,const core::ImageLayout&,const PipelineDefinition&,
        ProcessingPreparationOptions,std::shared_ptr<EngineHooks>);
    static std::weak_ptr<const IProcessingStage> stage(FrameProcessingEngine&,StageId);
};
}
