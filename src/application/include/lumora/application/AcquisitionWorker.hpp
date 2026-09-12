#pragma once

#include <lumora/application/CameraCommandMailbox.hpp>
#include <lumora/camera/ICameraProvider.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/LatestValueSlot.hpp>

#include <memory>

namespace lumora::application {

struct LiveSessionContext;

// Separate completion prevents a later priority status from hiding a committed
// binding. The control thread consumes this before exposing a newer generation.
struct CameraReconfigurationCompletion final {
    CameraCommandOutcome outcome;
    bool activated;
    std::uint64_t publicationRevision;
    std::shared_ptr<const CameraStatusSnapshot> camera;
};

// Dependencies outlive this worker. The owner serializes start/join; posting
// and requesting cancellation are thread-safe. Destruction joins defensively.
class AcquisitionWorker final {
public:
    // Production supplies its prepared fixed descriptor and complete ROI.
    // Without it, standalone use binds the first successfully applied mode.
    AcquisitionWorker(camera::ICameraProvider& provider,
                      CameraCommandMailbox& commands,
                      core::BufferPool& rawPool,
                      core::LatestValueSlot<core::RawFrame>& rawSlot,
                      core::IClock& clock,
                      core::LatestValueSlot<CameraStatusSnapshot>& statusSlot,
                      CameraStatusSnapshot initialStatus,
                      std::optional<camera::CameraConfiguration> preparedMode = std::nullopt);
    ~AcquisitionWorker();
    AcquisitionWorker(const AcquisitionWorker&) = delete;
    AcquisitionWorker& operator=(const AcquisitionWorker&) = delete;
    [[nodiscard]] core::Result<void> start();
    [[nodiscard]] core::Result<void> post(CameraCommand command);
    // Capacity-one typed attachment to the existing Apply mailbox. Context
    // storage is retained until cancellation or transfer to the worker binding.
    [[nodiscard]] core::Result<void> postReconfiguration(CameraCommand command,
        std::shared_ptr<LiveSessionContext> context, camera::CameraConfiguration preparedMode);
    [[nodiscard]] std::optional<CameraReconfigurationCompletion> takeReconfigurationCompletion();
    void requestStop() noexcept;
    void join() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::application
