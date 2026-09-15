#pragma once

#include "CameraSettingsAdapter.hpp"
#include <lumora/presentation/CameraActionPolicy.hpp>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace lumora::presentation { class WorkstationCoordinator; }
namespace lumora::qml {
class CameraAdapter final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The workstation owns camera controls")
    Q_PROPERTY(lumora::qml::CameraSettingsAdapter* settings READ settings CONSTANT FINAL)
    Q_PROPERTY(bool installationVisible READ installationVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool installationEnabled READ installationEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool settingsVisible READ settingsVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool settingsEnabled READ settingsEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged FINAL)
    Q_PROPERTY(QString selectedCameraId READ selectedCameraId NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool selectionEnabled READ selectionEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString warning READ warning NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString orientation READ orientation NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap currentConfiguration READ currentConfiguration NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap requestedConfiguration READ requestedConfiguration NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap appliedConfiguration READ appliedConfiguration NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString currentSummary READ currentSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString requestedSummary READ requestedSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString appliedSummary READ appliedSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool refreshVisible READ refreshVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool refreshEnabled READ refreshEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool connectVisible READ connectVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool connectEnabled READ connectEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool applyVisible READ applyVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool applyEnabled READ applyEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool confirmVisible READ confirmVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool confirmEnabled READ confirmEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool startVisible READ startVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool startEnabled READ startEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool stopVisible READ stopVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool stopEnabled READ stopEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool disconnectVisible READ disconnectVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool disconnectEnabled READ disconnectEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool retryVisible READ retryVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool retryEnabled READ retryEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool resumeLiveVisible READ resumeLiveVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool resumeLiveEnabled READ resumeLiveEnabled NOTIFY stateChanged FINAL)
public:
    explicit CameraAdapter(presentation::WorkstationCoordinator&, QObject* parent = nullptr);
    [[nodiscard]] CameraSettingsAdapter* settings() { return &settings_; }
    [[nodiscard]] const CameraSettingsAdapter* settings() const { return &settings_; }
    [[nodiscard]] bool settingsVisible() const { return policy_.settings.visible; }
    [[nodiscard]] bool settingsEnabled() const { return !closing_ && policy_.settings.enabled; }
    [[nodiscard]] bool installationVisible() const { return policy_.installation.visible; }
    [[nodiscard]] bool installationEnabled() const { return !closing_ && policy_.installation.enabled; }
    void refresh();
    void setClosing();
    Q_INVOKABLE bool selectCamera(const QString& id);
    Q_INVOKABLE bool refreshDevices();
    Q_INVOKABLE bool connectCamera();
    Q_INVOKABLE bool applyConfiguration();
    Q_INVOKABLE bool confirmConfiguration();
    Q_INVOKABLE bool startLive();
    Q_INVOKABLE bool stopLive();
    Q_INVOKABLE bool disconnectCamera();
    Q_INVOKABLE bool retry();
    Q_INVOKABLE bool resumeLive();
    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] QString selectedCameraId() const;
    [[nodiscard]] bool selectionEnabled() const;
    [[nodiscard]] bool pending() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString warning() const;
    [[nodiscard]] QString orientation() const;
    [[nodiscard]] QVariantMap currentConfiguration() const;
    [[nodiscard]] QVariantMap requestedConfiguration() const;
    [[nodiscard]] QVariantMap appliedConfiguration() const;
    [[nodiscard]] QString currentSummary() const;
    [[nodiscard]] QString requestedSummary() const;
    [[nodiscard]] QString appliedSummary() const;
    [[nodiscard]] bool refreshVisible() const;
    [[nodiscard]] bool refreshEnabled() const;
    [[nodiscard]] bool connectVisible() const;
    [[nodiscard]] bool connectEnabled() const;
    [[nodiscard]] bool applyVisible() const;
    [[nodiscard]] bool applyEnabled() const;
    [[nodiscard]] bool confirmVisible() const;
    [[nodiscard]] bool confirmEnabled() const;
    [[nodiscard]] bool startVisible() const;
    [[nodiscard]] bool startEnabled() const;
    [[nodiscard]] bool stopVisible() const;
    [[nodiscard]] bool stopEnabled() const;
    [[nodiscard]] bool disconnectVisible() const;
    [[nodiscard]] bool disconnectEnabled() const;
    [[nodiscard]] bool retryVisible() const;
    [[nodiscard]] bool retryEnabled() const;
    [[nodiscard]] bool resumeLiveVisible() const;
    [[nodiscard]] bool resumeLiveEnabled() const;
signals:
    void stateChanged();
    void devicesChanged();
private:
    bool dispatch(presentation::CameraStartupIntent);
    presentation::WorkstationCoordinator& coordinator_;
    CameraSettingsAdapter settings_;
    presentation::WorkstationState state_;
    presentation::CameraActionPolicy policy_;
    QString commandError_;
    QVariantList devices_;
    bool closing_{};
};
}
