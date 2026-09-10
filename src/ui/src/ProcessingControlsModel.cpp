#include <lumora/ui/ProcessingControlsModel.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>
#include <limits>
#include <utility>

namespace lumora::ui {
namespace {
constexpr auto dragInterval=std::chrono::nanoseconds{33'333'334};
}
struct ProcessingControlsModel::Impl {
    application::PresetRepository repository;
    core::IClock& clock;
    application::PresetState initial;
    std::optional<application::PresetState> accepted;
    std::optional<core::Error> error;
    std::optional<ProcessingSubmission> inFlight;
    std::optional<std::chrono::steady_clock::time_point> lastPublication;
    std::uint64_t generation{0}, nextRevision{0}, editSerial{0}, submittedSerial{0};
    bool dirty{false}, immediate{false};

    Impl(application::PresetRepository source,core::IClock& sourceClock)
        :repository(std::move(source)),clock(sourceClock),initial(repository.snapshot()) {}
    void changed(bool flush) {
        ++editSerial; dirty=true; immediate=flush; error.reset();
    }
};
ProcessingControlsModel::ProcessingControlsModel(application::PresetRepository repository,core::IClock& clock)
    :impl_(std::make_unique<Impl>(std::move(repository),clock)) {}
ProcessingControlsModel::~ProcessingControlsModel()=default;
application::PresetState ProcessingControlsModel::draft() const { return impl_->repository.snapshot(); }
std::vector<application::Preset> ProcessingControlsModel::presets() const { return impl_->repository.list(); }
const std::optional<application::PresetState>& ProcessingControlsModel::acknowledged() const { return impl_->accepted; }
const std::optional<core::Error>& ProcessingControlsModel::error() const { return impl_->error; }
bool ProcessingControlsModel::pending() const { return impl_->dirty || impl_->inFlight.has_value(); }
core::Result<void> ProcessingControlsModel::edit(processing::PipelineDefinition definition,ProcessingEditPhase phase) {
    auto& d=*impl_;
    const auto before=d.repository.snapshot();
    const bool sameValues=processing::semanticallyEqualPipelineDefinitions(before.activePipeline,definition);
    // A release only flushes unchanged values; a preset may have canceled the drag.
    if(phase==ProcessingEditPhase::Release && sameValues) {
        if(d.dirty) d.immediate=true;
        return core::Result<void>::success();
    }
    const bool duplicate=before.selectedId.value=="custom" && sameValues;
    auto result=d.repository.edit(std::move(definition));
    if(!result.hasValue()) { d.error=result.error(); return result; }
    if(!duplicate) d.changed(phase!=ProcessingEditPhase::Drag);
    // Release flushes an unsent draft even if the slider's final value is unchanged.
    else if(phase==ProcessingEditPhase::Release && d.dirty) d.immediate=true;
    return result;
}
core::Result<void> ProcessingControlsModel::selectPreset(const application::PresetId& id) {
    auto& d=*impl_;
    auto result=d.repository.apply(id);
    if(!result.hasValue()) { d.error=result.error(); return core::Result<void>::failure(result.error()); }
    d.changed(true);
    return core::Result<void>::success();
}
core::Result<void> ProcessingControlsModel::reset() { return selectPreset({"original"}); }
void ProcessingControlsModel::bindSession(std::uint64_t generation) {
    auto& d=*impl_;
    if(d.generation==generation) return;
    d.generation=generation; d.inFlight.reset(); d.changed(true);
}
std::optional<ProcessingSubmission> ProcessingControlsModel::takeSubmission() {
    auto& d=*impl_;
    if(!d.generation || !d.dirty || d.inFlight) return {};
    const auto now=d.clock.steadyNow();
    if(!d.immediate && d.lastPublication && now-*d.lastPublication<dragInterval) return {};
    if(d.nextRevision==std::numeric_limits<std::uint64_t>::max()) {
        d.error=core::Error{core::ErrorCategory::Configuration,"processing_revision_exhausted",
            "Processing settings could not be applied.","Restart the application to reset submission revisions.",true};
        d.dirty=false; return {};
    }
    auto state=d.repository.snapshot();
    state.activePipeline.version.configurationRevision=++d.nextRevision;
    d.inFlight=ProcessingSubmission{d.generation,std::move(state)};
    d.submittedSerial=d.editSerial; d.lastPublication=now; d.dirty=false; d.immediate=false;
    return d.inFlight;
}
void ProcessingControlsModel::rejectAdmission(std::uint64_t generation,std::uint64_t revision,core::Error error) {
    const auto& pending=impl_->inFlight;
    if(!pending || pending->sessionGeneration!=generation ||
        pending->state.activePipeline.version.configurationRevision!=revision) return;
    if(error.code=="stale_processing_session" || error.code=="processing_configuration_unavailable" ||
        error.code=="processing_configuration_busy") {
        // Admission raced retirement (or another caller). Keep desired edits and
        // discard only the association; a fresh snapshot will bind the session.
        bindSession(0);
        return;
    }
    (void)complete(generation,revision,std::move(error));
}
bool ProcessingControlsModel::complete(std::uint64_t generation,std::uint64_t revision,std::optional<core::Error> error) {
    auto& d=*impl_;
    if(!d.inFlight || generation!=d.generation || generation!=d.inFlight->sessionGeneration ||
        revision!=d.inFlight->state.activePipeline.version.configurationRevision) return false;
    auto completed=std::move(*d.inFlight); d.inFlight.reset();
    if(error) {
        if(d.editSerial==d.submittedSerial) {
            d.error=std::move(error);
            (void)d.repository.restore(d.accepted ? *d.accepted : d.initial);
            d.dirty=false; d.immediate=false;
        }
        return false;
    }
    d.accepted=std::move(completed.state);
    // A newer draft owns its own state and any later error; do not repaint old values.
    if(d.editSerial==d.submittedSerial) d.error.reset();
    return true;
}
}
