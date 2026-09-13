#include <lumora/application/LivePipeline.hpp>
#include "LiveResourcePreparation.hpp"
#include <lumora/application/CameraSettingsPolicy.hpp>
#include <lumora/core/CheckedMath.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>
#include <limits>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <lumora/processing/PipelineCompiler.hpp>
#include <array>
#include <condition_variable>
#include <atomic>
#include <cmath>
#include <thread>
#include <utility>

namespace lumora::application {
namespace {
using Result = core::Result<void>;
core::Error failure(std::string code, std::string detail,
                    core::ErrorCategory category=core::ErrorCategory::CameraConfiguration) {
    return {category,std::move(code),"Live pipeline operation failed.",std::move(detail),false};
}

processing::PipelineValidationError processingConfigurationError(
    std::string code, std::string detail) {
    auto violationCode=code;
    return {std::move(code),
        {{std::nullopt,std::move(violationCode),std::move(detail)}}};
}

std::optional<processing::PipelineValidationError> validateProcessingConfiguration(
    const processing::PipelineDefinition& definition) {
    constexpr std::array expectedOrder{
        processing::StageId::Normalize,
        processing::StageId::WindowLevel,
        processing::StageId::BrightnessContrast,
        processing::StageId::Gamma,
        processing::StageId::Clahe,
        processing::StageId::Denoise,
        processing::StageId::Sharpen,
        processing::StageId::Invert,
    };
    if(definition.stages.size()!=expectedOrder.size()) {
        return processingConfigurationError("preset_pipeline_incomplete",
            "Preset pipelines must contain all eight canonical stages.");
    }
    for(std::size_t index=0;index<expectedOrder.size();++index) {
        if(definition.stages[index].id!=expectedOrder[index]) {
            return processingConfigurationError("preset_pipeline_order_invalid",
                "Preset pipelines must use the complete canonical stage order.");
        }
    }
    const processing::PipelineCompiler compiler(processing::stageRegistry());
    auto compiled=compiler.compile(definition);
    if(!compiled.hasValue()) return std::move(compiled).error();
    return std::nullopt;
}

}
struct LivePipeline::Impl {
    camera::ICameraProvider& provider;
    core::IClock& clock;
    camera::CameraConfiguration fixed;
    ProcessorFactory factory;
    IInstallationProfiles* installationProfiles;
    std::optional<InstallationProfileCommand> installationIntent;
    std::optional<std::uint64_t> installationSaveRequest;
    std::uint64_t latestInstallationRequest{0};
    struct InstallationBinding final {
        std::optional<InstallationProfileReference> reference;
        core::Orientation orientation{false,false,core::Rotation::Degrees0};
    };
    bool installationBound{false};
    core::Orientation resourceOrientation{false,false,core::Rotation::Degrees0};
    std::optional<InstallationBinding> applyingInstallation;

    std::optional<detail::LiveResourcePreparation> resources;
    processing::ProcessingPreparationOptions preparationOptions;
    std::optional<std::uint64_t> retryIntent;
    std::optional<ProcessingConfigurationCommand> processingConfigurationIntent;
    std::uint64_t latestProcessingConfigurationRevision{0};
    std::optional<processing::PipelineDefinition> acceptedDefinition;
    struct Reconfiguration final {
        std::uint64_t requestId;
        camera::CameraConfiguration mode;
        detail::LiveResourcePreparation resources;
        std::unique_ptr<detail::PreparedLiveSession> session;
        InstallationBinding installation;
    };
    std::optional<Reconfiguration> staged;
    // Guarded by mutex. Closes processing admission before preparation starts.
    bool reconfigurationPending{false};
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

    Impl(camera::ICameraProvider& p,core::IClock& c,camera::CameraConfiguration request,ProcessorFactory f,processing::ProcessingPreparationOptions options,IInstallationProfiles* profiles)
        :provider(p),clock(c),fixed(std::move(request)),factory(std::move(f)),installationProfiles(profiles),preparationOptions(options) {
        if(installationProfiles) preparationOptions.orientation={false,false,core::Rotation::Degrees0};
        resourceOrientation=preparationOptions.orientation;
        state.activeOrientation=resourceOrientation;
    }
    const core::CameraIdentity* currentCameraIdentity() const {
        if(!state.camera || !state.camera->actualIdentity) return nullptr;
        for(const auto& descriptor:state.camera->discoveredDescriptors)
            if(descriptor.id==*state.camera->actualIdentity) return &descriptor.identity;
        return nullptr;
    }
    core::Result<InstallationBinding> resolveInstallation() const {
        using BindingResult=core::Result<InstallationBinding>;
        if(!installationProfiles) return BindingResult::success({std::nullopt,resourceOrientation});
        if(state.installationProfilePending || (state.installationProfiles && state.installationProfiles->savePending))
            return BindingResult::failure(failure("installation_save_pending","Wait for the installation save outcome before Apply, Confirm, or Start."));
        const auto* identity=currentCameraIdentity();
        if(!identity || !state.camera->capabilities)
            return BindingResult::failure(failure("installation_camera_identity_required","Discover and connect the current camera before resolving its installation."));
        if(!state.installationProfiles)
            return BindingResult::failure(failure("installation_load_pending","Wait for installation profiles to load."));
        auto resolved=resolveInstallationProfile(*state.installationProfiles,*identity,*state.camera->capabilities);
        if(!resolved.hasValue()) return BindingResult::failure(resolved.error());
        InstallationBinding binding;
        if(resolved.value()) {
            binding.reference=installationProfileReference(*resolved.value());
            binding.orientation=resolved.value()->orientation;
        }
        return BindingResult::success(binding);
    }
    void commitInstallation(const InstallationBinding& binding) {
        if(!installationProfiles) return;
        installationBound=true;
        state.activeInstallationProfile=binding.reference;
        state.activeOrientation=binding.orientation;
        state.installationProfileError.reset();
    }
    void observeInstallation() {
        if(!installationProfiles) return;
        auto latest=installationProfiles->latestStatus();
        std::lock_guard lock(mutex);
        state.installationProfiles=std::move(latest);
        if(installationSaveRequest && state.installationProfiles && state.installationProfiles->latestSaveOutcome
            && state.installationProfiles->latestSaveOutcome->requestId==*installationSaveRequest) {
            state.installationProfileOutcome=state.installationProfiles->latestSaveOutcome;
            state.installationProfileError=state.installationProfileOutcome->error;
            state.installationProfilePending=false;
            installationSaveRequest.reset();
        }
    }
    void finishInstallation(const InstallationProfileCommand& command,core::Error error) {
        std::lock_guard lock(mutex);
        state.installationProfileOutcome=InstallationSaveOutcome{command.requestId,std::nullopt,error};
        state.installationProfileError=std::move(error);
        state.installationProfilePending=false;
    }
    void cancelInstallationIntent() {
        std::optional<InstallationProfileCommand> command;
        { std::lock_guard lock(mutex);command=std::exchange(installationIntent,std::nullopt); }
        if(command) finishInstallation(*command,failure("cancelled","Lifecycle intent superseded the installation edit.",core::ErrorCategory::Cancelled));
    }
    void dispatchInstallation() {
        std::optional<InstallationProfileCommand> command;
        std::optional<core::Error> error;
        InstallationCameraProfile profile;
        {
            std::lock_guard lock(mutex);
            command=std::exchange(installationIntent,std::nullopt);
            if(!command) return;
            const auto* identity=currentCameraIdentity();
            const auto repository=state.installationProfiles;
            if(!repository || !repository->loadCompleted)
                error=failure("installation_load_pending","Wait for installation profiles to load.");
            else if(!repository->administratorMode)
                error=failure("installation_authority_required","Installation editing requires administrator installation mode.");
            else if(!state.camera || state.camera->sessionGeneration!=command->sessionGeneration)
                error=failure("stale_camera_session","Installation edit belongs to a retired camera session.");
            else if(state.camera->state!=CameraSessionState::ConnectedIdle)
                error=failure("invalid_camera_state","Stop the connected camera before saving its installation.");
            else if(!identity || state.camera->actualIdentity!=command->cameraId || !state.camera->capabilities)
                error=failure("installation_camera_mismatch","Installation edit must identify the current discovered camera.");
            else if(!command->confirmed)
                error=failure("installation_confirmation_required","Explicitly confirm the asymmetric Original and Enhanced previews before Save.");
            else if(repository->savePending)
                error=failure("installation_save_pending","An installation save is already pending.");
            else if(repository->loadError && !command->repairInvalid)
                error=*repository->loadError;
            else {
                const auto* previous=repository->loadError ? nullptr : findInstallationProfile(repository->profiles,*identity);
                if(previous && previous->revision==std::numeric_limits<std::uint64_t>::max())
                    error=failure("installation_revision_exhausted","The installation revision cannot advance.");
                else {
                    profile.revision=previous ? previous->revision+1U : 1U;
                    profile.identity=*identity;profile.capabilities=*state.camera->capabilities;
                    profile.orientation=command->orientation;profile.confirmed=true;
                    auto valid=validateInstallationProfile(profile);
                    if(!valid.hasValue()) error=valid.error();
                }
            }
        }
        if(error) { finishInstallation(*command,std::move(*error));return; }
        // Recheck lifecycle admission immediately before the asynchronous write.
        if(auto priorityCommand=incoming.tryPopPriority()) {
            finishInstallation(*command,failure("cancelled","Lifecycle intent superseded the installation edit.",core::ErrorCategory::Cancelled));
            send(std::move(*priorityCommand),true);return;
        }
        if(cancellation.stop_requested()) {
            finishInstallation(*command,failure("cancelled","The installation edit was cancelled.",core::ErrorCategory::Cancelled));return;
        }
        auto posted=installationProfiles->postSave(command->requestId,std::move(profile),command->repairInvalid);
        if(!posted.hasValue()) { finishInstallation(*command,posted.error());return; }
        installationSaveRequest=command->requestId;
    }
    void stopWorkers() noexcept {
        { std::lock_guard lock(mutex);
            state.processingAvailable=false;state.processingRetryPending=false;retryIntent.reset();state.processing={};
            if(processingConfigurationIntent) {
                try {
                    state.processingConfigurationOutcome=ProcessingConfigurationOutcome{
                        processingConfigurationIntent->sessionGeneration,
                        processingConfigurationIntent->definition.version.configurationRevision,
                        processingConfigurationError("processing_configuration_session_retired",
                            "The processing session retired before activation executed.")};
                } catch(...) { /* Teardown remains non-throwing under allocation failure. */ }
            }
            processingConfigurationIntent.reset();state.processingConfigurationPending=false;
            latestProcessingConfigurationRevision=0;reconfigurationPending=false;acceptedDefinition.reset();
        }
        if(cameraWorker) { cameraWorker->requestStop(); cameraWorker->join(); cameraWorker.reset(); }
        if(processingWorker) { processingWorker->requestStop(); processingWorker->join(); processingWorker.reset(); }
        staged.reset();processor.reset(); cameraCommands.reset(); cameraStatus.reset();
    }
    Result prepare(CameraStatusSnapshot seed) {
        auto made=detail::prepareLiveSession(*resources,seed.sessionGeneration,factory,std::nullopt);
        if(!made.hasValue()) return Result::failure(made.error());
        auto prepared=std::move(made.value());
        retiringContext=std::move(context);
        context=std::move(prepared->context);
        processor=std::move(prepared->processor);
        processingWorker=std::move(prepared->worker);
        cameraCommands=std::make_unique<CameraCommandMailbox>();
        cameraStatus=std::make_unique<core::LatestValueSlot<CameraStatusSnapshot>>();
        cameraWorker=std::make_unique<AcquisitionWorker>(provider,*cameraCommands,*context->rawPool,
            context->rawSlot,clock,*cameraStatus,seed,fixed);
        auto cameraStarted=cameraWorker->start();
        if(!cameraStarted.hasValue()) return cameraStarted;
        statusRevision=0;
        auto initialSnapshot=std::make_shared<CameraStatusSnapshot>(std::move(seed));
        const auto initialProcessingStatus=processor->status();
        {
            std::lock_guard lock(mutex);
            state.context=context;
            state.contextBound=false;
            state.camera=std::move(initialSnapshot);
            state.processing={}; state.processing.processorStatus=initialProcessingStatus;
            state.processingAvailable=true; state.processingRetryPending=false; retryIntent.reset();
            state.processingConfigurationOutcome.reset();state.processingConfigurationPending=false;
            processingConfigurationIntent.reset();latestProcessingConfigurationRevision=0;
        }
        return Result::success();
    }
    void observe() {
        if(processingWorker) {
            auto diagnostics=processingWorker->snapshot();
            diagnostics.processorStatus=processor->status();
            std::lock_guard lock(mutex);
            state.processingRetryPending=retryIntent.has_value() || diagnostics.processorStatus.retryPending;
            state.processing=std::move(diagnostics);
        }
        if(!cameraStatus) return;
        auto value=cameraStatus->consumeAfter(statusRevision);
        if(!value) return;
        statusRevision=value->revision;
        // Completion is deposited before worker publication and survives later
        // Stop/Disconnect snapshots. Never expose the new camera with old slots.
        auto completion=cameraWorker->takeReconfigurationCompletion();
        auto observed=value->value;
        const bool completedReconfiguration=completion && staged
            && completion->outcome.requestId==staged->requestId;
        std::optional<ProcessingWorkerSnapshot> committedProcessing;
        std::optional<InstallationBinding> committedInstallation;
        if(completedReconfiguration) {
            if(statusRevision<completion->publicationRevision) {
                observed=completion->camera;
                statusRevision=completion->publicationRevision;
            }
            if(completion->activated) {
                processingWorker->requestStop();
                processingWorker->join();
                processingWorker.reset();
                processor.reset();
                retiringContext=std::move(context);
                context=std::move(staged->session->context);
                processor=std::move(staged->session->processor);
                processingWorker=std::move(staged->session->worker);
                fixed=std::move(staged->mode);
                resources=std::move(staged->resources);
                resourceOrientation=staged->installation.orientation;
                committedInstallation=staged->installation;
                committedProcessing=processingWorker->snapshot();
                committedProcessing->processorStatus=processor->status();
            }
            staged.reset();
        }
        const auto& outcome=observed->latestOutcome;
        std::lock_guard lock(mutex);
        state.camera=std::move(observed);
        if(completedReconfiguration) {
            if(completion->activated) {
                state.context=context;
                state.contextBound=false;
                if(committedInstallation) commitInstallation(*committedInstallation);
                state.resources=resources->resources;
                state.processing=std::move(*committedProcessing);
                state.processingRetryPending=false;retryIntent.reset();
                state.processingConfigurationOutcome.reset();
                latestProcessingConfigurationRevision=0;
            }
            reconfigurationPending=false;
            applyingInstallation.reset();
            if(active && active->requestId==completion->outcome.requestId) {
                state.ordinaryOutcome=completion->outcome;
                active.reset();
            }
        }
        if(outcome && priority && outcome->requestId==priority->requestId) {
            state.priorityOutcome=outcome;
            incoming.completeBarrier(priority->requestId);
            priority.reset();
        }
        if(outcome && active && outcome->requestId==active->requestId) {
            state.ordinaryOutcome=outcome;
            if(applyingInstallation && !outcome->error) commitInstallation(*applyingInstallation);
            applyingInstallation.reset();
            active.reset();
        }
        if(installationProfiles && state.camera && (state.camera->state==CameraSessionState::Disconnected
            || state.camera->sourceReplacementRequired)) {
            installationBound=false;state.activeInstallationProfile.reset();
            state.activeOrientation={false,false,core::Rotation::Degrees0};
        }
    }
    void dispatchProcessingRetry() {
        std::optional<std::uint64_t> intent;
        { std::lock_guard lock(mutex); intent=std::exchange(retryIntent,std::nullopt); }
        if(!intent) return;
        const bool accepted=context && context->generation==*intent && processor && processor->requestRetry();
        auto status=processor ? processor->status() : processing::ProcessorStatus{};
        { std::lock_guard lock(mutex);
            state.processingRetryPending=retryIntent.has_value() || (accepted && status.retryPending);
            state.processing.processorStatus=std::move(status);
        }
    }
    void dispatchProcessingConfiguration() {
        std::optional<ProcessingConfigurationCommand> command;
        {
            std::lock_guard lock(mutex);
            if(!processingConfigurationIntent) return;
            command=std::move(processingConfigurationIntent);
            processingConfigurationIntent.reset();
        }

        ProcessingConfigurationOutcome outcome{
            command->sessionGeneration,
            command->definition.version.configurationRevision,
            std::nullopt};
        try {
            outcome.error=validateProcessingConfiguration(command->definition);
            if(!outcome.error) {
                auto activated=processor->activate(command->definition);
                if(!activated.hasValue()) outcome.error=std::move(activated).error();
            }
        } catch(const std::exception& exception) {
            outcome.error=processingConfigurationError("processing_configuration_exception",
                exception.what());
        } catch(...) {
            outcome.error=processingConfigurationError("processing_configuration_unknown_exception",
                "An unknown exception occurred while validating or activating the processing configuration.");
        }
        if(!outcome.error) acceptedDefinition=command->definition;
        auto processorStatus=processor->status();
        {
            std::lock_guard lock(mutex);
            state.processing.processorStatus=std::move(processorStatus);
            state.processingConfigurationOutcome=std::move(outcome);
            state.processingConfigurationPending=false;
        }
    }
    Result prepareReconfiguration(const CameraCommand& command,const InstallationBinding& installation) {
        const auto& apply=std::get<ApplyConfiguration>(command.payload);
        {
            std::lock_guard lock(mutex);
            if(!state.contextBound || retiringContext || staged)
                return Result::failure(failure("context_handoff_pending","Acknowledge the outstanding source before reconfiguration."));
            if(!state.camera || apply.sessionGeneration!=state.camera->sessionGeneration)
                return Result::failure(failure("stale_camera_session","Camera command belongs to a retired session."));
            if(state.camera->state!=CameraSessionState::ConnectedIdle || !state.camera->capabilities)
                return Result::failure(failure("invalid_camera_state","Stop the connected camera before changing its source mode."));
            if(apply.requestRevision==0 || apply.requestRevision<=state.camera->requestedRevision)
                return Result::failure(failure("stale_configuration_revision","Configuration revisions must increase."));
            auto valid=camera::validateCameraConfiguration(apply.configuration,*state.camera->capabilities);
            if(!valid.hasValue()) return valid;
            if(apply.sessionGeneration==std::numeric_limits<std::uint64_t>::max())
                return Result::failure(failure("camera_generation_exhausted","Camera generation cannot advance."));
            reconfigurationPending=true;
        }
        // Already-admitted processing work completes on the old generation;
        // concurrent submissions now get the existing retriable unavailable code.
        dispatchProcessingConfiguration();
        auto options=preparationOptions;options.orientation=installation.orientation;
        auto plan=detail::prepareLiveResources(apply.configuration,options,
            static_cast<bool>(factory),acceptedDefinition.value_or(processing::defaultPipeline()));
        if(plan.error) return Result::failure(*plan.error);
        // Each context keeps its existing per-session admission budget. The
        // bounded transition contains exactly one active and one candidate.
        auto total=core::checkedAdd(resources->resources.requiredStorageBytes,plan.resources.requiredStorageBytes);
        auto bound=core::checkedMultiply(preparationOptions.storageBudgetBytes,2U);
        if(!total.hasValue()) return Result::failure(total.error());
        if(!bound.hasValue()) return Result::failure(bound.error());
        if(total.value()>bound.value()) return Result::failure(failure("processing_resource_budget_exceeded",
            "The active and candidate resources exceed the two-session transition bound.",core::ErrorCategory::ResourceExhaustion));
        auto prepared=detail::prepareLiveSession(plan,apply.sessionGeneration+1U,factory,acceptedDefinition);
        if(!prepared.hasValue()) return Result::failure(prepared.error());
        staged.emplace(Reconfiguration{command.requestId,apply.configuration,std::move(plan),std::move(prepared.value()),installation});
        return Result::success();
    }
    void discardReconfiguration() {
        staged.reset();
        std::lock_guard lock(mutex);
        reconfigurationPending=false;
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
            seed.currentConfiguration.reset();
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
        const auto* apply=std::get_if<ApplyConfiguration>(&command.payload);
        InstallationBinding installation{std::nullopt,resourceOrientation};
        if(!isPriority && installationProfiles && (apply || std::holds_alternative<ConfirmConfiguration>(command.payload)
            || std::holds_alternative<StartStream>(command.payload))) {
            observeInstallation();
            std::lock_guard lock(mutex);
            auto resolved=resolveInstallation();
            std::optional<core::Error> error;
            if(!resolved.hasValue()) error=resolved.error();
            else {
                installation=resolved.value();
                if(!apply && (!installationBound || state.activeInstallationProfile!=installation.reference
                    || state.activeOrientation!=installation.orientation))
                    error=failure("installation_binding_stale","Apply the saved installation, review both previews, then Confirm before Start.");
                if(apply && state.camera->state!=CameraSessionState::ConnectedIdle)
                    error=failure("invalid_camera_state","Stop the connected camera before applying its installation.");
            }
            if(error) {
                state.installationProfileError=error;
                state.ordinaryOutcome=CameraCommandOutcome{command.requestId,std::move(error)};return;
            }
        }
        const bool rebind=!isPriority && apply && (!isCameraSettingsCompatible(apply->configuration,fixed)
            || installation.orientation!=resourceOrientation);
        if(rebind) {
            auto prepared=prepareReconfiguration(command,installation);
            if(!prepared.hasValue()) {
                discardReconfiguration();
                std::lock_guard lock(mutex);
                state.ordinaryOutcome=CameraCommandOutcome{command.requestId,prepared.error()};
                return;
            }
        }
        if(!isPriority) {
            auto priorityCommand=incoming.tryPopPriority();
            if(cancellation.stop_requested() || priorityCommand) {
                if(rebind) discardReconfiguration();
                {
                    std::lock_guard lock(mutex);
                    state.ordinaryOutcome=CameraCommandOutcome{command.requestId,
                        failure("cancelled","Lifecycle intent superseded preparation.",core::ErrorCategory::Cancelled)};
                }
                if(priorityCommand && !cancellation.stop_requested()) send(std::move(*priorityCommand),true);
                return;
            }
        }
        auto result=rebind
            ? cameraWorker->postReconfiguration(command,staged->session->context,staged->mode)
            : cameraWorker->post(command);
        if(!result.hasValue()) {
            if(rebind) discardReconfiguration();
            std::lock_guard lock(mutex);
            auto outcome=CameraCommandOutcome{command.requestId,result.error()};
            if(isPriority) { state.priorityOutcome=outcome;incoming.completeBarrier(command.requestId); }
            else state.ordinaryOutcome=outcome;
            return;
        }
        if(isPriority) priority=std::move(command);
        else {
            if(apply) applyingInstallation=installation;
            active=std::move(command);
        }
    }
    void run(std::stop_token stop) noexcept {
        try {
            CameraStatusSnapshot seed; seed.sessionGeneration=1;
            auto ready=prepare(seed);
            if(!ready.hasValue()) { std::lock_guard lock(mutex);state.error=ready.error(); }
            while(ready.hasValue() && !stop.stop_requested()) {
                dispatchProcessingRetry();
                observe();
                observeInstallation();
                bool bound=false;
                { std::lock_guard lock(mutex);bound=state.contextBound; }
                if(bound) retiringContext.reset();
                bool retire=false;
                { std::lock_guard lock(mutex);retire=state.camera && state.camera->sourceReplacementRequired; }
                if(retire && cameraWorker && !active && !priority) stopWorkers();
                bool dispatchedPriority=false;
                if(auto priorityCommand=incoming.tryPopPriority()) {
                    dispatchedPriority=true;
                    cancelInstallationIntent();
                    if(active) {
                        std::lock_guard lock(mutex);
                        state.ordinaryOutcome=CameraCommandOutcome{active->requestId,
                            failure("cancelled","Superseded by lifecycle command.",core::ErrorCategory::Cancelled)};
                        active.reset();
                        applyingInstallation.reset();
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
                if(!dispatchedPriority && !priority && !staged) dispatchProcessingConfiguration();
                if(!dispatchedPriority && !priority && !active && !staged) dispatchInstallation();
                std::unique_lock lock(mutex);
                changed.wait_for(lock,stop,std::chrono::milliseconds{2},[&]{return terminal;});
            }
        } catch(const std::exception& e) {
            try { std::lock_guard lock(mutex);state.error=failure("pipeline_exception",e.what(),core::ErrorCategory::Internal); } catch(...) {}
        } catch(...) {
            try { std::lock_guard lock(mutex);state.error=failure("pipeline_exception","Unknown control exception.",core::ErrorCategory::Internal); } catch(...) {}
        }
        accepting.store(false);
        cancelInstallationIntent();
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
    camera::CameraConfiguration request,ProcessorFactory factory,processing::ProcessingPreparationOptions options,
    IInstallationProfiles* installationProfiles)
    :impl_(std::make_unique<Impl>(provider,clock,std::move(request),std::move(factory),options,installationProfiles)) {}
Result LivePipeline::saveInstallationProfile(InstallationProfileCommand command) {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->installationProfiles || !impl_->accepting.load() || impl_->terminal || impl_->state.error)
        return Result::failure(failure("installation_unavailable","Installation editing is unavailable."));
    if(impl_->state.installationProfilePending)
        return Result::failure(failure("installation_save_pending","An installation save is already pending."));
    if(command.requestId==0 || command.requestId<=impl_->latestInstallationRequest)
        return Result::failure(failure("installation_request_not_increasing","Installation request IDs must increase."));
    impl_->latestInstallationRequest=command.requestId;
    impl_->installationIntent=std::move(command);
    impl_->state.installationProfilePending=true;
    impl_->state.installationProfileError.reset();
    impl_->changed.notify_all();
    return Result::success();
}
LivePipeline::~LivePipeline() { shutdown(); }
Result LivePipeline::start() {
    std::lock_guard lock(impl_->mutex);
    if(impl_->started || impl_->terminal) return Result::failure(failure("pipeline_already_started","Pipeline is single-use."));
    auto resources = detail::prepareLiveResources(impl_->fixed,impl_->preparationOptions,static_cast<bool>(impl_->factory));
    impl_->state.resources=resources.resources;
    if(resources.error) { impl_->state.error=resources.error; return Result::failure(std::move(*resources.error)); }
    impl_->resources=std::move(resources);
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
    if(auto* apply=std::get_if<ApplyConfiguration>(&command.payload)) {
        if(!isSupportedLiveCameraConfiguration(apply->configuration))
            return Result::failure(failure("unsupported_pipeline_request","The request requires a positive-rate continuous native mode."));
        if(impl_->state.camera && impl_->state.camera->state==CameraSessionState::Streaming
            && impl_->state.camera->appliedConfiguration
            && !isCameraSettingsCompatible(apply->configuration,impl_->state.camera->appliedConfiguration->actual))
            return Result::failure(failure("invalid_camera_state","Stop the camera before applying settings."));
    }
    auto result=impl_->incoming.post(std::move(command));impl_->changed.notify_all();return result;
}
Result LivePipeline::requestProcessingRetry(std::uint64_t generation) {
    std::lock_guard lock(impl_->mutex);
    if(impl_->state.context && impl_->state.context->generation!=generation)
        return Result::failure(failure("stale_processing_session","The processing session was replaced."));
    if(!impl_->accepting.load() || impl_->terminal || impl_->reconfigurationPending || !impl_->state.context || !impl_->state.processingAvailable
        || !impl_->state.processing.processorStatus.retrySupported)
        return Result::failure(failure("processing_retry_unavailable","Processing retry is unavailable."));
    if(!impl_->state.processingRetryPending) impl_->retryIntent=generation;
    impl_->state.processingRetryPending=true;
    impl_->changed.notify_all();
    return Result::success();
}
Result LivePipeline::setProcessingConfiguration(ProcessingConfigurationCommand command) {
    std::lock_guard lock(impl_->mutex);
    if(impl_->state.context
        && impl_->state.context->generation!=command.sessionGeneration)
        return Result::failure(failure("stale_processing_session",
            "The processing session was replaced.",core::ErrorCategory::Processing));
    if(!impl_->accepting.load() || impl_->terminal || impl_->reconfigurationPending || !impl_->state.context
        || !impl_->state.processingAvailable || !impl_->processor)
        return Result::failure(failure("processing_configuration_unavailable",
            "Processing configuration activation is unavailable.",core::ErrorCategory::Processing));
    const auto revision=command.definition.version.configurationRevision;
    if(revision==0U)
        return Result::failure(failure("processing_configuration_revision_required",
            "Processing configuration revisions must be nonzero.",core::ErrorCategory::Processing));
    if(impl_->state.processingConfigurationPending)
        return Result::failure(failure("processing_configuration_busy",
            "A processing configuration is already pending or executing.",core::ErrorCategory::Processing));
    if(revision<=impl_->latestProcessingConfigurationRevision)
        return Result::failure(failure("processing_configuration_revision_not_increasing",
            "Processing configuration revisions must increase within a session.",core::ErrorCategory::Processing));
    impl_->processingConfigurationIntent=std::move(command);
    impl_->latestProcessingConfigurationRevision=revision;
    impl_->state.processingConfigurationPending=true;
    impl_->changed.notify_all();
    return Result::success();
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
