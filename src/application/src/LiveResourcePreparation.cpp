#include "LiveResourcePreparation.hpp"
#include <lumora/core/CheckedMath.hpp>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <cmath>
namespace lumora::application::detail {
LiveResourcePreparation prepareLiveResources(const camera::CameraConfiguration& request,
    processing::ProcessingPreparationOptions options,bool customFactory,
    const processing::PipelineDefinition& definition) {
    LiveResourcePreparation result;
    result.resources.storageBudgetBytes=options.storageBudgetBytes;
    result.resources.customProcessorStorageUnknown=customFactory;
    auto fail=[&](core::Error error) { result.error=std::move(error); return result; };
    auto format=core::validateSourcePixelFormat(request.pixelFormat);
    if(!format.hasValue()) return fail(format.error());
    if(request.roi.width==0 || request.roi.height==0 || !request.requestedFps
        || !std::isfinite(*request.requestedFps) || *request.requestedFps<=0
        || request.acquisitionMode!=camera::AcquisitionMode::Continuous)
        return fail({core::ErrorCategory::CameraConfiguration,"unsupported_pipeline_mode","Live pipeline operation failed.","The pipeline requires a positive-rate continuous native mode.",false});
    auto pixels=core::checkedMultiply(request.roi.width,request.roi.height);
    auto stride=core::checkedMultiply(request.roi.width,request.pixelFormat.applicationStorage==core::StorageType::UInt8 ? 1U : 2U);
    if(!pixels.hasValue()) return fail(pixels.error());
    if(!stride.hasValue()) return fail(stride.error());
    auto raw=core::checkedMultiply(stride.value(),request.roi.height);
    auto canonical=core::checkedMultiply(pixels.value(),2);
    if(!raw.hasValue()) return fail(raw.error());
    if(!canonical.hasValue()) return fail(canonical.error());
    auto layout=core::ImageLayout::create(request.roi.width,request.roi.height,stride.value(),request.pixelFormat.applicationStorage,raw.value());
    if(!layout.hasValue()) return fail(layout.error());
    result.sourceLayout=layout.value(); result.rawBytes=raw.value(); result.canonicalBytes=canonical.value(); result.displayBytes=pixels.value();
    auto rawPool=core::BufferPool::plan(10,result.rawBytes);
    if(!rawPool.hasValue()) return fail(rawPool.error());
    auto external=core::checkedAdd(options.externalSessionStorageBytes,rawPool.value().requiredStorageBytes);
    if(!external.hasValue()) return fail(external.error());
    options.externalSessionStorageBytes=external.value();
    if(!customFactory) {
        auto assessment=processing::FrameProcessingEngine::plan({9,result.canonicalBytes},{16,result.displayBytes},layout.value(),definition,options);
        result.resources=assessment.resources;
        result.enginePlan=std::move(assessment.plan);
        result.error=std::move(assessment.error);
        return result;
    }
    // Legacy factories have no storage-planning contract. Admit the known pools,
    // preserve factory compatibility, and explicitly leave private storage unknown.
    auto processingPool=core::BufferPool::plan(9,result.canonicalBytes);
    auto displayPool=core::BufferPool::plan(16,result.displayBytes);
    if(!processingPool.hasValue()) return fail(processingPool.error());
    if(!displayPool.hasValue()) return fail(displayPool.error());
    auto& r=result.resources;
    r.externalSessionBytes=external.value();
    r.processingPoolBytes=processingPool.value().requiredStorageBytes;
    r.displayPoolBytes=displayPool.value().requiredStorageBytes;
    auto total=core::checkedAdd(r.externalSessionBytes,r.processingPoolBytes);
    if(total.hasValue()) total=core::checkedAdd(total.value(),r.displayPoolBytes);
    if(!total.hasValue()) return fail(total.error());
    r.requiredStorageBytes=total.value(); r.fixedStorageBytes=total.value();
    if(r.requiredStorageBytes>r.storageBudgetBytes)
        return fail({core::ErrorCategory::ResourceExhaustion,"processing_resource_budget_exceeded","Processing resources could not be prepared.","Known session pools exceed the selected storage budget; custom processor storage is unknown.",true});
    return result;
}

PreparedLiveSession::~PreparedLiveSession() {
    if(worker) { worker->requestStop(); worker->join(); }
}
core::Result<std::unique_ptr<PreparedLiveSession>> prepareLiveSession(
    const LiveResourcePreparation& plan, std::uint64_t generation,
    const LivePipeline::ProcessorFactory& factory,
    const std::optional<processing::PipelineDefinition>& acceptedDefinition) {
    using Result = core::Result<std::unique_ptr<PreparedLiveSession>>;
    try {
        auto session=std::make_unique<PreparedLiveSession>();
        session->context=std::make_shared<LiveSessionContext>();
        session->context->generation=generation;
        auto raw=core::BufferPool::create(10U,plan.rawBytes);
        if(!raw.hasValue()) return Result::failure(raw.error());
        session->context->rawPool=std::move(raw.value());
        auto canonical=core::BufferPool::create(9U,plan.canonicalBytes);
        if(!canonical.hasValue()) return Result::failure(canonical.error());
        session->context->processingPool=std::move(canonical.value());
        auto display=core::BufferPool::create(16U,plan.displayBytes);
        if(!display.hasValue()) return Result::failure(display.error());
        session->context->displayPool=std::move(display.value());
        if(factory) {
            auto made=factory(*session->context->processingPool,*session->context->displayPool,*plan.sourceLayout);
            if(!made.hasValue()) return Result::failure(made.error());
            session->processor=std::move(made.value());
        } else {
            auto made=processing::FrameProcessingEngine::create(*session->context->processingPool,
                *session->context->displayPool,*plan.enginePlan);
            if(!made.hasValue()) return Result::failure(made.error());
            session->processor=std::move(made.value());
        }
        if(!session->processor) return Result::failure({core::ErrorCategory::Processing,
            "processor_required","Processor factory returned null.","",false});
        // Built-in plans already contain the accepted definition. Legacy custom
        // factories must explicitly accept it before any camera mutation.
        if(factory && acceptedDefinition) {
            auto activated=session->processor->activate(*acceptedDefinition);
            if(!activated.hasValue()) return Result::failure({core::ErrorCategory::Processing,
                activated.error().code,"Processing settings could not be restored.","",true});
        }
        session->worker=std::make_unique<ProcessingWorker>(session->context->rawSlot,
            session->context->bundleSlot,*session->processor);
        auto started=session->worker->start();
        if(!started.hasValue()) return Result::failure(started.error());
        return Result::success(std::move(session));
    } catch(const std::exception& exception) {
        return Result::failure({core::ErrorCategory::Internal,"pipeline_exception",
            "Live resources could not be prepared.",exception.what(),true});
    } catch(...) {
        return Result::failure({core::ErrorCategory::Internal,"pipeline_exception",
            "Live resources could not be prepared.","Unknown preparation failure.",true});
    }
}
}
