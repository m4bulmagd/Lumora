#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/CameraSettingsDialog.hpp>
#include <lumora/application/StartupPreferences.hpp>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSizePolicy>
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
    auto* title = new QLabel(tr("Camera startup"), this);
    title->setStyleSheet(QStringLiteral("QLabel { font-weight: 600; }"));
    layout->addWidget(title);

    auto* state = new QLabel(tr("Waiting"), this);
    state->setObjectName(QStringLiteral("cameraStartupStateLabel"));
    state->setTextFormat(Qt::PlainText);
    state->setWordWrap(true);
    layout->addWidget(state);

    auto* cameras = new QComboBox(this);
    cameras->setObjectName(QStringLiteral("cameraSelectionCombo"));
    cameras->setAccessibleName(tr("Camera selection"));
    cameras->setPlaceholderText(tr("Select a camera"));
    cameras->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cameras->setMinimumContentsLength(10);
    layout->addWidget(cameras);

    auto* actionGrid = new QGridLayout;
    actionGrid->setContentsMargins(0, 0, 0, 0);
    actionGrid->setHorizontalSpacing(6);
    actionGrid->setVerticalSpacing(4);
    auto* refresh = makeButton(tr("Refresh"), "refreshCameraButton", this);
    auto* connectButton = makeButton(tr("Connect"), "connectCameraButton", this);
    actionGrid->addWidget(refresh, 0, 0);
    actionGrid->addWidget(connectButton, 0, 1);

    auto* requestedHeading = new QLabel(tr("Requested"), this);
    auto* requested = new QLabel(tr("Not available"), this);
    requested->setObjectName(QStringLiteral("requestedConfigurationLabel"));
    requested->setTextFormat(Qt::PlainText);
    requested->setWordWrap(true);
    auto* actualHeading = new QLabel(tr("Actual"), this);
    auto* actual = new QLabel(tr("Not available"), this);
    actual->setObjectName(QStringLiteral("actualConfigurationLabel"));
    actual->setTextFormat(Qt::PlainText);
    actual->setWordWrap(true);
    layout->addLayout(actionGrid);
    layout->addWidget(requestedHeading);
    layout->addWidget(requested);
    layout->addWidget(actualHeading);
    layout->addWidget(actual);
    auto* settings = makeButton(tr("Camera settings…"), "cameraSettingsButton", this);
    layout->addWidget(settings);
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
    actionGrid->addWidget(apply, 0, 0);
    actionGrid->addWidget(confirm, 0, 1);
    actionGrid->addWidget(start, 1, 0);
    actionGrid->addWidget(stop, 1, 1);
    actionGrid->addWidget(disconnect, 2, 0);
    actionGrid->addWidget(retry, 2, 1);
    actionGrid->addWidget(resumeLive, 3, 0, 1, 2);
    layout->addLayout(actionGrid);

    auto* warning = new QLabel(this);
    warning->setObjectName(QStringLiteral("startupWarningLabel"));
    warning->setTextFormat(Qt::PlainText);
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("QLabel { color: #ffcc66; }"));
    layout->addWidget(warning);

    connect(cameras, &QComboBox::activated, this, [this, cameras](int index) {
        const camera::CameraId selected{cameras->itemData(index).toString().toStdString()};
        if (settingsDialog_) {
            auto next = presentation_;
            next.selectedCameraId = selected;
            settingsDialog_->setPresentation(std::move(next));
        }
        emit selectionRequested(selected);
    });
    connect(settings, &QPushButton::clicked, this, [this] {
        if (!settingsDialog_) {
            settingsDialog_ = new CameraSettingsDialog(this);
            settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
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
}

void CameraStartupPanel::updatePresentation() {
    const auto describe = [this](const camera::CameraConfiguration& configuration) {
        const auto optionalNumber = [this](const std::optional<double>& value) {
            return value ? locale().toString(*value, 'g', QLocale::FloatingPointShortest) : tr("Automatic");
        };
        const auto exposure = configuration.exposure.mode == camera::ExposureMode::Manual
            ? tr("Manual %1 µs").arg(optionalNumber(
                  configuration.exposure.requestedMicroseconds))
            : tr("Automatic");
        const auto gain = configuration.gain.mode == camera::GainMode::Manual
            ? tr("Manual %1 dB").arg(optionalNumber(configuration.gain.requestedDb))
            : tr("Automatic");
        const auto acquisition =
            configuration.acquisitionMode == camera::AcquisitionMode::Continuous
            ? tr("Continuous")
            : tr("Triggered");
        return tr("%1 · %2 × %3 · %4 fps\nExposure: %5\nGain: %6 · %7")
            .arg(QString::fromStdString(configuration.pixelFormat.canonicalName))
            .arg(configuration.roi.width)
            .arg(configuration.roi.height)
            .arg(optionalNumber(configuration.requestedFps), exposure, gain, acquisition);
    };

    auto* requested = findChild<QLabel*>(QStringLiteral("requestedConfigurationLabel"));
    auto* actual = findChild<QLabel*>(QStringLiteral("actualConfigurationLabel"));
    const auto& status = presentation_.cameraStatus;
    const auto requestedConfiguration = presentation_.requestedConfiguration
        ? presentation_.requestedConfiguration
        : status ? status->requestedConfiguration : std::nullopt;
    requested->setText(requestedConfiguration ? describe(*requestedConfiguration)
                                              : tr("Not available"));
    actual->setText(status && status->appliedConfiguration
            ? describe(status->appliedConfiguration->actual)
            : tr("Not available"));

    auto* cameras = findChild<QComboBox*>(QStringLiteral("cameraSelectionCombo"));
    QStringList labels;
    QStringList identities;
    if (status) {
        for (const auto& descriptor : status->discoveredDescriptors) {
            const auto display = tr("%1 %2 — %3")
                .arg(QString::fromStdString(descriptor.identity.manufacturer),
                    QString::fromStdString(descriptor.identity.model),
                    QString::fromStdString(descriptor.identity.serial));
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
    if (presentation_.selectedCameraId) {
        const auto selected = QString::fromStdString(
            presentation_.selectedCameraId->value);
        for (int index = 0; index < cameras->count(); ++index) {
            if (cameras->itemData(index).toString() == selected) {
                cameras->setCurrentIndex(index);
                break;
            }
        }
    } else {
        cameras->setCurrentIndex(-1);
    }

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
    auto* warning = findChild<QLabel*>(QStringLiteral("startupWarningLabel"));
    const auto warningValue = presentation_.startupWarning
        ? presentation_.startupWarning
        : status ? status->latestError : std::nullopt;
    const auto warningSummary = [this](const core::Error& error) {
        if (error.code == "startup_readback_changed")
            return tr("Camera readback changed. Review and confirm settings before Start.");
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
    const bool ordinaryEnabled = globallyEnabled
        && !presentation_.ordinaryOperationPending;
    const bool connectedIdle = status
        && cameraState == application::CameraSessionState::ConnectedIdle;
    const bool sourceMatches = !status || !status->actualIdentity
        || status->actualIdentity == presentation_.selectedCameraId;
    const bool selectedAvailable = status && presentation_.selectedCameraId
        && std::any_of(status->discoveredDescriptors.begin(),
            status->discoveredDescriptors.end(), [&](const auto& descriptor) {
                return descriptor.available
                    && descriptor.id == *presentation_.selectedCameraId;
            });

    findChild<QPushButton*>(QStringLiteral("cameraSettingsButton"))
        ->setEnabled(globallyEnabled && status && status->actualIdentity
            && (connectedIdle || cameraState == application::CameraSessionState::Streaming));
    cameras->setEnabled(ordinaryEnabled);
    findChild<QPushButton*>(QStringLiteral("refreshCameraButton"))
        ->setEnabled(ordinaryEnabled
            && cameraState == application::CameraSessionState::Disconnected);
    findChild<QPushButton*>(QStringLiteral("connectCameraButton"))
        ->setEnabled(ordinaryEnabled && selectedAvailable
            && (cameraState == application::CameraSessionState::Disconnected
                || cameraState == application::CameraSessionState::Error));
    findChild<QPushButton*>(QStringLiteral("applyCameraButton"))
        ->setEnabled(ordinaryEnabled && connectedIdle && sourceMatches
            && requestedConfiguration.has_value());
    findChild<QPushButton*>(QStringLiteral("confirmCameraButton"))
        ->setEnabled(ordinaryEnabled && connectedIdle && sourceMatches && applied && !confirmed);
    start->setEnabled(presentation_.controlsEnabled
        && !presentation_.ordinaryOperationPending && status
        && status->state == application::CameraSessionState::ConnectedIdle
        && sourceMatches && confirmed && applied);
    findChild<QPushButton*>(QStringLiteral("stopCameraButton"))
        ->setEnabled(globallyEnabled
            && cameraState == application::CameraSessionState::Streaming);
    findChild<QPushButton*>(QStringLiteral("disconnectCameraButton"))
        ->setEnabled(globallyEnabled
            && cameraState != application::CameraSessionState::ShuttingDown
            && (presentation_.ordinaryOperationPending
                || (status && cameraState != application::CameraSessionState::Disconnected)));
    findChild<QPushButton*>(QStringLiteral("retryCameraButton"))
        ->setEnabled(ordinaryEnabled && status && status->desiredIdentity
            && (cameraState == application::CameraSessionState::Error
                || cameraState == application::CameraSessionState::Reconnecting));
    auto* resumeLive = findChild<QPushButton*>(QStringLiteral("resumeLiveButton"));
    resumeLive->setVisible(presentation_.resumeLiveAvailable);
    resumeLive->setEnabled(ordinaryEnabled && connectedIdle
        && presentation_.preferencesLoadCompleted
        && presentation_.resumeLiveAvailable);
}

}  // namespace lumora::ui
