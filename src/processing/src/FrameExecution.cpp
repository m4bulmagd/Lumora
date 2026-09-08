#include "PreparedRuntime.hpp"
#include <lumora/processing/DisplayMapper.hpp>
#include <lumora/processing/NormalizeStage.hpp>
#include <lumora/processing/OrientationTransform.hpp>
#include <lumora/processing/WindowLevelStage.hpp>
#include <chrono>
#include <cmath>
#include <limits>
#include <new>
namespace lumora::processing {
namespace {
using BundleResult=core::Result<std::shared_ptr<const core::FrameBundle>>;
using Clock=std::chrono::steady_clock;
constexpr std::array<std::string_view,8> stageTimingNames{
    "normalize","window_level","brightness_contrast","gamma","clahe","denoise","sharpen","invert"};
core::Error processingError(std::string code,std::string detail) {
    return {core::ErrorCategory::Processing,std::move(code),"The frame could not be processed.",std::move(detail),false};
}
// Called only when translating an exception; successful dispatch keeps static IDs.
std::string operationDiagnostic(std::string_view id,std::string_view detail) {
    return "operation="+std::string(id)+": "+std::string(detail);
}
core::SharedBuffer seal(std::optional<core::WritableBufferLease>& lease) {
    auto buffer=std::move(*lease).seal(); lease.reset(); return buffer;
}
struct Flight final {
    detail::EngineState& state;
    std::shared_ptr<const detail::PreparedRuntime> runtime;
    bool latched{};
    explicit Flight(detail::EngineState& owner) : state(owner) {
        std::shared_ptr<const core::Error> released;
        {
            std::lock_guard lock(state.stateMutex);
            runtime=state.active; state.inFlight=runtime;
            auto& status=state.processorStatus;
            const bool retry=state.retryPending.exchange(false,std::memory_order_acq_rel);
            if(status.activationSerial!=runtime->serial || retry) {
                status.activationSerial=runtime->serial;
                status.configurationRevision=runtime->definition.version.configurationRevision;
                status.mode=ProcessorMode::Enhanced;
                status.consecutiveEnhancementFailures=0;
                status.failingOperation.reset(); released=std::move(status.error);
                if(retry && status.retriesConsumed<std::numeric_limits<std::uint64_t>::max()) ++status.retriesConsumed;
            }
            latched=status.mode==ProcessorMode::OriginalOnlyLatched;
        }
    }
    ~Flight() {
        std::shared_ptr<const detail::PreparedRuntime> released;
        { std::lock_guard lock(state.stateMutex); released=std::move(state.inFlight); }
    }
};
}
BundleResult FrameProcessingEngine::process(std::shared_ptr<const core::RawFrame> raw) {
    const auto started=Clock::now();
    auto& workspace=state_->workspace;
    const auto& plan=*state_->plan;
    const auto& planned=plan.sourceLayout;
    if(!raw || raw->layout.width()!=planned.width() || raw->layout.height()!=planned.height() || raw->layout.storage()!=planned.storage())
        return BundleResult::failure(processingError("processing_source_layout_mismatch","A source dimension or storage change requires stopped preparation."));
    const auto& acquisition=raw->metadata.acquisitionSettings;
    if(!std::isfinite(acquisition.actualFps) || acquisition.actualFps<=0)
        return BundleResult::failure(processingError("processing_metadata_invalid","Actual acquisition FPS must be positive and finite."));
    auto source=ImageView::create(raw->layout,raw->pixels.bytes(),ImageDomain::SensorNative);
    if(!source.hasValue()) return BundleResult::failure(source.error());
    Flight flight(*state_);
    const auto& runtime=*flight.runtime;
    const auto& definition=runtime.definition;
    auto ready=workspace.replenish(!flight.latched);
    if(!ready.hasValue()) return BundleResult::failure(ready.error());
    core::ProcessingTimings timings;
    ProcessingOperation failedOperation{ProcessingOperation::Normalize};
    auto timed=[&](ProcessingOperation operationId,std::string_view id,std::span<std::byte> destination,auto&& operation) {
        using Result=decltype(operation());
        const auto before=Clock::now();
        auto invoke=[&]() -> Result {
            try {
                if(state_->hooks) {
                    auto intercepted=state_->hooks->before(operationId,destination);
                    if(!intercepted.hasValue()) return Result::failure(std::move(intercepted).error());
                }
                return operation();
            } catch(const std::bad_alloc& error) {
                return Result::failure(detail::preparationError("processing_operation_allocation_failed",operationDiagnostic(id,error.what())));
            } catch(const std::exception& error) {
                return Result::failure(processingError("processing_operation_exception",operationDiagnostic(id,error.what())));
            } catch(...) {
                return Result::failure(processingError("processing_operation_exception",operationDiagnostic(id,"Unknown stage exception.")));
            }
        };
        auto result=invoke();
        if(!result.hasValue()) failedOperation=operationId;
        auto recorded=timings.stages.append(id,std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-before));
        if(!recorded.hasValue()) return Result::failure(processingError("processing_timing_append_failed","The executed timing record exceeds its fixed capacity."));
        return result;
    };
    core::DisplayMapping originalMapping{};
    core::DisplayMapping enhancedMapping{};
    std::size_t enhancedIndex=definition.enhancedWindow ? 1U : 0U;
    bool originalOnly=flight.latched;
    { // Borrowed image views must leave scope before any lease is sealed.
    auto a=MutableImageView::create(plan.canonicalLayout,workspace.canonical_[0]->bytes(),ImageDomain::CanonicalU16).value();
    auto b=MutableImageView::create(plan.canonicalLayout,workspace.canonical_[1]->bytes(),ImageDomain::CanonicalU16).value();
    auto result=timed(ProcessingOperation::Normalize,"normalize",workspace.canonical_[0]->bytes(),[&] { return NormalizeStage{}.process(source.value(),a,acquisition.sourceFormat); });
    if(!result.hasValue()) return BundleResult::failure(result.error());
    result=timed(definition.enhancedWindow ? ProcessingOperation::SharedWindowLevel : ProcessingOperation::OriginalWindowLevel,definition.enhancedWindow ? "shared_window_level" : "original_window_level",workspace.canonical_[1]->bytes(),[&] { return WindowLevelStage{definition.window}.process(a.asConst(),b,acquisition.sourceFormat); });
    if(!result.hasValue()) return BundleResult::failure(result.error());
    auto map=[&](const ImageView& input,std::size_t route) {
        auto destination=workspace.display_[route]->bytes();
        auto native=workspace.orientationScratch_ ? std::span<std::byte>{workspace.orientationScratch_.get(),plan.nativeDisplayLayout.payloadBytes()} : destination;
        auto mapped=timed(route==0 ? ProcessingOperation::OriginalDisplayMap : ProcessingOperation::EnhancedDisplayMap,route==0 ? "original_display_map" : "enhanced_display_map",native,[&] {
            return DisplayMapper{}.map(input,plan.nativeDisplayLayout,core::DisplayStorage::Gray8,native,definition.version.configurationRevision);
        });
        if(!mapped.hasValue()) return mapped;
        if(workspace.orientationScratch_) {
            auto oriented=timed(route==0 ? ProcessingOperation::OriginalOrientation : ProcessingOperation::EnhancedOrientation,route==0 ? "original_orientation" : "enhanced_orientation",destination,[&] {
                return OrientationTransform{}.apply(plan.nativeDisplayLayout,core::DisplayStorage::Gray8,native,plan.displayLayout,destination,plan.options.orientation);
            });
            if(!oriented.hasValue()) return decltype(mapped)::failure(oriented.error());
        }
        return mapped;
    };
    auto originalMapped=map(b.asConst(),0);
    if(!originalMapped.hasValue()) return BundleResult::failure(originalMapped.error());
    originalMapping=originalMapped.value();
    for(std::size_t i=0;!originalOnly && i<definition.count;++i) {
        if(!runtime.stages[i]) continue;
        auto input=enhancedIndex==0 ? a.asConst() : b.asConst();
        auto output=enhancedIndex==0 ? b : a;
        result=timed(static_cast<ProcessingOperation>(static_cast<int>(definition.stages[i].id)+1),stageTimingNames[static_cast<std::size_t>(definition.stages[i].id)],workspace.canonical_[1U-enhancedIndex]->bytes(),[&] { return runtime.stages[i]->process(input,output,acquisition.sourceFormat); });
        if(!result.hasValue()) {
            if(result.error().category==core::ErrorCategory::ResourceExhaustion) return BundleResult::failure(result.error());
            originalOnly=detail::recordEnhancementFailure(*state_,failedOperation,result.error());
            if(!originalOnly) return BundleResult::failure(result.error());
            break;
        }
        enhancedIndex=1U-enhancedIndex;
    }
    if(!originalOnly) {
        auto mapped=map(enhancedIndex==0 ? a.asConst() : b.asConst(),1);
        if(!mapped.hasValue()) {
            if(mapped.error().category==core::ErrorCategory::ResourceExhaustion) return BundleResult::failure(mapped.error());
            originalOnly=detail::recordEnhancementFailure(*state_,failedOperation,mapped.error());
            if(!originalOnly) return BundleResult::failure(mapped.error());
        } else enhancedMapping=mapped.value();
    }
    }
    if(originalOnly) {
        workspace.display_[1].reset();
        auto original=core::DisplayFrame::create(raw->frameId,plan.displayLayout,seal(workspace.display_[0]),core::DisplayStorage::Gray8,originalMapping,plan.options.orientation,*state_->objects);
        if(!original.hasValue()) return BundleResult::failure(original.error());
        return core::FrameBundle::create(std::move(raw),std::move(original).value(),{},{},*state_->objects);
    }
    timings.total=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started);
    auto enhanced=core::ProcessedFrame::create(raw->frameId,plan.canonicalLayout,seal(workspace.canonical_[enhancedIndex]),definition.version,std::move(timings),*state_->objects);
    if(!enhanced.hasValue()) return BundleResult::failure(enhanced.error());
    auto original=core::DisplayFrame::create(raw->frameId,plan.displayLayout,seal(workspace.display_[0]),core::DisplayStorage::Gray8,originalMapping,plan.options.orientation,*state_->objects);
    if(!original.hasValue()) return BundleResult::failure(original.error());
    auto enhancedDisplay=core::DisplayFrame::create(raw->frameId,plan.displayLayout,seal(workspace.display_[1]),core::DisplayStorage::Gray8,enhancedMapping,plan.options.orientation,*state_->objects);
    if(!enhancedDisplay.hasValue()) return BundleResult::failure(enhancedDisplay.error());
    auto bundle=core::FrameBundle::create(std::move(raw),std::move(original).value(),std::move(enhanced).value(),std::move(enhancedDisplay).value(),*state_->objects);
    if(bundle.hasValue()) detail::recordEnhancedSuccess(*state_);
    return bundle;
}
}
