#include "PreparedRuntime.hpp"
#include <array>
#include <exception>
namespace lumora::processing {
FrameProcessingEngine::FrameProcessingEngine(core::BufferPool& processingPool,core::BufferPool& displayPool,
    std::shared_ptr<const detail::EnginePlan> plan) : state_(std::make_unique<detail::EngineState>(processingPool,displayPool,std::move(plan))) {}
FrameProcessingEngine::~FrameProcessingEngine()=default;
core::Result<std::unique_ptr<FrameProcessingEngine>> FrameProcessingEngine::create(core::BufferPool& processingPool,
    core::BufferPool& displayPool,const core::ImageLayout& layout,const PipelineDefinition& definition,ProcessingPreparationOptions options) {
    using Result=core::Result<std::unique_ptr<FrameProcessingEngine>>;
    const auto p=processingPool.stats(); const auto d=displayPool.stats();
    auto assessment=plan({p.capacity,p.bytesPerBuffer},{d.capacity,d.bytesPerBuffer},layout,definition,options);
    if(assessment.error) return Result::failure(std::move(*assessment.error));
    return create(processingPool,displayPool,*assessment.plan);
}
core::Result<std::unique_ptr<FrameProcessingEngine>> FrameProcessingEngine::create(core::BufferPool& processingPool,
    core::BufferPool& displayPool,const ProcessingPreparationPlan& plan) {
    return createPrepared(processingPool,displayPool,plan,{});
}
core::Result<std::unique_ptr<FrameProcessingEngine>> FrameProcessingEngine::createPrepared(core::BufferPool& processingPool,
    core::BufferPool& displayPool,const ProcessingPreparationPlan& plan,std::shared_ptr<detail::EngineHooks> hooks) {
    using Result=core::Result<std::unique_ptr<FrameProcessingEngine>>;
    try {
        const auto p=processingPool.stats(); const auto d=displayPool.stats();
        if(!plan.impl_ || p.capacity!=plan.impl_->processingPool.capacity || p.bytesPerBuffer!=plan.impl_->processingPool.bytesPerBuffer
            || d.capacity!=plan.impl_->displayPool.capacity || d.bytesPerBuffer!=plan.impl_->displayPool.bytesPerBuffer)
            return Result::failure(detail::preparationError("processing_pool_plan_mismatch","Supplied pools differ from the admitted plan."));
        auto engine=std::unique_ptr<FrameProcessingEngine>(new FrameProcessingEngine(processingPool,displayPool,plan.impl_));
        engine->state_->hooks=std::move(hooks);
        detail::CpuExecutorTestHooks cpuHooks;
        cpuHooks.context=engine->state_->hooks.get();
        cpuHooks.beforeThreadStart=[](void* context,std::size_t slot) {
            if(context) static_cast<detail::EngineHooks*>(context)->beforeCpuThreadStart(slot);
        };
        engine->state_->cpuExecutor=std::make_unique<detail::PreparedCpuExecutor>(plan.impl_->options.cpuExecutionSlots,cpuHooks);
        auto objects=core::FrameObjectPool::create(plan.impl_->objects);
        if(!objects.hasValue()) return Result::failure(std::move(objects).error());
        engine->state_->objects=std::move(objects).value();
        auto active=detail::activatePrepared(*engine->state_,plan.impl_->initial);
        if(!active.hasValue()) return Result::failure(*active.error().preparationError);
        auto ready=engine->state_->workspace.prepare(plan.impl_->sourceLayout,plan.impl_->options.orientation,plan.impl_->resources.orientationBytes);
        if(!ready.hasValue()) return Result::failure(std::move(ready).error());
        return Result::success(std::move(engine));
    } catch(const detail::CpuExecutorStartupError&) {
        return Result::failure(detail::preparationError("processing_cpu_executor_startup_failed","Persistent CPU helpers could not start."));
    } catch(...) {
        return Result::failure(detail::preparationError("processing_preparation_allocation_failed","Prepared owner allocation failed."));
    }
}
core::Result<void,PipelineValidationError> FrameProcessingEngine::activate(const PipelineDefinition& definition) {
    using Result=core::Result<void,PipelineValidationError>;
    std::lock_guard preparation(state_->preparationMutex);
    std::optional<ProcessingResources> candidateResources;
    try {
        auto prepared=detail::prepareDefinition(definition,state_->plan->canonicalLayout);
        if(!prepared.hasValue()) return Result::failure(std::move(prepared).error());
        candidateResources=state_->plan->resources;
        candidateResources->candidateRequiredBytes=prepared.value().requiredBytes;
        auto activated=detail::activatePrepared(*state_,std::move(prepared).value());
        if(!activated.hasValue()) activated.error().preparationResources=candidateResources;
        return activated;
    } catch(...) {
        auto error=detail::validationFailure(detail::preparationError("processing_preparation_allocation_failed","Candidate preparation failed."));
        error.preparationResources=candidateResources;
        return Result::failure(std::move(error));
    }
}
ProcessingResources FrameProcessingEngine::resources() const noexcept {
    auto result=state_->plan->resources;
    std::lock_guard lock(state_->stateMutex);
    std::array<const IProcessingStage*,17> owners{};
    std::size_t count{};
    auto add=[&](const detail::StageHandle& stage,std::size_t bytes) {
        if(!stage) return;
        for(std::size_t i=0;i<count;++i) if(owners[i]==stage.get()) return;
        owners[count++]=stage.get(); result.actualRetainedStageBytes+=bytes;
    };
    for(const auto& runtime:{state_->active,state_->inFlight}) if(runtime) {
        for(std::size_t i=0;i<runtime->definition.count;++i) add(runtime->stages[i],runtime->definition.stageBytes[i]);
    }
    add(state_->gammaCache,result.gammaCacheReserveBytes);
    if(state_->active) result.candidateRequiredBytes=state_->active->definition.requiredBytes;
    return result;
}
}
