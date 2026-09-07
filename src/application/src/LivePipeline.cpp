#include <lumora/application/LivePipeline.hpp>
#include <lumora/application/StartupPreferences.hpp>
#include <lumora/core/CheckedMath.hpp>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <array>
#include <condition_variable>
#include <atomic>
#include <cmath>
#include <thread>

namespace lumora::application {
namespace {
using Result = core::Result<void>;
core::Error failure(std::string code, std::string detail,
                    core::ErrorCategory category=core::ErrorCategory::CameraConfiguration) {
    return {category,std::move(code),"Live pipeline operation failed.",std::move(detail),false};
}

struct ResourcePlan final {
    core::ImageLayout sourceLayout;
    std::size_t rawBytes;
    std::size_t canonicalBytes;
    std::size_t displayBytes;
    std::size_t totalBytes;
};
core::Result<ResourcePlan> planResources(const camera::CameraConfiguration& request) {
    using PlanResult = core::Result<ResourcePlan>;
    auto format = core::validateSourcePixelFormat(request.pixelFormat);
    if (!format.hasValue()) return PlanResult::failure(format.error());
    if (request.roi.width == 0 || request.roi.height == 0 || !request.requestedFps
        || !std::isfinite(*request.requestedFps) || *request.requestedFps <= 0
        || request.acquisitionMode != camera::AcquisitionMode::Continuous) {
        return PlanResult::failure(failure("unsupported_pipeline_mode",
            "The pipeline requires nonzero dimensions and a finite positive-rate continuous native mode."));
    }
    const auto sampleBytes = request.pixelFormat.applicationStorage == core::StorageType::UInt8 ? 1U : 2U;
    auto pixels = core::checkedMultiply(request.roi.width, request.roi.height);
    if (!pixels.hasValue()) return PlanResult::failure(pixels.error());
    auto stride = core::checkedMultiply(request.roi.width, sampleBytes);
    if (!stride.hasValue()) return PlanResult::failure(stride.error());
    auto rawBytes = core::checkedMultiply(stride.value(), request.roi.height);
    if (!rawBytes.hasValue()) return PlanResult::failure(rawBytes.error());
    auto canonicalBytes = core::checkedMultiply(pixels.value(), 2U);
    if (!canonicalBytes.hasValue()) return PlanResult::failure(canonicalBytes.error());
    const std::array<std::pair<std::size_t, std::size_t>, 3> pools{{
        {10U, rawBytes.value()}, {9U, canonicalBytes.value()}, {16U, pixels.value()}}};
    std::size_t total = 0;
    for (const auto& [count, bytes] : pools) {
        auto poolBytes = core::checkedMultiply(count, bytes);
        if (!poolBytes.hasValue()) return PlanResult::failure(poolBytes.error());
        auto accumulated = core::checkedAdd(total, poolBytes.value());
        if (!accumulated.hasValue()) return PlanResult::failure(accumulated.error());
        total = accumulated.value();
    }
    auto layout = core::ImageLayout::create(request.roi.width, request.roi.height,
        stride.value(), request.pixelFormat.applicationStorage, rawBytes.value());
    if (!layout.hasValue()) return PlanResult::failure(layout.error());
    return PlanResult::success({layout.value(), rawBytes.value(), canonicalBytes.value(), pixels.value(), total});
}
}
struct LivePipeline::Impl {
    camera::ICameraProvider& provider;
    core::IClock& clock;
    camera::CameraConfiguration fixed;
    ProcessorFactory factory;
    std::optional<ResourcePlan> resources;
    mutable std::mutex mutex;
    std::condition_variable_any changed;
    LivePipelineSnapshot state;
    CameraCommandMailbox incoming;
    std::unique_ptr<CameraCommandMailbox> cameraCommands;
    std::unique_ptr<core::LatestValueSlot<CameraStatusSnapshot>> cameraStatus;
    std::unique_ptr<AcquisitionWorker> cameraWorker;
    std::unique_ptr<processing::IFrameProcessor> processor;
    std::unique_ptr<ProcessingWorker> processingWorker;
    std::shared_ptr<LiveSessionContext> context;
    std::shared_ptr<LiveSessionContext> retiringContext;
    std::optional<CameraCommand> active;
    std::optional<CameraCommand> priority;
    std::uint64_t statusRevision{0};
    std::jthread control;
    std::stop_source cancellation;
    std::atomic<bool> accepting{false};
    bool started{false};
    bool terminal{false};

    Impl(camera::ICameraProvider& p,core::IClock& c,camera::CameraConfiguration request,ProcessorFactory f)
        :provider(p),clock(c),fixed(std::move(request)),factory(std::move(f)) {}
    void stopWorkers() noexcept {
        if(cameraWorker) { cameraWorker->requestStop(); cameraWorker->join(); cameraWorker.reset(); }
        if(processingWorker) { processingWorker->requestStop(); processingWorker->join(); processingWorker.reset(); }
        processor.reset(); cameraCommands.reset(); cameraStatus.reset();
    }
    Result prepare(CameraStatusSnapshot seed) {
        const auto& plan = *resources;
        auto prepared=std::make_shared<LiveSessionContext>();
        prepared->generation=seed.sessionGeneration;
        auto raw=core::BufferPool::create(10U,plan.rawBytes);
        if(!raw.hasValue()) return Result::failure(raw.error());
        prepared->rawPool=std::move(raw.value());
        auto u16=core::BufferPool::create(9U,plan.canonicalBytes);
        if(!u16.hasValue()) return Result::failure(u16.error());
        prepared->processingPool=std::move(u16.value());
        auto display=core::BufferPool::create(16U,plan.displayBytes);
        if(!display.hasValue()) return Result::failure(display.error());
        prepared->displayPool=std::move(display.value());
        if(factory) {
            auto made=factory(*prepared->processingPool, *prepared->displayPool, plan.sourceLayout);
            if(!made.hasValue()) return Result::failure(made.error());
            processor=std::move(made.value());
        } else {
            auto made = processing::FrameProcessingEngine::create(*prepared->processingPool,
                *prepared->displayPool, plan.sourceLayout);
            if (!made.hasValue()) return Result::failure(made.error());
            processor = std::move(made).value();
        }
        if(!processor) return Result::failure(failure("processor_required","Processor factory returned null."));
        retiringContext=std::move(context);
        context=std::move(prepared);
        cameraCommands=std::make_unique<CameraCommandMailbox>();
        cameraStatus=std::make_unique<core::LatestValueSlot<CameraStatusSnapshot>>();
        processingWorker=std::make_unique<ProcessingWorker>(context->rawSlot,context->bundleSlot,*processor);
        auto processingStarted=processingWorker->start();
        if(!processingStarted.hasValue()) return processingStarted;
        cameraWorker=std::make_unique<AcquisitionWorker>(provider,*cameraCommands,*context->rawPool,
            context->rawSlot,clock,*cameraStatus,seed,fixed);
        auto cameraStarted=cameraWorker->start();
        if(!cameraStarted.hasValue()) return cameraStarted;
        statusRevision=0;
        auto initialSnapshot=std::make_shared<CameraStatusSnapshot>(std::move(seed));
        {
            std::lock_guard lock(mutex);
            state.context=context;
            state.contextBound=false;
            state.camera=std::move(initialSnapshot);
        }
        return Result::success();
    }
    void observe() {
        if(processingWorker) {
            auto diagnostics=processingWorker->snapshot();
            std::lock_guard lock(mutex);state.processing=std::move(diagnostics);
        }
        if(!cameraStatus) return;
        auto value=cameraStatus->consumeAfter(statusRevision);
        if(!value) return;
        statusRevision=value->revision;
        const auto& outcome=value->value->latestOutcome;
        std::lock_guard lock(mutex);
        state.camera=value->value;
        if(outcome && priority && outcome->requestId==priority->requestId) {
            state.priorityOutcome=outcome;
            incoming.completeBarrier(priority->requestId);
            priority.reset();
        }
        if(outcome && active && outcome->requestId==active->requestId) {
            state.ordinaryOutcome=outcome;
            active.reset();
        }
    }
    void send(CameraCommand command,bool isPriority) {
        {
            std::lock_guard lock(mutex);
            if(state.camera && state.camera->sourceReplacementRequired && !state.contextBound &&
                (std::holds_alternative<Connect>(command.payload) || std::holds_alternative<Retry>(command.payload) ||
                 std::holds_alternative<Discover>(command.payload))) {
                state.ordinaryOutcome=CameraCommandOutcome{command.requestId,
                    failure("context_handoff_pending","Acknowledge the outstanding source before replacement.")};return;
            }
        }
        if(!cameraWorker) {
            CameraStatusSnapshot seed;
            { std::lock_guard lock(mutex);seed=*state.camera; }
            const bool replacement=std::holds_alternative<Connect>(command.payload) || std::holds_alternative<Retry>(command.payload) ||
                (std::holds_alternative<Discover>(command.payload) && seed.state==CameraSessionState::Disconnected);
            if(!replacement) {
                std::lock_guard lock(mutex);
                auto outcome=CameraCommandOutcome{command.requestId,std::nullopt};
                if(isPriority) {
                    if(std::holds_alternative<Disconnect>(command.payload)) {
                        seed.state=CameraSessionState::Disconnected;seed.desiredIdentity.reset();seed.desiredStreaming=false;
                        state.camera=std::make_shared<CameraStatusSnapshot>(std::move(seed));
                    }
                    state.priorityOutcome=outcome;incoming.completeBarrier(command.requestId);
                } else state.ordinaryOutcome=CameraCommandOutcome{command.requestId,failure("camera_context_replacement_required","Connect or Retry to prepare a fresh source.")};
                return;
            }
            ++seed.sessionGeneration;
            seed.actualIdentity.reset();seed.capabilities.reset();seed.requestedConfiguration.reset();seed.appliedConfiguration.reset();
            seed.requestedRevision=0;seed.appliedRevision=0;seed.confirmedRevision.reset();seed.restoreEligible=false;
            seed.desiredStreaming=false;seed.consecutiveTimeouts=0;seed.latestOutcome.reset();seed.lastAcquiredAt.reset();
            seed.acquisitionCounters={};seed.mailboxStats={};seed.sourceReplacementRequired=false;
            auto prepared=prepare(std::move(seed));
            if(!prepared.hasValue()) {
                std::lock_guard lock(mutex);state.error=prepared.error();
                state.ordinaryOutcome=CameraCommandOutcome{command.requestId,prepared.error()};
                cancellation.request_stop();return;
            }
        }
        if(!isPriority) {
            auto priorityCommand=incoming.tryPopPriority();
            if(cancellation.stop_requested() || priorityCommand) {
                {
                    std::lock_guard lock(mutex);
                    state.ordinaryOutcome=CameraCommandOutcome{command.requestId,
                        failure("cancelled","Lifecycle intent superseded preparation.",core::ErrorCategory::Cancelled)};
                }
                if(priorityCommand && !cancellation.stop_requested()) send(std::move(*priorityCommand),true);
                return;
            }
        }
        auto result=cameraWorker->post(command);
        if(!result.hasValue()) {
            std::lock_guard lock(mutex);
            auto outcome=CameraCommandOutcome{command.requestId,result.error()};
            if(isPriority) { state.priorityOutcome=outcome;incoming.completeBarrier(command.requestId); }
            else state.ordinaryOutcome=outcome;
            return;
        }
        if(isPriority) priority=std::move(command);
        else active=std::move(command);
    }
    void run(std::stop_token stop) noexcept {
        try {
            CameraStatusSnapshot seed; seed.sessionGeneration=1;
            auto ready=prepare(seed);
            if(!ready.hasValue()) { std::lock_guard lock(mutex);state.error=ready.error(); }
            while(ready.hasValue() && !stop.stop_requested()) {
                observe();
                bool bound=false;
                { std::lock_guard lock(mutex);bound=state.contextBound; }
                if(bound) retiringContext.reset();
                bool retire=false;
                { std::lock_guard lock(mutex);retire=state.camera && state.camera->sourceReplacementRequired; }
                if(retire && cameraWorker && !active && !priority) stopWorkers();
                if(auto priorityCommand=incoming.tryPopPriority()) {
                    if(active) {
                        std::lock_guard lock(mutex);
                        state.ordinaryOutcome=CameraCommandOutcome{active->requestId,
                            failure("cancelled","Superseded by lifecycle command.",core::ErrorCategory::Cancelled)};
                        active.reset();
                    }
                    send(std::move(*priorityCommand),true);
                } else if(!active && !priority) {
                    if(auto command=incoming.tryPop()) {
                        if(std::holds_alternative<StartStream>(command->payload) && !bound) {
                            std::lock_guard lock(mutex);
                            state.ordinaryOutcome=CameraCommandOutcome{command->requestId,
                                failure("context_not_bound","Bind the current source before Start.")};
                        } else send(std::move(*command),false);
                    }
                }
                std::unique_lock lock(mutex);
                changed.wait_for(lock,stop,std::chrono::milliseconds{2},[&]{return terminal;});
            }
        } catch(const std::exception& e) {
            try { std::lock_guard lock(mutex);state.error=failure("pipeline_exception",e.what(),core::ErrorCategory::Internal); } catch(...) {}
        } catch(...) {
            try { std::lock_guard lock(mutex);state.error=failure("pipeline_exception","Unknown control exception.",core::ErrorCategory::Internal); } catch(...) {}
        }
        accepting.store(false);
        stopWorkers();
        context.reset();
        retiringContext.reset();
        // Destruction may release the last pool owner; do that outside the UI lock.
        try {
            std::shared_ptr<LiveSessionContext> released;
            { std::lock_guard lock(mutex);released=std::move(state.context);state.contextBound=false; }
        } catch(...) { /* Best-effort reporting cannot escape the thread boundary. */ }
    }
};
LivePipeline::LivePipeline(camera::ICameraProvider& provider,core::IClock& clock,
    camera::CameraConfiguration request,ProcessorFactory factory)
    :impl_(std::make_unique<Impl>(provider,clock,std::move(request),std::move(factory))) {}
LivePipeline::~LivePipeline() { shutdown(); }
Result LivePipeline::start() {
    std::lock_guard lock(impl_->mutex);
    if(impl_->started || impl_->terminal) return Result::failure(failure("pipeline_already_started","Pipeline is single-use."));
    auto resources = planResources(impl_->fixed);
    if (!resources.hasValue()) return Result::failure(resources.error());
    impl_->resources = std::move(resources).value();
    impl_->started=true;
    impl_->accepting.store(true);
    try { impl_->control=std::jthread([this]{impl_->run(impl_->cancellation.get_token());}); }
    catch(const std::exception& e) { impl_->accepting.store(false);return Result::failure(failure("pipeline_start_failed",e.what(),core::ErrorCategory::Internal)); }
    catch(...) { impl_->accepting.store(false);return Result::failure(failure("pipeline_start_failed","Unknown thread construction failure.",core::ErrorCategory::Internal)); }
    return Result::success();
}
Result LivePipeline::post(CameraCommand command) {
    if(std::holds_alternative<Shutdown>(command.payload)) {
        std::lock_guard lock(impl_->mutex);impl_->terminal=true;impl_->accepting.store(false);impl_->cancellation.request_stop();
        impl_->incoming.close();impl_->changed.notify_all();return Result::success();
    }
    std::lock_guard lock(impl_->mutex);
    if(!impl_->accepting.load() || impl_->terminal || impl_->state.error) return Result::failure(failure("cancelled","Pipeline is unavailable.",core::ErrorCategory::Cancelled));
    if(impl_->state.camera && impl_->state.camera->sourceReplacementRequired && !impl_->state.contextBound &&
        (std::holds_alternative<Connect>(command.payload) || std::holds_alternative<Retry>(command.payload) ||
         std::holds_alternative<Discover>(command.payload)))
        return Result::failure(failure("context_handoff_pending","Acknowledge the outstanding source before replacement."));
    if(auto* apply=std::get_if<ApplyConfiguration>(&command.payload);
        apply && !cameraConfigurationsEqual(apply->configuration,impl_->fixed))
        return Result::failure(failure("unsupported_pipeline_request","The pipeline accepts only its explicit fixed configuration."));
    auto result=impl_->incoming.post(std::move(command));impl_->changed.notify_all();return result;
}
LivePipelineSnapshot LivePipeline::snapshot() const { std::lock_guard lock(impl_->mutex);return impl_->state; }
Result LivePipeline::acknowledgeContext(std::uint64_t generation) {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->state.context || impl_->state.context->generation!=generation)
        return Result::failure(failure("stale_camera_session","Cannot acknowledge an obsolete source."));
    impl_->state.contextBound=true;impl_->changed.notify_all();return Result::success();
}
void LivePipeline::shutdown() noexcept {
    { std::lock_guard lock(impl_->mutex);impl_->terminal=true;impl_->accepting.store(false);impl_->incoming.close();impl_->cancellation.request_stop(); }
    impl_->changed.notify_all();
    if(impl_->control.joinable()) impl_->control.join();
}
}
