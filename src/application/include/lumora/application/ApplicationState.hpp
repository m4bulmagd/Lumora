#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Error.hpp>

#include <cstdint>
#include <optional>

namespace lumora::application {

enum class CameraSessionState {
    Disconnected, Discovering, Connecting, ConnectedIdle,
    Streaming, Reconnecting, Error, ShuttingDown,
};

// The worker authorizes identity, generation, revision and outstanding-operation
// guards before reporting these facts to the pure state machine.
enum class CameraSessionEvent {
    DiscoverRequested, DiscoverSucceeded, DiscoverFailed,
    ConnectRequested, OpenSucceeded, OpenFailed, CapabilitiesFailed,
    ApplyRequested, ApplySucceeded, ApplyFailed, ConfirmRequested,
    StartRequested, StartSucceeded, StartFailed,
    StopRequested, StopSucceeded, StopFailed,
    DeviceRemoved, TimeoutThresholdReached, RetryRequested,
    DisconnectRequested, DisconnectSucceeded, DisconnectFailed, ShutdownRequested,
};

enum class CameraRecoveryMode { OperatorRequestedRetry };

struct CameraCommandOutcome final {
    std::uint64_t requestId;
    std::optional<core::Error> error;
};

struct CameraMailboxStats final {
    // Saturating admission totals, not per-request execution outcomes. Rejected
    // new posts are not counted as cancellation of already admitted work.
    std::uint64_t coalesced{0U};
    std::uint64_t cancelled{0U};
};

// Publish as an immutable snapshot; admission is not execution success.
struct CameraStatusSnapshot final {
    CameraSessionState state{CameraSessionState::Disconnected};
    std::optional<camera::CameraId> desiredIdentity;
    std::optional<camera::CameraId> actualIdentity;
    std::uint64_t sessionGeneration{0U};
    std::optional<camera::CameraCapabilities> capabilities;
    std::optional<camera::CameraConfiguration> requestedConfiguration;
    std::optional<camera::AppliedCameraConfiguration> appliedConfiguration;
    std::uint64_t requestedRevision{0U};
    std::uint64_t appliedRevision{0U};
    std::optional<std::uint64_t> confirmedRevision;
    bool restoreEligible{false};
    bool desiredStreaming{false};
    std::uint64_t consecutiveTimeouts{0U};
    CameraRecoveryMode recoveryMode{CameraRecoveryMode::OperatorRequestedRetry};
    std::optional<core::Error> latestError;
    std::optional<CameraCommandOutcome> latestOutcome;
    CameraMailboxStats mailboxStats;
};

}  // namespace lumora::application
