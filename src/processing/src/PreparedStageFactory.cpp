#include "PreparedRuntime.hpp"
#include "StageStorage.hpp"
#include <lumora/core/CheckedMath.hpp>
#include <lumora/processing/ClaheStage.hpp>
#include <lumora/processing/DenoiseStage.hpp>
#include <lumora/processing/SharpenStage.hpp>
#include <lumora/processing/ToneStages.hpp>
#include <type_traits>
#include <limits>
#include <stdexcept>
namespace lumora::processing::detail {
core::Error preparationError(std::string code,std::string detail) {
    return {core::ErrorCategory::ResourceExhaustion,std::move(code),"Processing resources could not be prepared.",std::move(detail),true};
}
PipelineValidationError validationFailure(core::Error error) {
    PipelineValidationError result{error.code,{{std::nullopt,error.code,error.diagnosticDetail}},error};
    return result;
}
namespace {
// StageId is the stable canonical identity; retain its diagnostic name as context.
std::string stageExceptionDiagnostic(StageId id,std::string_view detail) {
    return "stage_id="+std::to_string(static_cast<int>(id))+" ("+std::string(stageName(id))+"): "+std::string(detail);
}
core::Result<std::size_t> stageBytes(const StageDefinition& stage,const core::ImageLayout& layout,std::size_t& scratch,std::size_t executionSlots) {
    std::size_t owner{};
    auto arrays = core::Result<std::size_t>::success(0);
    switch(stage.id) {
    case StageId::BrightnessContrast: owner=sizeof(BrightnessContrastStage); break;
    case StageId::Gamma: owner=sizeof(GammaStage); break;
    case StageId::Clahe: owner=StageStorage::claheOwnerBytes(); arrays=StageStorage::claheScratchBytes(std::get<ClaheParameters>(stage.parameters),layout,executionSlots); break;
    case StageId::Denoise: owner=StageStorage::denoiseOwnerBytes(); arrays=StageStorage::denoiseScratchBytes(std::get<DenoiseParameters>(stage.parameters),layout,executionSlots); break;
    case StageId::Sharpen: owner=StageStorage::sharpenOwnerBytes(); arrays=StageStorage::sharpenScratchBytes(std::get<SharpenParameters>(stage.parameters),layout,executionSlots); break;
    case StageId::Invert: owner=sizeof(InvertStage); break;
    default: return core::Result<std::size_t>::success(0);
    }
    if(!arrays.hasValue()) return arrays;
    scratch=arrays.value();
    auto sum=core::checkedAdd(owner+ownerControlReserve,scratch);
    if(!sum.hasValue()) return core::Result<std::size_t>::failure(preparationError("processing_resource_size_overflow","Stage storage overflows size_t."));
    return sum;
}
template<class T> core::Result<StageHandle> adopt(core::Result<std::unique_ptr<T>> made,std::size_t expected) {
    if(!made.hasValue()) return core::Result<StageHandle>::failure(std::move(made).error());
    if(made.value()->scratchBytes()!=expected) return core::Result<StageHandle>::failure(preparationError("processing_stage_storage_mismatch","Prepared arrays differ from admission."));
    auto pointer=std::move(made).value();
    return core::Result<StageHandle>::success(StageHandle(pointer.release(),std::default_delete<T>{},PreparedOwnerAllocator<std::byte>{ownerControlReserve}));
}
}
core::Result<PreparedDefinition,PipelineValidationError> prepareDefinition(const PipelineDefinition& definition,const core::ImageLayout& layout,std::size_t executionSlots) {
    using Result=core::Result<PreparedDefinition,PipelineValidationError>;
    auto compiled=PipelineCompiler(stageRegistry()).compile(definition);
    if(!compiled.hasValue()) return Result::failure(std::move(compiled).error());
    PreparedDefinition result;
    result.version=definition.version;
    result.requiredBytes=sizeof(PreparedRuntime)+ownerControlReserve;
    for(const auto& stage:compiled.value().definition().stages) {
        const auto index=result.count++;
        result.stages[index]=stage;
        if(stage.id==StageId::WindowLevel) { result.window=std::get<WindowLevelParameters>(stage.parameters); result.enhancedWindow=stage.enabled; }
        if(!stage.enabled) continue;
        auto bytes=stageBytes(stage,layout,result.scratchBytes[index],executionSlots);
        if(!bytes.hasValue()) return Result::failure(validationFailure(std::move(bytes).error()));
        result.stageBytes[index]=bytes.value();
        auto total=core::checkedAdd(result.requiredBytes,bytes.value());
        if(!total.hasValue()) return Result::failure(validationFailure(preparationError("processing_resource_size_overflow","Candidate storage overflows size_t.")));
        result.requiredBytes=total.value();
    }
    return Result::success(std::move(result));
}
core::Result<StageHandle> makeStage(const StageDefinition& stage,const core::ImageLayout& layout,std::size_t scratch,PreparedCpuExecutor& executor) {
    switch(stage.id) {
    case StageId::BrightnessContrast: return core::Result<StageHandle>::success(makePreparedOwner<BrightnessContrastStage>(std::get<BrightnessContrastParameters>(stage.parameters)));
    case StageId::Gamma: return core::Result<StageHandle>::success(makePreparedOwner<GammaStage>(std::get<GammaParameters>(stage.parameters)));
    case StageId::Clahe: return adopt(StageStorage::createClahe(std::get<ClaheParameters>(stage.parameters),layout,scratch,&executor),scratch);
    case StageId::Denoise: return adopt(StageStorage::createDenoise(std::get<DenoiseParameters>(stage.parameters),layout,scratch,&executor),scratch);
    case StageId::Sharpen: return adopt(StageStorage::createSharpen(std::get<SharpenParameters>(stage.parameters),layout,scratch,&executor),scratch);
    case StageId::Invert: return core::Result<StageHandle>::success(makePreparedOwner<InvertStage>());
    default: return core::Result<StageHandle>::success({});
    }
}
bool sameStage(const StageDefinition& a,const StageDefinition& b) noexcept {
    if(a.id!=b.id || a.parameters.index()!=b.parameters.index()) return false;
    return std::visit([&](const auto& x) {
        using T=std::decay_t<decltype(x)>;
        const auto& y=std::get<T>(b.parameters);
        if constexpr(std::is_same_v<T,BrightnessContrastParameters>) return x.brightness==y.brightness && x.contrast==y.contrast;
        else if constexpr(std::is_same_v<T,GammaParameters>) return x.gamma==y.gamma;
        else if constexpr(std::is_same_v<T,ClaheParameters>) return x.clipLimit==y.clipLimit && x.tileGridSize==y.tileGridSize;
        else if constexpr(std::is_same_v<T,DenoiseParameters>) return x.mode==y.mode && x.kernelSize==y.kernelSize && x.sigma==y.sigma;
        else if constexpr(std::is_same_v<T,SharpenParameters>) return x.amount==y.amount && x.radius==y.radius && x.threshold==y.threshold;
        else if constexpr(std::is_same_v<T,WindowLevelParameters>) return x.window==y.window && x.level==y.level;
        else return true;
    },a.parameters);
}
core::Result<void,PipelineValidationError> activatePrepared(EngineState& state,PreparedDefinition definition) {
    using Result=core::Result<void,PipelineValidationError>;
    if(definition.requiredBytes>state.plan->resources.activationEnvelopeBytes)
        return Result::failure(validationFailure(preparationError("processing_resource_budget_exceeded","Candidate exceeds the stopped preparation envelope.")));
    std::shared_ptr<const PreparedRuntime> previous;
    { std::lock_guard lock(state.stateMutex); previous=state.active; }
    auto next=makePreparedOwner<PreparedRuntime>();
    next->definition=std::move(definition);
    StageHandle gamma;
    double gammaValue{};
    for(std::size_t i=0;i<next->definition.count;++i) {
        const auto& stage=next->definition.stages[i];
        if(!stage.enabled || next->definition.stageBytes[i]==0) continue;
        if(stage.id==StageId::Gamma && state.gammaCache && state.gammaValue==std::get<GammaParameters>(stage.parameters).gamma)
            next->stages[i]=state.gammaCache;
        if(!next->stages[i] && previous) {
            for(std::size_t j=0;j<previous->definition.count;++j)
                if(previous->stages[j] && sameStage(stage,previous->definition.stages[j])) next->stages[i]=previous->stages[j];
        }
        if(!next->stages[i]) {
            auto prepareOne=[&]() -> core::Result<StageHandle> {
                try {
                    return state.hooks ? state.hooks->prepare(stage,state.plan->canonicalLayout,next->definition.scratchBytes[i],*state.cpuExecutor)
                        : makeStage(stage,state.plan->canonicalLayout,next->definition.scratchBytes[i],*state.cpuExecutor);
                } catch(const std::bad_alloc& error) {
                    return core::Result<StageHandle>::failure(preparationError("processing_preparation_allocation_failed",stageExceptionDiagnostic(stage.id,error.what())));
                } catch(const std::length_error& error) {
                    return core::Result<StageHandle>::failure(preparationError("processing_preparation_allocation_failed",stageExceptionDiagnostic(stage.id,error.what())));
                } catch(const std::exception& error) {
                    return core::Result<StageHandle>::failure({core::ErrorCategory::Processing,"processing_stage_prepare_failed","Stage preparation failed.",stageExceptionDiagnostic(stage.id,error.what()),false});
                } catch(...) {
                    return core::Result<StageHandle>::failure({core::ErrorCategory::Processing,"processing_stage_prepare_failed","Stage preparation failed.",stageExceptionDiagnostic(stage.id,"Unknown backend construction exception."),false});
                }
            };
            auto made=prepareOne();
            if(!made.hasValue()) return Result::failure(validationFailure(std::move(made).error()));
            next->stages[i]=std::move(made).value();
        }
        if(stage.id==StageId::Gamma) { gamma=next->stages[i]; gammaValue=std::get<GammaParameters>(stage.parameters).gamma; }
    }
    if(state.serial==std::numeric_limits<std::uint64_t>::max())
        return Result::failure(validationFailure(preparationError("processing_activation_serial_exhausted","Stopped preparation is required after activation serial exhaustion.")));
    next->serial=++state.serial;
    std::shared_ptr<const PreparedRuntime> retired;
    StageHandle retiredGamma;
    { std::lock_guard lock(state.stateMutex);
        retired=std::move(state.active); state.active=std::move(next);
        if(gamma) { retiredGamma=std::move(state.gammaCache); state.gammaCache=std::move(gamma); state.gammaValue=gammaValue; }
    }
    return Result::success();
}
}
