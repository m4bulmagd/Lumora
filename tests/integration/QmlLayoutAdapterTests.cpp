#include "LayoutAdapter.hpp"

#include <lumora/application/UiPreferences.hpp>
#include <lumora/configuration/ApplicationConfiguration.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/configuration/UiPreferencesCodec.hpp>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QtTest/QTest>
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace lumora;
using namespace std::chrono_literals;

configuration::ApplicationConfiguration documentWith(
    const application::UiPreferences& preferences) {
    configuration::ApplicationConfiguration document;
    const auto merged = configuration::UiPreferencesCodec::merge({}, preferences);
    EXPECT_TRUE(merged.hasValue());
    if (merged.hasValue()) document.ui = merged.value();
    return document;
}

class ControlledPreferencesIo final : public configuration::IStartupPreferencesIo {
public:
    explicit ControlledPreferencesIo(configuration::ApplicationConfiguration loaded)
        : loaded_(std::move(loaded)) {}

    core::Result<configuration::ApplicationConfiguration> load() override {
        std::unique_lock lock(mutex_);
        loadEntered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return !holdLoad_; });
        return core::Result<configuration::ApplicationConfiguration>::success(loaded_);
    }

    core::Result<void> save(
        const configuration::ApplicationConfiguration& configuration) override {
        std::unique_lock lock(mutex_);
        saveEntered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return !holdSave_; });
        if (std::exchange(failNextSave_, false)) {
            return core::Result<void>::failure({core::ErrorCategory::Storage,
                "injected_ui_save_failure", "UI preferences could not be saved.", {}, true});
        }
        saved_.push_back(configuration);
        return core::Result<void>::success();
    }

    void holdLoad() {
        std::lock_guard lock(mutex_);
        holdLoad_ = true;
        loadEntered_ = false;
    }
    void releaseLoad() {
        std::lock_guard lock(mutex_);
        holdLoad_ = false;
        changed_.notify_all();
    }
    void holdSave() {
        std::lock_guard lock(mutex_);
        holdSave_ = true;
        saveEntered_ = false;
    }
    void releaseSave() {
        std::lock_guard lock(mutex_);
        holdSave_ = false;
        changed_.notify_all();
    }
    void failNextSave() {
        std::lock_guard lock(mutex_);
        failNextSave_ = true;
    }
    bool waitForLoad() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 5s, [&] { return loadEntered_; });
    }
    bool waitForSave() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 5s, [&] { return saveEntered_; });
    }
    std::optional<configuration::ApplicationConfiguration> lastSaved() const {
        std::lock_guard lock(mutex_);
        if (saved_.empty()) return std::nullopt;
        return saved_.back();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    configuration::ApplicationConfiguration loaded_;
    std::vector<configuration::ApplicationConfiguration> saved_;
    bool holdLoad_{};
    bool loadEntered_{};
    bool holdSave_{};
    bool saveEntered_{};
    bool failNextSave_{};
};

struct Fixture final {
    ControlledPreferencesIo* io;
    configuration::StartupPreferencesService preferences;
    qml::LayoutAdapter adapter;
    QQuickWindow window;
    bool started{};

    explicit Fixture(application::UiPreferences loaded = {})
        : io(new ControlledPreferencesIo(documentWith(loaded))),
          preferences(std::unique_ptr<configuration::IStartupPreferencesIo>(io)),
          adapter(preferences) {
        window.resize(640, 480);
    }

    ~Fixture() {
        adapter.prepareShutdown();
        io->releaseLoad();
        io->releaseSave();
        if (started) {
            preferences.requestStop();
            preferences.join();
        }
        window.close();
    }

    bool start(bool show = false) {
        if (!preferences.start().hasValue()) return false;
        started = true;
        adapter.attachWindow(&window);
        if (show) {
            window.show();
            if (!QTest::qWaitForWindowExposed(&window)) return false;
        }
        adapter.refresh();
        return true;
    }

    bool wait(const std::function<bool()>& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        do {
            adapter.refresh();
            if (predicate()) return true;
            QTest::qWait(5);
        } while (std::chrono::steady_clock::now() < deadline);
        adapter.refresh();
        return predicate();
    }

    void stopAndJoin() {
        if (!started) return;
        preferences.requestStop();
        io->releaseLoad();
        io->releaseSave();
        preferences.join();
        started = false;
    }

    std::optional<application::UiPreferences> lastSavedUi() const {
        const auto saved = io->lastSaved();
        if (!saved) return std::nullopt;
        return configuration::UiPreferencesCodec::decode(saved->ui).preferences;
    }
};

QRect fallbackGeometry(const QRect& available) {
    const QSize size{std::min(1280, available.width()),
        std::min(800, available.height())};
    return QRect(QPoint{available.x() + (available.width() - size.width()) / 2,
                     available.y() + (available.height() - size.height()) / 2}, size);
}

TEST(QmlLayoutAdapter, DelayedLoadPreservesLocalPanelEditAndRestoresUntouchedFields) {
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    const application::WindowGeometry loadedGeometry{available.x() + 40, available.y() + 30,
        std::min(700, available.width()), std::min(500, available.height())};
    Fixture fixture({loadedGeometry, false, false, false, true});
    fixture.io->holdLoad();
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.io->waitForLoad());
    EXPECT_FALSE(fixture.adapter.ready());

    QSignalSpy transitions(&fixture.adapter, &qml::LayoutAdapter::layoutChanging);
    fixture.adapter.setPanelsCollapsed(true);
    EXPECT_TRUE(fixture.adapter.panelsCollapsed());
    EXPECT_EQ(transitions.count(), 1);

    fixture.io->releaseLoad();
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
    EXPECT_TRUE(fixture.adapter.panelsCollapsed());
    EXPECT_TRUE(fixture.adapter.diagnosticsVisible());
    EXPECT_EQ(fixture.window.geometry(),
        QRect(loadedGeometry.x, loadedGeometry.y, loadedGeometry.width, loadedGeometry.height));
}

TEST(QmlLayoutAdapter, ShutdownDuringLoadWithoutEditsDoesNotReplaceStoredLayout) {
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    const application::WindowGeometry loadedGeometry{available.x() + 50, available.y() + 40,
        std::min(720, available.width()), std::min(520, available.height())};
    Fixture fixture({loadedGeometry, true, true, true, true});
    fixture.io->holdLoad();
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.io->waitForLoad());
    fixture.adapter.prepareShutdown();
    fixture.stopAndJoin();
    EXPECT_FALSE(fixture.io->lastSaved());
}

TEST(QmlLayoutAdapter, InitialMappingDuringLoadIsNotTreatedAsAUserGeometryEdit) {
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    const application::WindowGeometry loadedGeometry{available.x() + 55, available.y() + 35,
        std::min(710, available.width()), std::min(510, available.height())};
    Fixture fixture({loadedGeometry, true, false, false, true});
    fixture.io->holdLoad();
    ASSERT_TRUE(fixture.start(true));
    ASSERT_TRUE(fixture.io->waitForLoad());
    fixture.adapter.prepareShutdown();
    fixture.stopAndJoin();
    EXPECT_FALSE(fixture.io->lastSaved());
}

TEST(QmlLayoutAdapter, ShutdownDuringLoadMergesOnlyLocalEditsIntoStoredLayout) {
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    const application::WindowGeometry loadedGeometry{available.x() + 60, available.y() + 45,
        std::min(730, available.width()), std::min(530, available.height())};
    const application::UiPreferences loaded{
        loadedGeometry, false, true, false, true};
    Fixture fixture(loaded);
    fixture.io->holdLoad();
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.io->waitForLoad());
    fixture.adapter.setPanelsCollapsed(true);
    fixture.adapter.prepareShutdown();
    fixture.stopAndJoin();
    const auto saved = fixture.lastSavedUi();
    ASSERT_TRUE(saved);
    EXPECT_EQ(saved->normalGeometry, loaded.normalGeometry);
    EXPECT_TRUE(saved->panelsCollapsed);
    EXPECT_EQ(saved->maximized, loaded.maximized);
    EXPECT_EQ(saved->fullscreen, loaded.fullscreen);
    EXPECT_EQ(saved->diagnosticsVisible, loaded.diagnosticsVisible);
}

TEST(QmlLayoutAdapter, NativeGeometryAndMaximizeDuringLoadTakePrecedenceOverStoredState) {
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    const application::WindowGeometry loadedGeometry{available.x() + 5, available.y() + 5,
        std::min(700, available.width()), std::min(500, available.height())};
    Fixture fixture({loadedGeometry, true, false, false, true});
    fixture.io->holdLoad();
    ASSERT_TRUE(fixture.start(true));
    ASSERT_TRUE(fixture.io->waitForLoad());
    const QRect localGeometry{available.x() + 35, available.y() + 30,
        std::min(620, available.width()), std::min(460, available.height())};
    QSignalSpy widthChanges(&fixture.window, &QWindow::widthChanged);
    QSignalSpy xChanges(&fixture.window, &QWindow::xChanged);
    fixture.window.setGeometry(localGeometry);
    ASSERT_TRUE(fixture.wait([&] {
        return widthChanges.count() > 0 && xChanges.count() > 0
            && fixture.window.geometry() == localGeometry;
    }));
    fixture.window.setWindowStates(Qt::WindowMaximized);
    ASSERT_TRUE(fixture.wait([&] {
        if (!fixture.window.windowStates().testFlag(Qt::WindowMaximized)) return false;
        if (QGuiApplication::platformName() != QStringLiteral("xcb")) return true;
        return fixture.window.width() > localGeometry.width()
            || fixture.window.height() > localGeometry.height();
    }));

    fixture.io->releaseLoad();
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
    EXPECT_TRUE(fixture.window.windowStates().testFlag(Qt::WindowMaximized));
    fixture.adapter.toggleFullscreen();
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.fullscreen(); }));
    fixture.adapter.exitFullscreen();
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.fullscreen(); }));
    EXPECT_TRUE(fixture.window.windowStates().testFlag(Qt::WindowMaximized));
    fixture.window.setWindowStates(Qt::WindowNoState);
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.window.windowStates().testFlag(Qt::WindowMaximized)
            && fixture.window.geometry() == localGeometry;
    }));
}

TEST(QmlLayoutAdapter, ReachablePartlyNegativeGeometryIsRetainedAndDetachedOrOverflowingGeometryFallsBack) {
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    const int width = std::min(640, available.width());
    const int height = std::min(480, available.height());
    const application::WindowGeometry partlyNegative{
        available.x() - std::min(20, width / 4), available.y() + 10, width, height};
    {
        Fixture fixture({partlyNegative, false, false, false, false});
        ASSERT_TRUE(fixture.start());
        ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
        EXPECT_EQ(fixture.window.geometry(), QRect(partlyNegative.x, partlyNegative.y,
            partlyNegative.width, partlyNegative.height));
    }
    {
        const application::WindowGeometry offscreen{
            available.right() + 10000, available.bottom() + 10000, width, height};
        Fixture fixture({offscreen, false, false, false, false});
        ASSERT_TRUE(fixture.start());
        ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
        EXPECT_EQ(fixture.window.geometry(), fallbackGeometry(available));
        EXPECT_FALSE(fixture.adapter.warning().isEmpty());
    }
    {
        const application::WindowGeometry overflowing{
            std::numeric_limits<int>::max() - 10, available.y(), 100, height};
        Fixture fixture({overflowing, false, false, false, false});
        ASSERT_TRUE(fixture.start());
        ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
        EXPECT_EQ(fixture.window.geometry(), fallbackGeometry(available));
        EXPECT_FALSE(fixture.adapter.warning().isEmpty());
    }
}

TEST(QmlLayoutAdapter, LayoutChangingPrecedesPanelAndActualFullscreenTransitions) {
    Fixture fixture({std::nullopt, false, true, false, false});
    ASSERT_TRUE(fixture.start(true));
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
    ASSERT_TRUE(fixture.window.windowStates().testFlag(Qt::WindowMaximized));

    bool sawOldPanelState = false;
    bool sawWindowBeforeFullscreen = false;
    const auto connection = QObject::connect(&fixture.adapter, &qml::LayoutAdapter::layoutChanging,
        &fixture.adapter, [&] {
            if (!fixture.adapter.panelsCollapsed()) sawOldPanelState = true;
            if (!fixture.adapter.fullscreen()
                && !fixture.window.windowStates().testFlag(Qt::WindowFullScreen)) {
                sawWindowBeforeFullscreen = true;
            }
        });
    fixture.adapter.setPanelsCollapsed(true);
    EXPECT_TRUE(sawOldPanelState);
    EXPECT_TRUE(fixture.adapter.panelsCollapsed());

    sawWindowBeforeFullscreen = false;
    QTest::keyClick(&fixture.window, Qt::Key_F11);
    ASSERT_TRUE(fixture.wait([&] {
        return fixture.adapter.fullscreen()
            && fixture.window.windowStates().testFlag(Qt::WindowFullScreen);
    }));
    EXPECT_TRUE(sawWindowBeforeFullscreen);
    QTest::keyClick(&fixture.window, Qt::Key_Escape);
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.fullscreen(); }));
    EXPECT_TRUE(fixture.window.windowStates().testFlag(Qt::WindowMaximized));
    QObject::disconnect(connection);
}

TEST(QmlLayoutAdapter, OrdinaryGeometryIsDebouncedAndShutdownFlushesNewestCompleteLayout) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start(true));
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
    const auto available = fixture.window.screen()->availableGeometry();
    const QRect first{available.x() + 10, available.y() + 20,
        std::min(600, available.width()), std::min(440, available.height())};
    const QRect newest{available.x() + 25, available.y() + 35,
        std::min(620, available.width()), std::min(460, available.height())};
    fixture.window.setGeometry(first);
    QCoreApplication::processEvents();
    fixture.window.setGeometry(newest);
    QCoreApplication::processEvents();
    EXPECT_FALSE(fixture.preferences.latestStatus()->latestAttemptedUiSaveRevision);
    ASSERT_TRUE(fixture.wait([&] {
        const auto status = fixture.preferences.latestStatus();
        return status->latestSavedUiRevision
            && status->latestSavedUiRevision == status->latestAttemptedUiSaveRevision;
    }));
    auto saved = fixture.lastSavedUi();
    ASSERT_TRUE(saved && saved->normalGeometry);
    EXPECT_EQ(*saved->normalGeometry,
        (application::WindowGeometry{newest.x(), newest.y(), newest.width(), newest.height()}));

    fixture.adapter.setDiagnosticsVisible(true);
    fixture.adapter.prepareShutdown();
    ASSERT_TRUE(fixture.wait([&] {
        const auto latest = fixture.lastSavedUi();
        return latest && latest->diagnosticsVisible;
    }));
    saved = fixture.lastSavedUi();
    ASSERT_TRUE(saved && saved->normalGeometry);
    EXPECT_EQ(*saved->normalGeometry,
        (application::WindowGeometry{newest.x(), newest.y(), newest.width(), newest.height()}));
}

TEST(QmlLayoutAdapter, FullscreenBoundsNeverReplaceOrdinaryGeometry) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start(true));
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
    const auto available = fixture.window.screen()->availableGeometry();
    const QRect ordinary{available.x() + 30, available.y() + 25,
        std::min(650, available.width()), std::min(470, available.height())};
    fixture.window.setGeometry(ordinary);
    QCoreApplication::processEvents();
    fixture.adapter.toggleFullscreen();
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.fullscreen(); }));
    fixture.adapter.prepareShutdown();
    ASSERT_TRUE(fixture.wait([&] {
        const auto saved = fixture.lastSavedUi();
        return saved && saved->fullscreen;
    }));
    const auto saved = fixture.lastSavedUi();
    ASSERT_TRUE(saved && saved->normalGeometry);
    EXPECT_EQ(*saved->normalGeometry,
        (application::WindowGeometry{ordinary.x(), ordinary.y(), ordinary.width(), ordinary.height()}));
}

TEST(QmlLayoutAdapter, AdmissionAndDurabilityFailuresRemainVisible) {
    {
        Fixture fixture;
        ASSERT_TRUE(fixture.start());
        ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
        fixture.preferences.requestStop();
        fixture.adapter.setPanelsCollapsed(true);
        fixture.adapter.prepareShutdown();
        EXPECT_FALSE(fixture.adapter.warning().isEmpty());
    }
    {
        Fixture fixture;
        fixture.io->failNextSave();
        ASSERT_TRUE(fixture.start());
        ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.ready(); }));
        fixture.adapter.setDiagnosticsVisible(true);
        fixture.adapter.prepareShutdown();
        ASSERT_TRUE(fixture.wait([&] {
            const auto status = fixture.preferences.latestStatus();
            return status->latestAttemptedUiSaveRevision && status->uiWarning;
        }));
        EXPECT_FALSE(fixture.adapter.warning().isEmpty());
        EXPECT_FALSE(fixture.preferences.latestStatus()->latestSavedUiRevision);
    }
}

}  // namespace
