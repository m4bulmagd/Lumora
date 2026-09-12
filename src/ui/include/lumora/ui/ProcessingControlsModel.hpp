#pragma once

#include <lumora/application/PresetRepository.hpp>
#include <lumora/core/Clock.hpp>
#include <memory>
#include <optional>

namespace lumora::ui {

enum class ProcessingEditPhase { Commit, Drag, Release };
struct ProcessingSubmission final {
    std::uint64_t sessionGeneration;
    application::PresetState state;
};

// UI-thread state owner. No I/O, camera operations or engine preparation.
class ProcessingControlsModel final {
public:
    ProcessingControlsModel(application::PresetRepository repository, core::IClock& clock);
    ~ProcessingControlsModel();
    [[nodiscard]] application::PresetState draft() const;
    [[nodiscard]] std::vector<application::Preset> presets() const;
    [[nodiscard]] const std::optional<application::PresetState>& acknowledged() const;
    [[nodiscard]] const std::optional<core::Error>& error() const;
    [[nodiscard]] bool pending() const;
    [[nodiscard]] core::Result<void> edit(processing::PipelineDefinition definition,
        ProcessingEditPhase phase = ProcessingEditPhase::Commit);
    [[nodiscard]] core::Result<void> selectPreset(const application::PresetId& id);
    [[nodiscard]] core::Result<void> reset();
    void bindSession(std::uint64_t generation);
    [[nodiscard]] std::optional<ProcessingSubmission> takeSubmission();
    void rejectAdmission(std::uint64_t generation, std::uint64_t revision, core::Error error);
    // True only for a matching successful activation; then acknowledged() is safe to save.
    bool complete(std::uint64_t generation, std::uint64_t revision,
        std::optional<core::Error> error = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
