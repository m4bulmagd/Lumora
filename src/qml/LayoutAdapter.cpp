#include "LayoutAdapter.hpp"

#include <lumora/configuration/StartupPreferencesService.hpp>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QStringList>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace lumora::qml {
namespace {

constexpr int SaveDelayMilliseconds = 200;
constexpr std::int64_t MinimumVisibleTitleWidth = 160;
constexpr std::int64_t MinimumVisibleTitleHeight = 32;
constexpr std::int64_t MinimumVisibleBodyHeight = 120;

std::optional<QRect> usableGeometry(const application::WindowGeometry& geometry) {
    if (geometry.width <= 0 || geometry.height <= 0) return std::nullopt;
    const auto left = static_cast<std::int64_t>(geometry.x);
    const auto top = static_cast<std::int64_t>(geometry.y);
    const auto width = static_cast<std::int64_t>(geometry.width);
    const auto height = static_cast<std::int64_t>(geometry.height);
    const auto right = left + width;
    const auto bottom = top + height;
    if (right <= left || bottom <= top
        || right - 1 > std::numeric_limits<int>::max()
        || right - 1 < std::numeric_limits<int>::min()
        || bottom - 1 > std::numeric_limits<int>::max()
        || bottom - 1 < std::numeric_limits<int>::min()) return std::nullopt;

    // A restorable window must leave enough of its top strip for drag/exit
    // access and enough total height to expose useful content. Horizontal
    // coordinates remain signed, so reachable windows on left-hand monitors
    // and slightly overhanging windows are preserved without clamping.
    const auto requiredTitleWidth = std::min(MinimumVisibleTitleWidth, width);
    const auto requiredTitleHeight = std::min(MinimumVisibleTitleHeight, height);
    const auto requiredBodyHeight = std::min(MinimumVisibleBodyHeight, height);
    for (const auto* screen : QGuiApplication::screens()) {
        const auto available = screen->availableGeometry();
        const auto screenLeft = static_cast<std::int64_t>(available.x());
        const auto screenTop = static_cast<std::int64_t>(available.y());
        const auto screenRight = screenLeft + available.width();
        const auto screenBottom = screenTop + available.height();
        const auto visibleWidth = std::max<std::int64_t>(0,
            std::min(right, screenRight) - std::max(left, screenLeft));
        const auto titleBottom = std::min(bottom, top + MinimumVisibleTitleHeight);
        const auto visibleTitleHeight = std::max<std::int64_t>(0,
            std::min(titleBottom, screenBottom) - std::max(top, screenTop));
        const auto visibleBodyHeight = std::max<std::int64_t>(0,
            std::min(bottom, screenBottom) - std::max(top, screenTop));
        if (visibleWidth >= requiredTitleWidth
            && visibleTitleHeight >= requiredTitleHeight
            && visibleBodyHeight >= requiredBodyHeight) {
            return QRect(geometry.x, geometry.y, geometry.width, geometry.height);
        }
    }
    return std::nullopt;
}

QRect fallbackGeometry(QQuickWindow* window) {
    auto* screen = window ? window->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return {0, 0, 1280, 800};
    const auto available = screen->availableGeometry();
    const QSize size{std::min(1280, available.width()),
        std::min(800, available.height())};
    return QRect(QPoint{available.x() + (available.width() - size.width()) / 2,
                     available.y() + (available.height() - size.height()) / 2}, size);
}

application::WindowGeometry storedGeometry(const QRect& geometry) {
    return {geometry.x(), geometry.y(), geometry.width(), geometry.height()};
}

QString errorText(const std::optional<core::Error>& error) {
    return error ? QString::fromStdString(error->operatorSummary) : QString{};
}

}  // namespace

LayoutAdapter::LayoutAdapter(
    configuration::StartupPreferencesService& preferences, QObject* parent)
    : QObject(parent), preferences_(preferences) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(SaveDelayMilliseconds);
    connect(&saveTimer_, &QTimer::timeout, this, [this] { submitSave(); });
}

void LayoutAdapter::attachWindow(QQuickWindow* window) {
    if (window_ == window) return;
    if (window_) window_->removeEventFilter(this);
    window_ = window;
    windowBaselineEstablished_ = window_ && window_->isExposed();
    if (window_) window_->installEventFilter(this);
    refresh();
    if (loadApplied_ && window_) applyWindowState();
}

void LayoutAdapter::setPanelsCollapsed(bool collapsed) {
    if (current_.panelsCollapsed == collapsed) return;
    emit layoutChanging();
    current_.panelsCollapsed = collapsed;
    panelsDirty_ = true;
    emit stateChanged();
    scheduleSave();
}

void LayoutAdapter::toggleFullscreen() {
    if (current_.fullscreen) {
        exitFullscreen();
        return;
    }
    if (!window_) return;
    captureOrdinaryGeometry();
    const auto states = window_->windowStates();
    if (!states.testFlag(Qt::WindowMinimized)) {
        current_.maximized = states.testFlag(Qt::WindowMaximized);
    }
    emit layoutChanging();
    current_.fullscreen = true;
    fullscreenDirty_ = true;
    applyingWindow_ = true;
    window_->setWindowStates(Qt::WindowFullScreen);
    applyingWindow_ = false;
    emit stateChanged();
    scheduleSave();
}

void LayoutAdapter::exitFullscreen() {
    if (!current_.fullscreen || !window_) return;
    emit layoutChanging();
    current_.fullscreen = false;
    fullscreenDirty_ = true;
    applyingWindow_ = true;
    window_->setWindowStates(current_.maximized ? Qt::WindowMaximized : Qt::WindowNoState);
    if (!current_.maximized && current_.normalGeometry) {
        if (const auto geometry = usableGeometry(*current_.normalGeometry)) {
            window_->setGeometry(*geometry);
        }
    }
    applyingWindow_ = false;
    emit stateChanged();
    scheduleSave();
}

void LayoutAdapter::setDiagnosticsVisible(bool visible) {
    if (current_.diagnosticsVisible == visible) return;
    emit layoutChanging();
    current_.diagnosticsVisible = visible;
    diagnosticsDirty_ = true;
    emit stateChanged();
    scheduleSave();
}

void LayoutAdapter::applyLoadedPreferences(
    const application::UiPreferences& preferences) {
    const bool presentationChanges = (!panelsDirty_
            && current_.panelsCollapsed != preferences.panelsCollapsed)
        || (!diagnosticsDirty_ && current_.diagnosticsVisible != preferences.diagnosticsVisible)
        || (!fullscreenDirty_ && current_.fullscreen != preferences.fullscreen);
    if (presentationChanges) emit layoutChanging();
    if (!panelsDirty_) current_.panelsCollapsed = preferences.panelsCollapsed;
    if (!diagnosticsDirty_) current_.diagnosticsVisible = preferences.diagnosticsVisible;
    if (!fullscreenDirty_) {
        current_.fullscreen = preferences.fullscreen;
        current_.maximized = preferences.maximized;
    }
    if (!geometryDirty_) {
        if (preferences.normalGeometry) {
            if (const auto geometry = usableGeometry(*preferences.normalGeometry)) {
                current_.normalGeometry = storedGeometry(*geometry);
            } else {
                current_.normalGeometry = storedGeometry(fallbackGeometry(window_.data()));
                localWarning_ = tr("The saved window position was outside the usable screen area; a centered layout was restored.");
            }
        } else {
            current_.normalGeometry = storedGeometry(fallbackGeometry(window_.data()));
        }
    }
    applyWindowState();
    ready_ = true;
    loadApplied_ = true;
    emit stateChanged();
    if (savePending_) scheduleSave();
}

void LayoutAdapter::applyWindowState() {
    if (!window_) return;
    applyingWindow_ = true;
    if (current_.normalGeometry) {
        if (const auto geometry = usableGeometry(*current_.normalGeometry)) {
            window_->setGeometry(*geometry);
        }
    }
    if (current_.fullscreen) window_->setWindowStates(Qt::WindowFullScreen);
    else if (current_.maximized) window_->setWindowStates(Qt::WindowMaximized);
    else window_->setWindowStates(Qt::WindowNoState);
    applyingWindow_ = false;
}

void LayoutAdapter::refresh() {
    const auto status = preferences_.latestStatus();
    const auto observedRevision = std::max(
        status->latestAttemptedUiSaveRevision.value_or(0U),
        status->latestSavedUiRevision.value_or(0U));
    if (observedRevision >= nextRevision_) nextRevision_ = observedRevision + 1U;
    if (!loadApplied_ && status->loadCompleted && window_) {
        applyLoadedPreferences(status->loadedUiPreferences.value_or(
            application::UiPreferences{}));
    }
    updateWarning();
}

void LayoutAdapter::captureOrdinaryGeometry() {
    if (!window_) return;
    const auto states = window_->windowStates();
    if (states.testFlag(Qt::WindowFullScreen) || states.testFlag(Qt::WindowMaximized)
        || states.testFlag(Qt::WindowMinimized)) return;
    const auto geometry = window_->geometry();
    if (geometry.width() <= 0 || geometry.height() <= 0) return;
    const auto next = storedGeometry(geometry);
    if (current_.normalGeometry == next) return;
    current_.normalGeometry = next;
    geometryDirty_ = true;
    savePending_ = true;
}

void LayoutAdapter::scheduleSave() {
    savePending_ = true;
    if (ready_) saveTimer_.start();
}

void LayoutAdapter::submitSave(bool beforeLoad) {
    if (!savePending_ || (!ready_ && !beforeLoad)) return;
    if (ready_ || geometryDirty_) captureOrdinaryGeometry();
    application::UiPreferencesUpdate update;
    if (geometryDirty_) update.normalGeometry = current_.normalGeometry;
    if (panelsDirty_) update.panelsCollapsed = current_.panelsCollapsed;
    if (fullscreenDirty_) {
        update.maximized = current_.maximized;
        update.fullscreen = current_.fullscreen;
    }
    if (diagnosticsDirty_) update.diagnosticsVisible = current_.diagnosticsVisible;
    if (!update.normalGeometry && !update.panelsCollapsed && !update.maximized
        && !update.fullscreen && !update.diagnosticsVisible) {
        savePending_ = false;
        return;
    }
    const auto revision = nextRevision_++;
    const auto result = preferences_.postUiUpdate(revision, std::move(update));
    savePending_ = false;
    if (result.hasValue()) {
        commandWarning_.clear();
        geometryDirty_ = false;
        panelsDirty_ = false;
        fullscreenDirty_ = false;
        diagnosticsDirty_ = false;
    } else {
        commandWarning_ = QString::fromStdString(result.error().operatorSummary);
    }
    updateWarning();
}

void LayoutAdapter::prepareShutdown() {
    saveTimer_.stop();
    if (ready_ || geometryDirty_) captureOrdinaryGeometry();
    submitSave(true);
}

void LayoutAdapter::updateWarning() {
    QStringList messages;
    if (!commandWarning_.isEmpty()) messages.append(commandWarning_);
    const auto serviceWarning = errorText(preferences_.latestStatus()->uiWarning);
    if (!serviceWarning.isEmpty() && !messages.contains(serviceWarning)) {
        messages.append(serviceWarning);
    }
    if (!localWarning_.isEmpty() && !messages.contains(localWarning_)) {
        messages.append(localWarning_);
    }
    const auto next = messages.join(QLatin1Char(' '));
    if (warning_ == next) return;
    warning_ = next;
    emit stateChanged();
}

bool LayoutAdapter::eventFilter(QObject* watched, QEvent* event) {
    if (watched != window_.data()) return QObject::eventFilter(watched, event);
    if (event->type() == QEvent::Expose && window_ && window_->isExposed()) {
        windowBaselineEstablished_ = true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_F11) {
            if (!key->isAutoRepeat()) toggleFullscreen();
            key->accept();
            return true;
        }
        if (key->key() == Qt::Key_Escape && current_.fullscreen) {
            if (!key->isAutoRepeat()) exitFullscreen();
            key->accept();
            return true;
        }
    }
    const bool nativeLayoutEvent = event->type() == QEvent::Move
        || event->type() == QEvent::Resize || event->type() == QEvent::WindowStateChange;
    if (!windowBaselineEstablished_ && nativeLayoutEvent) {
        return QObject::eventFilter(watched, event);
    }
    if (!applyingWindow_) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            captureOrdinaryGeometry();
            if (geometryDirty_) scheduleSave();
        } else if (event->type() == QEvent::WindowStateChange && window_) {
            const auto states = window_->windowStates();
            if (!states.testFlag(Qt::WindowMinimized)) {
                const bool nextFullscreen = states.testFlag(Qt::WindowFullScreen);
                const bool nextMaximized = states.testFlag(Qt::WindowMaximized);
                if (current_.fullscreen != nextFullscreen) emit layoutChanging();
                const bool changed = current_.fullscreen != nextFullscreen
                    || (!nextFullscreen && current_.maximized != nextMaximized);
                if (changed) {
                    current_.fullscreen = nextFullscreen;
                    // Fullscreen replaces the native maximized flag. Keep the
                    // preceding ordinary state so Exit can restore it.
                    if (!nextFullscreen) current_.maximized = nextMaximized;
                    fullscreenDirty_ = true;
                    if (!nextFullscreen && !nextMaximized) captureOrdinaryGeometry();
                    emit stateChanged();
                    scheduleSave();
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace lumora::qml
