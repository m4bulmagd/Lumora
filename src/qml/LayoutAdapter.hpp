#pragma once

#include <lumora/application/UiPreferences.hpp>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <cstdint>

namespace lumora::configuration { class StartupPreferencesService; }

namespace lumora::qml {

class LayoutAdapter final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The workstation owns window layout")
    Q_PROPERTY(bool panelsCollapsed READ panelsCollapsed NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool diagnosticsVisible READ diagnosticsVisible NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString warning READ warning NOTIFY stateChanged FINAL)

public:
    explicit LayoutAdapter(
        configuration::StartupPreferencesService& preferences, QObject* parent = nullptr);

    [[nodiscard]] bool panelsCollapsed() const noexcept { return current_.panelsCollapsed; }
    [[nodiscard]] bool fullscreen() const noexcept { return current_.fullscreen; }
    [[nodiscard]] bool diagnosticsVisible() const noexcept { return current_.diagnosticsVisible; }
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] QString warning() const { return warning_; }

    Q_INVOKABLE void attachWindow(QQuickWindow* window);
    Q_INVOKABLE void setPanelsCollapsed(bool collapsed);
    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void exitFullscreen();
    Q_INVOKABLE void setDiagnosticsVisible(bool visible);
    void refresh();
    void prepareShutdown();

signals:
    void stateChanged();
    void layoutChanging();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyLoadedPreferences(const application::UiPreferences& preferences);
    void applyWindowState();
    void captureOrdinaryGeometry();
    void scheduleSave();
    void submitSave(bool beforeLoad = false);
    void updateWarning();

    configuration::StartupPreferencesService& preferences_;
    QPointer<QQuickWindow> window_;
    QTimer saveTimer_;
    application::UiPreferences current_;
    std::uint64_t nextRevision_{1U};
    bool loadApplied_{};
    bool panelsDirty_{};
    bool fullscreenDirty_{};
    bool diagnosticsDirty_{};
    bool geometryDirty_{};
    bool savePending_{};
    bool applyingWindow_{};
    bool windowBaselineEstablished_{};
    bool ready_{};
    QString localWarning_;
    QString commandWarning_;
    QString warning_;
};

}  // namespace lumora::qml
