#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/WorkstationController.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QCoreApplication>
#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>

namespace {
using namespace lumora;
using Intent = ui::CameraStartupIntent;

camera::CameraConfiguration preparedRequest() {
    return {{"Mono8", 0x01080001U, 8, 255, core::SourcePacking::Unpacked,
                core::BitAlignment::LeastSignificant, core::StorageType::UInt8},
        {0, 0, 8, 6}, 30.0, {camera::ExposureMode::Manual, 100.0},
        {camera::GainMode::Manual, 0.0}, camera::AcquisitionMode::Continuous};
}

camera::sim::SimulatedCameraOptions fixedControls() {
    camera::CameraCapabilities capabilities;
    capabilities.pixelFormats = {preparedRequest().pixelFormat};
    capabilities.roi = {{0, 0, 1, 1}, {0, 0, 8, 6}, {1, 1, 1, 1}};
    capabilities.frameRate = {1, 60, 1, camera::ControlAccess::ReadOnly};
    capabilities.exposure = {1, 1000, 1, camera::ControlAccess::WritableStopped};
    capabilities.exposureModes = {camera::ExposureMode::Manual};
    capabilities.exposureModeAccess = camera::ControlAccess::ReadOnly;
    capabilities.gain = {0, 0, 0, camera::ControlAccess::Unavailable};
    capabilities.gainModeAccess = camera::ControlAccess::Unavailable;
    return {{"SIM-CONTROLS"}, capabilities, camera::sim::SimulationPattern::Ramp,
        27.0, 123, camera::sim::SimulationPacingMode::Manual};
}

class Memory final : public configuration::IStartupPreferencesIo {
public:
    core::Result<configuration::ApplicationConfiguration> load() override {
        return core::Result<configuration::ApplicationConfiguration>::success({});
    }
    core::Result<void> save(const configuration::ApplicationConfiguration&) override {
        return core::Result<void>::success();
    }
};

struct Harness final {
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider{fixedControls(), clock};
    application::LivePipeline pipeline{provider, clock, preparedRequest()};
    configuration::StartupPreferencesService preferences{std::make_unique<Memory>()};
    ui::WorkstationView view;
    ui::CameraStartupPanel panel;
    ui::WorkstationController controller{pipeline, preferences, view, panel, clock, preparedRequest()};
    ~Harness() { controller.shutdown(); preferences.requestStop(); preferences.join(); }
    bool wait(const std::function<bool()>& condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do {
            controller.poll();
            QCoreApplication::processEvents();
            if (condition()) return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    bool act(Intent intent) {
        return controller.dispatch(intent).hasValue()
            && wait([&] { return !panel.presentation().ordinaryOperationPending; });
    }
    bool connect() {
        if (!preferences.start().hasValue() || !pipeline.start().hasValue()
            || !controller.start().hasValue()) return false;
        if (!wait([&] {
                const auto& presentation = panel.presentation();
                return presentation.preferencesLoadCompleted && presentation.cameraStatus
                    && !presentation.cameraStatus->discoveredDescriptors.empty()
                    && !presentation.ordinaryOperationPending;
            })) return false;
        controller.selectCamera({"SIM-CONTROLS"});
        return act(Intent::Connect);
    }
};

TEST(CameraControls, FirstApplyRetainsActualFixedValuesAndStillRequiresConfirmation) {
    Harness harness;
    ASSERT_TRUE(harness.connect());
    auto camera = harness.pipeline.snapshot().camera;
    ASSERT_TRUE(camera);
    ASSERT_TRUE(camera->currentConfiguration);
    EXPECT_EQ(camera->currentConfiguration->requestedFps, 27.0);
    EXPECT_FALSE(camera->appliedConfiguration);
    EXPECT_FALSE(camera->confirmedRevision);
    EXPECT_FALSE(harness.controller.dispatch(Intent::Start).hasValue());
    ASSERT_TRUE(harness.act(Intent::Apply));
    camera = harness.pipeline.snapshot().camera;
    ASSERT_TRUE(camera->appliedConfiguration);
    EXPECT_EQ(camera->appliedConfiguration->requested.requestedFps, 27.0);
    EXPECT_EQ(camera->appliedConfiguration->actual.requestedFps, 27.0);
    EXPECT_EQ(camera->appliedConfiguration->actual.exposure.requestedMicroseconds, 100.0);
    EXPECT_FALSE(camera->appliedConfiguration->actual.gain.mode);
    EXPECT_FALSE(camera->appliedConfiguration->actual.gain.requestedDb);
    EXPECT_FALSE(camera->confirmedRevision);
    EXPECT_FALSE(harness.controller.dispatch(Intent::Start).hasValue());
    ASSERT_TRUE(harness.act(Intent::Confirm));
    ASSERT_TRUE(harness.act(Intent::Start));
    EXPECT_EQ(harness.pipeline.snapshot().camera->state, application::CameraSessionState::Streaming);
    ASSERT_TRUE(harness.act(Intent::Stop));
}

TEST(CameraControls, WritableEditPreservesFixedValuesAndForgedFixedEditIsRejected) {
    Harness harness;
    ASSERT_TRUE(harness.connect());
    ASSERT_TRUE(harness.act(Intent::Apply));
    auto camera = harness.pipeline.snapshot().camera;
    ASSERT_TRUE(camera->appliedConfiguration);
    auto request = camera->appliedConfiguration->actual;
    request.exposure.requestedMicroseconds = 250;
    ASSERT_TRUE(harness.controller.applyCameraSettings(
        camera->sessionGeneration, {"SIM-CONTROLS"}, request).hasValue());
    ASSERT_TRUE(harness.wait([&] { return !harness.panel.presentation().ordinaryOperationPending; }));
    camera = harness.pipeline.snapshot().camera;
    EXPECT_EQ(camera->appliedConfiguration->actual.requestedFps, 27.0);
    EXPECT_EQ(camera->appliedConfiguration->actual.exposure.requestedMicroseconds, 250.0);
    const auto revision = camera->appliedRevision;
    request.requestedFps = 42;
    EXPECT_FALSE(harness.controller.applyCameraSettings(
        camera->sessionGeneration, {"SIM-CONTROLS"}, request).hasValue());
    EXPECT_EQ(harness.pipeline.snapshot().camera->appliedRevision, revision);
}
}  // namespace
