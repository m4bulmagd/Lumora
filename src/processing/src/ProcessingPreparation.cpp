#include "PreparedRuntime.hpp"
#include <lumora/core/CheckedMath.hpp>
#include <lumora/processing/OrientationTransform.hpp>
#include <lumora/processing/ToneStages.hpp>
#include <limits>
namespace lumora::processing {
namespace {
core::Result<core::ImageLayout> tight(const core::ImageLayout& source,core::StorageType storage,std::size_t sampleBytes) {
    auto stride=core::checkedMultiply(source.width(),sampleBytes);
    if(!stride.hasValue()) return core::Result<core::ImageLayout>::failure(stride.error());
    auto bytes=core::checkedMultiply(stride.value(),source.height());
    if(!bytes.hasValue()) return core::Result<core::ImageLayout>::failure(bytes.error());
    return core::ImageLayout::create(source.width(),source.height(),stride.value(),storage,bytes.value());
}
}
const ProcessingResources& ProcessingPreparationPlan::resources() const noexcept { return impl_->resources; }
ProcessingPreparationAssessment FrameProcessingEngine::plan(ProcessingPoolSpecification processingPool,
    ProcessingPoolSpecification displayPool,const core::ImageLayout& sourceLayout,
    const PipelineDefinition& definition,ProcessingPreparationOptions options) {
    ProcessingPreparationAssessment result;
    auto& r=result.resources;
    r.storageBudgetBytes=options.storageBudgetBytes;
    r.externalSessionBytes=options.externalSessionStorageBytes;
    auto fail=[&](core::Error error) { result.error=std::move(error); return result; };
    auto overflow=[&] { return fail(detail::preparationError("processing_resource_size_overflow","Complete session storage is not representable.")); };
    try {
        if(options.cpuExecutionSlots<1 || options.cpuExecutionSlots>4)
            return fail(detail::preparationError("invalid_cpu_execution_slots","CPU execution slots must be in [1,4]."));
        r.cpuExecutionSlots=options.cpuExecutionSlots;
        r.cpuHelperThreads=options.cpuExecutionSlots-1;
        r.cpuExecutorBytes=sizeof(detail::PreparedCpuExecutor);
        auto canonical=tight(sourceLayout,core::StorageType::UInt16,2);
        auto native=tight(sourceLayout,core::StorageType::UInt8,1);
        if(!canonical.hasValue() || !native.hasValue()) return overflow();
        auto display=OrientationTransform::outputLayout(native.value(),core::DisplayStorage::Gray8,options.orientation);
        if(!display.hasValue()) return fail(display.error());
        if(processingPool.bytesPerBuffer<canonical.value().payloadBytes()) return fail(detail::preparationError("processing_buffer_too_small","U16 blocks do not fit the prepared layout."));
        if(displayPool.bytesPerBuffer<display.value().payloadBytes()) return fail(detail::preparationError("display_buffer_too_small","Gray8 blocks do not fit the prepared layout."));
        auto pixels=core::BufferPool::plan(processingPool.capacity,processingPool.bytesPerBuffer);
        auto displays=core::BufferPool::plan(displayPool.capacity,displayPool.bytesPerBuffer);
        if(!pixels.hasValue()) return fail(pixels.error());
        if(!displays.hasValue()) return fail(displays.error());
        r.processingPoolBytes=pixels.value().requiredStorageBytes;
        r.displayPoolBytes=displays.value().requiredStorageBytes;
        auto candidate=detail::prepareDefinition(definition,canonical.value());
        if(!candidate.hasValue()) {
            const auto& e=candidate.error();
            return fail(e.preparationError ? *e.preparationError : core::Error{core::ErrorCategory::Processing,e.code,"Pipeline validation failed.",e.violations.front().detail,false});
        }
        r.candidateRequiredBytes=candidate.value().requiredBytes;
        auto controls=core::checkedMultiply(processingPool.capacity,2);
        if(controls.hasValue()) controls=core::checkedAdd(controls.value(),displayPool.capacity);
        if(!controls.hasValue()) return overflow();
        if(controls.value()<4) return fail(detail::preparationError("invalid_frame_object_pool_capacity","Enhanced publication requires four controls."));
        auto objects=core::FrameObjectPool::plan({processingPool.capacity,displayPool.capacity,processingPool.capacity,controls.value()});
        if(!objects.hasValue()) return fail(objects.error());
        r.frameObjectBytes=objects.value().requiredStorageBytes();
        const core::Orientation identity{false,false,core::Rotation::Degrees0};
        if(options.orientation!=identity) {
            const auto bytes=native.value().payloadBytes();
            const auto alignment=alignof(std::max_align_t);
            auto aligned=core::checkedAdd(bytes,(alignment-bytes%alignment)%alignment);
            if(!aligned.hasValue()) return overflow();
            r.orientationBytes=aligned.value();
        }
        r.engineStateBytes=sizeof(FrameProcessingEngine)+sizeof(detail::EngineState)+sizeof(detail::EnginePlan)+detail::ownerControlReserve;
        for(auto bytes:{r.externalSessionBytes,r.processingPoolBytes,r.displayPoolBytes,r.frameObjectBytes,r.orientationBytes,r.engineStateBytes,r.cpuExecutorBytes}) {
            auto sum=core::checkedAdd(r.fixedStorageBytes,bytes);
            if(!sum.hasValue()) return overflow();
            r.fixedStorageBytes=sum.value();
        }
        r.gammaCacheReserveBytes=sizeof(GammaStage)+detail::ownerControlReserve;
        auto fixed=core::checkedAdd(r.fixedStorageBytes,r.gammaCacheReserveBytes);
        if(!fixed.hasValue()) return overflow();
        if(options.activationEnvelopeBytes) r.activationEnvelopeBytes=*options.activationEnvelopeBytes;
        else if(fixed.value()<=r.storageBudgetBytes) r.activationEnvelopeBytes=(r.storageBudgetBytes-fixed.value())/3;
        auto reserve=core::checkedMultiply(r.activationEnvelopeBytes,3);
        if(!reserve.hasValue()) return overflow();
        r.activationReserveBytes=reserve.value();
        auto total=core::checkedAdd(fixed.value(),r.activationReserveBytes);
        if(!total.hasValue()) return overflow();
        r.requiredStorageBytes=total.value();
        if(r.requiredStorageBytes>r.storageBudgetBytes || r.candidateRequiredBytes>r.activationEnvelopeBytes)
            return fail(detail::preparationError("processing_resource_budget_exceeded","Session reserve or candidate requirement exceeds the preparation budget/envelope."));
        auto prepared=detail::makePreparedOwner<detail::EnginePlan>(detail::EnginePlan{sourceLayout,canonical.value(),native.value(),display.value(),objects.value(),processingPool,displayPool,options,r,std::move(candidate).value()});
        result.plan=ProcessingPreparationPlan(std::move(prepared));
        return result;
    } catch(...) {
        return fail(detail::preparationError("processing_preparation_allocation_failed","Preparation could not allocate bounded owner storage."));
    }
}
}
