#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/Result.hpp>

#include <chrono>
#include <memory>
#include <stop_token>

namespace lumora::camera {

// A device is thread-confined: all calls to one instance must be made by the
// same thread. It begins closed. open(), startStream(), stopStream(), and
// close() are idempotent lifecycle operations.
class ICameraDevice {
public:
    virtual ~ICameraDevice() = default;

    [[nodiscard]] virtual core::Result<void> open() = 0;
    [[nodiscard]] virtual core::Result<CameraCapabilities> capabilities() = 0;
    // Return authoritative current facts without changing configuration or
    // lifecycle state. Validate with validateCameraConfigurationReadback: actual
    // FPS is required, absent controls remain null, and readable Auto values may
    // be reported. Reading alone grants no application Apply/Confirm authority.
    [[nodiscard]] virtual core::Result<CameraConfiguration> readConfiguration() = 0;

    // Plan against fresh backend actual facts with planCameraConfigurationChange
    // and execute only the permitted changed nodes in its mask. Retained fixed
    // fields never authorize setters, including during rollback. Requests use
    // validateCameraConfiguration (Auto numeric requests are null); returned
    // actual facts follow the separate readback contract above.
    [[nodiscard]] virtual core::Result<AppliedCameraConfiguration> applyConfiguration(
        const CameraConfiguration& configuration) = 0;
    [[nodiscard]] virtual core::Result<void> startStream() = 0;

    // Waits no longer than timeout and is cancellable through stopToken.
    // Frame bytes are written only to destination. A successful result owns an
    // immutable RawFrame and never exposes adapter-owned memory. Failures use
    // core::ErrorCategory::Cancelled/"cancelled",
    // core::ErrorCategory::Acquisition/"acquisition_timeout", or
    // core::ErrorCategory::InvalidFrame/"invalid_frame" identities where
    // applicable.
    [[nodiscard]] virtual core::Result<std::shared_ptr<const core::RawFrame>> retrieve(
        std::chrono::milliseconds timeout,
        core::BufferPool& destination,
        std::stop_token stopToken = {}) = 0;

    [[nodiscard]] virtual core::Result<void> stopStream() noexcept = 0;
    [[nodiscard]] virtual core::Result<void> close() noexcept = 0;
};

}  // namespace lumora::camera
