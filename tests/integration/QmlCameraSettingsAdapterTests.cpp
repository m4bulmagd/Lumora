#include "CameraSettingsAdapter.hpp"
#include "SimulatorComposition.hpp"

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/application/InstallationProfiles.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/presentation/CameraActionPolicy.hpp>
#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <QTemporaryDir>
#include <QVariantMap>
#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace {
using namespace lumora;
using namespace std::chrono_literals;

class InstallationProfiles final : public application::IInstallationProfiles {
public:
    InstallationProfiles() {
        state_.loadCompleted = true;
        state_.administratorMode = true;
        state_.policy = application::InstallationProfilePolicy::SimulatorIdentityFallback;
    }
    std::shared_ptr<const application::InstallationProfilesSnapshot> latestStatus() const override {
        std::lock_guard lock(mutex_);
        return std::make_shared<const application::InstallationProfilesSnapshot>(state_);
    }
    core::Result<void> postSave(
        std::uint64_t, application::InstallationCameraProfile, bool) override {
        return core::Result<void>::success();
    }
    void setPending(bool pending) {
        std::lock_guard lock(mutex_);
        state_.savePending = pending;
    }
private:
    mutable std::mutex mutex_;
    application::InstallationProfilesSnapshot state_;
};

struct Fixture final {
    QTemporaryDir directory;
    core::ManualClock clock;
    camera::sim::SimulatedCameraOptions options;
    camera::CameraConfiguration request;
    camera::sim::SimulatedCameraProvider provider;
    configuration::ConfigurationStore store;
    configuration::StartupPreferencesService preferences;
    application::LivePipeline pipeline;
    presentation::WorkstationCoordinator coordinator;
    qml::CameraSettingsAdapter adapter;

    explicit Fixture(camera::sim::SimulatedCameraOptions source = app::simulatorOptions(),
        camera::CameraConfiguration fixed = app::simulatorConfiguration(),
        application::IInstallationProfiles* installations = nullptr)
        : options(std::move(source)), request(std::move(fixed)),
          provider(options, clock),
          store(directory.filePath("camera-settings.json").toStdString()),
          preferences(store), pipeline(provider, clock, request, {}, {}, installations),
          coordinator(pipeline, preferences, clock, request), adapter(coordinator) {}

    ~Fixture() {
        coordinator.beginShutdown();
        coordinator.completeRendererShutdown();
        preferences.requestStop();
        preferences.join();
    }

    bool wait(const std::function<bool()>& ready) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        do {
            clock.advance(10ms);
            coordinator.poll();
            if (const auto handoff = coordinator.pendingContextHandoff()) {
                if (!coordinator.completeContextHandoff(handoff->id).hasValue()) return false;
            }
            adapter.refresh();
            if (ready()) return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return ready();
    }

    bool startStopped() {
        if (!directory.isValid() || !preferences.start().hasValue()
            || !pipeline.start().hasValue() || !coordinator.start().hasValue()) return false;
        if (!wait([&] {
                const auto& state = coordinator.state();
                return state.preferencesLoadCompleted && state.cameraStatus
                    && !state.cameraStatus->discoveredDescriptors.empty()
                    && !state.ordinaryOperationPending;
            })) return false;
        coordinator.selectCamera(options.id);
        if (!coordinator.dispatch(presentation::CameraStartupIntent::Connect).hasValue()) return false;
        return wait([&] {
            const auto& state = coordinator.state();
            return state.cameraStatus
                && state.cameraStatus->state == application::CameraSessionState::ConnectedIdle
                && state.cameraStatus->currentConfiguration && state.cameraStatus->capabilities
                && !state.ordinaryOperationPending;
        });
    }

    bool startStreaming() {
        if (!startStopped()) return false;
        if (!coordinator.dispatch(presentation::CameraStartupIntent::Apply).hasValue()) return false;
        if (!wait([&] {
                return presentation::CameraActionPolicy::evaluate(coordinator.state()).confirm.enabled;
            })) return false;
        if (!coordinator.dispatch(presentation::CameraStartupIntent::Confirm).hasValue()) return false;
        if (!wait([&] {
                return presentation::CameraActionPolicy::evaluate(coordinator.state()).start.enabled;
            })) return false;
        if (!coordinator.dispatch(presentation::CameraStartupIntent::Start).hasValue()) return false;
        return wait([&] {
            return coordinator.state().cameraStatus
                && coordinator.state().cameraStatus->state
                    == application::CameraSessionState::Streaming;
        });
    }
};

QVariantMap modeRow(const QVariantList& rows, int value) {
    for (const auto& row : rows) {
        const auto map = row.toMap();
        if (map.value(QStringLiteral("value")).toInt() == value) return map;
    }
    return {};
}

// Opening and typing are local editor operations. Even incomplete text remains
// visible, blocks Apply and is discarded by closing without changing the whole
// requested camera configuration or preferences.
TEST(QmlCameraSettingsAdapter, OpenAndInvalidVerbatimEditRemainLocalUntilClosed) {
    Fixture fixture;
    EXPECT_FALSE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.wait([&] {
        const auto status = fixture.preferences.latestStatus();
        return status->latestSavedRevision.has_value()
            && status->latestSavedRevision == status->latestAttemptedSaveRevision;
    }));
    const auto before = fixture.coordinator.state().requestedConfiguration;
    ASSERT_TRUE(before);
    const auto saved = fixture.preferences.latestStatus()->latestSavedRevision;
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.isOpen());
    EXPECT_TRUE(fixture.adapter.editable());
    EXPECT_TRUE(fixture.adapter.applyEnabled());
    EXPECT_EQ(fixture.adapter.exposureMode(), static_cast<int>(camera::ExposureMode::Manual));
    EXPECT_EQ(fixture.adapter.gainMode(), static_cast<int>(camera::GainMode::Manual));
    EXPECT_EQ(fixture.adapter.exposureText(), QStringLiteral("1000"));
    EXPECT_EQ(fixture.adapter.gainText(), QStringLiteral("0"));
    EXPECT_FALSE(modeRow(fixture.adapter.exposureModes(),
        static_cast<int>(camera::ExposureMode::Manual)).isEmpty());
    EXPECT_FALSE(modeRow(fixture.adapter.gainModes(),
        static_cast<int>(camera::GainMode::Manual)).isEmpty());
    EXPECT_TRUE(fixture.adapter.sourceSummary().contains(QStringLiteral("SIM-LIVE")));
    EXPECT_FALSE(fixture.adapter.currentSummary().isEmpty());
    EXPECT_TRUE(fixture.adapter.exposureRange().contains(QStringLiteral("10000")));
    EXPECT_TRUE(fixture.adapter.gainRange().contains(QStringLiteral("10")));

    ASSERT_TRUE(fixture.adapter.editExposureText(QStringLiteral("2345e")));
    EXPECT_EQ(fixture.adapter.exposureText(), QStringLiteral("2345e"));
    EXPECT_FALSE(fixture.adapter.applyEnabled());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_EQ(fixture.adapter.exposureText(), QStringLiteral("2345e"));
    EXPECT_FALSE(fixture.adapter.applyEnabled());
    EXPECT_FALSE(fixture.adapter.apply());
    for (const auto* rejected : {"nan", "inf", "0", "10001"}) {
        ASSERT_TRUE(fixture.adapter.editExposureText(QString::fromLatin1(rejected)));
        EXPECT_EQ(fixture.adapter.exposureText(), QString::fromLatin1(rejected));
        EXPECT_FALSE(fixture.adapter.applyEnabled());
        EXPECT_FALSE(fixture.adapter.apply());
    }
    EXPECT_TRUE(application::cameraConfigurationsEqual(
        *fixture.coordinator.state().requestedConfiguration, *before));
    EXPECT_EQ(fixture.preferences.latestStatus()->latestSavedRevision, saved);

    fixture.adapter.closeSettings();
    EXPECT_FALSE(fixture.adapter.isOpen());
    EXPECT_TRUE(application::cameraConfigurationsEqual(
        *fixture.coordinator.state().requestedConfiguration, *before));
    EXPECT_EQ(fixture.preferences.latestStatus()->latestSavedRevision, saved);
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_EQ(fixture.adapter.exposureText(), QStringLiteral("1000"));
}

// Exact fractional requests are legal despite integer simulator increments.
// Apply submits the complete request while actual readback reflects register
// rounding; Apply alone never confirms or persists the camera profile.
TEST(QmlCameraSettingsAdapter, ExactFractionalApplyPreservesWholeRequestAndActualReadback) {
    Fixture fixture;
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.wait([&] {
        return fixture.preferences.latestStatus()->latestSavedRevision.has_value();
    }));
    const auto before = fixture.coordinator.state().requestedConfiguration;
    ASSERT_TRUE(before);
    const auto saved = fixture.preferences.latestStatus()->latestSavedRevision;
    const auto initialRevision = fixture.coordinator.state().cameraStatus->requestedRevision;
    ASSERT_TRUE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.adapter.editExposureText(QStringLiteral("2345.678912345678")));
    ASSERT_TRUE(fixture.adapter.editGainText(QStringLiteral("3.456789123456789")));
    EXPECT_EQ(fixture.adapter.exposureText(), QStringLiteral("2345.678912345678"));
    EXPECT_EQ(fixture.adapter.gainText(), QStringLiteral("3.456789123456789"));
    ASSERT_TRUE(fixture.adapter.apply());
    EXPECT_FALSE(fixture.adapter.applyEnabled());
    EXPECT_FALSE(fixture.adapter.editGainText(QStringLiteral("4")));
    ASSERT_TRUE(fixture.wait([&] {
        const auto camera = fixture.coordinator.state().cameraStatus;
        return camera && !fixture.coordinator.state().ordinaryOperationPending
            && camera->appliedRevision > initialRevision
            && camera->appliedRevision == camera->requestedRevision;
    }));

    const auto camera = fixture.coordinator.state().cameraStatus;
    ASSERT_TRUE(camera->requestedConfiguration);
    ASSERT_TRUE(camera->currentConfiguration);
    auto expected = *before;
    expected.exposure.requestedMicroseconds = 2345.678912345678;
    expected.gain.requestedDb = 3.456789123456789;
    EXPECT_TRUE(application::cameraConfigurationsEqual(*camera->requestedConfiguration, expected));
    EXPECT_TRUE(application::cameraConfigurationsEqual(
        camera->appliedConfiguration->requested, expected));
    EXPECT_DOUBLE_EQ(*camera->requestedConfiguration->exposure.requestedMicroseconds,
        2345.678912345678);
    EXPECT_DOUBLE_EQ(*camera->requestedConfiguration->gain.requestedDb,
        3.456789123456789);
    EXPECT_DOUBLE_EQ(*camera->currentConfiguration->exposure.requestedMicroseconds, 2346.0);
    EXPECT_DOUBLE_EQ(*camera->currentConfiguration->gain.requestedDb, 3.0);
    EXPECT_TRUE(fixture.adapter.currentSummary().contains(QStringLiteral("2346")));
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("Review"),
        Qt::CaseInsensitive));
    EXPECT_EQ(fixture.preferences.latestStatus()->latestSavedRevision, saved);
    EXPECT_FALSE(camera->confirmedRevision
        && *camera->confirmedRevision == camera->appliedRevision);
}

// Automatic modes are submitted as a complete request with no numeric value.
// The mode change must preserve every fixed acquisition field and Apply alone
// must still avoid confirmation and preference persistence.
TEST(QmlCameraSettingsAdapter, AutomaticApplyClearsNumericRequestsAndPreservesWholeConfiguration) {
    auto options = app::simulatorOptions();
    options.capabilities.exposureModes.push_back(camera::ExposureMode::Auto);
    options.capabilities.gainModes.push_back(camera::GainMode::Auto);
    Fixture fixture(std::move(options));
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.wait([&] {
        const auto status = fixture.preferences.latestStatus();
        return status->latestSavedRevision.has_value()
            && status->latestSavedRevision == status->latestAttemptedSaveRevision;
    }));
    const auto before = fixture.coordinator.state().requestedConfiguration;
    ASSERT_TRUE(before);
    const auto saved = fixture.preferences.latestStatus()->latestSavedRevision;
    const auto revision = fixture.coordinator.state().cameraStatus->requestedRevision;
    ASSERT_TRUE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.adapter.setExposureMode(static_cast<int>(camera::ExposureMode::Auto)));
    ASSERT_TRUE(fixture.adapter.setGainMode(static_cast<int>(camera::GainMode::Auto)));
    EXPECT_TRUE(fixture.adapter.exposureText().isEmpty());
    EXPECT_TRUE(fixture.adapter.gainText().isEmpty());
    ASSERT_TRUE(fixture.adapter.apply());
    ASSERT_TRUE(fixture.wait([&] {
        const auto camera = fixture.coordinator.state().cameraStatus;
        return camera && !fixture.coordinator.state().ordinaryOperationPending
            && camera->appliedRevision > revision
            && camera->appliedRevision == camera->requestedRevision;
    }));
    const auto camera = fixture.coordinator.state().cameraStatus;
    ASSERT_TRUE(camera->requestedConfiguration);
    ASSERT_TRUE(camera->currentConfiguration);
    auto expected = *before;
    expected.exposure.mode = camera::ExposureMode::Auto;
    expected.exposure.requestedMicroseconds.reset();
    expected.gain.mode = camera::GainMode::Auto;
    expected.gain.requestedDb.reset();
    EXPECT_TRUE(application::cameraConfigurationsEqual(*camera->requestedConfiguration, expected));
    EXPECT_TRUE(application::cameraConfigurationsEqual(
        camera->appliedConfiguration->requested, expected));
    EXPECT_FALSE(camera->requestedConfiguration->exposure.requestedMicroseconds);
    EXPECT_FALSE(camera->requestedConfiguration->gain.requestedDb);
    EXPECT_FALSE(camera->currentConfiguration->exposure.requestedMicroseconds);
    EXPECT_FALSE(camera->currentConfiguration->gain.requestedDb);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestSavedRevision, saved);
    EXPECT_FALSE(camera->confirmedRevision
        && *camera->confirmedRevision == camera->appliedRevision);
}

// Auto clears only the outgoing request. Switching back restores the retained
// writable manual text, while a read-only numeric field is sourced from current
// camera readback rather than an editor cache.
TEST(QmlCameraSettingsAdapter, ModeChangesRetainWritableManualCacheAndUseReadbackForReadOnly) {
    auto options = app::simulatorOptions();
    options.capabilities.exposureModes.push_back(camera::ExposureMode::Auto);
    options.capabilities.gainModes.push_back(camera::GainMode::Auto);
    Fixture fixture(std::move(options));
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.gainValueEnabled());
    ASSERT_TRUE(fixture.adapter.editGainText(QStringLiteral("4.125")));
    ASSERT_TRUE(fixture.adapter.setGainMode(static_cast<int>(camera::GainMode::Auto)));
    EXPECT_EQ(fixture.adapter.gainMode(), static_cast<int>(camera::GainMode::Auto));
    EXPECT_TRUE(fixture.adapter.gainText().isEmpty());
    EXPECT_FALSE(fixture.adapter.gainValueEnabled());
    ASSERT_TRUE(fixture.adapter.setGainMode(static_cast<int>(camera::GainMode::Manual)));
    EXPECT_EQ(fixture.adapter.gainText(), QStringLiteral("4.125"));
    EXPECT_TRUE(fixture.adapter.gainValueEnabled());

    auto automaticRequest = app::simulatorConfiguration();
    automaticRequest.exposure.mode = camera::ExposureMode::Auto;
    automaticRequest.exposure.requestedMicroseconds.reset();
    automaticRequest.gain.mode = camera::GainMode::Auto;
    automaticRequest.gain.requestedDb.reset();
    auto automaticOptions = app::simulatorOptions();
    automaticOptions.capabilities.exposureModes.push_back(camera::ExposureMode::Auto);
    automaticOptions.capabilities.gainModes.push_back(camera::GainMode::Auto);
    Fixture automatic(std::move(automaticOptions), automaticRequest);
    ASSERT_TRUE(automatic.startStopped());
    ASSERT_TRUE(automatic.adapter.openSettings());
    EXPECT_TRUE(automatic.adapter.exposureText().isEmpty());
    EXPECT_TRUE(automatic.adapter.gainText().isEmpty());
    ASSERT_TRUE(automatic.adapter.setExposureMode(static_cast<int>(camera::ExposureMode::Manual)));
    ASSERT_TRUE(automatic.adapter.setGainMode(static_cast<int>(camera::GainMode::Manual)));
    EXPECT_EQ(automatic.adapter.exposureText(), QStringLiteral("1"));
    EXPECT_EQ(automatic.adapter.gainText(), QStringLiteral("0"));

    auto readOnlyOptions = app::simulatorOptions();
    readOnlyOptions.capabilities.exposure.access = camera::ControlAccess::ReadOnly;
    Fixture readOnly(std::move(readOnlyOptions));
    ASSERT_TRUE(readOnly.startStopped());
    ASSERT_TRUE(readOnly.adapter.openSettings());
    EXPECT_EQ(readOnly.adapter.exposureText(), QStringLiteral("1"));
    EXPECT_FALSE(readOnly.adapter.exposureValueEnabled());
    EXPECT_TRUE(readOnly.adapter.exposureReason().contains(QStringLiteral("read-only"),
        Qt::CaseInsensitive));

    auto fixedModeOptions = app::simulatorOptions();
    fixedModeOptions.capabilities.exposureModeAccess = camera::ControlAccess::ReadOnly;
    fixedModeOptions.capabilities.gainModeAccess = camera::ControlAccess::ReadOnly;
    Fixture fixedMode(std::move(fixedModeOptions));
    ASSERT_TRUE(fixedMode.startStopped());
    ASSERT_TRUE(fixedMode.adapter.openSettings());
    EXPECT_FALSE(fixedMode.adapter.exposureModeEnabled());
    EXPECT_FALSE(fixedMode.adapter.gainModeEnabled());
    EXPECT_TRUE(fixedMode.adapter.exposureValueEnabled());
    EXPECT_TRUE(fixedMode.adapter.gainValueEnabled());
    EXPECT_TRUE(fixedMode.adapter.exposureReason().contains(QStringLiteral("mode is read-only"),
        Qt::CaseInsensitive));
    EXPECT_TRUE(fixedMode.adapter.gainReason().contains(QStringLiteral("mode is read-only"),
        Qt::CaseInsensitive));

    auto unavailableOptions = app::simulatorOptions();
    unavailableOptions.capabilities.exposureModes.clear();
    unavailableOptions.capabilities.gainModes.clear();
    unavailableOptions.capabilities.exposureModeAccess = camera::ControlAccess::Unavailable;
    unavailableOptions.capabilities.gainModeAccess = camera::ControlAccess::Unavailable;
    unavailableOptions.capabilities.exposure = {0.0, 0.0, 0.0,
        camera::ControlAccess::Unavailable};
    unavailableOptions.capabilities.gain = {0.0, 0.0, 0.0,
        camera::ControlAccess::Unavailable};
    auto unavailableRequest = app::simulatorConfiguration();
    unavailableRequest.exposure = {};
    unavailableRequest.gain = {};
    Fixture unavailable(std::move(unavailableOptions), unavailableRequest);
    ASSERT_TRUE(unavailable.startStopped());
    ASSERT_TRUE(unavailable.adapter.openSettings());
    EXPECT_EQ(unavailable.adapter.exposureMode(), -1);
    EXPECT_EQ(unavailable.adapter.gainMode(), -1);
    EXPECT_FALSE(unavailable.adapter.exposureModeEnabled());
    EXPECT_FALSE(unavailable.adapter.gainModeEnabled());
    EXPECT_FALSE(unavailable.adapter.exposureValueEnabled());
    EXPECT_FALSE(unavailable.adapter.gainValueEnabled());
    EXPECT_TRUE(unavailable.adapter.exposureRange().isEmpty());
    EXPECT_TRUE(unavailable.adapter.gainRange().isEmpty());
    EXPECT_TRUE(unavailable.adapter.exposureReason().contains(QStringLiteral("unavailable"),
        Qt::CaseInsensitive));
    EXPECT_TRUE(unavailable.adapter.gainReason().contains(QStringLiteral("unavailable"),
        Qt::CaseInsensitive));
}

// If an Auto readback has no actual measurement and the numeric control is
// read-only, returning to Manual must not invent a writable cached value. The
// disabled field stays empty and explains that its required readback is absent.
TEST(QmlCameraSettingsAdapter, ReadOnlyAutoToManualKeepsAbsentMeasurementAndBlocksApply) {
    auto options = app::simulatorOptions();
    options.capabilities.exposureModes.push_back(camera::ExposureMode::Auto);
    options.capabilities.exposure.access = camera::ControlAccess::ReadOnly;
    Fixture fixture(std::move(options));
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_FALSE(fixture.adapter.exposureValueEnabled());
    EXPECT_TRUE(fixture.adapter.exposureText().isEmpty());
    EXPECT_FALSE(fixture.adapter.applyEnabled());
    ASSERT_TRUE(fixture.adapter.setExposureMode(static_cast<int>(camera::ExposureMode::Auto)));
    ASSERT_TRUE(fixture.adapter.setExposureMode(static_cast<int>(camera::ExposureMode::Manual)));
    EXPECT_EQ(fixture.adapter.exposureMode(), static_cast<int>(camera::ExposureMode::Manual));
    EXPECT_TRUE(fixture.adapter.exposureText().isEmpty());
    EXPECT_FALSE(fixture.adapter.exposureValueEnabled());
    EXPECT_FALSE(fixture.adapter.applyEnabled());
    EXPECT_FALSE(fixture.adapter.exposureReason().contains(QStringLiteral("Enter"),
        Qt::CaseInsensitive));
    EXPECT_TRUE(fixture.adapter.exposureReason().contains(QStringLiteral("readback"),
        Qt::CaseInsensitive));
    EXPECT_TRUE(fixture.adapter.exposureReason().contains(QStringLiteral("unavailable"),
        Qt::CaseInsensitive));
    EXPECT_FALSE(fixture.adapter.apply());
}

// A streaming source remains inspectable but never editable. Every command
// rechecks current coordinator authority, and shutdown closes the editor and
// prevents reopening it.
TEST(QmlCameraSettingsAdapter, StreamingSessionDriftAndClosingRejectFreshCommands) {
    Fixture fixture;
    ASSERT_TRUE(fixture.startStreaming());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.isOpen());
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_FALSE(fixture.adapter.applyEnabled());
    EXPECT_FALSE(fixture.adapter.editExposureText(QStringLiteral("2000")));
    EXPECT_FALSE(fixture.adapter.setGainMode(static_cast<int>(camera::GainMode::Manual)));
    EXPECT_FALSE(fixture.adapter.apply());
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("Stop"), Qt::CaseInsensitive));
    EXPECT_FALSE(fixture.adapter.currentSummary().isEmpty());

    ASSERT_TRUE(fixture.coordinator.dispatch(presentation::CameraStartupIntent::Stop).hasValue());
    ASSERT_TRUE(fixture.wait([&] {
        const auto camera = fixture.coordinator.state().cameraStatus;
        return camera && camera->state == application::CameraSessionState::ConnectedIdle
            && !fixture.coordinator.state().ordinaryOperationPending;
    }));
    EXPECT_TRUE(fixture.adapter.editable());
    EXPECT_FALSE(fixture.adapter.status().contains(QStringLiteral("Stop"), Qt::CaseInsensitive));
    fixture.adapter.closeSettings();
    ASSERT_TRUE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.coordinator.dispatch(presentation::CameraStartupIntent::Start).hasValue());
    ASSERT_TRUE(fixture.wait([&] {
        const auto camera = fixture.coordinator.state().cameraStatus;
        return camera && camera->state == application::CameraSessionState::Streaming;
    }));
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("Stop"), Qt::CaseInsensitive));

    ASSERT_TRUE(fixture.coordinator.dispatch(presentation::CameraStartupIntent::Disconnect).hasValue());
    ASSERT_TRUE(fixture.wait([&] {
        const auto camera = fixture.coordinator.state().cameraStatus;
        return camera && camera->state == application::CameraSessionState::Disconnected;
    }));
    EXPECT_TRUE(fixture.adapter.isOpen());
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_TRUE(fixture.adapter.currentSummary().isEmpty());
    fixture.adapter.closeSettings();
    EXPECT_FALSE(fixture.adapter.openSettings());

    fixture.adapter.setClosing();
    EXPECT_FALSE(fixture.adapter.isOpen());
    EXPECT_FALSE(fixture.adapter.openSettings());
    EXPECT_FALSE(fixture.adapter.editGainText(QStringLiteral("2")));
    EXPECT_FALSE(fixture.adapter.apply());
}

// Installation profile work is surfaced by the shared camera action policy.
// The dialog remains inspectable, while direct edits and Apply are rejected
// until that independent operation is no longer pending.
TEST(QmlCameraSettingsAdapter, InstallationPendingDisablesEditingAndApply) {
    InstallationProfiles installations;
    Fixture fixture(app::simulatorOptions(), app::simulatorConfiguration(), &installations);
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.editable());
    installations.setPending(true);
    ASSERT_TRUE(fixture.wait([&] {
        return fixture.coordinator.state().installationProfilePending;
    }));
    EXPECT_TRUE(fixture.adapter.isOpen());
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_FALSE(fixture.adapter.applyEnabled());
    EXPECT_FALSE(fixture.adapter.editExposureText(QStringLiteral("2000")));
    EXPECT_FALSE(fixture.adapter.apply());
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("installation"),
        Qt::CaseInsensitive));
}

} // namespace
