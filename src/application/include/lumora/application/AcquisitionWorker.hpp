#pragma once

#include <lumora/application/CameraCommandMailbox.hpp>
#include <lumora/camera/ICameraProvider.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/LatestValueSlot.hpp>

#include <memory>

namespace lumora::application {

// Dependencies outlive this worker. The owner serializes start/join; posting
// and requesting cancellation are thread-safe. Destruction joins defensively.
class AcquisitionWorker final {
public:
    AcquisitionWorker(camera::ICameraProvider& provider,
                      CameraCommandMailbox& commands,
                      core::BufferPool& rawPool,
                      core::LatestValueSlot<core::RawFrame>& rawSlot,
                      core::IClock& clock,
                      core::LatestValueSlot<CameraStatusSnapshot>& statusSlot,
                      CameraStatusSnapshot initialStatus);
    ~AcquisitionWorker();
    AcquisitionWorker(const AcquisitionWorker&) = delete;
    AcquisitionWorker& operator=(const AcquisitionWorker&) = delete;
    [[nodiscard]] core::Result<void> start();
    [[nodiscard]] core::Result<void> post(CameraCommand command);
    void requestStop() noexcept;
    void join() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::application
