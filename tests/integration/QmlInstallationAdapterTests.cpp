#include "InstallationAdapter.hpp"
#include "SimulatorComposition.hpp"

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/InstallationProfilesService.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/presentation/CameraActionPolicy.hpp>
#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace {
using namespace lumora;
using namespace std::chrono_literals;
using Intent = presentation::CameraStartupIntent;

// Real temporary-file store and asynchronous service, with a controlled write
// gate/failure for deterministic pending/outcome assertions. No production path,
// environment override, privilege acquisition or machine-permission claim.
class ControlledIo final : public configuration::IInstallationProfilesIo {
public:
    explicit ControlledIo(std::filesystem::path path) : store(std::move(path), true) {}
    configuration::InstallationProfileStore store;
    core::Result<std::vector<application::InstallationCameraProfile>> load() override {
        return store.load();
    }
    core::Result<application::InstallationCameraProfile> save(
        application::InstallationCameraProfile profile, bool repair) override {
        bool fail;
        {
            std::unique_lock lock(mutex_);
            entered_ = true;
            changed_.wait(lock, [&] { return !hold_; });
            fail = std::exchange(failNext_, false);
        }
        if (fail) return core::Result<application::InstallationCameraProfile>::failure(
            {core::ErrorCategory::Storage, "injected_write_failure", "Installation write failed", "", true});
        return store.save(std::move(profile), repair);
    }
    void hold() { std::lock_guard lock(mutex_); hold_ = true; entered_ = false; }
    void release() { std::lock_guard lock(mutex_); hold_ = false; changed_.notify_all(); }
    bool entered() const { std::lock_guard lock(mutex_); return entered_; }
    void failNext() { std::lock_guard lock(mutex_); failNext_ = true; }
private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool hold_{false}, entered_{false}, failNext_{false};
};

struct Fixture final {
    QTemporaryDir directory;
    core::ManualClock clock;
    camera::sim::SimulatedCameraOptions options{app::simulatorOptions()};
    camera::sim::SimulatedCameraProvider provider{options, clock};
    ControlledIo* io;
    configuration::InstallationProfilesService installations;
    configuration::StartupPreferencesService preferences;
    application::LivePipeline pipeline;
    presentation::WorkstationCoordinator coordinator;
    qml::InstallationAdapter adapter;

    explicit Fixture(bool administrator = true)
        : io(new ControlledIo(directory.filePath("installation.json").toStdString())),
          installations(std::unique_ptr<configuration::IInstallationProfilesIo>(io), administrator,
              application::InstallationProfilePolicy::SimulatorIdentityFallback),
          preferences(configuration::ConfigurationStore{directory.filePath("preferences.json").toStdString()}),
          pipeline(provider, clock, app::simulatorConfiguration(), {}, {}, &installations),
          coordinator(pipeline, preferences, clock, app::simulatorConfiguration()), adapter(coordinator) {}
    ~Fixture() {
        io->release();
        coordinator.beginShutdown();
        coordinator.completeRendererShutdown();
        preferences.requestStop();
        preferences.join();
        installations.requestStop();
        installations.join();
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
        if (!directory.isValid() || !installations.start().hasValue() || !preferences.start().hasValue()
            || !pipeline.start().hasValue() || !coordinator.start().hasValue()) return false;
        if (!wait([&] {
                const auto& state = coordinator.state();
                return state.preferencesLoadCompleted && state.installationProfiles
                    && state.installationProfiles->loadCompleted && state.cameraStatus
                    && !state.cameraStatus->discoveredDescriptors.empty() && !state.ordinaryOperationPending;
            })) return false;
        coordinator.selectCamera(options.id);
        return act(Intent::Connect);
    }
    bool act(Intent intent) {
        if (!coordinator.dispatch(intent).hasValue()) return false;
        return wait([&] { return !coordinator.state().ordinaryOperationPending; });
    }
    QByteArray machineBytes(const QString& suffix = {}) const {
        QFile file(directory.filePath("installation.json") + suffix);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
};

TEST(QmlInstallationAdapter, OperatorInspectsWithoutEditingOrSaving) {
    Fixture fixture(false);
    EXPECT_FALSE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.isOpen());
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_FALSE(fixture.adapter.saveEnabled());
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("Read-only"), Qt::CaseInsensitive));
    EXPECT_FALSE(fixture.adapter.sourceSummary().isEmpty());
    EXPECT_FALSE(fixture.adapter.setFlipHorizontal(true));
    EXPECT_FALSE(fixture.adapter.setRotation(1));
    EXPECT_FALSE(fixture.adapter.setConfirmation(true));
    EXPECT_FALSE(fixture.adapter.save());
    EXPECT_TRUE(fixture.machineBytes().isEmpty());
    fixture.adapter.closeSettings();
    EXPECT_FALSE(fixture.adapter.isOpen());
}

TEST(QmlInstallationAdapter, LocalDraftRequiresConfirmationAndDurableSaveDoesNotActivate) {
    Fixture fixture;
    application::InstallationCameraProfile other;
    other.identity = {"Lumora", "Other", "OTHER", "test", {}};
    other.capabilities = fixture.options.capabilities;
    other.revision = 1;
    other.confirmed = true;
    other.orientation = {false, true, core::Rotation::Degrees180};
    ASSERT_TRUE(fixture.io->store.save(other).hasValue());
    const auto beforeBytes = fixture.machineBytes();
    const auto beforeProfiles = QJsonDocument::fromJson(beforeBytes).object().value("profiles").toArray();
    ASSERT_EQ(beforeProfiles.size(), 1);
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.act(Intent::Apply));
    ASSERT_TRUE(fixture.act(Intent::Confirm));
    const auto context = fixture.pipeline.snapshot().context;
    const auto active = fixture.coordinator.state().activeOrientation;
    const auto request = fixture.coordinator.state().requestedConfiguration;
    ASSERT_TRUE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.adapter.setFlipHorizontal(true));
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    ASSERT_TRUE(fixture.adapter.setRotation(1));
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.flipHorizontal());
    EXPECT_EQ(fixture.adapter.rotationIndex(), 1);
    EXPECT_FALSE(fixture.adapter.setRotation(-1));
    EXPECT_FALSE(fixture.adapter.setRotation(4));
    EXPECT_EQ(fixture.adapter.rotationIndex(), 1);
    EXPECT_EQ(fixture.machineBytes(), beforeBytes);
    EXPECT_EQ(fixture.coordinator.state().activeOrientation, active);
    EXPECT_FALSE(fixture.adapter.save());
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    fixture.io->hold();
    ASSERT_TRUE(fixture.adapter.save());
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    EXPECT_FALSE(fixture.adapter.save());
    EXPECT_FALSE(fixture.adapter.setFlipVertical(true));
    ASSERT_TRUE(fixture.wait([&] { return fixture.io->entered(); }));
    EXPECT_EQ(fixture.machineBytes(), beforeBytes);
    EXPECT_EQ(fixture.pipeline.snapshot().context, context);
    fixture.io->release();
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && fixture.coordinator.state().installationProfileOutcome
            && fixture.coordinator.state().installationProfileOutcome->savedProfile.has_value();
    }));
    const auto saved = *fixture.coordinator.state().installationProfileOutcome->savedProfile;
    EXPECT_EQ(saved.orientation, (core::Orientation{true, false, core::Rotation::Degrees90}));
    EXPECT_EQ(saved.revision, 1U);
    EXPECT_TRUE(saved.confirmed);
    EXPECT_TRUE(application::cameraIdentityKeysEqual(saved.identity,
        fixture.coordinator.state().cameraStatus->discoveredDescriptors.front().identity));
    EXPECT_TRUE(application::cameraCapabilitiesEqual(saved.capabilities, fixture.options.capabilities));
    const auto afterProfiles = QJsonDocument::fromJson(fixture.machineBytes()).object().value("profiles").toArray();
    ASSERT_EQ(afterProfiles.size(), 2);
    EXPECT_TRUE(afterProfiles.contains(beforeProfiles.at(0)));
    EXPECT_EQ(fixture.coordinator.state().activeOrientation, active);
    EXPECT_EQ(fixture.pipeline.snapshot().context, context);
    EXPECT_TRUE(application::cameraConfigurationsEqual(*fixture.coordinator.state().requestedConfiguration, *request));
    EXPECT_FALSE(fixture.coordinator.dispatch(Intent::Confirm).hasValue());
    EXPECT_FALSE(fixture.coordinator.dispatch(Intent::Start).hasValue());
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("does not activate"), Qt::CaseInsensitive));
    ASSERT_TRUE(fixture.act(Intent::Apply));
    EXPECT_NE(fixture.pipeline.snapshot().context, context);
    EXPECT_EQ(fixture.coordinator.state().activeOrientation, std::optional{saved.orientation});
    EXPECT_FALSE(fixture.coordinator.dispatch(Intent::Start).hasValue());
    ASSERT_TRUE(fixture.act(Intent::Confirm));
    ASSERT_TRUE(fixture.act(Intent::Start));
    EXPECT_EQ(fixture.coordinator.state().cameraStatus->state, application::CameraSessionState::Streaming);
}

TEST(QmlInstallationAdapter, InvalidFileNeedsSeparateRepairConsentAndPreservesOriginal) {
    Fixture fixture;
    {
        QFile file(fixture.directory.filePath("installation.json"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write("broken installation"), 19);
    }
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.repairVisible());
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    EXPECT_FALSE(fixture.adapter.save());
    EXPECT_EQ(fixture.machineBytes(), QByteArray("broken installation"));
    ASSERT_TRUE(fixture.adapter.setRepairConsent(true));
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    ASSERT_TRUE(fixture.adapter.save());
    ASSERT_TRUE(fixture.wait([&] {
        const auto& outcome = fixture.coordinator.state().installationProfileOutcome;
        return !fixture.adapter.pending() && outcome && outcome->savedProfile.has_value();
    }));
    EXPECT_EQ(fixture.machineBytes(QStringLiteral(".invalid-backup")), QByteArray("broken installation"));
    EXPECT_TRUE(fixture.io->store.load().hasValue());
    EXPECT_FALSE(fixture.adapter.repairVisible());
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
}

TEST(QmlInstallationAdapter, AsynchronousFailureReleasesPendingAndRequiresReconfirmation) {
    Fixture fixture;
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.adapter.setFlipVertical(true));
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    fixture.io->failNext();
    ASSERT_TRUE(fixture.adapter.save());
    ASSERT_TRUE(fixture.wait([&] {
        const auto& outcome = fixture.coordinator.state().installationProfileOutcome;
        return !fixture.adapter.pending() && outcome && outcome->error.has_value();
    }));
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    EXPECT_FALSE(fixture.adapter.save());
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("failed"), Qt::CaseInsensitive));
    EXPECT_TRUE(fixture.machineBytes().isEmpty());
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    ASSERT_TRUE(fixture.adapter.save());
    ASSERT_TRUE(fixture.wait([&] {
        const auto& outcome = fixture.coordinator.state().installationProfileOutcome;
        return !fixture.adapter.pending() && outcome && outcome->savedProfile.has_value();
    }));
}

TEST(QmlInstallationAdapter, ReopenedPendingSaveInitializesFromCompletedSavedOrientation) {
    Fixture fixture;
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.adapter.setFlipHorizontal(true));
    ASSERT_TRUE(fixture.adapter.setRotation(1));
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    fixture.io->hold();
    ASSERT_TRUE(fixture.adapter.save());
    ASSERT_TRUE(fixture.wait([&] { return fixture.io->entered(); }));
    ASSERT_TRUE(fixture.adapter.pending());
    fixture.adapter.closeSettings();
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_FALSE(fixture.adapter.setConfirmation(true));
    fixture.io->release();
    ASSERT_TRUE(fixture.wait([&] {
        const auto& outcome = fixture.coordinator.state().installationProfileOutcome;
        return !fixture.adapter.pending() && outcome && outcome->savedProfile.has_value();
    }));
    const auto& saved = *fixture.coordinator.state().installationProfileOutcome->savedProfile;
    ASSERT_EQ(saved.orientation, (core::Orientation{true, false, core::Rotation::Degrees90}));
    EXPECT_TRUE(fixture.adapter.editable());
    EXPECT_TRUE(fixture.adapter.flipHorizontal());
    EXPECT_FALSE(fixture.adapter.flipVertical());
    EXPECT_EQ(fixture.adapter.rotationIndex(), 1);
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    EXPECT_FALSE(fixture.adapter.saveEnabled());
    const auto savedBytes = fixture.machineBytes();
    fixture.adapter.refresh();
    EXPECT_EQ(fixture.machineBytes(), savedBytes);
    // Once initialized after completion, ordinary local edits still survive refresh.
    ASSERT_TRUE(fixture.adapter.setFlipVertical(true));
    fixture.adapter.refresh();
    EXPECT_TRUE(fixture.adapter.flipHorizontal());
    EXPECT_TRUE(fixture.adapter.flipVertical());
    EXPECT_EQ(fixture.adapter.rotationIndex(), 1);
}

TEST(QmlInstallationAdapter, SynchronousDuplicateCommandIdRejectionReleasesLatchBeforeRetry) {
    Fixture fixture;
    ASSERT_TRUE(fixture.startStopped());
    const auto source = fixture.coordinator.state().cameraStatus;
    ASSERT_TRUE(source && source->actualIdentity);
    // A completed command through the public pipeline API consumes ID 1.
    // The coordinator's first command then encounters real synchronous admission
    // rejection. No service fake, private counter mutation or production hook.
    ASSERT_TRUE(fixture.pipeline.saveInstallationProfile({1, source->sessionGeneration,
        *source->actualIdentity, {false, false, core::Rotation::Degrees0}, true, false}).hasValue());
    ASSERT_TRUE(fixture.wait([&] {
        const auto& state = fixture.coordinator.state();
        return !state.installationProfilePending && state.installationProfileOutcome
            && state.installationProfileOutcome->savedProfile.has_value();
    }));
    const auto firstBytes = fixture.machineBytes();
    ASSERT_FALSE(firstBytes.isEmpty());
    ASSERT_TRUE(fixture.adapter.openSettings());
    ASSERT_TRUE(fixture.adapter.setFlipHorizontal(true));
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    ASSERT_TRUE(fixture.adapter.saveEnabled());
    EXPECT_FALSE(fixture.adapter.save());
    EXPECT_FALSE(fixture.adapter.pending());
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    EXPECT_FALSE(fixture.adapter.saveEnabled());
    EXPECT_TRUE(fixture.adapter.editable());
    EXPECT_TRUE(fixture.adapter.status().contains(QStringLiteral("failed"), Qt::CaseInsensitive));
    EXPECT_EQ(fixture.machineBytes(), firstBytes);
    ASSERT_TRUE(fixture.coordinator.state().installationProfileOutcome);
    ASSERT_TRUE(fixture.coordinator.state().installationProfileOutcome->error);
    EXPECT_EQ(fixture.coordinator.state().installationProfileOutcome->error->code,
        "installation_request_not_increasing");
    EXPECT_FALSE(fixture.adapter.save());
    EXPECT_EQ(fixture.machineBytes(), firstBytes);
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    ASSERT_TRUE(fixture.adapter.save());
    ASSERT_TRUE(fixture.wait([&] {
        const auto& outcome = fixture.coordinator.state().installationProfileOutcome;
        return !fixture.adapter.pending() && outcome && outcome->requestId == 2
            && outcome->savedProfile.has_value();
    }));
    const auto& saved = *fixture.coordinator.state().installationProfileOutcome->savedProfile;
    EXPECT_EQ(saved.revision, 2U);
    EXPECT_EQ(saved.orientation, (core::Orientation{true, false, core::Rotation::Degrees0}));
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    EXPECT_FALSE(fixture.adapter.saveEnabled());
    EXPECT_NE(fixture.machineBytes(), firstBytes);
}

TEST(QmlInstallationAdapter, StreamingSourceReplacementAndClosingRejectFreshCommands) {
    Fixture fixture;
    ASSERT_TRUE(fixture.startStopped());
    ASSERT_TRUE(fixture.act(Intent::Apply));
    ASSERT_TRUE(fixture.act(Intent::Confirm));
    ASSERT_TRUE(fixture.act(Intent::Start));
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_FALSE(fixture.adapter.setConfirmation(true));
    EXPECT_FALSE(fixture.adapter.setFlipHorizontal(true));
    EXPECT_FALSE(fixture.adapter.save());
    ASSERT_TRUE(fixture.act(Intent::Stop));
    ASSERT_TRUE(fixture.wait([&] { return fixture.adapter.editable(); }));
    ASSERT_TRUE(fixture.adapter.setConfirmation(true));
    ASSERT_TRUE(fixture.act(Intent::Disconnect));
    EXPECT_FALSE(fixture.adapter.editable());
    EXPECT_FALSE(fixture.adapter.confirmationChecked());
    EXPECT_FALSE(fixture.adapter.save());
    ASSERT_TRUE(fixture.act(Intent::Connect));
    EXPECT_FALSE(fixture.adapter.editable());
    fixture.adapter.closeSettings();
    ASSERT_TRUE(fixture.adapter.openSettings());
    EXPECT_TRUE(fixture.adapter.editable());
    fixture.adapter.setClosing();
    EXPECT_FALSE(fixture.adapter.isOpen());
    EXPECT_FALSE(fixture.adapter.openSettings());
    EXPECT_FALSE(fixture.adapter.save());
}
}  // namespace
