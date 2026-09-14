#include "CameraAdapter.hpp"
#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <QLocale>
#include <algorithm>

namespace lumora::qml {
namespace {
QVariantMap configurationMap(const camera::CameraConfiguration& configuration) {
    QVariantMap result{{"pixelFormat", QString::fromStdString(configuration.pixelFormat.canonicalName)},
        {"x", configuration.roi.x}, {"y", configuration.roi.y},
        {"width", configuration.roi.width}, {"height", configuration.roi.height},
        {"exposureMode", !configuration.exposure.mode ? "Unavailable" :
            *configuration.exposure.mode == camera::ExposureMode::Manual ? "Manual" : "Automatic"},
        {"gainMode", !configuration.gain.mode ? "Unavailable" :
            *configuration.gain.mode == camera::GainMode::Manual ? "Manual" : "Automatic"},
        {"acquisitionMode", configuration.acquisitionMode == camera::AcquisitionMode::Continuous
            ? "Continuous" : "Triggered"}};
    if (configuration.requestedFps) result.insert("fps", *configuration.requestedFps);
    if (configuration.exposure.requestedMicroseconds)
        result.insert("exposureMicroseconds", *configuration.exposure.requestedMicroseconds);
    if (configuration.gain.requestedDb) result.insert("gainDb", *configuration.gain.requestedDb);
    return result;
}
QString summary(const QVariantMap& configuration) {
    if (configuration.isEmpty()) return QStringLiteral("Not available");
    const auto optionalNumber = [&](const char* key) {
        return configuration.contains(key)
            ? QLocale::c().toString(configuration.value(key).toDouble(), 'g', QLocale::FloatingPointShortest)
            : QStringLiteral("Automatic");
    };
    const auto control = [&](const char* modeKey, const char* valueKey, const QString& unit) {
        const auto mode = configuration.value(modeKey).toString();
        if (mode == "Unavailable") return mode;
        if (mode == "Manual") return QStringLiteral("Manual %1 %2").arg(optionalNumber(valueKey), unit);
        return configuration.contains(valueKey)
            ? QStringLiteral("Automatic (actual %1 %2)").arg(optionalNumber(valueKey), unit)
            : QStringLiteral("Automatic");
    };
    return QStringLiteral("%1 · %2 × %3 at (%4, %5) · %6 fps · Exposure: %7 · Gain: %8 · %9")
        .arg(configuration.value("pixelFormat").toString(),
            configuration.value("width").toString(), configuration.value("height").toString(),
            configuration.value("x").toString(), configuration.value("y").toString(),
            optionalNumber("fps"), control("exposureMode", "exposureMicroseconds", QStringLiteral("µs")),
            control("gainMode", "gainDb", QStringLiteral("dB")), configuration.value("acquisitionMode").toString());
}
QVariantList descriptors(const presentation::WorkstationState& state) {
    QVariantList result;
    if (!state.cameraStatus) return result;
    for (const auto& descriptor : state.cameraStatus->discoveredDescriptors) {
        result.append(QVariantMap{{"id", QString::fromStdString(descriptor.id.value)},
            {"displayName", QString::fromStdString(descriptor.identity.model + " · " + descriptor.id.value)},
            {"available", descriptor.available}});
    }
    return result;
}
}
CameraAdapter::CameraAdapter(presentation::WorkstationCoordinator& coordinator, QObject* parent)
    : QObject(parent), coordinator_(coordinator), settings_(coordinator, this) { refresh(); }

void CameraAdapter::refresh() {
    state_ = coordinator_.state();
    policy_ = presentation::CameraActionPolicy::evaluate(state_);
    settings_.refresh();
    auto nextDevices = descriptors(state_);
    if (nextDevices != devices_) {
        devices_ = std::move(nextDevices);
        emit devicesChanged();
    }
    emit stateChanged();
}
void CameraAdapter::setClosing() { closing_ = true; settings_.setClosing(); refresh(); }
bool CameraAdapter::selectCamera(const QString& id) {
    const auto& current = coordinator_.state();
    const auto policy = presentation::CameraActionPolicy::evaluate(current);
    const camera::CameraId identity{id.toStdString()};
    const bool available = current.cameraStatus &&
        std::any_of(current.cameraStatus->discoveredDescriptors.begin(),
            current.cameraStatus->discoveredDescriptors.end(), [&](const auto& descriptor) {
                return descriptor.id == identity && descriptor.available;
            });
    if (!policy.selectionEnabled || !available) {
        commandError_ = tr("This camera cannot be selected now.");
        refresh();
        return false;
    }
    coordinator_.selectCamera(identity);
    commandError_.clear();
    refresh();
    return true;
}
bool CameraAdapter::dispatch(presentation::CameraStartupIntent intent) {
    // The coordinator rechecks the current backend snapshot; the projected
    // button state is never an authorization token.
    auto result = coordinator_.dispatch(intent);
    commandError_ = result.hasValue() ? QString{} : QString::fromStdString(result.error().operatorSummary);
    refresh();
    return result.hasValue();
}
bool CameraAdapter::refreshDevices() { return dispatch(presentation::CameraStartupIntent::Refresh); }
bool CameraAdapter::connectCamera() { return dispatch(presentation::CameraStartupIntent::Connect); }
bool CameraAdapter::applyConfiguration() { return dispatch(presentation::CameraStartupIntent::Apply); }
bool CameraAdapter::confirmConfiguration() { return dispatch(presentation::CameraStartupIntent::Confirm); }
bool CameraAdapter::startLive() { return dispatch(presentation::CameraStartupIntent::Start); }
bool CameraAdapter::stopLive() { return dispatch(presentation::CameraStartupIntent::Stop); }
bool CameraAdapter::disconnectCamera() { return dispatch(presentation::CameraStartupIntent::Disconnect); }
bool CameraAdapter::retry() { return dispatch(presentation::CameraStartupIntent::Retry); }
bool CameraAdapter::resumeLive() { return dispatch(presentation::CameraStartupIntent::ResumeLive); }

QVariantList CameraAdapter::devices() const { return devices_; }
QString CameraAdapter::selectedCameraId() const {
    return state_.selectedCameraId ? QString::fromStdString(state_.selectedCameraId->value) : QString{};
}
bool CameraAdapter::selectionEnabled() const { return policy_.selectionEnabled; }
bool CameraAdapter::pending() const { return state_.ordinaryOperationPending; }
QString CameraAdapter::status() const {
    if (closing_) return tr("Closing");
    if (!state_.controlsEnabled && !error().isEmpty()) return tr("Camera unavailable");
    if (!state_.cameraStatus) return tr("Disconnected");
    switch (state_.cameraStatus->state) {
    case application::CameraSessionState::Disconnected: return tr("Disconnected");
    case application::CameraSessionState::Discovering: return tr("Discovering");
    case application::CameraSessionState::Connecting: return tr("Connecting");
    case application::CameraSessionState::ConnectedIdle: return tr("Connected · stopped");
    case application::CameraSessionState::Streaming: return tr("Streaming");
    case application::CameraSessionState::Reconnecting: return tr("Recovery required");
    case application::CameraSessionState::Error: return tr("Camera error");
    case application::CameraSessionState::ShuttingDown: return tr("Closing");
    }
    return {};
}
QString CameraAdapter::error() const {
    if (!commandError_.isEmpty()) return commandError_;
    if (state_.cameraStatus && state_.cameraStatus->latestError)
        return QString::fromStdString(state_.cameraStatus->latestError->operatorSummary);
    return warning();
}
QString CameraAdapter::warning() const {
    return state_.startupWarning ? QString::fromStdString(state_.startupWarning->operatorSummary) : QString{};
}
QString CameraAdapter::orientation() const {
    if (!state_.activeOrientation) return tr("No active installation");
    const auto value = *state_.activeOrientation;
    return QStringLiteral("%1° · horizontal flip %2 · vertical flip %3")
        .arg(static_cast<int>(value.rotation) * 90)
        .arg(value.flipHorizontal ? tr("on") : tr("off"), value.flipVertical ? tr("on") : tr("off"));
}
QVariantMap CameraAdapter::currentConfiguration() const {
    return state_.cameraStatus && state_.cameraStatus->currentConfiguration
        ? configurationMap(*state_.cameraStatus->currentConfiguration) : QVariantMap{};
}
QVariantMap CameraAdapter::requestedConfiguration() const {
    return state_.requestedConfiguration ? configurationMap(*state_.requestedConfiguration) : QVariantMap{};
}
QVariantMap CameraAdapter::appliedConfiguration() const {
    return state_.cameraStatus && state_.cameraStatus->appliedConfiguration
        ? configurationMap(state_.cameraStatus->appliedConfiguration->actual) : QVariantMap{};
}
QString CameraAdapter::currentSummary() const { return summary(currentConfiguration()); }
QString CameraAdapter::requestedSummary() const { return summary(requestedConfiguration()); }
QString CameraAdapter::appliedSummary() const { return summary(appliedConfiguration()); }
bool CameraAdapter::refreshVisible() const { return policy_.refresh.visible; }
bool CameraAdapter::refreshEnabled() const { return policy_.refresh.enabled; }
bool CameraAdapter::connectVisible() const { return policy_.connect.visible; }
bool CameraAdapter::connectEnabled() const { return policy_.connect.enabled; }
bool CameraAdapter::applyVisible() const { return policy_.apply.visible; }
bool CameraAdapter::applyEnabled() const { return policy_.apply.enabled; }
bool CameraAdapter::confirmVisible() const { return policy_.confirm.visible; }
bool CameraAdapter::confirmEnabled() const { return policy_.confirm.enabled; }
bool CameraAdapter::startVisible() const { return policy_.start.visible; }
bool CameraAdapter::startEnabled() const { return policy_.start.enabled; }
bool CameraAdapter::stopVisible() const { return policy_.stop.visible; }
bool CameraAdapter::stopEnabled() const { return policy_.stop.enabled; }
bool CameraAdapter::disconnectVisible() const { return policy_.disconnect.visible; }
bool CameraAdapter::disconnectEnabled() const { return policy_.disconnect.enabled; }
bool CameraAdapter::retryVisible() const { return policy_.retry.visible; }
bool CameraAdapter::retryEnabled() const { return policy_.retry.enabled; }
bool CameraAdapter::resumeLiveVisible() const { return policy_.resumeLive.visible; }
bool CameraAdapter::resumeLiveEnabled() const { return policy_.resumeLive.enabled; }
}
