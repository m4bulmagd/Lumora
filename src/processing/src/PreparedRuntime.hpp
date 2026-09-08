#pragma once
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <lumora/processing/IProcessingStage.hpp>
#include "PreparedOwner.hpp"
#include "FrameEngineTestAccess.hpp"
#include <array>
#include <mutex>
namespace lumora::processing::detail {
using StageHandle = std::shared_ptr<const IProcessingStage>;
struct PreparedDefinition final {
    core::PipelineVersion version{1,1,0};
    std::array<StageDefinition,8> stages{};
    std::array<std::size_t,8> scratchBytes{};
    std::array<std::size_t,8> stageBytes{};
    std::size_t count{};
    WindowLevelParameters window{};
    bool enhancedWindow{};
    std::size_t requiredBytes{};
};
struct PreparedRuntime final {
    PreparedDefinition definition;
    std::array<StageHandle,8> stages{};
    std::uint64_t serial{};
};
struct EnginePlan final {
    core::ImageLayout sourceLayout;
    core::ImageLayout canonicalLayout;
    core::ImageLayout nativeDisplayLayout;
    core::ImageLayout displayLayout;
    core::FrameObjectPoolPlan objects;
    ProcessingPoolSpecification processingPool;
    ProcessingPoolSpecification displayPool;
    ProcessingPreparationOptions options;
    ProcessingResources resources;
    PreparedDefinition initial;
};
struct EngineState final {
    EngineState(core::BufferPool& processingPool, core::BufferPool& displayPool,
        std::shared_ptr<const EnginePlan> planValue) : plan(std::move(planValue)), workspace(processingPool,displayPool) {}
    std::shared_ptr<const EnginePlan> plan;
    ProcessingWorkspace workspace;
    std::shared_ptr<core::FrameObjectPool> objects;
    std::mutex preparationMutex;
    mutable std::mutex stateMutex;
    std::shared_ptr<const PreparedRuntime> active;
    std::shared_ptr<const PreparedRuntime> inFlight;
    StageHandle gammaCache;
    double gammaValue{};
    std::shared_ptr<EngineHooks> hooks;
    ProcessorStatus processorStatus;
    std::atomic<bool> retryPending{false};
    std::uint64_t serial{};
};
core::Error preparationError(std::string code, std::string detail);
PipelineValidationError validationFailure(core::Error error);
core::Result<PreparedDefinition,PipelineValidationError> prepareDefinition(const PipelineDefinition&, const core::ImageLayout&);
core::Result<StageHandle> makeStage(const StageDefinition&,const core::ImageLayout&,std::size_t scratchBytes);
bool recordEnhancementFailure(EngineState&,ProcessingOperation,const core::Error&);
void recordEnhancedSuccess(EngineState&);
bool sameStage(const StageDefinition&,const StageDefinition&) noexcept;
core::Result<void,PipelineValidationError> activatePrepared(EngineState&,PreparedDefinition);
}
