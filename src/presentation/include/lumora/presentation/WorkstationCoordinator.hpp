#pragma once

#include <lumora/application/LivePipeline.hpp>
#include <lumora/presentation/ProcessingControlsModel.hpp>
#include <lumora/presentation/WorkstationState.hpp>
#include <memory>
#include <optional>

namespace lumora::configuration { class StartupPreferencesService; }
namespace lumora::presentation {

struct ProcessingPresentation final {
    bool loadCompleted{false};
    std::optional<core::Error> loadError;
    std::optional<core::Error> persistenceWarning;
    processing::ProcessorStatus processorStatus;
    bool retryPending{false};
};

// C++ renderer contract, never an operator-controlled session identity. The
// adapter retains candidate owners until it has retired the previous source,
// bound the candidate, and completed this exact handoff. Poll never acknowledges.
struct ContextHandoff final {
    std::uint64_t id;
    std::shared_ptr<application::LiveSessionContext> candidate;
};

// Serialized frontend-thread policy. Composition starts the pipeline/preferences
// workers first and keeps them alive through the two shutdown phases.
class WorkstationCoordinator final {
public:
    WorkstationCoordinator(application::LivePipeline& pipeline,
        configuration::StartupPreferencesService& preferences, core::IClock& clock,
        camera::CameraConfiguration fixedRequest);
    ~WorkstationCoordinator();
    [[nodiscard]] core::Result<void> start();
    void poll();
    [[nodiscard]] const WorkstationState& state() const noexcept;
    [[nodiscard]] const ProcessingPresentation& processingState() const noexcept;
    [[nodiscard]] ProcessingControlsModel* processingControls() const noexcept;
    void selectCamera(camera::CameraId id);
    [[nodiscard]] core::Result<void> beginCameraSettingsEdit(std::uint64_t generation, camera::CameraId id);
    [[nodiscard]] core::Result<void> applyCameraSettings(std::uint64_t generation,
        camera::CameraId id, camera::CameraConfiguration requested);
    [[nodiscard]] core::Result<void> saveInstallationProfile(std::uint64_t generation,
        camera::CameraId id, core::Orientation orientation, bool confirmed, bool repairInvalid);
    [[nodiscard]] core::Result<void> dispatch(CameraStartupIntent intent);
    [[nodiscard]] core::Result<void> retryProcessing();
    [[nodiscard]] std::optional<ContextHandoff> pendingContextHandoff() const;
    [[nodiscard]] core::Result<void> completeContextHandoff(std::uint64_t handoffId);
    // Join backend while retaining renderer context handles. Adapter then retires
    // all rendering owners, including hidden/invalidated surfaces, before phase 2.
    void beginShutdown() noexcept;
    void completeRendererShutdown() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lumora::presentation
