#pragma once

#include <lumora/presentation/InstallationSettingsDraft.hpp>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <optional>

namespace lumora::presentation { class WorkstationCoordinator; }

namespace lumora::qml {
class InstallationAdapter final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The workstation owns installation settings")
    Q_PROPERTY(bool open READ isOpen NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool editable READ editable NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool saveEnabled READ saveEnabled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool repairVisible READ repairVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool flipHorizontal READ flipHorizontal NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool flipVertical READ flipVertical NOTIFY stateChanged FINAL)
    Q_PROPERTY(int rotationIndex READ rotationIndex NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool confirmationChecked READ confirmationChecked NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool repairChecked READ repairChecked NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString activeSummary READ activeSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString savedSummary READ savedSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
public:
    explicit InstallationAdapter(presentation::WorkstationCoordinator&, QObject* parent = nullptr);
    void refresh();
    void setClosing();

    [[nodiscard]] bool isOpen() const { return state_.open; }
    [[nodiscard]] bool editable() const { return state_.editable; }
    [[nodiscard]] bool saveEnabled() const { return state_.saveEnabled; }
    [[nodiscard]] bool pending() const { return state_.pending; }
    [[nodiscard]] bool repairVisible() const { return state_.repairVisible; }
    [[nodiscard]] bool flipHorizontal() const { return state_.flipHorizontal; }
    [[nodiscard]] bool flipVertical() const { return state_.flipVertical; }
    [[nodiscard]] int rotationIndex() const { return state_.rotationIndex; }
    [[nodiscard]] bool confirmationChecked() const { return state_.confirmationChecked; }
    [[nodiscard]] bool repairChecked() const { return state_.repairChecked; }
    [[nodiscard]] QString sourceSummary() const { return state_.sourceSummary; }
    [[nodiscard]] QString activeSummary() const { return state_.activeSummary; }
    [[nodiscard]] QString savedSummary() const { return state_.savedSummary; }
    [[nodiscard]] QString status() const { return state_.status; }

    Q_INVOKABLE bool openSettings();
    Q_INVOKABLE void closeSettings();
    Q_INVOKABLE bool setFlipHorizontal(bool);
    Q_INVOKABLE bool setFlipVertical(bool);
    Q_INVOKABLE bool setRotation(int);
    Q_INVOKABLE bool setConfirmation(bool);
    Q_INVOKABLE bool setRepairConsent(bool);
    Q_INVOKABLE bool save();
signals:
    void stateChanged();
private:
    bool prepareEdit();
    struct State {
        bool open{}, editable{}, saveEnabled{}, pending{}, repairVisible{};
        bool flipHorizontal{}, flipVertical{}, confirmationChecked{}, repairChecked{};
        int rotationIndex{};
        QString sourceSummary, activeSummary, savedSummary, status;
        bool operator==(const State&) const = default;
    };
    presentation::WorkstationCoordinator& coordinator_;
    std::optional<presentation::InstallationSettingsDraft> draft_;
    State state_;
    QString commandError_;
    bool closing_{};
};
}  // namespace lumora::qml
