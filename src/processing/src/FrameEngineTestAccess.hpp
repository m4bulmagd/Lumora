#pragma once
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <lumora/processing/IProcessingStage.hpp>
#include <span>
#include "PreparedCpuObservation.hpp"
namespace lumora::processing::detail {
class PreparedCpuExecutor;
// Private dependency, never installed. Tests own their synchronized context via
// this shared owner; callbacks must not retain a cycle back to the engine.
class EngineHooks {
public:
    virtual ~EngineHooks() = default;
    virtual void beforeCpuThreadStart(std::size_t) {}
    virtual core::Result<std::shared_ptr<const IProcessingStage>> prepare(
        const StageDefinition&, const core::ImageLayout&, std::size_t scratchBytes,PreparedCpuExecutor&);
    virtual core::Result<void> before(ProcessingOperation,std::span<std::byte>) { return core::Result<void>::success(); }
};
struct FrameEngineTestAccess final {
    static core::Result<std::unique_ptr<FrameProcessingEngine>> create(
        core::BufferPool&,core::BufferPool&,const core::ImageLayout&,const PipelineDefinition&,
        ProcessingPreparationOptions,std::shared_ptr<EngineHooks>);
    using CpuWork = void (*)(void*,std::size_t slot,std::size_t begin,std::size_t end) noexcept;
    static void runCpu(FrameProcessingEngine&,std::size_t itemCount,void* context,CpuWork) noexcept;
    static void setCpuWorkObserver(FrameProcessingEngine&,void* context,CpuWorkObserver) noexcept;
    static std::weak_ptr<const IProcessingStage> stage(FrameProcessingEngine&,StageId);
};
}
