#include "CameraSettingsAdapter.hpp"

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/presentation/CameraActionPolicy.hpp>
#include <lumora/presentation/CameraSettingsModel.hpp>
#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <QLocale>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace lumora::qml {
namespace {

QString number(double value) {
    return QLocale::c().toString(value, 'g', QLocale::FloatingPointShortest);
}

template<typename Mode>
QVariantList modeRows(const std::vector<Mode>& modes) {
    QVariantList rows;
    for (const auto mode : modes) {
        if (mode != Mode::Manual && mode != Mode::Auto) continue;
        rows.append(QVariantMap{
            {QStringLiteral("text"), mode == Mode::Manual
                ? CameraSettingsAdapter::tr("Manual") : CameraSettingsAdapter::tr("Automatic")},
            {QStringLiteral("value"), static_cast<int>(mode)}});
    }
    return rows;
}

template<typename Mode>
bool advertised(const std::vector<Mode>& modes, int requested) {
    return std::any_of(modes.begin(), modes.end(), [requested](Mode mode) {
        return static_cast<int>(mode) == requested
            && (mode == Mode::Manual || mode == Mode::Auto);
    });
}

QVariantList formatRows(const std::vector<core::SourcePixelFormat>& formats) {
    QVariantList rows;
    for (const auto& format : formats) {
        const auto name = QString::fromStdString(format.canonicalName);
        rows.append(QVariantMap{
            {QStringLiteral("text"), name},
            {QStringLiteral("value"), name}});
    }
    return rows;
}

std::optional<double> parseNumber(const QString& text) {
    const auto bytes = text.trimmed().toUtf8();
    const char* begin = bytes.constData();
    const char* end = begin + bytes.size();
    if (begin != end && *begin == '+') {
        ++begin;
        if (begin != end && (*begin == '+' || *begin == '-')) return {};
    }
    double value{};
    const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value)) return {};
    return value;
}

std::optional<std::uint32_t> parseUnsigned(const QString& text) {
    const auto bytes = text.toUtf8();
    const char* begin = bytes.constData();
    const char* end = begin + bytes.size();
    if (begin == end || std::any_of(begin, end, [](char value) {
            return value < '0' || value > '9';
        })) return {};
    std::uint64_t value{};
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end
        || value > std::numeric_limits<std::uint32_t>::max()) return {};
    return static_cast<std::uint32_t>(value);
}

bool inRange(const std::optional<double>& value, const camera::NumericCapability& capability) {
    return value && *value >= capability.minimum && *value <= capability.maximum;
}

QString range(const camera::NumericCapability& capability) {
    return CameraSettingsAdapter::tr("Range: %1 to %2; increment: %3")
        .arg(number(capability.minimum), number(capability.maximum), number(capability.increment));
}

QString range(const camera::RegionOfInterestCapability& capability) {
    return CameraSettingsAdapter::tr(
        "ROI x: %1 to %2 (increment %3); y: %4 to %5 (increment %6); "
        "width: %7 to %8 (increment %9); height: %10 to %11 (increment %12)")
        .arg(capability.minimum.x).arg(capability.maximum.x).arg(capability.increment.x)
        .arg(capability.minimum.y).arg(capability.maximum.y).arg(capability.increment.y)
        .arg(capability.minimum.width).arg(capability.maximum.width)
        .arg(capability.increment.width).arg(capability.minimum.height)
        .arg(capability.maximum.height).arg(capability.increment.height);
}

bool validRoi(const core::RegionOfInterest& value,
    const camera::RegionOfInterestCapability& capability) {
    const auto validDimension = [](std::uint32_t current, std::uint32_t minimum,
                                    std::uint32_t maximum, std::uint32_t increment) {
        return increment != 0U && current >= minimum && current <= maximum
            && (current - minimum) % increment == 0U;
    };
    return validDimension(value.x, capability.minimum.x, capability.maximum.x,
               capability.increment.x)
        && validDimension(value.y, capability.minimum.y, capability.maximum.y,
            capability.increment.y)
        && validDimension(value.width, capability.minimum.width, capability.maximum.width,
            capability.increment.width)
        && validDimension(value.height, capability.minimum.height, capability.maximum.height,
            capability.increment.height)
        && static_cast<std::uint64_t>(value.x) + value.width <= capability.maximum.width
        && static_cast<std::uint64_t>(value.y) + value.height <= capability.maximum.height;
}

QString accessReason(camera::ControlAccess access, const QString& control,
    const presentation::WorkstationState& presentation) {
    if (access == camera::ControlAccess::Unavailable)
        return CameraSettingsAdapter::tr("%1 is unavailable.").arg(control);
    if (access == camera::ControlAccess::ReadOnly)
        return CameraSettingsAdapter::tr("%1 is read-only.").arg(control);
    if (presentation.cameraStatus
        && presentation.cameraStatus->state == application::CameraSessionState::Streaming)
        return CameraSettingsAdapter::tr("Stop acquisition to edit %1.").arg(control);
    return {};
}

QString modeValue(const std::optional<camera::ExposureMode>& mode,
    const std::optional<double>& value, const QString& unit) {
    if (!mode) return CameraSettingsAdapter::tr("Unavailable");
    if (*mode == camera::ExposureMode::Auto) {
        return value ? CameraSettingsAdapter::tr("Automatic (actual %1 %2)")
                           .arg(number(*value), unit)
                     : CameraSettingsAdapter::tr("Automatic");
    }
    return value ? CameraSettingsAdapter::tr("Manual %1 %2").arg(number(*value), unit)
                 : CameraSettingsAdapter::tr("Unavailable");
}

QString modeValue(const std::optional<camera::GainMode>& mode,
    const std::optional<double>& value, const QString& unit) {
    if (!mode) return CameraSettingsAdapter::tr("Unavailable");
    if (*mode == camera::GainMode::Auto) {
        return value ? CameraSettingsAdapter::tr("Automatic (actual %1 %2)")
                           .arg(number(*value), unit)
                     : CameraSettingsAdapter::tr("Automatic");
    }
    return value ? CameraSettingsAdapter::tr("Manual %1 %2").arg(number(*value), unit)
                 : CameraSettingsAdapter::tr("Unavailable");
}

QString configurationSummary(const camera::CameraConfiguration& value) {
    return CameraSettingsAdapter::tr(
        "%1 · %2 × %3 at (%4, %5) · %6 fps · Exposure: %7 · Gain: %8")
        .arg(QString::fromStdString(value.pixelFormat.canonicalName))
        .arg(value.roi.width).arg(value.roi.height).arg(value.roi.x).arg(value.roi.y)
        .arg(value.requestedFps ? number(*value.requestedFps)
                                : CameraSettingsAdapter::tr("Unavailable"),
            modeValue(value.exposure.mode, value.exposure.requestedMicroseconds,
                QStringLiteral("µs")),
            modeValue(value.gain.mode, value.gain.requestedDb, QStringLiteral("dB")));
}

QString cameraSourceSummary(const application::CameraStatusSnapshot& source) {
    if (!source.actualIdentity) return CameraSettingsAdapter::tr("Camera unavailable");
    const auto descriptor = std::find_if(source.discoveredDescriptors.begin(),
        source.discoveredDescriptors.end(), [&](const auto& value) {
            return value.id == *source.actualIdentity;
        });
    if (descriptor == source.discoveredDescriptors.end()) {
        return CameraSettingsAdapter::tr("Camera: %1")
            .arg(QString::fromStdString(source.actualIdentity->value));
    }
    return CameraSettingsAdapter::tr("Camera: %1 %2 — %3")
        .arg(QString::fromStdString(descriptor->identity.manufacturer),
            QString::fromStdString(descriptor->identity.model),
            QString::fromStdString(descriptor->identity.serial));
}

QString controlReason(camera::ControlAccess modeAccess, camera::ControlAccess valueAccess,
    const QString& control, const presentation::WorkstationState& presentation) {
    QStringList reasons;
    const auto appendAccess = [&](camera::ControlAccess access, const QString& part) {
        if (access == camera::ControlAccess::Unavailable)
            reasons.append(CameraSettingsAdapter::tr("%1 is unavailable.").arg(part));
        else if (access == camera::ControlAccess::ReadOnly)
            reasons.append(CameraSettingsAdapter::tr("%1 is read-only.").arg(part));
    };
    appendAccess(modeAccess, CameraSettingsAdapter::tr("%1 mode").arg(control));
    appendAccess(valueAccess, CameraSettingsAdapter::tr("%1 value").arg(control));
    if (!reasons.isEmpty()) return reasons.join(QLatin1Char(' '));
    if (presentation.cameraStatus
        && presentation.cameraStatus->state == application::CameraSessionState::Streaming)
        return CameraSettingsAdapter::tr("Stop acquisition to edit %1.").arg(control);
    return {};
}

} // namespace

CameraSettingsAdapter::CameraSettingsAdapter(
    presentation::WorkstationCoordinator& coordinator, QObject* parent)
    : QObject(parent), coordinator_(coordinator) {}

void CameraSettingsAdapter::refresh() {
    if (!draft_) return;
    draft_->update(coordinator_.state());
    State next;
    next.open = true;
    const auto& source = draft_->source();
    const auto& configuration = draft_->configuration();
    const auto& presentation = draft_->presentation();
    const auto policy = presentation::CameraActionPolicy::evaluate(presentation);
    const auto readback = draft_->readback();
    if (source) next.sourceSummary = cameraSourceSummary(*source);
    if (readback) next.currentSummary = configurationSummary(*readback);

    if (closing_) {
        next.status = tr("Closing");
    } else if (!commandError_.isEmpty()) {
        next.status = commandError_;
    } else if (draft_->rebindCompleted()) {
        next.status = tr("Settings were applied. Review the current readback, then close and reopen before Confirm and Start.");
    } else if (draft_->invalidated()) {
        next.status = tr("The camera or settings session changed. Close and reopen before editing.");
    } else if (draft_->normalizationError()) {
        next.status = tr("Fresh camera settings are unavailable or invalid. Close and reopen after a valid readback.");
    } else if (!source || !configuration) {
        next.status = tr("Camera settings are unavailable. Connect a camera, then close and reopen.");
    } else if (presentation.cameraStatus
        && presentation.cameraStatus->state == application::CameraSessionState::Streaming) {
        next.status = tr("Stop acquisition before editing camera settings. Pausing the viewer does not stop the camera.");
    } else if (!presentation.cameraStatus
        || presentation.cameraStatus->state != application::CameraSessionState::ConnectedIdle) {
        next.status = tr("Camera settings are available only while the connected camera is stopped.");
    } else if (policy.installationPending) {
        next.status = tr("Wait for the installation settings operation to finish.");
    } else if (!presentation.controlsEnabled || presentation.ordinaryOperationPending) {
        next.status = tr("Wait for the current camera operation to finish.");
    } else if (policy.confirm.visible && presentation.requestedConfiguration && configuration
        && application::cameraConfigurationsEqual(
            *presentation.requestedConfiguration, *configuration)) {
        next.status = tr("Settings were applied. Review the current readback, then close before Confirm and Start.");
    } else {
        next.status = tr("Edit camera settings, then Apply.");
    }

    next.editable = !closing_ && !policy.installationPending && draft_->editable();
    if (source && source->capabilities) {
        const auto& capabilities = *source->capabilities;
        next.pixelFormats = formatRows(capabilities.pixelFormats);
        next.exposureModes = modeRows(capabilities.exposureModes);
        next.gainModes = modeRows(capabilities.gainModes);
        if (capabilities.frameRate.access != camera::ControlAccess::Unavailable)
            next.frameRateRange = range(capabilities.frameRate);
        if (capabilities.roi.access != camera::ControlAccess::Unavailable)
            next.roiRange = range(capabilities.roi);
        if (capabilities.exposure.access != camera::ControlAccess::Unavailable)
            next.exposureRange = range(capabilities.exposure);
        if (capabilities.gain.access != camera::ControlAccess::Unavailable)
            next.gainRange = range(capabilities.gain);
        next.frameRateEnabled = next.editable
            && presentation::isWritableCameraControl(capabilities.frameRate.access);
        next.pixelFormatEnabled = next.editable
            && presentation::isWritableCameraControl(capabilities.pixelFormatAccess)
            && !capabilities.pixelFormats.empty();
        next.roiEnabled = next.editable
            && presentation::isWritableCameraControl(capabilities.roi.access);
        next.exposureModeEnabled = next.editable
            && presentation::isWritableCameraControl(capabilities.exposureModeAccess);
        next.gainModeEnabled = next.editable
            && presentation::isWritableCameraControl(capabilities.gainModeAccess);
        next.frameRateReason = accessReason(
            capabilities.frameRate.access, tr("frame rate"), presentation);
        next.pixelFormatReason = accessReason(
            capabilities.pixelFormatAccess, tr("pixel format"), presentation);
        next.roiReason = accessReason(
            capabilities.roi.access, tr("the image region"), presentation);
        next.exposureReason = controlReason(capabilities.exposureModeAccess,
            capabilities.exposure.access, tr("Exposure"), presentation);
        next.gainReason = controlReason(capabilities.gainModeAccess,
            capabilities.gain.access, tr("Gain"), presentation);

        if (configuration) {
            next.frameRateText = frameRateText_;
            next.pixelFormat = QString::fromStdString(configuration->pixelFormat.canonicalName);
            next.roiXText = roiXText_;
            next.roiYText = roiYText_;
            next.roiWidthText = roiWidthText_;
            next.roiHeightText = roiHeightText_;
            next.exposureMode = configuration->exposure.mode
                ? static_cast<int>(*configuration->exposure.mode) : -1;
            next.gainMode = configuration->gain.mode
                ? static_cast<int>(*configuration->gain.mode) : -1;
            const bool exposureManual = configuration->exposure.mode
                == camera::ExposureMode::Manual;
            const bool gainManual = configuration->gain.mode == camera::GainMode::Manual;
            next.exposureText = exposureManual ? exposureManualText_ : QString{};
            next.gainText = gainManual ? gainManualText_ : QString{};
            next.exposureValueEnabled = next.editable && exposureManual
                && presentation::isWritableCameraControl(capabilities.exposure.access);
            next.gainValueEnabled = next.editable && gainManual
                && presentation::isWritableCameraControl(capabilities.gain.access);
            if (exposureManual && !exposureTextValid_) {
                if (presentation::isWritableCameraControl(capabilities.exposure.access)) {
                    next.exposureReason = tr("Enter a finite exposure from %1 to %2.")
                        .arg(number(capabilities.exposure.minimum),
                            number(capabilities.exposure.maximum));
                } else if (capabilities.exposure.access == camera::ControlAccess::ReadOnly) {
                    next.exposureReason = exposureManualValue_
                        ? tr("Exposure readback is invalid for Manual mode.")
                        : tr("Exposure readback is unavailable for Manual mode.");
                }
            }
            if (gainManual && !gainTextValid_) {
                if (presentation::isWritableCameraControl(capabilities.gain.access)) {
                    next.gainReason = tr("Enter a finite gain from %1 to %2.")
                        .arg(number(capabilities.gain.minimum), number(capabilities.gain.maximum));
                } else if (capabilities.gain.access == camera::ControlAccess::ReadOnly) {
                    next.gainReason = gainManualValue_
                        ? tr("Gain readback is invalid for Manual mode.")
                        : tr("Gain readback is unavailable for Manual mode.");
                }
            }
            if (presentation::isWritableCameraControl(capabilities.frameRate.access)
                && !frameRateTextValid_) {
                next.frameRateReason = tr("Enter a finite positive frame rate from %1 to %2.")
                    .arg(number(capabilities.frameRate.minimum),
                        number(capabilities.frameRate.maximum));
            }
            const bool roiSyntaxValid = roiXTextValid_ && roiYTextValid_
                && roiWidthTextValid_ && roiHeightTextValid_;
            if (presentation::isWritableCameraControl(capabilities.roi.access)
                && !roiSyntaxValid) {
                next.roiReason = tr("Enter unsigned whole numbers for x, y, width and height.");
            } else if (presentation::isWritableCameraControl(capabilities.roi.access)
                && !validRoi(configuration->roi, capabilities.roi)) {
                next.roiReason = tr("The image region must satisfy the advertised ranges, increments and sensor bounds.");
            }
            const bool visibleTextValid = (!exposureManual || exposureTextValid_)
                && (!gainManual || gainTextValid_) && frameRateTextValid_
                && roiSyntaxValid;
            next.applyEnabled = next.editable && visibleTextValid && draft_->canApply();
        }
    }
    const bool changed = next != state_;
    if (changed) {
        state_ = std::move(next);
        emit stateChanged();
    }
}

void CameraSettingsAdapter::setClosing() {
    closing_ = true;
    closeSettings();
}

bool CameraSettingsAdapter::openSettings() {
    coordinator_.poll();
    if (closing_) return false;
    if (draft_) {
        refresh();
        return true;
    }
    const auto policy = presentation::CameraActionPolicy::evaluate(coordinator_.state());
    if (!policy.settings.enabled) return false;
    draft_.emplace();
    draft_->update(coordinator_.state());
    const auto& configuration = draft_->configuration();
    if (!draft_->source() || !draft_->source()->capabilities || !configuration) {
        draft_.reset();
        return false;
    }
    const auto readback = draft_->readback();
    const auto& capabilities = *draft_->source()->capabilities;
    const auto initialManualValue = [&](const auto& mode, const auto& requested,
                                        const auto& actual,
                                        const camera::NumericCapability& capability) {
        using Mode = typename std::decay_t<decltype(mode)>::value_type;
        if (mode == Mode::Manual) return requested;
        if (presentation::isWritableCameraControl(capability.access))
            return std::optional<double>{capability.minimum};
        return actual;
    };
    exposureManualValue_ = initialManualValue(configuration->exposure.mode,
        configuration->exposure.requestedMicroseconds,
        readback ? readback->exposure.requestedMicroseconds : std::nullopt,
        capabilities.exposure);
    gainManualValue_ = initialManualValue(configuration->gain.mode,
        configuration->gain.requestedDb,
        readback ? readback->gain.requestedDb : std::nullopt,
        capabilities.gain);
    exposureManualText_ = exposureManualValue_ ? number(*exposureManualValue_) : QString{};
    gainManualText_ = gainManualValue_ ? number(*gainManualValue_) : QString{};
    frameRateText_ = configuration->requestedFps
        ? number(*configuration->requestedFps) : QString{};
    roiXText_ = QString::number(configuration->roi.x);
    roiYText_ = QString::number(configuration->roi.y);
    roiWidthText_ = QString::number(configuration->roi.width);
    roiHeightText_ = QString::number(configuration->roi.height);
    frameRateTextValid_ = !presentation::isWritableCameraControl(capabilities.frameRate.access)
        || (configuration->requestedFps && *configuration->requestedFps > 0.0
            && inRange(configuration->requestedFps, capabilities.frameRate));
    roiXTextValid_ = true;
    roiYTextValid_ = true;
    roiWidthTextValid_ = true;
    roiHeightTextValid_ = true;
    exposureTextValid_ = inRange(exposureManualValue_, capabilities.exposure);
    gainTextValid_ = inRange(gainManualValue_, capabilities.gain);
    commandError_.clear();
    refresh();
    return true;
}

void CameraSettingsAdapter::closeSettings() {
    const bool changed = state_.open;
    draft_.reset();
    state_ = {};
    frameRateText_.clear();
    roiXText_.clear();
    roiYText_.clear();
    roiWidthText_.clear();
    roiHeightText_.clear();
    exposureManualText_.clear();
    gainManualText_.clear();
    exposureManualValue_.reset();
    gainManualValue_.reset();
    frameRateTextValid_ = false;
    roiXTextValid_ = false;
    roiYTextValid_ = false;
    roiWidthTextValid_ = false;
    roiHeightTextValid_ = false;
    exposureTextValid_ = false;
    gainTextValid_ = false;
    commandError_.clear();
    if (changed) emit stateChanged();
}

bool CameraSettingsAdapter::beginLocalEdit() {
    coordinator_.poll();
    refresh();
    if (!draft_ || closing_ || !state_.editable) return false;
    if (const auto source = draft_->beginEditing()) {
        const auto result = coordinator_.beginCameraSettingsEdit(
            source->sessionGeneration, source->cameraId);
        if (!result.hasValue()) {
            commandError_ = QString::fromStdString(result.error().operatorSummary);
            refresh();
            return false;
        }
    }
    commandError_.clear();
    return true;
}

bool CameraSettingsAdapter::editText(const QString& text, bool exposure) {
    if (!beginLocalEdit() || !draft_ || !draft_->configuration() || !draft_->source()
        || !draft_->source()->capabilities) return false;
    const auto& capabilities = *draft_->source()->capabilities;
    const auto& capability = exposure ? capabilities.exposure : capabilities.gain;
    const bool manual = exposure
        ? draft_->configuration()->exposure.mode == camera::ExposureMode::Manual
        : draft_->configuration()->gain.mode == camera::GainMode::Manual;
    if (!manual || !presentation::isWritableCameraControl(capability.access)) return false;
    auto value = parseNumber(text);
    const bool valid = inRange(value, capability);
    if (exposure) {
        exposureManualText_ = text;
        exposureTextValid_ = valid;
        if (valid) exposureManualValue_ = value;
    } else {
        gainManualText_ = text;
        gainTextValid_ = valid;
        if (valid) gainManualValue_ = value;
    }
    bool accepted = true;
    if (valid) {
        auto configuration = *draft_->configuration();
        if (exposure) configuration.exposure.requestedMicroseconds = value;
        else configuration.gain.requestedDb = value;
        accepted = draft_->setConfiguration(std::move(configuration));
    }
    refresh();
    return accepted;
}

bool CameraSettingsAdapter::editExposureText(const QString& text) {
    return editText(text, true);
}

bool CameraSettingsAdapter::editGainText(const QString& text) {
    return editText(text, false);
}

bool CameraSettingsAdapter::editFrameRateText(const QString& text) {
    if (!beginLocalEdit() || !draft_ || !draft_->configuration() || !draft_->source()
        || !draft_->source()->capabilities) return false;
    const auto& capability = draft_->source()->capabilities->frameRate;
    if (!presentation::isWritableCameraControl(capability.access)) return false;
    frameRateText_ = text;
    const auto value = parseNumber(text);
    frameRateTextValid_ = value && *value > 0.0 && inRange(value, capability);
    bool accepted = true;
    if (frameRateTextValid_) {
        auto configuration = *draft_->configuration();
        configuration.requestedFps = value;
        accepted = draft_->setConfiguration(std::move(configuration));
    }
    refresh();
    return accepted;
}

bool CameraSettingsAdapter::setPixelFormat(const QString& requested) {
    if (!beginLocalEdit() || !draft_ || !draft_->configuration() || !draft_->source()
        || !draft_->source()->capabilities) return false;
    const auto& capabilities = *draft_->source()->capabilities;
    if (!presentation::isWritableCameraControl(capabilities.pixelFormatAccess)) return false;
    const auto match = std::find_if(capabilities.pixelFormats.begin(),
        capabilities.pixelFormats.end(), [&](const auto& format) {
            return QString::fromStdString(format.canonicalName) == requested;
        });
    if (match == capabilities.pixelFormats.end()) return false;
    auto configuration = *draft_->configuration();
    configuration.pixelFormat = *match;
    const bool accepted = draft_->setConfiguration(std::move(configuration));
    refresh();
    return accepted;
}

bool CameraSettingsAdapter::editRoiText(const QString& field, const QString& text) {
    const bool knownField = field == QStringLiteral("x") || field == QStringLiteral("y")
        || field == QStringLiteral("width") || field == QStringLiteral("height");
    if (!knownField) {
        coordinator_.poll();
        refresh();
        return false;
    }
    if (!beginLocalEdit() || !draft_ || !draft_->configuration() || !draft_->source()
        || !draft_->source()->capabilities) return false;
    const auto& capability = draft_->source()->capabilities->roi;
    if (!presentation::isWritableCameraControl(capability.access)) return false;

    const auto value = parseUnsigned(text);
    QString* raw = nullptr;
    bool* syntaxValid = nullptr;
    if (field == QStringLiteral("x")) {
        raw = &roiXText_;
        syntaxValid = &roiXTextValid_;
    } else if (field == QStringLiteral("y")) {
        raw = &roiYText_;
        syntaxValid = &roiYTextValid_;
    } else if (field == QStringLiteral("width")) {
        raw = &roiWidthText_;
        syntaxValid = &roiWidthTextValid_;
    } else {
        raw = &roiHeightText_;
        syntaxValid = &roiHeightTextValid_;
    }
    *raw = text;
    *syntaxValid = value.has_value();

    bool accepted = true;
    if (value) {
        auto configuration = *draft_->configuration();
        if (field == QStringLiteral("x")) configuration.roi.x = *value;
        else if (field == QStringLiteral("y")) configuration.roi.y = *value;
        else if (field == QStringLiteral("width")) configuration.roi.width = *value;
        else configuration.roi.height = *value;
        accepted = draft_->setConfiguration(std::move(configuration));
    }
    refresh();
    return accepted;
}

bool CameraSettingsAdapter::setMode(int mode, bool exposure) {
    if (!beginLocalEdit() || !draft_ || !draft_->configuration() || !draft_->source()
        || !draft_->source()->capabilities) return false;
    const auto& capabilities = *draft_->source()->capabilities;
    const auto modeWritable = exposure ? capabilities.exposureModeAccess
                                       : capabilities.gainModeAccess;
    if (!presentation::isWritableCameraControl(modeWritable)) return false;
    auto configuration = *draft_->configuration();
    if (exposure) {
        if (!advertised(capabilities.exposureModes, mode)) return false;
        const auto requested = static_cast<camera::ExposureMode>(mode);
        configuration.exposure.mode = requested;
        if (requested == camera::ExposureMode::Auto) {
            configuration.exposure.requestedMicroseconds.reset();
        } else if (!presentation::isWritableCameraControl(capabilities.exposure.access)) {
            const auto readback = draft_->readback();
            configuration.exposure.requestedMicroseconds = readback
                ? readback->exposure.requestedMicroseconds : std::nullopt;
            exposureManualValue_ = configuration.exposure.requestedMicroseconds;
            exposureManualText_ = exposureManualValue_ ? number(*exposureManualValue_) : QString{};
            exposureTextValid_ = inRange(exposureManualValue_, capabilities.exposure);
        } else {
            configuration.exposure.requestedMicroseconds = exposureManualValue_;
        }
    } else {
        if (!advertised(capabilities.gainModes, mode)) return false;
        const auto requested = static_cast<camera::GainMode>(mode);
        configuration.gain.mode = requested;
        if (requested == camera::GainMode::Auto) {
            configuration.gain.requestedDb.reset();
        } else if (!presentation::isWritableCameraControl(capabilities.gain.access)) {
            const auto readback = draft_->readback();
            configuration.gain.requestedDb = readback ? readback->gain.requestedDb : std::nullopt;
            gainManualValue_ = configuration.gain.requestedDb;
            gainManualText_ = gainManualValue_ ? number(*gainManualValue_) : QString{};
            gainTextValid_ = inRange(gainManualValue_, capabilities.gain);
        } else {
            configuration.gain.requestedDb = gainManualValue_;
        }
    }
    const bool accepted = draft_->setConfiguration(std::move(configuration));
    refresh();
    return accepted;
}

bool CameraSettingsAdapter::setExposureMode(int mode) { return setMode(mode, true); }
bool CameraSettingsAdapter::setGainMode(int mode) { return setMode(mode, false); }

bool CameraSettingsAdapter::apply() {
    coordinator_.poll();
    refresh();
    if (!draft_ || closing_ || !state_.applyEnabled) return false;
    const auto request = draft_->prepareApply();
    if (!request) return false;
    const auto result = coordinator_.applyCameraSettings(request->source.sessionGeneration,
        request->source.cameraId, request->requested);
    commandError_ = result.hasValue() ? QString{}
        : QString::fromStdString(result.error().operatorSummary);
    // Observe the coordinator's published admission before any subsequent poll
    // can deliver a fast completion for the shared source-bound tracker.
    refresh();
    return result.hasValue();
}

} // namespace lumora::qml
