#include <lumora/presentation/CameraSettingsDraft.hpp>
#include <lumora/presentation/CameraSettingsModel.hpp>

#include <lumora/application/CameraSettingsPolicy.hpp>
#include <lumora/application/StartupPreferences.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <limits>
#include <utility>

namespace lumora::presentation {
namespace {
bool samePixelFormat(
    const core::SourcePixelFormat& left,
    const core::SourcePixelFormat& right) {
    return left.canonicalName == right.canonicalName
        && left.canonicalEncoding == right.canonicalEncoding
        && left.validBits == right.validBits
        && left.sampleMaximum == right.sampleMaximum
        && left.packing == right.packing
        && left.alignment == right.alignment
        && left.applicationStorage == right.applicationStorage;
}

bool sameSourceMode(
    const camera::CameraConfiguration& actual,
    const camera::CameraConfiguration& requested) {
    return samePixelFormat(actual.pixelFormat, requested.pixelFormat)
        && actual.roi.x == requested.roi.x
        && actual.roi.y == requested.roi.y
        && actual.roi.width == requested.roi.width
        && actual.roi.height == requested.roi.height;
}

}  // namespace

void CameraSettingsDraft::initialize() {
    source_ = presentation_.cameraStatus;
    draft_ = presentation_.requestedConfiguration;
    if (!draft_ && source_) draft_ = source_->requestedConfiguration;
    observedRequest_ = presentation_.requestedConfiguration
        ? presentation_.requestedConfiguration
        : source_ ? source_->requestedConfiguration : std::nullopt;
    observedRevision_ = source_ ? source_->requestedRevision : 0;
    if (!source_ || !source_->actualIdentity || !source_->capabilities || !draft_
        || !source_->currentConfiguration) {
        normalizationFailed_ = true;
        return;
    }
    const auto& capabilities = *source_->capabilities;
    auto normalized = normalizeCameraSettingsDraft(
        *draft_, *source_->currentConfiguration, capabilities);
    if (!normalized.hasValue()) {
        normalizationFailed_ = true;
        return;
    }
    draft_ = std::move(normalized).value();
    if (presentation_.selectedCameraId != source_->actualIdentity) invalidated_ = true;
}

void CameraSettingsDraft::update(const WorkstationState& next) {
    presentation_ = next;
    if (!initialized_) {
        initialized_ = true;
        initialize();
    } else if (source_) {
        const auto& current = presentation_.cameraStatus;
        const auto request = presentation_.requestedConfiguration
            ? presentation_.requestedConfiguration
            : current ? current->requestedConfiguration : std::nullopt;
        const bool sameCamera = current && source_->actualIdentity
            && current->actualIdentity == source_->actualIdentity
            && presentation_.selectedCameraId == source_->actualIdentity;
        const bool sameCapabilities = current && source_->capabilities && current->capabilities
            && application::cameraCapabilitiesEqual(*source_->capabilities, *current->capabilities);
        if (sameCamera && sameCapabilities && draft_) {
            if (!current->currentConfiguration) {
                invalidated_ = true;
            } else {
                const auto validReadback = camera::validateCameraConfigurationReadback(
                    *current->currentConfiguration, *current->capabilities);
                if (!validReadback.hasValue()
                    || !cameraSettingsFixedFieldsMatchCurrent(
                        *draft_, *current->currentConfiguration, *current->capabilities)) {
                    invalidated_ = true;
                }
            }
        }
        if (submitted_ && !submissionAdmitted_) {
            submissionAdmitted_ = sameCamera && sameCapabilities
                && current->sessionGeneration == source_->sessionGeneration
                && presentation_.ordinaryOperationPending && request
                && application::cameraConfigurationsEqual(*request, *submitted_);
            if (!submissionAdmitted_) submitted_.reset();
        }
        const bool ownSuccessfulRebind = !invalidated_ && !rebindCompleted_
            && submitted_ && submissionAdmitted_
            && sameCamera && sameCapabilities && !current->sourceReplacementRequired
            && current->state == application::CameraSessionState::ConnectedIdle
            && source_->sessionGeneration != std::numeric_limits<std::uint64_t>::max()
            && current->sessionGeneration == source_->sessionGeneration + 1U
            && request && current->requestedConfiguration && current->appliedConfiguration
            && current->currentConfiguration
            && current->requestedRevision > observedRevision_
            && current->requestedRevision == current->appliedRevision
            && application::cameraConfigurationsEqual(*request, *submitted_)
            && application::cameraConfigurationsEqual(
                *current->requestedConfiguration, *submitted_)
            && application::cameraConfigurationsEqual(
                current->appliedConfiguration->requested, *submitted_)
            && sameSourceMode(current->appliedConfiguration->actual, *submitted_)
            && sameSourceMode(*current->currentConfiguration, *submitted_);
        if (ownSuccessfulRebind) {
            rebindCompleted_ = true;
            completedGeneration_ = current->sessionGeneration;
            observedRequest_ = request;
            observedRevision_ = current->requestedRevision;
        } else if (rebindCompleted_) {
            if (!current || !completedGeneration_
                || current->sessionGeneration != *completedGeneration_
                || !sameCamera || !sameCapabilities || !submitted_ || !request
                || current->sourceReplacementRequired
                || current->state != application::CameraSessionState::ConnectedIdle
                || !current->requestedConfiguration || !current->appliedConfiguration
                || !current->currentConfiguration
                || current->requestedRevision != observedRevision_
                || current->appliedRevision != observedRevision_
                || !application::cameraConfigurationsEqual(*request, *submitted_)
                || !application::cameraConfigurationsEqual(
                    *current->requestedConfiguration, *submitted_)
                || !application::cameraConfigurationsEqual(
                    current->appliedConfiguration->requested, *submitted_)
                || !sameSourceMode(current->appliedConfiguration->actual, *submitted_)
                || !sameSourceMode(*current->currentConfiguration, *submitted_)) {
                rebindCompleted_ = false;
                invalidated_ = true;
            }
        } else if (!current || !source_->actualIdentity
            || current->actualIdentity != source_->actualIdentity
            || current->sessionGeneration != source_->sessionGeneration
            || current->sourceReplacementRequired
            || !sameCapabilities
            || presentation_.selectedCameraId != source_->actualIdentity) {
            invalidated_ = true;
        } else {
            const bool changed = current->requestedRevision != observedRevision_
                || request.has_value() != observedRequest_.has_value()
                || (request && observedRequest_
                    && !application::cameraConfigurationsEqual(*request, *observedRequest_));
            if (changed) {
                if (!submitted_ || !request || !application::cameraConfigurationsEqual(*request, *submitted_)) {
                    invalidated_ = true;
                } else {
                    observedRequest_ = request;
                    if (current->requestedRevision != observedRevision_) submitted_.reset();
                    observedRevision_ = current->requestedRevision;
                }
            }
        }
    } else {
        invalidated_ = true;
    }
}

std::optional<camera::CameraConfiguration> CameraSettingsDraft::readback() const {
    const auto& current = presentation_.cameraStatus;
    const bool originalSource = source_ && current
        && current->actualIdentity == source_->actualIdentity
        && current->sessionGeneration == source_->sessionGeneration;
    const bool completedRebind = rebindCompleted_ && source_ && current
        && completedGeneration_ && current->actualIdentity == source_->actualIdentity
        && current->sessionGeneration == *completedGeneration_;
    return originalSource || completedRebind ? current->currentConfiguration : std::nullopt;
}

bool CameraSettingsDraft::editable() const {
    const auto& current = presentation_.cameraStatus;
    return initialized_ && !normalizationFailed_ && !invalidated_ && !rebindCompleted_
        && draft_ && source_ && source_->actualIdentity && source_->capabilities && current
        && current->state == application::CameraSessionState::ConnectedIdle
        && current->sessionGeneration == source_->sessionGeneration
        && current->actualIdentity == source_->actualIdentity
        && !current->sourceReplacementRequired
        && presentation_.selectedCameraId == source_->actualIdentity
        && presentation_.controlsEnabled && !presentation_.ordinaryOperationPending;
}

std::optional<CameraSettingsDraft::SourceToken> CameraSettingsDraft::beginEditing() {
    if (editingIntentReported_ || !editable()) return std::nullopt;
    editingIntentReported_ = true;
    return SourceToken{source_->sessionGeneration, *source_->actualIdentity};
}

bool CameraSettingsDraft::setConfiguration(camera::CameraConfiguration configuration) {
    if (!editable()) return false;
    draft_ = std::move(configuration);
    return true;
}

bool CameraSettingsDraft::canApply() const {
    const auto& current = presentation_.cameraStatus;
    return editable() && current->currentConfiguration
        && application::isSupportedLiveCameraConfiguration(*draft_)
        && camera::validateCameraConfiguration(*draft_, *source_->capabilities).hasValue()
        && camera::planCameraConfigurationChange(
            *draft_, *current->currentConfiguration, *source_->capabilities, false).hasValue();
}

std::optional<CameraSettingsDraft::ApplyRequest> CameraSettingsDraft::prepareApply() {
    if (!canApply()) return std::nullopt;
    submitted_ = draft_;
    submissionAdmitted_ = false;
    return ApplyRequest{{source_->sessionGeneration, *source_->actualIdentity}, *draft_};
}

}  // namespace lumora::presentation
