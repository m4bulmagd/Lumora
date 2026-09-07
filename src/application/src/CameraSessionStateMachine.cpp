#include <lumora/application/CameraSessionStateMachine.hpp>

namespace lumora::application {
namespace {
using S = CameraSessionState;
using E = CameraSessionEvent;

std::optional<S> nextState(S state, E event) {
    switch (event) {
    case E::ShutdownRequested:
        return S::ShuttingDown;
    case E::DisconnectRequested:
        return state;
    case E::DisconnectSucceeded:
        return S::Disconnected;
    case E::DisconnectFailed:
        return S::Error;
    case E::DiscoverRequested:
        if (state == S::Disconnected) { return S::Discovering; }
        break;
    case E::DiscoverSucceeded:
        if (state == S::Discovering) { return S::Disconnected; }
        break;
    case E::DiscoverFailed:
        if (state == S::Discovering) { return S::Error; }
        break;
    case E::ConnectRequested:
        if (state == S::ConnectedIdle || state == S::Streaming) { return state; }
        if (state == S::Disconnected || state == S::Error || state == S::Reconnecting) {
            return S::Connecting;
        }
        break;
    case E::OpenSucceeded:
        if (state == S::Connecting) { return S::ConnectedIdle; }
        break;
    case E::OpenFailed:
    case E::CapabilitiesFailed:
        if (state == S::Connecting) { return S::Error; }
        break;
    case E::ApplyRequested:
    case E::ApplySucceeded:
    case E::ApplyFailed:
    case E::ConfirmRequested:
        if (state == S::ConnectedIdle) { return state; }
        break;
    case E::StartRequested:
        if (state == S::ConnectedIdle || state == S::Streaming) { return state; }
        break;
    case E::StartSucceeded:
        if (state == S::ConnectedIdle || state == S::Streaming) { return S::Streaming; }
        break;
    case E::StartFailed:
        if (state == S::ConnectedIdle) { return S::Error; }
        break;
    case E::StopRequested:
    case E::StopSucceeded:
        if (state == S::Streaming) {
            return event == E::StopSucceeded ? S::ConnectedIdle : S::Streaming;
        }
        if (state == S::ConnectedIdle || state == S::Disconnected
            || state == S::Error || state == S::Reconnecting) { return state; }
        break;
    case E::StopFailed:
        if (state == S::ConnectedIdle || state == S::Streaming
            || state == S::Error || state == S::Reconnecting) { return S::Error; }
        break;
    case E::DeviceRemoved:
    case E::TimeoutThresholdReached:
        if (state == S::Streaming) { return S::Reconnecting; }
        break;
    case E::RetryRequested:
        if (state == S::Error || state == S::Reconnecting) { return S::Connecting; }
        break;
    }
    return std::nullopt;
}
}  // namespace

core::Result<CameraSessionStateMachine> CameraSessionStateMachine::fromInitialState(CameraSessionState state) {
    if (state != S::Disconnected && state != S::Error && state != S::Reconnecting) {
        return core::Result<CameraSessionStateMachine>::failure({core::ErrorCategory::CameraConfiguration,
            "invalid_initial_camera_state", "Initial camera state must not require a device.", "", false});
    }
    CameraSessionStateMachine machine;
    machine.state_ = state;
    return core::Result<CameraSessionStateMachine>::success(machine);
}

core::Result<void> CameraSessionStateMachine::apply(CameraSessionEvent event) {
    if (state_ == S::ShuttingDown && event != E::ShutdownRequested) {
        return core::Result<void>::failure({core::ErrorCategory::Cancelled,
            "cancelled", "Camera session is shutting down.", "", false});
    }
    if (const auto next = nextState(state_, event)) {
        state_ = *next;
        return core::Result<void>::success();
    }
    return core::Result<void>::failure({core::ErrorCategory::CameraConfiguration,
        "invalid_camera_state", "Camera command is unavailable in the current state.", "", false});
}

}  // namespace lumora::application
