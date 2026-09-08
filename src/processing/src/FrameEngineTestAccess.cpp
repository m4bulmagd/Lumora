#include "PreparedRuntime.hpp"
namespace lumora::processing::detail {
core::Result<std::shared_ptr<const IProcessingStage>> EngineHooks::prepare(const StageDefinition& stage,const core::ImageLayout& layout,std::size_t scratch) {
    return makeStage(stage,layout,scratch);
}
core::Result<std::unique_ptr<FrameProcessingEngine>> FrameEngineTestAccess::create(core::BufferPool& p,core::BufferPool& d,
    const core::ImageLayout& layout,const PipelineDefinition& definition,ProcessingPreparationOptions options,std::shared_ptr<EngineHooks> hooks) {
    auto ps=p.stats(); auto ds=d.stats();
    auto assessment=FrameProcessingEngine::plan({ps.capacity,ps.bytesPerBuffer},{ds.capacity,ds.bytesPerBuffer},layout,definition,options);
    if(assessment.error) return core::Result<std::unique_ptr<FrameProcessingEngine>>::failure(*assessment.error);
    return FrameProcessingEngine::createPrepared(p,d,*assessment.plan,std::move(hooks));
}
void FrameEngineTestAccess::runCpu(FrameProcessingEngine& engine,std::size_t n,void* c,CpuWork w) noexcept { engine.state_->cpuExecutor->run(n,c,w); }
std::weak_ptr<const IProcessingStage> FrameEngineTestAccess::stage(FrameProcessingEngine& engine,StageId id) {
    std::lock_guard lock(engine.state_->stateMutex);
    const auto& active=engine.state_->active;
    for(std::size_t i=0;i<active->definition.count;++i) if(active->definition.stages[i].id==id) return active->stages[i];
    return {};
}
}
