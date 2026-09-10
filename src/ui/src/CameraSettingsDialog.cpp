#include <lumora/ui/CameraSettingsDialog.hpp>

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
#include <utility>

namespace lumora::ui {
namespace {

// Retain double precision without padding the visible value with trailing zeros.
class NumericEntry final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QSize sizeHint() const override {
        auto size = QDoubleSpinBox::sizeHint();
        size.setWidth(210);
        return size;
    }
    QSize minimumSizeHint() const override { return {120, QDoubleSpinBox::minimumSizeHint().height()}; }
    QString textFromValue(double value) const override {
        return locale().toString(value, 'g', QLocale::FloatingPointShortest);
    }
};

QString number(const QWidget& widget, double value) {
    return widget.locale().toString(value, 'g', QLocale::FloatingPointShortest);
}

bool validRange(const camera::NumericCapability& capability) {
    return std::isfinite(capability.minimum) && std::isfinite(capability.maximum)
        && std::isfinite(capability.increment) && capability.minimum <= capability.maximum
        && capability.increment > 0;
}

}  // namespace

struct CameraSettingsDialog::Impl final {
    CameraSettingsDialog& dialog;
    CameraStartupPanelPresentation presentation;
    std::shared_ptr<const application::CameraStatusSnapshot> source;
    std::optional<camera::CameraConfiguration> draft;
    std::optional<camera::CameraConfiguration> observedRequest;
    std::optional<camera::CameraConfiguration> submitted;
    std::uint64_t observedRevision{0};
    bool initialized{false};
    bool submissionAdmitted{false};
    bool invalidated{false};
    QComboBox* exposureMode;
    QComboBox* gainMode;
    NumericEntry* exposureValue;
    NumericEntry* gainValue;
    QLabel* sourceLabel;
    QLabel* fixedFields;
    QLabel* actual;
    QLabel* statusLabel;
    QPushButton* apply;

    explicit Impl(CameraSettingsDialog& owner) : dialog(owner) {
        auto* layout = new QVBoxLayout(&dialog);
        layout->setSpacing(12);
        sourceLabel = label("cameraSettingsSource", CameraSettingsDialog::tr("Source camera"));
        layout->addWidget(sourceLabel);
        fixedFields = label("cameraSettingsFixedFields", CameraSettingsDialog::tr("Read-only camera settings"));
        layout->addWidget(fixedFields);
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        exposureMode = mode(form, "cameraExposureMode", CameraSettingsDialog::tr("&Exposure mode"));
        exposureValue = numeric(form, "cameraExposureValue", CameraSettingsDialog::tr("Exposure (&µs)"));
        gainMode = mode(form, "cameraGainMode", CameraSettingsDialog::tr("&Gain mode"));
        gainValue = numeric(form, "cameraGainValue", CameraSettingsDialog::tr("Gain (&dB)"));
        layout->addLayout(form);
        actual = label("cameraSettingsActual", CameraSettingsDialog::tr("Actual camera readback"));
        layout->addWidget(actual);
        statusLabel = label("cameraSettingsStatus", CameraSettingsDialog::tr("Camera settings status"));
        layout->addWidget(statusLabel);
        layout->addStretch();
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        apply = new QPushButton(CameraSettingsDialog::tr("&Apply settings"), &dialog);
        apply->setObjectName("applyCameraSettingsButton");
        apply->setAutoDefault(false);
        auto* close = new QPushButton(CameraSettingsDialog::tr("&Close"), &dialog);
        close->setObjectName("closeCameraSettingsButton");
        close->setAutoDefault(false);
        buttons->addWidget(apply);
        buttons->addWidget(close);
        layout->addLayout(buttons);
        QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(apply, &QPushButton::clicked, &dialog, [this] {
            refresh();
            if (!apply->isEnabled() || !draft || !source || !source->actualIdentity) return;
            submitted = draft;
            submissionAdmitted = false;
            emit dialog.settingsApplyRequested(source->sessionGeneration, *source->actualIdentity, *draft);
        });
        QObject::connect(exposureValue, &QDoubleSpinBox::valueChanged, &dialog, [this](double value) {
            if (draft && draft->exposure.mode == camera::ExposureMode::Manual) {
                draft->exposure.requestedMicroseconds = value;
                refresh();
            }
        });
        QObject::connect(gainValue, &QDoubleSpinBox::valueChanged, &dialog, [this](double value) {
            if (draft && draft->gain.mode == camera::GainMode::Manual) {
                draft->gain.requestedDb = value;
                refresh();
            }
        });
        QObject::connect(exposureMode, &QComboBox::currentIndexChanged, &dialog, [this](int index) {
            if (!draft || index < 0) return;
            draft->exposure.mode = static_cast<camera::ExposureMode>(exposureMode->currentData().toInt());
            draft->exposure.requestedMicroseconds = draft->exposure.mode == camera::ExposureMode::Manual
                ? std::optional<double>{exposureValue->value()} : std::nullopt;
            refresh();
        });
        QObject::connect(gainMode, &QComboBox::currentIndexChanged, &dialog, [this](int index) {
            if (!draft || index < 0) return;
            draft->gain.mode = static_cast<camera::GainMode>(gainMode->currentData().toInt());
            draft->gain.requestedDb = draft->gain.mode == camera::GainMode::Manual
                ? std::optional<double>{gainValue->value()} : std::nullopt;
            refresh();
        });
        refresh();
    }

    QLabel* label(const char* name, const QString& accessibleName) {
        auto* result = new QLabel(&dialog);
        result->setObjectName(QString::fromLatin1(name));
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        result->setAccessibleName(accessibleName);
        result->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return result;
    }

    QComboBox* mode(QFormLayout* form, const char* name, const QString& title) {
        auto* result = new QComboBox(&dialog);
        result->setObjectName(QString::fromLatin1(name));
        auto accessible = title;
        accessible.remove(QLatin1Char('&'));
        result->setAccessibleName(accessible);
        form->addRow(title, result);
        return result;
    }

    NumericEntry* numeric(QFormLayout* form, const char* name, const QString& title) {
        auto* result = new NumericEntry(&dialog);
        result->setObjectName(QString::fromLatin1(name));
        result->setDecimals(std::numeric_limits<double>::max_digits10);
        result->setKeyboardTracking(false);
        auto accessible = title;
        accessible.remove(QLatin1Char('&'));
        result->setAccessibleName(accessible);
        form->addRow(title, result);
        return result;
    }

    void initializeNumeric(NumericEntry* entry, const camera::NumericCapability& capability,
        const std::optional<double>& value) {
        const QSignalBlocker blocker(entry);
        if (validRange(capability)) {
            entry->setRange(capability.minimum, capability.maximum);
            entry->setSingleStep(capability.increment);
            const auto description = CameraSettingsDialog::tr("Range: %1 to %2; increment: %3")
                .arg(number(dialog, capability.minimum), number(dialog, capability.maximum),
                    number(dialog, capability.increment));
            entry->setAccessibleDescription(description);
            entry->setToolTip(description);
            entry->setValue(value && std::isfinite(*value) ? *value : capability.minimum);
        }
        // The draft retains the original optional value, even when it cannot be
        // represented by the widget. Validation must reject it until edited.
    }

    void initialize() {
        source = presentation.cameraStatus;
        draft = presentation.requestedConfiguration;
        if (!draft && source) draft = source->requestedConfiguration;
        observedRequest = draft;
        observedRevision = source ? source->requestedRevision : 0;
        if (!source || !source->actualIdentity || !source->capabilities || !draft) return;
        const auto& capabilities = *source->capabilities;
        const QSignalBlocker exposureBlock(exposureMode), gainBlock(gainMode);
        for (const auto value : capabilities.exposureModes) {
            if (value == camera::ExposureMode::Manual || value == camera::ExposureMode::Auto)
                exposureMode->addItem(value == camera::ExposureMode::Manual
                    ? CameraSettingsDialog::tr("Manual") : CameraSettingsDialog::tr("Automatic"), static_cast<int>(value));
        }
        for (const auto value : capabilities.gainModes) {
            if (value == camera::GainMode::Manual || value == camera::GainMode::Auto)
                gainMode->addItem(value == camera::GainMode::Manual
                    ? CameraSettingsDialog::tr("Manual") : CameraSettingsDialog::tr("Automatic"), static_cast<int>(value));
        }
        exposureMode->setCurrentIndex(exposureMode->findData(static_cast<int>(draft->exposure.mode)));
        gainMode->setCurrentIndex(gainMode->findData(static_cast<int>(draft->gain.mode)));
        initializeNumeric(exposureValue, capabilities.exposure, draft->exposure.requestedMicroseconds);
        initializeNumeric(gainValue, capabilities.gain, draft->gain.requestedDb);
        if (presentation.selectedCameraId != source->actualIdentity)
            invalidated = true;
    }

    void update(CameraStartupPanelPresentation next) {
        presentation = std::move(next);
        if (!initialized) {
            initialized = true;
            initialize();
        } else if (source) {
            const auto& current = presentation.cameraStatus;
            if (!current || !source->actualIdentity || current->actualIdentity != source->actualIdentity
                || current->sessionGeneration != source->sessionGeneration
                || current->sourceReplacementRequired
                || !source->capabilities || !current->capabilities
                || !application::cameraCapabilitiesEqual(*source->capabilities, *current->capabilities)
                || presentation.selectedCameraId != source->actualIdentity) {
                invalidated = true;
            } else {
                const auto request = presentation.requestedConfiguration
                    ? presentation.requestedConfiguration : current->requestedConfiguration;
                if (submitted && !submissionAdmitted) {
                    submissionAdmitted = presentation.ordinaryOperationPending && request
                        && application::cameraConfigurationsEqual(*request, *submitted);
                    if (!submissionAdmitted) submitted.reset();
                }
                const bool changed = current->requestedRevision != observedRevision
                    || request.has_value() != observedRequest.has_value()
                    || (request && observedRequest
                        && !application::cameraConfigurationsEqual(*request, *observedRequest));
                if (changed) {
                    if (!submitted || !request || !application::cameraConfigurationsEqual(*request, *submitted)) {
                        invalidated = true;
                    } else {
                        observedRequest = request;
                        if (current->requestedRevision != observedRevision) submitted.reset();
                        observedRevision = current->requestedRevision;
                    }
                }
            }
        } else {
            invalidated = true;
        }
        refresh();
    }

    QString describeActual(const camera::CameraConfiguration& value) const {
        const auto exposure = value.exposure.mode == camera::ExposureMode::Auto
            ? CameraSettingsDialog::tr("Automatic")
            : value.exposure.requestedMicroseconds ? number(dialog, *value.exposure.requestedMicroseconds)
                : CameraSettingsDialog::tr("Unavailable");
        const auto gain = value.gain.mode == camera::GainMode::Auto
            ? CameraSettingsDialog::tr("Automatic")
            : value.gain.requestedDb ? number(dialog, *value.gain.requestedDb) : CameraSettingsDialog::tr("Unavailable");
        return CameraSettingsDialog::tr("Actual readback\nExposure: %1 µs · Gain: %2 dB\n%3 fps · %4 · ROI x %5, y %6, %7 × %8")
            .arg(exposure, gain, value.requestedFps ? number(dialog, *value.requestedFps) : CameraSettingsDialog::tr("Unavailable"),
                QString::fromStdString(value.pixelFormat.canonicalName))
            .arg(value.roi.x).arg(value.roi.y).arg(value.roi.width).arg(value.roi.height);
    }

    void refresh() {
        sourceLabel->setText(source && source->actualIdentity
            ? CameraSettingsDialog::tr("Camera: %1").arg(QString::fromStdString(source->actualIdentity->value))
            : CameraSettingsDialog::tr("Camera unavailable"));
        if (draft) {
            fixedFields->setText(CameraSettingsDialog::tr("Read-only settings\nFrame rate: %1 fps · Format: %2\nROI: x %3, y %4, width %5, height %6\nAcquisition: %7")
                .arg(draft->requestedFps ? number(dialog, *draft->requestedFps) : CameraSettingsDialog::tr("Automatic"),
                    QString::fromStdString(draft->pixelFormat.canonicalName))
                .arg(draft->roi.x).arg(draft->roi.y).arg(draft->roi.width).arg(draft->roi.height)
                .arg(draft->acquisitionMode == camera::AcquisitionMode::Continuous
                    ? CameraSettingsDialog::tr("Continuous") : CameraSettingsDialog::tr("Triggered")));
        } else {
            fixedFields->setText(CameraSettingsDialog::tr("Requested camera settings unavailable"));
        }
        const auto& current = presentation.cameraStatus;
        // Never show a replacement camera's readback beside this draft.
        actual->setText(source && current && current->actualIdentity == source->actualIdentity
            && current->sessionGeneration == source->sessionGeneration && current->appliedConfiguration
            ? describeActual(current->appliedConfiguration->actual)
            : CameraSettingsDialog::tr("Actual readback unavailable"));
        QString message;
        bool editable = false;
        if (invalidated) {
            message = CameraSettingsDialog::tr("Camera or settings changed. Close and reopen this dialog before editing.");
        } else if (!source || !source->actualIdentity || !source->capabilities || !draft || !current) {
            message = CameraSettingsDialog::tr("Camera settings unavailable. Connect a camera, then close and reopen this dialog.");
        } else if (current->state == application::CameraSessionState::Streaming) {
            message = CameraSettingsDialog::tr("Stop acquisition before editing camera settings. Pausing the viewer does not stop the camera.");
        } else if (current->state != application::CameraSessionState::ConnectedIdle) {
            message = CameraSettingsDialog::tr("Camera settings are available only while the connected camera is stopped.");
        } else if (!presentation.controlsEnabled || presentation.ordinaryOperationPending) {
            message = CameraSettingsDialog::tr("Wait for the current camera operation to finish.");
        } else {
            editable = true;
        }
        const bool valid = draft && source && source->capabilities
            && exposureMode->currentIndex() >= 0 && gainMode->currentIndex() >= 0
            && camera::validateCameraConfiguration(*draft, *source->capabilities).hasValue();
        if (editable && !valid) {
            message = CameraSettingsDialog::tr("Settings unavailable or outside the camera's supported ranges. Review exposure, gain and camera capabilities before applying.");
        } else if (editable) {
            message = CameraSettingsDialog::tr("Apply settings, review the actual readback, then explicitly Confirm before Start.");
        }
        exposureMode->setEnabled(editable && exposureMode->count() > 0);
        gainMode->setEnabled(editable && gainMode->count() > 0);
        exposureValue->setEnabled(editable && draft && draft->exposure.mode == camera::ExposureMode::Manual
            && source && source->capabilities && validRange(source->capabilities->exposure));
        gainValue->setEnabled(editable && draft && draft->gain.mode == camera::GainMode::Manual
            && source && source->capabilities && validRange(source->capabilities->gain));
        apply->setEnabled(editable && valid);
        statusLabel->setText(message);
    }
};

CameraSettingsDialog::CameraSettingsDialog(QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this)) {
    setObjectName("cameraSettingsDialog");
    setWindowTitle(tr("Camera settings"));
    setAccessibleName(tr("Camera settings"));
    setModal(false);
    resize(560, 560);
}

CameraSettingsDialog::~CameraSettingsDialog() = default;

void CameraSettingsDialog::setPresentation(CameraStartupPanelPresentation presentation) {
    impl_->update(std::move(presentation));
}

}  // namespace lumora::ui
