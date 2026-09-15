#include "InstallationAdapter.hpp"

#include <lumora/presentation/CameraActionPolicy.hpp>
#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <QStringList>
#include <utility>

namespace lumora::qml {
namespace {
QString orientationDescription(core::Orientation orientation) {
    QStringList parts;
    if (orientation.flipHorizontal) parts.append(InstallationAdapter::tr("H flip"));
    if (orientation.flipVertical) parts.append(InstallationAdapter::tr("V flip"));
    switch (orientation.rotation) {
    case core::Rotation::Degrees0: parts.append(QStringLiteral("0°")); break;
    case core::Rotation::Degrees90: parts.append(QStringLiteral("90°")); break;
    case core::Rotation::Degrees180: parts.append(QStringLiteral("180°")); break;
    case core::Rotation::Degrees270: parts.append(QStringLiteral("270°")); break;
    }
    return parts.join(QStringLiteral(" · "));
}
}

InstallationAdapter::InstallationAdapter(presentation::WorkstationCoordinator& coordinator, QObject* parent)
    : QObject(parent), coordinator_(coordinator) {}

void InstallationAdapter::refresh() {
    if (!draft_) return;
    draft_->update(coordinator_.state());
    const auto& presentation = draft_->presentation();
    const auto& repository = presentation.installationProfiles;
    State next;
    next.open = true;
    next.editable = !closing_ && draft_->editable();
    next.saveEnabled = !closing_ && draft_->saveEnabled();
    next.pending = draft_->pending();
    next.repairVisible = draft_->repairVisible();
    const auto orientation = draft_->orientation();
    next.flipHorizontal = orientation.flipHorizontal;
    next.flipVertical = orientation.flipVertical;
    next.rotationIndex = static_cast<int>(orientation.rotation);
    next.confirmationChecked = draft_->confirmationChecked();
    next.repairChecked = draft_->repairChecked();
    if (const auto& identity = draft_->identity()) {
        next.sourceSummary = tr("%1 %2 · %3").arg(QString::fromStdString(identity->manufacturer),
            QString::fromStdString(identity->model), QString::fromStdString(identity->serial));
    }
    next.activeSummary = presentation.activeOrientation
        ? tr("Active: %1").arg(orientationDescription(*presentation.activeOrientation))
        : tr("Active: no camera binding");
    const auto* saved = repository && draft_->identity()
        ? application::findInstallationProfile(repository->profiles, *draft_->identity()) : nullptr;
    next.savedSummary = saved
        ? tr("Saved revision %1: %2").arg(saved->revision).arg(orientationDescription(saved->orientation))
        : tr("Saved: no installation profile");

    if (draft_->invalidated()) {
        next.status = tr("Camera or session changed. Close and reopen this editor for the current camera.");
    } else if (!repository || !repository->loadCompleted) {
        next.status = tr("Loading installation settings…");
    } else if (!repository->administratorMode) {
        next.status = tr("Read-only. Editing requires an administrator launch with --installation.");
    } else if (next.pending) {
        next.status = tr("Saving installation settings… The active image orientation stays unchanged until Apply.");
    } else if (!presentation.cameraStatus
        || presentation.cameraStatus->state != application::CameraSessionState::ConnectedIdle
        || presentation.ordinaryOperationPending) {
        next.status = tr("Stop acquisition and wait for camera operations to finish before editing installation settings.");
    } else if (!commandError_.isEmpty()) {
        next.status = commandError_;
    } else if (repository->loadError) {
        next.status = tr("The installation file is invalid or unavailable: %1\nRepair requires separate consent and preservation of the original file.")
            .arg(QString::fromStdString(repository->loadError->operatorSummary));
    } else if (presentation.installationProfileOutcome && presentation.installationProfileOutcome->error) {
        next.status = tr("Save failed: %1\nReview settings and confirm again to retry.")
            .arg(QString::fromStdString(presentation.installationProfileOutcome->error->operatorSummary));
    } else if (presentation.installationProfileOutcome && presentation.installationProfileOutcome->savedProfile) {
        next.status = tr("Installation saved. Apply → review Original and Enhanced → Confirm → Start. Saving does not activate these settings.");
    } else {
        next.status = tr("Save installation → Apply → review Original and Enhanced → Confirm → Start.");
    }
    if (state_ != next) {
        state_ = std::move(next);
        emit stateChanged();
    }
}

void InstallationAdapter::setClosing() {
    closing_ = true;
    closeSettings();
}

bool InstallationAdapter::openSettings() {
    if (closing_) return false;
    coordinator_.poll();
    if (draft_) {
        refresh();
        return true;
    }
    if (!presentation::CameraActionPolicy::evaluate(coordinator_.state()).installation.enabled) return false;
    draft_.emplace();
    commandError_.clear();
    refresh();
    return true;
}

void InstallationAdapter::closeSettings() {
    draft_.reset();
    commandError_.clear();
    if (state_ != State{}) {
        state_ = {};
        emit stateChanged();
    }
}

bool InstallationAdapter::prepareEdit() {
    if (closing_ || !draft_) return false;
    coordinator_.poll();
    refresh();
    if (!draft_->editable()) return false;
    commandError_.clear();
    return true;
}

bool InstallationAdapter::setFlipHorizontal(bool value) {
    if (!prepareEdit()) return false;
    auto orientation = draft_->orientation();
    orientation.flipHorizontal = value;
    const bool accepted = draft_->setOrientation(orientation);
    refresh();
    return accepted;
}

bool InstallationAdapter::setFlipVertical(bool value) {
    if (!prepareEdit()) return false;
    auto orientation = draft_->orientation();
    orientation.flipVertical = value;
    const bool accepted = draft_->setOrientation(orientation);
    refresh();
    return accepted;
}

bool InstallationAdapter::setRotation(int value) {
    if (value < 0 || value > 3 || !prepareEdit()) return false;
    auto orientation = draft_->orientation();
    orientation.rotation = static_cast<core::Rotation>(value);
    const bool accepted = draft_->setOrientation(orientation);
    refresh();
    return accepted;
}

bool InstallationAdapter::setConfirmation(bool value) {
    if (!prepareEdit()) return false;
    const bool accepted = draft_->setConfirmation(value);
    refresh();
    return accepted;
}

bool InstallationAdapter::setRepairConsent(bool value) {
    if (!prepareEdit()) return false;
    const bool accepted = draft_->setRepairConsent(value);
    refresh();
    return accepted;
}

bool InstallationAdapter::save() {
    if (!prepareEdit()) return false;
    const auto request = draft_->prepareSave();
    if (!request) return false;
    const auto result = coordinator_.saveInstallationProfile(request->sessionGeneration,
        request->cameraId, request->orientation, request->confirmed, request->repairInvalid);
    if (!result.hasValue()) {
        draft_->rejectSubmission();
        commandError_ = tr("Save failed: %1\nReview settings and confirm again to retry.")
            .arg(QString::fromStdString(result.error().operatorSummary));
    }
    refresh();
    return result.hasValue();
}
}  // namespace lumora::qml
