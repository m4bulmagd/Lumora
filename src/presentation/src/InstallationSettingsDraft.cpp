#include <lumora/presentation/InstallationSettingsDraft.hpp>

#include <algorithm>

namespace lumora::presentation {
void InstallationSettingsDraft::update(const WorkstationState& presentation) {
    const auto& camera = presentation.cameraStatus;
    const camera::CameraDescriptor* descriptor = nullptr;
    if (camera && camera->actualIdentity) {
        const auto found = std::find_if(camera->discoveredDescriptors.begin(),
            camera->discoveredDescriptors.end(), [&](const auto& value) {
                return value.id == *camera->actualIdentity;
            });
        if (found != camera->discoveredDescriptors.end()) descriptor = &*found;
    }
    if (!source_ && camera && camera->actualIdentity && camera->capabilities && descriptor) {
        source_ = camera;
        identity_ = descriptor->identity;
    }
    if (source_ && (!camera || camera->actualIdentity != source_->actualIdentity
        || presentation.selectedCameraId != source_->actualIdentity
        || camera->sessionGeneration != source_->sessionGeneration || !descriptor
        || !camera->capabilities
        || !application::cameraIdentityKeysEqual(*identity_, descriptor->identity)
        || !application::cameraCapabilitiesEqual(*source_->capabilities, *camera->capabilities))) {
        invalidated_ = true;
        confirmed_ = false;
    }
    const auto& repository = presentation.installationProfiles;
    // A newly opened inspector must seed from the completed save, not the
    // previous profile that remains published while the write is pending.
    if (!initialized_ && source_ && !invalidated_ && repository && repository->loadCompleted
        && !presentation.installationProfilePending && !repository->savePending) {
        orientation_ = presentation.activeOrientation.value_or(
            core::Orientation{false, false, core::Rotation::Degrees0});
        if (const auto* saved = application::findInstallationProfile(repository->profiles, *identity_))
            orientation_ = saved->orientation;
        initialized_ = true;
    }
    if (localPending_ && presentation.installationProfileOutcome
        && presentation.installationProfileOutcome->requestId != outcomeAtSubmission_) {
        localPending_ = false;
        confirmed_ = false;
    }
    presentation_ = presentation;
}

bool InstallationSettingsDraft::editable() const {
    const auto& repository = presentation_.installationProfiles;
    const auto& camera = presentation_.cameraStatus;
    return repository && repository->administratorMode && repository->loadCompleted
        && presentation_.controlsEnabled && source_ && initialized_ && !invalidated_
        && camera && camera->state == application::CameraSessionState::ConnectedIdle
        && !presentation_.ordinaryOperationPending && !pending();
}

bool InstallationSettingsDraft::pending() const {
    return localPending_ || presentation_.installationProfilePending
        || (presentation_.installationProfiles && presentation_.installationProfiles->savePending);
}

bool InstallationSettingsDraft::repairVisible() const {
    return presentation_.installationProfiles && presentation_.installationProfiles->loadError.has_value();
}

bool InstallationSettingsDraft::saveEnabled() const {
    return editable() && confirmed_ && (!repairVisible() || repair_);
}

bool InstallationSettingsDraft::setOrientation(core::Orientation orientation) {
    if (!editable()) return false;
    switch (orientation.rotation) {
    case core::Rotation::Degrees0:
    case core::Rotation::Degrees90:
    case core::Rotation::Degrees180:
    case core::Rotation::Degrees270: break;
    default: return false;
    }
    if (orientation_ != orientation) {
        orientation_ = orientation;
        confirmed_ = false;
    }
    return true;
}

bool InstallationSettingsDraft::setConfirmation(bool confirmed) {
    if (!editable()) return false;
    confirmed_ = confirmed;
    return true;
}

bool InstallationSettingsDraft::setRepairConsent(bool repair) {
    if (!editable() || (repair && !repairVisible())) return false;
    if (repair_ != repair) {
        repair_ = repair;
        confirmed_ = false;
    }
    return true;
}

std::optional<InstallationSettingsDraft::SaveRequest> InstallationSettingsDraft::prepareSave() {
    if (!saveEnabled()) return {};
    localPending_ = true;
    outcomeAtSubmission_ = presentation_.installationProfileOutcome
        ? std::optional{presentation_.installationProfileOutcome->requestId} : std::nullopt;
    confirmed_ = false;
    return SaveRequest{source_->sessionGeneration, *source_->actualIdentity, orientation_, true, repair_};
}

void InstallationSettingsDraft::rejectSubmission() {
    localPending_ = false;
    confirmed_ = false;
}
}  // namespace lumora::presentation
