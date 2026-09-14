#include "QmlWorkstation.hpp"
#include "CameraAdapter.hpp"
#include "ViewerAdapter.hpp"
#include "ViewerSurface.hpp"
#include "QuickImageItem.hpp"
#include "SimulatorComposition.hpp"

#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/core/Clock.hpp>
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>
#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <limits>

namespace {
using namespace lumora;
using namespace std::chrono_literals;

processing::ProcessingPreparationOptions preparation() {
    processing::ProcessingPreparationOptions options;
    options.cpuExecutionSlots = 1;
    const auto reserved = qml::reserveSimulatorRendererStorage(options);
    EXPECT_TRUE(reserved.hasValue());
    return options;
}

struct RuntimeFixture {
    QTemporaryDir directory;
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider{app::simulatorOptions(), clock};
    configuration::ConfigurationStore store{directory.filePath("startup.json").toStdString()};
    configuration::StartupPreferencesService preferences{store};
    application::LivePipeline pipeline;
    QQuickWindow window;
    qml::QmlWorkstation workstation;
    std::unique_ptr<qml::ViewerSurface> surface;

    explicit RuntimeFixture(processing::ProcessingPreparationOptions options = preparation(),
        camera::CameraConfiguration request = app::simulatorConfiguration())
        : pipeline(provider, clock, request, {}, options),
          workstation(pipeline, preferences, clock, std::move(request)),
          surface(std::make_unique<qml::ViewerSurface>()) {
        window.resize(640, 480);
        surface->setParentItem(window.contentItem());
        surface->setSize({640, 480});
        surface->setViewer(workstation.viewer());
    }
    ~RuntimeFixture() {
        workstation.requestShutdown();
        EXPECT_TRUE(wait([&] { return workstation.closed(); }));
        preferences.requestStop();
        preferences.join();
    }
    bool wait(const std::function<bool()>& predicate, bool advance = true) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        do {
            if (advance) clock.advance(10ms);
            workstation.poll();
            if (predicate()) return true;
            QTest::qWait(5);
        } while (std::chrono::steady_clock::now() < deadline);
        return predicate();
    }
    bool start(bool visible = true) {
        if (visible) {
            window.show();
            if (!QTest::qWaitForWindowExposed(&window)) return false;
        }
        if (!preferences.start().hasValue() || !pipeline.start().hasValue() ||
            !workstation.start().hasValue()) return false;
        return wait([&] {
            return workstation.coordinator().state().preferencesLoadCompleted &&
                !workstation.camera()->devices().isEmpty() && !workstation.camera()->pending();
        });
    }
    bool connect() {
        auto* camera = workstation.camera();
        return camera->selectCamera("SIM-LIVE") && camera->connectCamera() &&
            wait([&] { return camera->applyEnabled(); });
    }
    bool live() {
        auto* camera = workstation.camera();
        return start() && connect() && camera->applyConfiguration() &&
            wait([&] { return camera->confirmEnabled(); }) && camera->confirmConfiguration() &&
            wait([&] { return camera->startEnabled(); }) && camera->startLive() &&
            wait([&] { return camera->stopEnabled() && workstation.viewer()->hasFrame(); });
    }
};

TEST(QmlRuntime, StartupRequiresSelectionApplyConfirmAndExplicitStart) {
    RuntimeFixture f;
    ASSERT_TRUE(f.start());
    auto* camera = f.workstation.camera();
    EXPECT_FALSE(camera->connectCamera());
    EXPECT_FALSE(camera->applyConfiguration());
    EXPECT_FALSE(camera->confirmConfiguration());
    EXPECT_FALSE(camera->startLive());
    EXPECT_FALSE(camera->resumeLive());
    EXPECT_FALSE(f.workstation.viewer()->hasFrame());
    ASSERT_TRUE(f.connect());
    EXPECT_FALSE(camera->startLive());
    EXPECT_FALSE(camera->confirmConfiguration());
    EXPECT_EQ(camera->currentConfiguration().value("width").toUInt(), 640U);
    EXPECT_EQ(camera->currentConfiguration().value("height").toUInt(), 480U);
    ASSERT_TRUE(camera->applyConfiguration());
    ASSERT_TRUE(f.wait([&] { return camera->confirmEnabled(); }));
    EXPECT_FALSE(camera->startLive());
    ASSERT_TRUE(camera->confirmConfiguration());
    ASSERT_TRUE(f.wait([&] { return camera->startEnabled(); }));
    EXPECT_FALSE(f.workstation.viewer()->hasFrame());
    EXPECT_EQ(f.pipeline.snapshot().camera->state, application::CameraSessionState::ConnectedIdle);
    ASSERT_TRUE(camera->startLive());
    ASSERT_TRUE(f.wait([&] { return f.workstation.viewer()->hasFrame(); }));
    EXPECT_EQ(f.pipeline.snapshot().camera->state, application::CameraSessionState::Streaming);
}

TEST(QmlRuntime, UnchangedDiscoveryDoesNotResetTheCameraSelector) {
    RuntimeFixture f;
    QSignalSpy changed(f.workstation.camera(), &qml::CameraAdapter::devicesChanged);
    ASSERT_TRUE(f.start(false));
    EXPECT_EQ(changed.count(), 1);
    const auto devices = f.workstation.camera()->devices();
    for (int i = 0; i < 8; ++i) f.workstation.poll();
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(f.workstation.camera()->devices(), devices);
    EXPECT_FALSE(f.workstation.camera()->selectCamera("unrecognized-camera"));
    EXPECT_TRUE(f.workstation.camera()->selectedCameraId().isEmpty());
}

TEST(QmlRuntime, RequestedReviewPreservesPrecisionAndControlModes) {
    auto request = app::simulatorConfiguration();
    request.exposure.requestedMicroseconds = 1234.56789012345;
    request.gain.mode.reset();
    request.requestedFps.reset();
    RuntimeFixture f(preparation(), request);
    const auto description = f.workstation.camera()->requestedSummary();
    EXPECT_TRUE(description.contains("1234.56789012345")) << description.toStdString();
    EXPECT_TRUE(description.contains("Manual")) << description.toStdString();
    EXPECT_TRUE(description.contains("Automatic")) << description.toStdString();
    EXPECT_TRUE(description.contains("Unavailable")) << description.toStdString();
    EXPECT_TRUE(description.contains("Continuous")) << description.toStdString();
}

TEST(QmlRuntime, SavedSmallerRoiRequiresExplicitResumeAndRebindsItsLayout) {
    RuntimeFixture f;
    auto request = app::simulatorConfiguration();
    request.roi = {0, 0, 320, 240};
    configuration::ApplicationConfiguration saved;
    saved.cameraProfiles.lastSelectedCameraId = camera::CameraId{"SIM-LIVE"};
    saved.cameraProfiles.profiles.push_back({1, {"SIM-LIVE"},
        {"Lumora", "Generated Camera", "SIM-LIVE", "Simulator", std::nullopt},
        app::simulatorOptions().capabilities, request, request, true});
    ASSERT_TRUE(f.store.save(saved).hasValue());
    ASSERT_TRUE(f.start());
    auto* camera = f.workstation.camera();
    ASSERT_TRUE(f.wait([&] { return camera->resumeLiveEnabled(); }));
    EXPECT_EQ(f.pipeline.snapshot().camera->state, application::CameraSessionState::ConnectedIdle);
    EXPECT_FALSE(f.workstation.viewer()->hasFrame());
    EXPECT_EQ(camera->requestedConfiguration().value("width").toUInt(), 320U);
    ASSERT_TRUE(camera->resumeLive());
    ASSERT_TRUE(f.wait([&] { return f.workstation.viewer()->hasFrame(); }));
    EXPECT_EQ(camera->appliedConfiguration().value("width").toUInt(), 320U);
    EXPECT_EQ(camera->appliedConfiguration().value("height").toUInt(), 240U);
    ASSERT_TRUE(f.pipeline.snapshot().resources);
    EXPECT_FALSE(f.pipeline.snapshot().resources->customProcessorStorageUnknown);
    EXPECT_GE(f.pipeline.snapshot().resources->externalSessionBytes, 9'830'400U);
}

TEST(QmlRuntime, RendererInitializationDiagnosticAppearsBeforeFirstTicketAndClearsOnRebind) {
    RuntimeFixture f;
    ASSERT_TRUE(f.start());
    EXPECT_FALSE(f.workstation.viewer()->hasFrame());
    // Signal injection covers the real adapter connection, not a driver fault.
    f.window.sceneGraphError(QQuickWindow::ContextNotAvailable,
        QStringLiteral("fixture: initialization failed before the first ticket"));
    f.workstation.poll();
    EXPECT_FALSE(f.workstation.viewer()->error().isEmpty());
    EXPECT_EQ(f.workstation.viewer()->imageItem().metrics().consumed, 0U);
    QQuickWindow replacement;
    replacement.resize(640, 480);
    f.surface->setParentItem(replacement.contentItem());
    replacement.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&replacement));
    ASSERT_TRUE(f.wait([&] { return f.workstation.viewer()->error().isEmpty(); }));
    // Restore the fixture's window lifetime before destroying the replacement.
    f.surface->setParentItem(f.window.contentItem());
}

TEST(QmlRuntime, DefaultEngineAdmissionIncludesMaximumCompareReserve) {
    RuntimeFixture f;
    ASSERT_TRUE(f.start(false));
    ASSERT_TRUE(f.connect());
    const auto snapshot = f.pipeline.snapshot();
    ASSERT_TRUE(snapshot.resources);
    EXPECT_FALSE(snapshot.resources->customProcessorStorageUnknown);
    // 640*480 pixels * 2 panes * 4 bytes * (current + replacement + two texture sets).
    EXPECT_GE(snapshot.resources->externalSessionBytes, 9'830'400U);
    EXPECT_LE(snapshot.resources->requiredStorageBytes, snapshot.resources->storageBudgetBytes);
}

TEST(QmlRuntime, RendererReserveRejectsOverflowWithoutMutatingOptions) {
    processing::ProcessingPreparationOptions options;
    options.externalSessionStorageBytes = 123;
    ASSERT_TRUE(qml::reserveSimulatorRendererStorage(options).hasValue());
    EXPECT_EQ(options.externalSessionStorageBytes, 9'830'523U);
    options.externalSessionStorageBytes = std::numeric_limits<std::size_t>::max() - 1;
    const auto original = options.externalSessionStorageBytes;
    EXPECT_FALSE(qml::reserveSimulatorRendererStorage(options).hasValue());
    EXPECT_EQ(options.externalSessionStorageBytes, original);
    EXPECT_FALSE(qml::reserveSimulatorRendererStorage(options, 0, 480).hasValue());
    EXPECT_FALSE(qml::reserveSimulatorRendererStorage(options,
        std::numeric_limits<std::size_t>::max(), 480).hasValue());
}

TEST(QmlRuntime, DefaultEngineRejectsSessionBelowRendererReserve) {
    auto options = preparation();
    options.storageBudgetBytes = 9'830'399U;
    RuntimeFixture f(options);
    const auto started = f.pipeline.start();
    ASSERT_FALSE(started.hasValue());
    EXPECT_FALSE(f.pipeline.snapshot().context);
    EXPECT_FALSE(started.error().operatorSummary.empty());
    EXPECT_FALSE(f.workstation.start().hasValue());
    EXPECT_FALSE(f.workstation.error().isEmpty());
    EXPECT_FALSE(f.workstation.camera()->startLive());
}

TEST(QmlRuntime, ComparePauseAndResumePublishCompletedViewerState) {
    RuntimeFixture f;
    ASSERT_TRUE(f.live());
    auto* viewer = f.workstation.viewer();
    ASSERT_TRUE(f.wait([&] { return viewer->compareAvailable(); }));
    ASSERT_TRUE(viewer->setDisplayMode("compare"));
    ASSERT_TRUE(f.wait([&] { return viewer->displayMode() == "compare"; }));
    EXPECT_EQ(viewer->imageItem().metrics().textures, 2U);
    ASSERT_TRUE(viewer->pause());
    ASSERT_TRUE(f.wait([&] { return viewer->playbackState() == "Paused"; }));
    const auto frozen = viewer->sourceFrameId();
    const auto count = viewer->displayedFrameCount();
    for (int i = 0; i < 10; ++i) { f.clock.advance(40ms); f.workstation.poll(); QTest::qWait(5); }
    EXPECT_EQ(viewer->sourceFrameId(), frozen);
    EXPECT_EQ(viewer->displayedFrameCount(), count);
    ASSERT_TRUE(viewer->resume());
    ASSERT_TRUE(f.wait([&] { return viewer->sourceFrameId() != frozen; }));
}

TEST(QmlRuntime, ViewportRejectsNonFiniteInputAndUsesLogicalHostGeometry) {
    RuntimeFixture f;
    ASSERT_TRUE(f.live());
    auto* viewer = f.workstation.viewer();
    f.surface->setSize({400, 200});
    EXPECT_EQ(viewer->imageItem().size(), QSizeF(400, 200));
    EXPECT_EQ(viewer->imageItem().parent(), nullptr);
    ASSERT_TRUE(viewer->actualPixels());
    const auto before = viewer->imageItem().imageRects();
    EXPECT_FALSE(viewer->zoomAt(0, 0, 0));
    EXPECT_FALSE(viewer->zoomAt(0, 0, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(viewer->panBy(std::numeric_limits<double>::quiet_NaN(), 1));
    EXPECT_FALSE(viewer->setDisplayMode("arbitrary"));
    EXPECT_EQ(viewer->imageItem().imageRects(), before);
    EXPECT_TRUE(viewer->zoomAt(200, 100, 2));
    EXPECT_TRUE(viewer->panBy(5, 10));
}

TEST(QmlRuntime, ContextReplacementRetiresOldRendererAndRebindsExactCandidate) {
    RuntimeFixture f;
    ASSERT_TRUE(f.live());
    auto* camera = f.workstation.camera();
    auto old = f.pipeline.snapshot().context;
    ASSERT_TRUE(old);
    std::weak_ptr oldOwner = old;
    const auto raw = old->rawPool, processing = old->processingPool, display = old->displayPool;
    ASSERT_TRUE(camera->stopLive());
    ASSERT_TRUE(f.wait([&] { return camera->applyEnabled(); }));
    ASSERT_TRUE(camera->disconnectCamera());
    ASSERT_TRUE(f.wait([&] { return camera->connectEnabled(); }));
    ASSERT_TRUE(camera->connectCamera());
    ASSERT_TRUE(f.wait([&] { return camera->applyEnabled(); }));
    const auto current = f.pipeline.snapshot().context;
    ASSERT_TRUE(current);
    EXPECT_NE(current->generation, old->generation);
    old.reset();
    ASSERT_TRUE(f.wait([&] { return oldOwner.expired(); }));
    EXPECT_EQ(raw->stats().inUse, 0U);
    EXPECT_EQ(processing->stats().inUse, 0U);
    EXPECT_EQ(display->stats().inUse, 0U);
    EXPECT_FALSE(f.workstation.coordinator().pendingContextHandoff());
    EXPECT_TRUE(f.pipeline.snapshot().contextBound);
}

class QmlRuntimeShutdown : public ::testing::TestWithParam<int> {};
TEST_P(QmlRuntimeShutdown, RetirementCompletesWithEverySurfaceLifetime) {
    RuntimeFixture f;
    if (GetParam() == 3) { ASSERT_TRUE(f.start(false)); ASSERT_TRUE(f.connect()); }
    else { ASSERT_TRUE(f.live()); }
    auto context = f.pipeline.snapshot().context;
    ASSERT_TRUE(context);
    std::weak_ptr weak = context;
    const auto raw = context->rawPool, processing = context->processingPool, display = context->displayPool;
    context.reset();
    QPointer<qml::QuickImageItem> sink(&f.workstation.viewer()->imageItem());
    if (GetParam() == 1) { f.window.hide(); QTest::qWait(20); }
    if (GetParam() == 2) { f.surface.reset(); QTest::qWait(20); EXPECT_FALSE(sink.isNull()); }
    QSignalSpy completed(&f.workstation, &qml::QmlWorkstation::shutdownComplete);
    f.workstation.requestShutdown();
    EXPECT_TRUE(f.workstation.closing());
    EXPECT_FALSE(f.workstation.camera()->startLive());
    ASSERT_TRUE(f.wait([&] { return f.workstation.closed(); }));
    EXPECT_EQ(completed.count(), 1);
    f.workstation.requestShutdown();
    f.workstation.poll();
    EXPECT_EQ(completed.count(), 1);
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(raw->stats().inUse, 0U);
    EXPECT_EQ(processing->stats().inUse, 0U);
    EXPECT_EQ(display->stats().inUse, 0U);
    EXPECT_EQ(sink->metrics().textures, 0U);
    EXPECT_EQ(sink->metrics().conversionImages, 0U);
}
INSTANTIATE_TEST_SUITE_P(VisibleHiddenDeletedNeverShown, QmlRuntimeShutdown,
    ::testing::Values(0, 1, 2, 3));
}
