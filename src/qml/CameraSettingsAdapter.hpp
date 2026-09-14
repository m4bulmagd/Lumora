#pragma once

#include <lumora/presentation/CameraSettingsDraft.hpp>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace lumora::presentation { class WorkstationCoordinator; }

namespace lumora::qml {

class CameraSettingsAdapter final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The workstation owns camera settings")
    Q_PROPERTY(bool open READ isOpen NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool editable READ editable NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool applyEnabled READ applyEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString currentSummary READ currentSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList exposureModes READ exposureModes NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList gainModes READ gainModes NOTIFY stateChanged FINAL)
    Q_PROPERTY(int exposureMode READ exposureMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(int gainMode READ gainMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool exposureModeEnabled READ exposureModeEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool gainModeEnabled READ gainModeEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool exposureValueEnabled READ exposureValueEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool gainValueEnabled READ gainValueEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString exposureText READ exposureText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString gainText READ gainText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString exposureRange READ exposureRange NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString gainRange READ gainRange NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString exposureReason READ exposureReason NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString gainReason READ gainReason NOTIFY stateChanged FINAL)

public:
    explicit CameraSettingsAdapter(
        presentation::WorkstationCoordinator& coordinator, QObject* parent = nullptr);

    void refresh();
    void setClosing();

    [[nodiscard]] bool isOpen() const { return state_.open; }
    [[nodiscard]] bool editable() const { return state_.editable; }
    [[nodiscard]] bool applyEnabled() const { return state_.applyEnabled; }
    [[nodiscard]] QString status() const { return state_.status; }
    [[nodiscard]] QString sourceSummary() const { return state_.sourceSummary; }
    [[nodiscard]] QString currentSummary() const { return state_.currentSummary; }
    [[nodiscard]] QVariantList exposureModes() const { return state_.exposureModes; }
    [[nodiscard]] QVariantList gainModes() const { return state_.gainModes; }
    [[nodiscard]] int exposureMode() const { return state_.exposureMode; }
    [[nodiscard]] int gainMode() const { return state_.gainMode; }
    [[nodiscard]] bool exposureModeEnabled() const { return state_.exposureModeEnabled; }
    [[nodiscard]] bool gainModeEnabled() const { return state_.gainModeEnabled; }
    [[nodiscard]] bool exposureValueEnabled() const { return state_.exposureValueEnabled; }
    [[nodiscard]] bool gainValueEnabled() const { return state_.gainValueEnabled; }
    [[nodiscard]] QString exposureText() const { return state_.exposureText; }
    [[nodiscard]] QString gainText() const { return state_.gainText; }
    [[nodiscard]] QString exposureRange() const { return state_.exposureRange; }
    [[nodiscard]] QString gainRange() const { return state_.gainRange; }
    [[nodiscard]] QString exposureReason() const { return state_.exposureReason; }
    [[nodiscard]] QString gainReason() const { return state_.gainReason; }

    Q_INVOKABLE bool openSettings();
    Q_INVOKABLE void closeSettings();
    Q_INVOKABLE bool editExposureText(const QString& text);
    Q_INVOKABLE bool editGainText(const QString& text);
    Q_INVOKABLE bool setExposureMode(int mode);
    Q_INVOKABLE bool setGainMode(int mode);
    Q_INVOKABLE bool apply();

signals:
    void stateChanged();

private:
    bool beginLocalEdit();
    bool editText(const QString& text, bool exposure);
    bool setMode(int mode, bool exposure);

    struct State final {
        bool open{false};
        bool editable{false};
        bool applyEnabled{false};
        QString status, sourceSummary, currentSummary;
        QVariantList exposureModes, gainModes;
        int exposureMode{-1}, gainMode{-1};
        bool exposureModeEnabled{false}, gainModeEnabled{false};
        bool exposureValueEnabled{false}, gainValueEnabled{false};
        QString exposureText, gainText, exposureRange, gainRange;
        QString exposureReason, gainReason;
        bool operator==(const State&) const = default;
    };

    presentation::WorkstationCoordinator& coordinator_;
    std::optional<presentation::CameraSettingsDraft> draft_;
    State state_;
    QString exposureManualText_, gainManualText_, commandError_;
    std::optional<double> exposureManualValue_, gainManualValue_;
    bool exposureTextValid_{false}, gainTextValid_{false};
    bool closing_{false};
};

} // namespace lumora::qml
