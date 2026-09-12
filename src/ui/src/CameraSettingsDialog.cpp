#include <lumora/ui/CameraSettingsDialog.hpp>

#include <lumora/application/CameraSettingsPolicy.hpp>
#include <lumora/application/StartupPreferences.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iterator>
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

bool supportsFrameRatePrecision(const NumericEntry& entry, const camera::NumericCapability& capability) {
    if (!validRange(capability) || capability.minimum <= 0.0) return false;
    // Fixed-decimal rounding must be finer than the closest doubles anywhere
    // in the positive range. Qt's decimal limit cannot preserve all denormals.
    const auto smallestGap = capability.minimum - std::nextafter(capability.minimum, 0.0);
    const auto decimalQuantum = std::pow(10.0, -entry.decimals());
    return smallestGap > decimalQuantum;
}

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

QString storageName(core::StorageType storage) {
    switch (storage) {
    case core::StorageType::UInt8: return CameraSettingsDialog::tr("8-bit storage");
    case core::StorageType::UInt16: return CameraSettingsDialog::tr("16-bit storage");
    }
    return CameraSettingsDialog::tr("Unknown storage");
}

QString pixelFormatName(const core::SourcePixelFormat& format) {
    return CameraSettingsDialog::tr("%1 — %2 valid bits, %3")
        .arg(QString::fromStdString(format.canonicalName))
        .arg(format.validBits)
        .arg(storageName(format.applicationStorage));
}

QString pixelFormatDescription(const core::SourcePixelFormat& format) {
    QString packing;
    switch (format.packing) {
    case core::SourcePacking::Unpacked: packing = CameraSettingsDialog::tr("unpacked"); break;
    case core::SourcePacking::Packed: packing = CameraSettingsDialog::tr("packed"); break;
    default: packing = CameraSettingsDialog::tr("unknown packing"); break;
    }
    QString alignment;
    switch (format.alignment) {
    case core::BitAlignment::LeastSignificant:
        alignment = CameraSettingsDialog::tr("least-significant-bit aligned"); break;
    case core::BitAlignment::MostSignificant:
        alignment = CameraSettingsDialog::tr("most-significant-bit aligned"); break;
    default: alignment = CameraSettingsDialog::tr("unknown alignment"); break;
    }
    return CameraSettingsDialog::tr(
        "%1; sample maximum %2; encoding 0x%3; %4; %5")
        .arg(pixelFormatName(format))
        .arg(format.sampleMaximum)
        .arg(static_cast<qulonglong>(format.canonicalEncoding), 8, 16, QLatin1Char('0'))
        .arg(packing, alignment);
}

bool roiCapabilityFitsSignedEditors(const camera::RegionOfInterestCapability& capability) {
    const auto fits = [](std::uint32_t minimum, std::uint32_t maximum,
                          std::uint32_t increment) {
        return minimum <= maximum && increment > 0U
            && maximum <= static_cast<std::uint32_t>(INT_MAX)
            && increment <= static_cast<std::uint32_t>(INT_MAX);
    };
    return fits(capability.minimum.x, capability.maximum.x, capability.increment.x)
        && fits(capability.minimum.y, capability.maximum.y, capability.increment.y)
        && fits(capability.minimum.width, capability.maximum.width, capability.increment.width)
        && fits(capability.minimum.height, capability.maximum.height, capability.increment.height);
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
    bool rebindCompleted{false};
    bool roiEditorsRepresentable{false};
    std::optional<std::uint64_t> completedGeneration;
    QComboBox* pixelFormat;
    QSpinBox* roiX;
    QSpinBox* roiY;
    QSpinBox* roiWidth;
    QSpinBox* roiHeight;
    QComboBox* exposureMode;
    QComboBox* gainMode;
    NumericEntry* frameRateValue;
    NumericEntry* exposureValue;
    NumericEntry* gainValue;
    QLabel* sourceLabel;
    QLabel* fixedFields;
    QLabel* actual;
    QLabel* statusLabel;
    QPushButton* apply;

    explicit Impl(CameraSettingsDialog& owner) : dialog(owner) {
        auto* layout = new QVBoxLayout(&dialog);
        layout->setSpacing(8);
        sourceLabel = label("cameraSettingsSource", CameraSettingsDialog::tr("Source camera"));
        layout->addWidget(sourceLabel);
        fixedFields = label("cameraSettingsFixedFields", CameraSettingsDialog::tr("Selected source and acquisition mode"));
        layout->addWidget(fixedFields);
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        pixelFormat = mode(form, "cameraPixelFormat", CameraSettingsDialog::tr("&Pixel format"));
        pixelFormat->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        pixelFormat->setMinimumContentsLength(12);
        auto* roiGrid = new QGridLayout;
        roiGrid->setContentsMargins(0, 0, 0, 0);
        roiGrid->setHorizontalSpacing(8);
        roiGrid->setVerticalSpacing(4);
        roiX = roi(roiGrid, 0, 0, "cameraRoiX", CameraSettingsDialog::tr("&X"));
        roiY = roi(roiGrid, 0, 2, "cameraRoiY", CameraSettingsDialog::tr("&Y"));
        roiWidth = roi(roiGrid, 1, 0, "cameraRoiWidth", CameraSettingsDialog::tr("&Width"));
        roiHeight = roi(roiGrid, 1, 2, "cameraRoiHeight", CameraSettingsDialog::tr("&Height"));
        form->addRow(CameraSettingsDialog::tr("Image region"), roiGrid);
        frameRateValue = numeric(form, "cameraFrameRateValue", CameraSettingsDialog::tr("&Frame rate (fps)"));
        // Qt caps decimal places at DBL_MAX_10_EXP + DBL_DIG. Configure before
        // assigning bounds or values, which QDoubleSpinBox otherwise rounds.
        frameRateValue->setDecimals(std::numeric_limits<double>::max_exponent10
            + std::numeric_limits<double>::digits10);
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
        QObject::connect(pixelFormat, &QComboBox::currentIndexChanged, &dialog, [this](int index) {
            if (!draft || !source || !source->capabilities || index < 0
                || static_cast<std::size_t>(index) >= source->capabilities->pixelFormats.size()) {
                return;
            }
            draft->pixelFormat = source->capabilities->pixelFormats[static_cast<std::size_t>(index)];
            refresh();
        });
        const auto bindRoi = [this](QSpinBox* entry, std::uint32_t core::RegionOfInterest::* field) {
            QObject::connect(entry, &QSpinBox::valueChanged, &dialog, [this, field](int value) {
                if (!draft || value < 0) return;
                draft->roi.*field = static_cast<std::uint32_t>(value);
                refresh();
            });
        };
        bindRoi(roiX, &core::RegionOfInterest::x);
        bindRoi(roiY, &core::RegionOfInterest::y);
        bindRoi(roiWidth, &core::RegionOfInterest::width);
        bindRoi(roiHeight, &core::RegionOfInterest::height);
        QObject::connect(frameRateValue, &QDoubleSpinBox::valueChanged, &dialog, [this](double value) {
            if (draft) {
                draft->requestedFps = value;
                refresh();
            }
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

    QSpinBox* roi(
        QGridLayout* grid,
        int row,
        int column,
        const char* name,
        const QString& title) {
        auto* result = new QSpinBox(&dialog);
        result->setObjectName(QString::fromLatin1(name));
        result->setKeyboardTracking(false);
        result->setMinimumWidth(80);
        auto accessible = title;
        accessible.remove(QLatin1Char('&'));
        result->setAccessibleName(accessible);
        auto* titleLabel = new QLabel(title, &dialog);
        titleLabel->setBuddy(result);
        grid->addWidget(titleLabel, row, column);
        grid->addWidget(result, row, column + 1);
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

    void initializeRoiEntry(
        QSpinBox* entry,
        std::uint32_t minimum,
        std::uint32_t maximum,
        std::uint32_t increment,
        std::uint32_t value) {
        const QSignalBlocker blocker(entry);
        entry->setRange(static_cast<int>(minimum), static_cast<int>(maximum));
        entry->setSingleStep(static_cast<int>(increment));
        const auto description = CameraSettingsDialog::tr("Range: %1 to %2; increment: %3")
            .arg(minimum).arg(maximum).arg(increment);
        entry->setAccessibleDescription(description);
        entry->setToolTip(description);
        if (value <= static_cast<std::uint32_t>(INT_MAX)) {
            entry->setValue(static_cast<int>(value));
        }
        // The draft retains an out-of-range or misaligned source value. Qt may
        // display a bounded value, but only an explicit edit changes the draft.
    }

    void initializeRoi(
        const camera::RegionOfInterestCapability& capability,
        const core::RegionOfInterest& value) {
        roiEditorsRepresentable = roiCapabilityFitsSignedEditors(capability);
        const auto valueFits = [](std::uint32_t current, std::uint32_t minimum,
                                   std::uint32_t maximum) {
            return current >= minimum && current <= maximum
                && current <= static_cast<std::uint32_t>(INT_MAX);
        };
        roiEditorsRepresentable = roiEditorsRepresentable
            && valueFits(value.x, capability.minimum.x, capability.maximum.x)
            && valueFits(value.y, capability.minimum.y, capability.maximum.y)
            && valueFits(value.width, capability.minimum.width, capability.maximum.width)
            && valueFits(value.height, capability.minimum.height, capability.maximum.height);
        if (!roiEditorsRepresentable) return;
        initializeRoiEntry(roiX, capability.minimum.x, capability.maximum.x,
            capability.increment.x, value.x);
        initializeRoiEntry(roiY, capability.minimum.y, capability.maximum.y,
            capability.increment.y, value.y);
        initializeRoiEntry(roiWidth, capability.minimum.width, capability.maximum.width,
            capability.increment.width, value.width);
        initializeRoiEntry(roiHeight, capability.minimum.height, capability.maximum.height,
            capability.increment.height, value.height);
    }

    void initialize() {
        source = presentation.cameraStatus;
        draft = presentation.requestedConfiguration;
        if (!draft && source) draft = source->requestedConfiguration;
        observedRequest = draft;
        observedRevision = source ? source->requestedRevision : 0;
        if (!source || !source->actualIdentity || !source->capabilities || !draft) return;
        const auto& capabilities = *source->capabilities;
        const QSignalBlocker formatBlock(pixelFormat), exposureBlock(exposureMode), gainBlock(gainMode);
        for (std::size_t index = 0; index < capabilities.pixelFormats.size(); ++index) {
            const auto& format = capabilities.pixelFormats[index];
            pixelFormat->addItem(pixelFormatName(format), static_cast<qulonglong>(index));
            pixelFormat->setItemData(static_cast<int>(index), pixelFormatDescription(format), Qt::ToolTipRole);
        }
        const auto selectedFormat = std::find_if(capabilities.pixelFormats.begin(),
            capabilities.pixelFormats.end(), [this](const auto& format) {
                return samePixelFormat(format, draft->pixelFormat);
            });
        pixelFormat->setCurrentIndex(selectedFormat == capabilities.pixelFormats.end()
            ? -1 : static_cast<int>(std::distance(capabilities.pixelFormats.begin(), selectedFormat)));
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
        initializeRoi(capabilities.roi, draft->roi);
        initializeNumeric(frameRateValue, capabilities.frameRate, draft->requestedFps);
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
            const auto request = presentation.requestedConfiguration
                ? presentation.requestedConfiguration
                : current ? current->requestedConfiguration : std::nullopt;
            const bool sameCamera = current && source->actualIdentity
                && current->actualIdentity == source->actualIdentity
                && presentation.selectedCameraId == source->actualIdentity;
            const bool sameCapabilities = current && source->capabilities && current->capabilities
                && application::cameraCapabilitiesEqual(*source->capabilities, *current->capabilities);
            if (submitted && !submissionAdmitted) {
                submissionAdmitted = sameCamera && sameCapabilities
                    && current->sessionGeneration == source->sessionGeneration
                    && presentation.ordinaryOperationPending && request
                    && application::cameraConfigurationsEqual(*request, *submitted);
                if (!submissionAdmitted) submitted.reset();
            }
            const bool ownSuccessfulRebind = !invalidated && !rebindCompleted
                && submitted && submissionAdmitted
                && sameCamera && sameCapabilities && !current->sourceReplacementRequired
                && current->state == application::CameraSessionState::ConnectedIdle
                && source->sessionGeneration != std::numeric_limits<std::uint64_t>::max()
                && current->sessionGeneration == source->sessionGeneration + 1U
                && request && current->requestedConfiguration && current->appliedConfiguration
                && current->requestedRevision > observedRevision
                && current->requestedRevision == current->appliedRevision
                && application::cameraConfigurationsEqual(*request, *submitted)
                && application::cameraConfigurationsEqual(
                    *current->requestedConfiguration, *submitted)
                && application::cameraConfigurationsEqual(
                    current->appliedConfiguration->requested, *submitted)
                && sameSourceMode(current->appliedConfiguration->actual, *submitted);
            if (ownSuccessfulRebind) {
                rebindCompleted = true;
                completedGeneration = current->sessionGeneration;
                observedRequest = request;
                observedRevision = current->requestedRevision;
            } else if (rebindCompleted) {
                if (!current || !completedGeneration
                    || current->sessionGeneration != *completedGeneration
                    || !sameCamera || !sameCapabilities || !submitted || !request
                    || current->sourceReplacementRequired
                    || current->state != application::CameraSessionState::ConnectedIdle
                    || !current->requestedConfiguration || !current->appliedConfiguration
                    || current->requestedRevision != observedRevision
                    || current->appliedRevision != observedRevision
                    || !application::cameraConfigurationsEqual(*request, *submitted)
                    || !application::cameraConfigurationsEqual(
                        *current->requestedConfiguration, *submitted)
                    || !application::cameraConfigurationsEqual(
                        current->appliedConfiguration->requested, *submitted)
                    || !sameSourceMode(current->appliedConfiguration->actual, *submitted)) {
                    rebindCompleted = false;
                    invalidated = true;
                }
            } else if (!current || !source->actualIdentity
                || current->actualIdentity != source->actualIdentity
                || current->sessionGeneration != source->sessionGeneration
                || current->sourceReplacementRequired
                || !sameCapabilities
                || presentation.selectedCameraId != source->actualIdentity) {
                invalidated = true;
            } else {
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
        return CameraSettingsDialog::tr("Actual readback\nExposure: %1 µs · Gain: %2 dB\n%3 fps · %4\nROI x %5, y %6, %7 × %8")
            .arg(exposure, gain, value.requestedFps ? number(dialog, *value.requestedFps) : CameraSettingsDialog::tr("Unavailable"),
                pixelFormatDescription(value.pixelFormat))
            .arg(value.roi.x).arg(value.roi.y).arg(value.roi.width).arg(value.roi.height);
    }

    void refresh() {
        sourceLabel->setText(source && source->actualIdentity
            ? CameraSettingsDialog::tr("Camera: %1").arg(QString::fromStdString(source->actualIdentity->value))
            : CameraSettingsDialog::tr("Camera unavailable"));
        if (draft) {
            const auto formatDescription = pixelFormatDescription(draft->pixelFormat);
            fixedFields->setText(CameraSettingsDialog::tr("Selected source\n%1\nROI: x %2, y %3, width %4, height %5\nRead-only acquisition: %6")
                .arg(formatDescription)
                .arg(draft->roi.x).arg(draft->roi.y).arg(draft->roi.width).arg(draft->roi.height)
                .arg(draft->acquisitionMode == camera::AcquisitionMode::Continuous
                    ? CameraSettingsDialog::tr("Continuous") : CameraSettingsDialog::tr("Triggered")));
            pixelFormat->setAccessibleDescription(formatDescription);
            pixelFormat->setToolTip(formatDescription);
        } else {
            fixedFields->setText(CameraSettingsDialog::tr("Requested camera settings unavailable"));
        }
        const auto& current = presentation.cameraStatus;
        // Never show a replacement camera's readback beside this draft.
        const bool currentSourceReadback = source && current
            && current->actualIdentity == source->actualIdentity
            && current->sessionGeneration == source->sessionGeneration;
        const bool completedRebindReadback = rebindCompleted && source && current
            && completedGeneration && current->actualIdentity == source->actualIdentity
            && current->sessionGeneration == *completedGeneration;
        actual->setText((currentSourceReadback || completedRebindReadback)
            && current->appliedConfiguration
            ? describeActual(current->appliedConfiguration->actual)
            : CameraSettingsDialog::tr("Actual readback unavailable"));
        QString message;
        bool editable = false;
        if (rebindCompleted) {
            message = CameraSettingsDialog::tr("Source settings were applied. Review the actual readback, then close and reopen this dialog before Confirm and Start.");
        } else if (invalidated) {
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
        const bool frameRatePrecisionSupported = source && source->capabilities
            && supportsFrameRatePrecision(*frameRateValue, source->capabilities->frameRate);
        const bool valid = draft && source && source->capabilities && frameRatePrecisionSupported
            && draft->requestedFps && std::isfinite(*draft->requestedFps) && *draft->requestedFps > 0.0
            && pixelFormat->currentIndex() >= 0 && roiEditorsRepresentable
            && exposureMode->currentIndex() >= 0 && gainMode->currentIndex() >= 0
            && application::isSupportedLiveCameraConfiguration(*draft)
            && camera::validateCameraConfiguration(*draft, *source->capabilities).hasValue();
        if (editable && source && source->capabilities && validRange(source->capabilities->frameRate)
            && source->capabilities->frameRate.minimum > 0.0 && !frameRatePrecisionSupported) {
            message = CameraSettingsDialog::tr("The camera's frame-rate range exceeds this editor's numeric precision. Camera settings cannot be applied here.");
        } else if (editable && !roiEditorsRepresentable) {
            message = CameraSettingsDialog::tr("The camera's ROI range or increment cannot be represented by this editor. Camera settings cannot be applied here.");
        } else if (editable && !valid) {
            message = CameraSettingsDialog::tr("Settings unavailable or outside the camera's supported ranges. Review pixel format, ROI, frame rate, exposure, gain and camera capabilities before applying.");
        } else if (editable) {
            message = CameraSettingsDialog::tr("Apply settings, review the actual readback, then explicitly Confirm before Start.");
        }
        pixelFormat->setEnabled(editable && pixelFormat->count() > 0);
        roiX->setEnabled(editable && roiEditorsRepresentable);
        roiY->setEnabled(editable && roiEditorsRepresentable);
        roiWidth->setEnabled(editable && roiEditorsRepresentable);
        roiHeight->setEnabled(editable && roiEditorsRepresentable);
        exposureMode->setEnabled(editable && exposureMode->count() > 0);
        gainMode->setEnabled(editable && gainMode->count() > 0);
        frameRateValue->setEnabled(editable && draft && frameRatePrecisionSupported);
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
