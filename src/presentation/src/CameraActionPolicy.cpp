#include <lumora/presentation/CameraActionPolicy.hpp>
#include <lumora/application/StartupPreferences.hpp>

#include <algorithm>

namespace lumora::presentation {

CameraActionPolicy CameraActionPolicy::evaluate(const WorkstationState& state) {
    CameraActionPolicy result;
    const auto& camera = state.cameraStatus;
    const auto cameraState = camera ? camera->state
        : application::CameraSessionState::Disconnected;
    const auto& requested = state.requestedConfiguration;
    result.connectedIdle = camera
        && cameraState == application::CameraSessionState::ConnectedIdle;
    result.streaming = camera && cameraState == application::CameraSessionState::Streaming;
    result.sourceMatches = camera && camera->actualIdentity && state.selectedCameraId
        && camera->actualIdentity == state.selectedCameraId;
    result.selectedAvailable = camera && state.selectedCameraId
        && std::any_of(camera->discoveredDescriptors.begin(), camera->discoveredDescriptors.end(),
            [&](const auto& descriptor) {
                return descriptor.available && descriptor.id == *state.selectedCameraId;
            });
    result.applied = camera && requested && camera->requestedConfiguration
        && camera->appliedConfiguration && camera->requestedRevision != 0
        && camera->appliedRevision == camera->requestedRevision
        && application::cameraConfigurationsEqual(*requested, *camera->requestedConfiguration)
        && application::cameraConfigurationsEqual(*requested, camera->appliedConfiguration->requested);
    result.confirmed = camera && camera->confirmedRevision && camera->appliedRevision != 0
        && *camera->confirmedRevision == camera->appliedRevision;
    result.installationPending = state.installationProfilePending
        || (state.installationProfiles && state.installationProfiles->savePending);
    const bool ordinaryEnabled = state.controlsEnabled && !state.ordinaryOperationPending
        && !result.installationPending;
    const bool canDiscover = cameraState == application::CameraSessionState::Disconnected;
    const bool canInspect = result.sourceMatches && (result.connectedIdle || result.streaming);
    result.selectionEnabled = ordinaryEnabled;
    result.settings = {canInspect, state.controlsEnabled && canInspect};
    const bool canInspectInstallation = state.installationProfiles && canInspect;
    result.installation = {canInspectInstallation, state.controlsEnabled && canInspectInstallation};
    result.refresh = {canDiscover, ordinaryEnabled && canDiscover};
    result.connect = {canDiscover, ordinaryEnabled && canDiscover && result.selectedAvailable};
    const bool canApply = result.connectedIdle && result.sourceMatches && requested.has_value();
    result.apply = {canApply, ordinaryEnabled && canApply};
    const bool canConfirm = result.connectedIdle && result.sourceMatches && result.applied && !result.confirmed;
    result.confirm = {canConfirm, ordinaryEnabled && canConfirm && state.installationBindingCurrent};
    result.start = {result.connectedIdle && result.sourceMatches,
        ordinaryEnabled && result.connectedIdle && result.sourceMatches && result.applied
            && result.confirmed && state.installationBindingCurrent && state.contextBound};
    // Stop can cancel an admitted startup continuation before streaming begins.
    result.stop = {result.streaming, state.controlsEnabled
        && cameraState != application::CameraSessionState::ShuttingDown
        && (result.streaming || result.connectedIdle || state.ordinaryOperationPending)};
    const bool canDisconnect = camera && !canDiscover
        && cameraState != application::CameraSessionState::ShuttingDown;
    result.disconnect = {canDisconnect, state.controlsEnabled
        && cameraState != application::CameraSessionState::ShuttingDown
        && (canDisconnect || state.ordinaryOperationPending)};
    const bool canRetry = camera && camera->desiredIdentity
        && (cameraState == application::CameraSessionState::Error
            || cameraState == application::CameraSessionState::Reconnecting);
    result.retry = {canRetry, ordinaryEnabled && canRetry};
    result.resumeLive = {state.resumeLiveAvailable, ordinaryEnabled && result.connectedIdle
        && state.preferencesLoadCompleted && state.resumeLiveAvailable};
    return result;
}

bool CameraActionPolicy::allows(CameraStartupIntent intent) const noexcept {
    switch (intent) {
    case CameraStartupIntent::Refresh: return refresh.enabled;
    case CameraStartupIntent::Connect: return connect.enabled;
    case CameraStartupIntent::Apply: return apply.enabled;
    case CameraStartupIntent::Confirm: return confirm.enabled;
    case CameraStartupIntent::Start: return start.enabled;
    case CameraStartupIntent::Stop: return stop.enabled;
    case CameraStartupIntent::Disconnect: return disconnect.enabled;
    case CameraStartupIntent::Retry: return retry.enabled;
    case CameraStartupIntent::ResumeLive: return resumeLive.enabled;
    }
    return false;
}

}  // namespace lumora::presentation
