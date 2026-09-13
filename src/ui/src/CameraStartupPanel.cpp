#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/CameraSettingsDialog.hpp>
#include <lumora/ui/InstallationSettingsDialog.hpp>
#include <lumora/ui/OrientationPresentation.hpp>
#include <lumora/application/StartupPreferences.hpp>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace lumora::ui {
namespace {

QPushButton* makeButton(
    const QString& text,
    const char* objectName,
    QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(QString::fromLatin1(objectName));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

}  // namespace

CameraStartupPanel::CameraStartupPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("cameraStartupPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* title = new QLabel(tr("Camera"), this);
    title->setObjectName(QStringLiteral("cameraPanelTitleLabel"));
    title->setAccessibleName(tr("Camera controls"));
    title->setStyleSheet(QStringLiteral("QLabel { font-weight: 600; }"));
    layout->addWidget(title);

    auto* state = new QLabel(tr("Waiting"), this);
    state->setObjectName(QStringLiteral("cameraStartupStateLabel"));
    state->setAccessibleName(tr("Camera connection and acquisition state"));
    state->setTextFormat(Qt::PlainText);
    state->setWordWrap(true);
    layout->addWidget(state);

    auto* viewerState = new QLabel(tr("Viewer: waiting for image"), this);
    viewerState->setObjectName(QStringLiteral("cameraViewerStateLabel"));
    viewerState->setAccessibleName(tr("Viewer pause and image freshness state"));
    viewerState->setTextFormat(Qt::PlainText);
    viewerState->setWordWrap(true);
    layout->addWidget(viewerState);

    auto* cameras = new QComboBox(this);
    cameras->setObjectName(QStringLiteral("cameraSelectionCombo"));
    cameras->setAccessibleName(tr("Camera selection"));
    cameras->setPlaceholderText(tr("Select a camera"));
    cameras->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cameras->setMinimumContentsLength(10);
    layout->addWidget(cameras);

    auto* guidance = new QLabel(this);
    guidance->setObjectName(QStringLiteral("cameraGuidanceLabel"));
    guidance->setAccessibleName(tr("Camera action guidance"));
    guidance->setTextFormat(Qt::PlainText);
    guidance->setWordWrap(true);
    guidance->hide();
    layout->addWidget(guidance);

    auto* actionGrid = new QGridLayout;
    actionGrid->setContentsMargins(0, 0, 0, 0);
    actionGrid->setHorizontalSpacing(6);
    actionGrid->setVerticalSpacing(4);
    auto* refresh = makeButton(tr("Refresh"), "refreshCameraButton", this);
    auto* connectButton = makeButton(tr("Connect"), "connectCameraButton", this);
    actionGrid->addWidget(refresh, 0, 0);
    actionGrid->addWidget(connectButton, 0, 1);

    auto* reviewToggle = new QToolButton(this);
    reviewToggle->setObjectName(QStringLiteral("cameraReviewToggle"));
    reviewToggle->setText(tr("Review"));
    reviewToggle->setAccessibleName(tr("Review requested and actual camera settings"));
    reviewToggle->setToolTip(tr("Review requested and actual camera settings"));
    reviewToggle->setCheckable(true);
    reviewToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    reviewToggle->setArrowType(Qt::RightArrow);
    reviewToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* reviewDetails = new QWidget(this);
    reviewDetails->setObjectName(QStringLiteral("cameraReviewDetails"));
    auto* reviewLayout = new QVBoxLayout(reviewDetails);
    reviewLayout->setContentsMargins(0, 0, 0, 0);
    reviewLayout->setSpacing(2);
    auto* requestedHeading = new QLabel(tr("Requested"), reviewDetails);
    auto* requested = new QLabel(tr("Not available"), this);
    requested->setObjectName(QStringLiteral("requestedConfigurationLabel"));
    requested->setTextFormat(Qt::PlainText);
    requested->setWordWrap(true);
    auto* actualHeading = new QLabel(tr("Actual"), reviewDetails);
    auto* actual = new QLabel(tr("Not available"), this);
    actual->setObjectName(QStringLiteral("actualConfigurationLabel"));
    actual->setTextFormat(Qt::PlainText);
    actual->setWordWrap(true);
    reviewLayout->addWidget(requestedHeading);
    reviewLayout->addWidget(requested);
    reviewLayout->addWidget(actualHeading);
    reviewLayout->addWidget(actual);
    reviewDetails->hide();
    layout->addLayout(actionGrid);
    layout->addWidget(reviewToggle);
    layout->addWidget(reviewDetails);
    auto* settings = makeButton(tr("Settings…"), "cameraSettingsButton", this);
    settings->setAccessibleName(tr("Camera settings"));
    auto* installationStatus = new QLabel(this);
    installationStatus->setObjectName("installationProfileStatusLabel");
    installationStatus->setTextFormat(Qt::PlainText); installationStatus->setWordWrap(true);
    auto* installation = makeButton(tr("Installation…"), "installationSettingsButton", this);
    installation->setAccessibleName(tr("Installation settings"));
    auto* settingsLayout = new QVBoxLayout;
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(2);
    settingsLayout->addWidget(settings);
    settingsLayout->addWidget(installation);
    layout->addLayout(settingsLayout);
    layout->addWidget(installationStatus);
    auto* apply = makeButton(tr("Apply"), "applyCameraButton", this);
    auto* confirm = makeButton(tr("Confirm"), "confirmCameraButton", this);
    auto* start = makeButton(tr("Start"), "startCameraButton", this);
    auto* stop = makeButton(tr("Stop"), "stopCameraButton", this);
    auto* disconnect = makeButton(tr("Disconnect"), "disconnectCameraButton", this);
    auto* retry = makeButton(tr("Retry"), "retryCameraButton", this);
    auto* resumeLive = makeButton(tr("Resume Live"), "resumeLiveButton", this);
    actionGrid = new QGridLayout;
    actionGrid->setContentsMargins(0, 0, 0, 0);
    actionGrid->setHorizontalSpacing(6);
    actionGrid->setVerticalSpacing(4);
    actionGrid->addWidget(apply, 0, 0, 1, 2);
    actionGrid->addWidget(confirm, 1, 0, 1, 2);
    actionGrid->addWidget(start, 2, 0);
    actionGrid->addWidget(stop, 2, 0);
    actionGrid->addWidget(disconnect, 2, 1);
    actionGrid->addWidget(retry, 3, 0, 1, 2);
    actionGrid->addWidget(resumeLive, 4, 0, 1, 2);
    layout->addLayout(actionGrid);

    auto* warning = new QLabel(this);
    warning->setObjectName(QStringLiteral("startupWarningLabel"));
    warning->setTextFormat(Qt::PlainText);
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("QLabel { color: #ffcc66; }"));
    layout->addWidget(warning);

    connect(reviewToggle, &QToolButton::toggled, this,
        [reviewToggle, reviewDetails](bool expanded) {
            reviewToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            reviewDetails->setVisible(expanded);
        });

    connect(cameras, &QComboBox::activated, this, [this, cameras](int index) {
        const camera::CameraId selected{cameras->itemData(index).toString().toStdString()};
        if (settingsDialog_) {
            auto next = presentation_;
            next.selectedCameraId = selected;
            settingsDialog_->setPresentation(std::move(next));
        }
        if (installationDialog_) {
            auto next = presentation_; next.selectedCameraId = selected;
            installationDialog_->setPresentation(std::move(next));
        }
        emit selectionRequested(selected);
    });
    connect(installation, &QPushButton::clicked, this, [this] {
        if (!installationDialog_) {
            installationDialog_ = new InstallationSettingsDialog(this);
            installationDialog_->setAttribute(Qt::WA_DeleteOnClose);
            connect(installationDialog_, &InstallationSettingsDialog::installationSaveRequested,
                this, &CameraStartupPanel::installationSaveRequested);
            connect(installationDialog_, &QDialog::finished, this, [this] { installationDialog_ = nullptr; });
            installationDialog_->setPresentation(presentation_);
        }
        installationDialog_->show(); installationDialog_->raise(); installationDialog_->activateWindow();
    });
    connect(settings, &QPushButton::clicked, this, [this] {
        if (!settingsDialog_) {
            settingsDialog_ = new CameraSettingsDialog(this);
            settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
            connect(settingsDialog_, &CameraSettingsDialog::settingsEditingStarted,
                this, &CameraStartupPanel::settingsEditingStarted);
            connect(settingsDialog_, &CameraSettingsDialog::settingsApplyRequested,
                this, &CameraStartupPanel::settingsApplyRequested);
            connect(settingsDialog_, &QDialog::finished, this, [this] { settingsDialog_ = nullptr; });
            settingsDialog_->setPresentation(presentation_);
        }
        settingsDialog_->show();
        settingsDialog_->raise();
        settingsDialog_->activateWindow();
    });
    connect(refresh, &QPushButton::clicked, this, &CameraStartupPanel::refreshRequested);
    connect(connectButton, &QPushButton::clicked, this, &CameraStartupPanel::connectRequested);
    connect(apply, &QPushButton::clicked, this, &CameraStartupPanel::applyRequested);
    connect(confirm, &QPushButton::clicked, this, &CameraStartupPanel::confirmRequested);
    connect(start, &QPushButton::clicked, this, &CameraStartupPanel::startRequested);
    connect(stop, &QPushButton::clicked, this, &CameraStartupPanel::stopRequested);
    connect(disconnect, &QPushButton::clicked, this, &CameraStartupPanel::disconnectRequested);
    connect(retry, &QPushButton::clicked, this, &CameraStartupPanel::retryRequested);
    connect(resumeLive, &QPushButton::clicked, this, &CameraStartupPanel::resumeLiveRequested);

    updatePresentation();
}

const CameraStartupPanelPresentation& CameraStartupPanel::presentation() const noexcept {
    return presentation_;
}

void CameraStartupPanel::setPresentation(CameraStartupPanelPresentation presentation) {
    presentation_ = std::move(presentation);
    updatePresentation();
    if (settingsDialog_) settingsDialog_->setPresentation(presentation_);
    if (installationDialog_) installationDialog_->setPresentation(presentation_);
}

void CameraStartupPanel::updatePresentation() {
    const auto describe = [this](const camera::CameraConfiguration& configuration) {
        const auto optionalNumber = [this](const std::optional<double>& value) {
            return value ? locale().toString(*value, 'g', QLocale::FloatingPointShortest) : tr("Automatic");
        };
        const auto exposure = !configuration.exposure.mode
            ? tr("Unavailable")
            : configuration.exposure.mode == camera::ExposureMode::Manual
                ? tr("Manual %1 µs").arg(optionalNumber(
                      configuration.exposure.requestedMicroseconds))
                : configuration.exposure.requestedMicroseconds
                    ? tr("Automatic (actual %1 µs)").arg(optionalNumber(
                          configuration.exposure.requestedMicroseconds))
                    : tr("Automatic");
        const auto gain = !configuration.gain.mode
            ? tr("Unavailable")
            : configuration.gain.mode == camera::GainMode::Manual
                ? tr("Manual %1 dB").arg(optionalNumber(configuration.gain.requestedDb))
                : configuration.gain.requestedDb
                    ? tr("Automatic (actual %1 dB)").arg(optionalNumber(
                          configuration.gain.requestedDb))
                    : tr("Automatic");
        const auto acquisition =
            configuration.acquisitionMode == camera::AcquisitionMode::Continuous
            ? tr("Continuous")
            : tr("Triggered");
        return tr("%1 · ROI x %2, y %3, %4 × %5 · %6 fps\nExposure: %7\nGain: %8 · %9")
            .arg(QString::fromStdString(configuration.pixelFormat.canonicalName))
            .arg(configuration.roi.x)
            .arg(configuration.roi.y)
            .arg(configuration.roi.width)
            .arg(configuration.roi.height)
            .arg(optionalNumber(configuration.requestedFps))
            .arg(exposure, gain, acquisition);
    };

    auto* requested = findChild<QLabel*>(QStringLiteral("requestedConfigurationLabel"));
    auto* actual = findChild<QLabel*>(QStringLiteral("actualConfigurationLabel"));
    const auto& status = presentation_.cameraStatus;
    const auto requestedConfiguration = presentation_.requestedConfiguration
        ? presentation_.requestedConfiguration
        : status ? status->requestedConfiguration : std::nullopt;
    requested->setText(requestedConfiguration ? describe(*requestedConfiguration)
                                              : tr("Not available"));
    actual->setText(status && status->currentConfiguration
            ? describe(*status->currentConfiguration)
            : tr("Not available"));

    auto* cameras = findChild<QComboBox*>(QStringLiteral("cameraSelectionCombo"));
    QStringList labels;
    QStringList identities;
    if (status) {
        for (const auto& descriptor : status->discoveredDescriptors) {
            auto display = tr("%1 %2 — %3")
                .arg(QString::fromStdString(descriptor.identity.manufacturer),
                    QString::fromStdString(descriptor.identity.model),
                    QString::fromStdString(descriptor.identity.serial));
            if (!descriptor.available) display += tr(" — Unavailable");
            labels.push_back(display);
            identities.push_back(QString::fromStdString(descriptor.id.value));
        }
    }
    bool discoveryChanged = cameras->count() != labels.size();
    for (int index = 0; !discoveryChanged && index < cameras->count(); ++index) {
        discoveryChanged = cameras->itemText(index) != labels.at(index)
            || cameras->itemData(index).toString() != identities.at(index);
    }
    if (discoveryChanged) {
        cameras->clear();
        for (qsizetype index = 0; index < labels.size(); ++index) {
            cameras->addItem(labels.at(index), identities.at(index));
        }
    }
    int selectedIndex = -1;
    if (presentation_.selectedCameraId) {
        const auto selected = QString::fromStdString(
            presentation_.selectedCameraId->value);
        for (int index = 0; index < cameras->count(); ++index) {
            if (cameras->itemData(index).toString() == selected) {
                selectedIndex = index;
                break;
            }
        }
    }
    cameras->setCurrentIndex(selectedIndex);

    const auto cameraState = status ? status->state
                                    : application::CameraSessionState::Disconnected;
    auto stateText = tr("Waiting");
    if (presentation_.preferencesLoadCompleted && status) {
        switch (cameraState) {
        case application::CameraSessionState::Disconnected: stateText = tr("Disconnected"); break;
        case application::CameraSessionState::Discovering: stateText = tr("Discovering"); break;
        case application::CameraSessionState::Connecting: stateText = tr("Connecting"); break;
        case application::CameraSessionState::ConnectedIdle: stateText = tr("Connected — idle"); break;
        case application::CameraSessionState::Streaming: stateText = tr("Streaming"); break;
        case application::CameraSessionState::Reconnecting: stateText = tr("Reconnecting"); break;
        case application::CameraSessionState::Error: stateText = tr("Camera error"); break;
        case application::CameraSessionState::ShuttingDown: stateText = tr("Shutting down"); break;
        }
    }
    findChild<QLabel*>(QStringLiteral("cameraStartupStateLabel"))->setText(stateText);
    const auto& workstation = presentation_.workstationStatus;
    QString viewerText;
    if (workstation.viewerState == ViewerState::Paused) {
        switch (workstation.freshness) {
        case FrameFreshness::Current: viewerText = tr("Viewer: Paused — current image retained"); break;
        case FrameFreshness::Stale: viewerText = tr("Viewer: Paused — stale image retained"); break;
        case FrameFreshness::WaitingForFrame: viewerText = tr("Viewer: Paused — waiting for image"); break;
        }
    } else {
        switch (workstation.freshness) {
        case FrameFreshness::Current: viewerText = tr("Viewer: Live"); break;
        case FrameFreshness::Stale: viewerText = tr("Viewer: Stale image"); break;
        case FrameFreshness::WaitingForFrame: viewerText = tr("Viewer: Waiting for image"); break;
        }
    }
    findChild<QLabel*>(QStringLiteral("cameraViewerStateLabel"))->setText(viewerText);
    auto* warning = findChild<QLabel*>(QStringLiteral("startupWarningLabel"));
    const auto warningValue = presentation_.startupWarning
        ? presentation_.startupWarning
        : status ? status->latestError : std::nullopt;
    const auto warningSummary = [this](const core::Error& error) {
        const auto hasSuffix = [&error](const std::string& suffix) {
            return error.code.size() >= suffix.size()
                && error.code.compare(error.code.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        const auto hasPrefix = [&error](const std::string& prefix) {
            return error.code.rfind(prefix, 0) == 0;
        };
        if (error.code == "startup_readback_changed")
            return tr("Camera readback changed. Review and confirm settings before Start.");
        if (error.code == "camera_actual_fps_invalid" || hasPrefix("capability_"))
            return tr("Camera capabilities or current readback are invalid. Disconnect, reconnect, and review the camera settings before applying.");
        if (hasSuffix("_not_writable"))
            return tr("A fixed camera setting changed. Reopen Settings and retain the current camera value.");
        if (hasSuffix("_not_writable_while_streaming"))
            return tr("Stop acquisition before changing this camera setting.");
        if (error.code == "camera_restore_mismatch")
            return tr("Camera rollback did not restore the previous settings. Disconnect and reconnect before continuing.");
        if (error.code == "camera_reconfiguration_busy")
            return tr("Wait for the current camera settings operation to finish.");
        if (error.code == "acquisition_timeout")
            return tr("Camera retrieval timed out. Check the camera connection.");
        if (error.code == "startup_save_source_unsafe" || error.code == "configuration_invalid_preservation_failed")
            return tr("Startup preferences were not saved because the existing configuration could not be read or preserved safely.");
        if (error.code == "configuration_invalid_preserved")
            return tr("Invalid configuration was preserved. Review and confirm settings before saving new preferences.");
        if (error.code == "configuration_read_failed")
            return tr("Configuration could not be read. Check file access before saving startup preferences.");
        if (error.code == "configuration_write_failed" || error.code == "configuration_replace_failed" ||
            error.code == "configuration_directory_unavailable")
            return tr("Startup preferences could not be saved. Check configuration file permissions and available disk space.");
        if (error.code == "configuration_not_confirmed" || error.code == "configuration_not_applied")
            return tr("Apply settings and explicitly confirm the camera readback before Start.");
        if (error.code == "camera_identity_required" || error.code == "camera_not_found" || error.code == "simulator_not_found")
            return tr("The selected camera is unavailable. Check its connection and select the intended camera.");
        if (error.code == "camera_format_not_available" || error.code == "unsupported_pipeline_mode" ||
            error.code == "unsupported_pipeline_request" || error.code == "camera_mode_change_requires_rebinding")
            return tr("The requested format, frame rate or image region is unavailable for this camera session. Review the requested settings.");
        if (error.code == "startup_action_unavailable" || error.code == "invalid_camera_state" ||
            error.code == "context_handoff_pending" || error.code == "context_not_bound")
            return tr("Wait for the current camera operation to finish before trying this action.");
        if (error.code == "startup_service_worker_exception" || error.code == "startup_service_start_failed")
            return tr("The startup preferences worker stopped unexpectedly. Preferences may not have been saved.");
        switch (error.category) {
        case core::ErrorCategory::CameraDiscovery: return tr("Camera discovery failed. Check camera connections and refresh the list.");
        case core::ErrorCategory::CameraConnection: return tr("The camera connection failed. Check the selected camera and use Retry or Disconnect.");
        case core::ErrorCategory::CameraConfiguration: return tr("Camera settings could not be accepted. Review the requested and actual settings.");
        case core::ErrorCategory::Acquisition: return tr("Camera acquisition failed. Check the camera status before restarting.");
        case core::ErrorCategory::InvalidFrame: return tr("An invalid camera frame was discarded. Check the camera format and connection.");
        case core::ErrorCategory::Processing: return tr("Image processing failed. The displayed image may no longer be live.");
        case core::ErrorCategory::Configuration: return tr("Startup preferences are unavailable or could not be saved. Review the configuration status.");
        case core::ErrorCategory::ResourceExhaustion: return tr("Live imaging resources are exhausted. Stop acquisition and check available memory.");
        case core::ErrorCategory::Cancelled: return tr("The camera operation was cancelled.");
        default: return tr("Camera startup encountered an error (%1).").arg(QString::fromStdString(error.code));
        }
    };
    warning->setText(warningValue
            ? tr("Warning: %1").arg(warningSummary(*warningValue))
            : QString{});
    warning->setVisible(warningValue.has_value());

    auto* start = findChild<QPushButton*>(QStringLiteral("startCameraButton"));
    const bool confirmed = status && status->confirmedRevision
        && status->appliedRevision != 0U
        && *status->confirmedRevision == status->appliedRevision;
    const bool applied = status && status->appliedConfiguration
        && requestedConfiguration && status->requestedConfiguration
        && application::cameraConfigurationsEqual(*requestedConfiguration, *status->requestedConfiguration)
        && status->requestedRevision != 0U
        && status->appliedRevision == status->requestedRevision;
    const bool globallyEnabled = presentation_.controlsEnabled;
    const bool installationPending = presentation_.installationProfilePending
        || (presentation_.installationProfiles && presentation_.installationProfiles->savePending);
    const bool ordinaryEnabled = globallyEnabled
        && !presentation_.ordinaryOperationPending && !installationPending;
    const bool connectedIdle = status
        && cameraState == application::CameraSessionState::ConnectedIdle;
    const bool streaming = status
        && cameraState == application::CameraSessionState::Streaming;
    const bool sourceMatches = status && status->actualIdentity
        && presentation_.selectedCameraId
        && status->actualIdentity == presentation_.selectedCameraId;
    const bool selectedAvailable = status && presentation_.selectedCameraId
        && std::any_of(status->discoveredDescriptors.begin(),
            status->discoveredDescriptors.end(), [&](const auto& descriptor) {
                return descriptor.available
                    && descriptor.id == *presentation_.selectedCameraId;
            });

    auto* guidance = findChild<QLabel*>(QStringLiteral("cameraGuidanceLabel"));
    QString guidanceText;
    if (!presentation_.selectedCameraId) {
        guidanceText = tr("Select the intended camera before connecting.");
    } else if (!selectedAvailable && cameraState == application::CameraSessionState::Disconnected) {
        guidanceText = tr("The selected camera is unavailable. Check its connection or select another camera.");
    } else if (status && status->actualIdentity && !sourceMatches) {
        guidanceText = tr("The connected camera does not match the current selection. Disconnect before changing source.");
    } else if (streaming) {
        guidanceText = tr("Stop acquisition before changing camera settings. Settings remain available for inspection.");
    }
    guidance->setText(guidanceText);
    guidance->setVisible(!guidanceText.isEmpty());

    auto* installation = findChild<QPushButton*>("installationSettingsButton");
    const bool installationAvailable = presentation_.installationProfiles != nullptr;
    installation->setVisible(installationAvailable && sourceMatches
        && (connectedIdle || streaming));
    installation->setEnabled(globallyEnabled && installationAvailable
        && sourceMatches && (connectedIdle || streaming));
    auto* installationStatus = findChild<QLabel*>("installationProfileStatusLabel");
    const bool hasInstallationStatus = installationAvailable
        || presentation_.activeOrientation.has_value()
        || presentation_.installationProfilePending
        || presentation_.installationProfileError.has_value()
        || presentation_.installationProfileOutcome.has_value()
        || !presentation_.installationBindingCurrent;
    installationStatus->setVisible(hasInstallationStatus);
    QString installationText = presentation_.activeOrientation
        ? tr("Active installation: %1").arg(orientationDescription(*presentation_.activeOrientation))
        : tr("Installation: no active camera binding");
    if (installationPending) installationText += tr("\nSaving installation settings…");
    else if (presentation_.installationProfileError)
        installationText += tr("\n%1").arg(QString::fromStdString(presentation_.installationProfileError->operatorSummary));
    else if (!presentation_.installationBindingCurrent)
        installationText += tr("\nReview installation settings, then Apply → review → Confirm → Start.");
    installationStatus->setText(installationText);

    auto* reviewToggle = findChild<QToolButton*>(QStringLiteral("cameraReviewToggle"));
    auto* reviewDetails = findChild<QWidget*>(QStringLiteral("cameraReviewDetails"));
    const bool hasReview = requestedConfiguration.has_value()
        || (status && status->currentConfiguration.has_value());
    const bool requiresReview = connectedIdle && sourceMatches && applied && !confirmed;
    if (requiresReview && !reviewRequired_) reviewToggle->setChecked(true);
    else if (!requiresReview && reviewRequired_) reviewToggle->setChecked(false);
    reviewRequired_ = requiresReview;
    reviewToggle->setVisible(hasReview);
    reviewDetails->setVisible(hasReview && reviewToggle->isChecked());

    auto* settings = findChild<QPushButton*>(QStringLiteral("cameraSettingsButton"));
    settings->setVisible(sourceMatches && (connectedIdle || streaming));
    settings->setEnabled(globallyEnabled && sourceMatches && (connectedIdle || streaming));
    cameras->setEnabled(ordinaryEnabled);
    auto* refresh = findChild<QPushButton*>(QStringLiteral("refreshCameraButton"));
    const bool canDiscover = cameraState == application::CameraSessionState::Disconnected;
    refresh->setVisible(canDiscover);
    refresh->setEnabled(ordinaryEnabled && canDiscover);
    auto* connectButton = findChild<QPushButton*>(QStringLiteral("connectCameraButton"));
    connectButton->setVisible(canDiscover);
    connectButton->setEnabled(ordinaryEnabled && selectedAvailable && canDiscover);
    auto* apply = findChild<QPushButton*>(QStringLiteral("applyCameraButton"));
    apply->setVisible(connectedIdle && sourceMatches && requestedConfiguration.has_value());
    apply->setEnabled(ordinaryEnabled && connectedIdle && sourceMatches
        && requestedConfiguration.has_value());
    auto* confirm = findChild<QPushButton*>(QStringLiteral("confirmCameraButton"));
    confirm->setVisible(connectedIdle && sourceMatches && applied && !confirmed);
    confirm->setEnabled(ordinaryEnabled && connectedIdle && sourceMatches && applied
        && !confirmed && presentation_.installationBindingCurrent);
    start->setVisible(connectedIdle && sourceMatches);
    start->setEnabled(ordinaryEnabled && presentation_.installationBindingCurrent && status
        && status->state == application::CameraSessionState::ConnectedIdle
        && sourceMatches && confirmed && applied);
    auto* stop = findChild<QPushButton*>(QStringLiteral("stopCameraButton"));
    stop->setVisible(streaming);
    stop->setEnabled(globallyEnabled && streaming);
    auto* disconnect = findChild<QPushButton*>(QStringLiteral("disconnectCameraButton"));
    const bool canDisconnect = status
        && cameraState != application::CameraSessionState::Disconnected
        && cameraState != application::CameraSessionState::ShuttingDown;
    disconnect->setVisible(canDisconnect);
    disconnect->setEnabled(globallyEnabled
            && cameraState != application::CameraSessionState::ShuttingDown
            && (presentation_.ordinaryOperationPending
                || (status && cameraState != application::CameraSessionState::Disconnected)));
    auto* retry = findChild<QPushButton*>(QStringLiteral("retryCameraButton"));
    const bool canRetry = status && status->desiredIdentity
        && (cameraState == application::CameraSessionState::Error
            || cameraState == application::CameraSessionState::Reconnecting);
    retry->setVisible(canRetry);
    retry->setEnabled(ordinaryEnabled && canRetry);
    auto* resumeLive = findChild<QPushButton*>(QStringLiteral("resumeLiveButton"));
    resumeLive->setVisible(presentation_.resumeLiveAvailable);
    resumeLive->setEnabled(ordinaryEnabled && connectedIdle
        && presentation_.preferencesLoadCompleted
        && presentation_.resumeLiveAvailable);
}

}  // namespace lumora::ui
