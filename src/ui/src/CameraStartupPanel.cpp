#include <lumora/ui/CameraStartupPanel.hpp>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
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
        emit selectionRequested({cameras->itemData(index).toString().toStdString()});
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
}

void CameraStartupPanel::updatePresentation() {
    const auto describe = [this](const camera::CameraConfiguration& configuration) {
        const auto optionalNumber = [this](const std::optional<double>& value) {
            return value ? QString::number(*value, 'g', 12) : tr("Automatic");
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
    const auto requestedConfiguration = presentation_.fixedRequestedConfiguration
        ? presentation_.fixedRequestedConfiguration
        : status ? status->requestedConfiguration : std::nullopt;
    requested->setText(requestedConfiguration ? describe(*requestedConfiguration)
                                              : tr("Not available"));
    actual->setText(status && status->appliedConfiguration
            ? describe(status->appliedConfiguration->actual)
            : tr("Not available"));

    auto* cameras = findChild<QComboBox*>(QStringLiteral("cameraSelectionCombo"));
    cameras->clear();
    if (status) {
        for (const auto& descriptor : status->discoveredDescriptors) {
            const auto display = tr("%1 %2 — %3")
                .arg(QString::fromStdString(descriptor.identity.manufacturer),
                    QString::fromStdString(descriptor.identity.model),
                    QString::fromStdString(descriptor.identity.serial));
            cameras->addItem(display, QString::fromStdString(descriptor.id.value));
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
    warning->setText(warningValue
            ? tr("Warning: %1").arg(QString::fromStdString(warningValue->operatorSummary))
            : QString{});
    warning->setVisible(warningValue.has_value());

    auto* start = findChild<QPushButton*>(QStringLiteral("startCameraButton"));
    const bool confirmed = status && status->confirmedRevision
        && status->appliedRevision != 0U
        && *status->confirmedRevision == status->appliedRevision;
    const bool applied = status && status->appliedConfiguration
        && status->requestedRevision != 0U
        && status->appliedRevision == status->requestedRevision;
    const bool globallyEnabled = presentation_.controlsEnabled;
    const bool ordinaryEnabled = globallyEnabled
        && !presentation_.ordinaryOperationPending;
    const bool connectedIdle = status
        && cameraState == application::CameraSessionState::ConnectedIdle;
    const bool selectedAvailable = status && presentation_.selectedCameraId
        && std::any_of(status->discoveredDescriptors.begin(),
            status->discoveredDescriptors.end(), [&](const auto& descriptor) {
                return descriptor.available
                    && descriptor.id == *presentation_.selectedCameraId;
            });

    cameras->setEnabled(ordinaryEnabled);
    findChild<QPushButton*>(QStringLiteral("refreshCameraButton"))
        ->setEnabled(ordinaryEnabled
            && (cameraState == application::CameraSessionState::Disconnected
                || cameraState == application::CameraSessionState::Error));
    findChild<QPushButton*>(QStringLiteral("connectCameraButton"))
        ->setEnabled(ordinaryEnabled && selectedAvailable
            && (cameraState == application::CameraSessionState::Disconnected
                || cameraState == application::CameraSessionState::Error));
    findChild<QPushButton*>(QStringLiteral("applyCameraButton"))
        ->setEnabled(ordinaryEnabled && connectedIdle
            && requestedConfiguration.has_value());
    findChild<QPushButton*>(QStringLiteral("confirmCameraButton"))
        ->setEnabled(ordinaryEnabled && connectedIdle && applied && !confirmed);
    start->setEnabled(presentation_.controlsEnabled
        && !presentation_.ordinaryOperationPending && status
        && status->state == application::CameraSessionState::ConnectedIdle
        && confirmed && applied);
    findChild<QPushButton*>(QStringLiteral("stopCameraButton"))
        ->setEnabled(globallyEnabled
            && cameraState == application::CameraSessionState::Streaming);
    findChild<QPushButton*>(QStringLiteral("disconnectCameraButton"))
        ->setEnabled(globallyEnabled && status
            && cameraState != application::CameraSessionState::Disconnected
            && cameraState != application::CameraSessionState::Discovering
            && cameraState != application::CameraSessionState::ShuttingDown);
    findChild<QPushButton*>(QStringLiteral("retryCameraButton"))
        ->setEnabled(ordinaryEnabled && status
            && (cameraState == application::CameraSessionState::Error
                || cameraState == application::CameraSessionState::Reconnecting));
    auto* resumeLive = findChild<QPushButton*>(QStringLiteral("resumeLiveButton"));
    resumeLive->setVisible(presentation_.resumeLiveAvailable);
    resumeLive->setEnabled(ordinaryEnabled && connectedIdle
        && presentation_.preferencesLoadCompleted
        && presentation_.resumeLiveAvailable);
}

}  // namespace lumora::ui
