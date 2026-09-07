#include <lumora/configuration/ApplicationConfiguration.hpp>
#include <lumora/configuration/ConfigurationCodec.hpp>
#include <lumora/configuration/ConfigurationStore.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

namespace lumora::configuration {
namespace {

core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
}

camera::CameraCapabilities capabilities() {
    return {{mono8()},
        {{0U, 0U, 8U, 8U}, {0U, 0U, 640U, 480U}, {1U, 1U, 8U, 8U}},
        {1.0, 60.0, 0.1, false}, {10.0, 10000.0, 1.0, true},
        {camera::ExposureMode::Manual, camera::ExposureMode::Auto},
        {0.0, 24.0, 0.1, true},
        {camera::GainMode::Manual, camera::GainMode::Auto}};
}

camera::CameraConfiguration cameraConfiguration() {
    return {mono8(), {0U, 0U, 640U, 480U}, 30.0,
        {camera::ExposureMode::Manual, 1000.0},
        {camera::GainMode::Manual, 2.0}, camera::AcquisitionMode::Continuous};
}

application::StartupPreferences preferences() {
    return {1U, {"camera-1"}, {"Lumora", "Simulator", "SIM-1", "virtual", "1.0"},
        capabilities(), cameraConfiguration(), cameraConfiguration(), true};
}

[[nodiscard]] std::filesystem::path pathFromQString(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    const auto encoded = path.toUtf8();
    return std::filesystem::path(
        std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())));
#endif
}

class StartupPreferencesTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        path_ = pathFromQString(temp_.path()) / "config.json";
    }

    [[nodiscard]] core::Result<ApplicationConfiguration> loadSchema1() const {
#ifdef _WIN32
        QFile file(QString::fromStdWString(path_.native()));
#else
        QFile file(QString::fromUtf8(path_.native()));
#endif
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || file.write(R"json({
                "schemaVersion": 1,
                "application": {"theme":"dark"},
                "cameraProfiles": {"legacy":true},
                "processing": {"preset":"Standard"},
                "presets": {"selected":"Standard"},
                "capture": {"directory":"captures"},
                "ui": {"sidebar":true}
            })json") < 0) {
            return core::Result<ApplicationConfiguration>::failure({
                core::ErrorCategory::Configuration, "test_fixture_write_failed",
                "Test configuration could not be written.", {}, false});
        }
        file.close();
        return ConfigurationStore(path_).load();
    }

    [[nodiscard]] core::Result<ApplicationConfiguration> roundTripConfirmed() const {
        ApplicationConfiguration configuration;
        configuration.application.insert("theme", "dark");
        configuration.cameraProfiles.insert("profile", "legacy");
        configuration.processing.insert("pipeline", "standard");
        configuration.presets.insert("selected", "Standard");
        configuration.capture.insert("directory", "captures");
        configuration.ui.insert("sidebar", true);
        configuration.startup = preferences();
        ConfigurationStore store(path_);
        const auto saved = store.save(configuration);
        if (!saved.hasValue()) {
            return core::Result<ApplicationConfiguration>::failure(saved.error());
        }
        return store.load();
    }

    QTemporaryDir temp_;
    std::filesystem::path path_;
};

TEST_F(StartupPreferencesTest, Schema1DoesNotInferConfirmation) {
    const auto loaded = loadSchema1();
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_FALSE(loaded.value().usedDefaults);
    EXPECT_EQ(loaded.value().schemaVersion, 2);
    EXPECT_FALSE(loaded.value().startup.has_value());
    EXPECT_EQ(loaded.value().application.value("theme"), "dark");
    EXPECT_EQ(loaded.value().cameraProfiles.value("legacy"), true);
}

TEST_F(StartupPreferencesTest, ConfirmedRecordRoundTrips) {
    const auto loaded = roundTripConfirmed();
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().diagnosticDetail;
    ASSERT_TRUE(loaded.value().startup.has_value());
    EXPECT_TRUE(loaded.value().startup->confirmed);
    EXPECT_EQ(loaded.value().startup->cameraId.value, "camera-1");
    EXPECT_EQ(loaded.value().startup->identity.serial, "SIM-1");
    EXPECT_TRUE(application::cameraCapabilitiesEqual(
        loaded.value().startup->confirmedCapabilities, capabilities()));
    EXPECT_TRUE(application::cameraConfigurationsEqual(
        loaded.value().startup->requested, cameraConfiguration()));
    EXPECT_TRUE(application::cameraConfigurationsEqual(
        loaded.value().startup->lastApplied, cameraConfiguration()));
    EXPECT_EQ(loaded.value().application.value("theme"), "dark");
    EXPECT_EQ(loaded.value().cameraProfiles.value("profile"), "legacy");
    EXPECT_EQ(loaded.value().processing.value("pipeline"), "standard");
    EXPECT_EQ(loaded.value().presets.value("selected"), "Standard");
    EXPECT_EQ(loaded.value().capture.value("directory"), "captures");
    EXPECT_EQ(loaded.value().ui.value("sidebar"), true);
}

TEST_F(StartupPreferencesTest, CanonicalEncodingIgnoresCapabilitySetOrder) {
    ApplicationConfiguration first;
    first.startup = preferences();
    auto second = first;
    std::reverse(second.startup->confirmedCapabilities.exposureModes.begin(),
        second.startup->confirmedCapabilities.exposureModes.end());
    std::reverse(second.startup->confirmedCapabilities.gainModes.begin(),
        second.startup->confirmedCapabilities.gainModes.end());

    const auto firstEncoded = ConfigurationCodec::encode(first);
    const auto secondEncoded = ConfigurationCodec::encode(second);

    ASSERT_TRUE(firstEncoded.hasValue());
    ASSERT_TRUE(secondEncoded.hasValue());
    EXPECT_EQ(firstEncoded.value(), secondEncoded.value());
}

TEST_F(StartupPreferencesTest, DuplicateCapabilityRecordIsRejected) {
    ApplicationConfiguration configuration;
    configuration.startup = preferences();
    const auto encoded = ConfigurationCodec::encode(configuration);
    ASSERT_TRUE(encoded.hasValue());
    auto root = QJsonDocument::fromJson(encoded.value()).object();
    auto startup = root.value("startup").toObject();
    auto capabilityObject = startup.value("confirmedCapabilities").toObject();
    auto formats = capabilityObject.value("pixelFormats").toArray();
    formats.append(formats.at(0));
    capabilityObject.insert("pixelFormats", formats);
    startup.insert("confirmedCapabilities", capabilityObject);
    root.insert("startup", startup);

    const auto decoded = ConfigurationCodec::decode(QJsonDocument(root).toJson());

    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.error().code, "configuration_invalid_startup");
}

TEST_F(StartupPreferencesTest, FutureStartupRecordVersionIsRejected) {
    ApplicationConfiguration configuration;
    configuration.startup = preferences();
    const auto encoded = ConfigurationCodec::encode(configuration);
    ASSERT_TRUE(encoded.hasValue());
    auto root = QJsonDocument::fromJson(encoded.value()).object();
    auto startup = root.value("startup").toObject();
    startup.insert("recordVersion", 2);
    root.insert("startup", startup);

    const auto decoded = ConfigurationCodec::decode(QJsonDocument(root).toJson());

    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.error().code, "configuration_invalid_startup");
}

struct IoState final {
    std::mutex mutex;
    std::condition_variable changed;
    bool loadCalled{false};
    bool blockLoad{false};
    bool loadReleased{false};
    bool unpreservedLoad{false};
    bool failLoad{false};
    int loadException{0};
    std::thread::id loadThread;
    bool blockFirstSave{false};
    bool failSave{false};
    bool savesReleased{false};
    std::vector<std::string> savedSerials;
    std::vector<std::thread::id> saveThreads;
    std::optional<ApplicationConfiguration> savedDocument;
};

class RecordingIo final : public IStartupPreferencesIo {
public:
    explicit RecordingIo(std::shared_ptr<IoState> state)
        : state_(std::move(state)) {}

    core::Result<ApplicationConfiguration> load() override {
        {
            std::unique_lock lock(state_->mutex);
            state_->loadCalled = true;
            state_->loadThread = std::this_thread::get_id();
            state_->changed.notify_all();
            if (state_->blockLoad) {
                state_->changed.wait(lock, [&] { return state_->loadReleased; });
            }
        }
        state_->changed.notify_all();
        if (state_->loadException == 1) { throw std::runtime_error("load failed"); }
        if (state_->loadException == 2) { throw 7; }
        if (state_->failLoad) {
            return core::Result<ApplicationConfiguration>::failure({core::ErrorCategory::Configuration,
                "scripted_read_failure", "Source could not be read.", {}, true});
        }
        ApplicationConfiguration configuration;
        configuration.startup = preferences();
        configuration.application.insert("theme", "retained-theme");
        if (state_->unpreservedLoad) {
            configuration.usedDefaults = true;
            configuration.loadWarning = core::Error{core::ErrorCategory::Configuration,
                "configuration_invalid_preservation_failed", "Source was not preserved.", {}, true};
        }
        return core::Result<ApplicationConfiguration>::success(std::move(configuration));
    }

    core::Result<void> save(const ApplicationConfiguration& configuration) override {
        std::unique_lock lock(state_->mutex);
        state_->savedSerials.push_back(configuration.startup->identity.serial);
        state_->saveThreads.push_back(std::this_thread::get_id());
        state_->savedDocument = configuration;
        state_->changed.notify_all();
        if (state_->blockFirstSave && state_->savedSerials.size() == 1U) {
            state_->changed.wait(lock, [&] { return state_->savesReleased; });
        }
        if (state_->failSave) {
            return core::Result<void>::failure({core::ErrorCategory::Configuration,
                "scripted_save_failure", "Startup preferences were not saved.",
                "Scripted save failure.", true});
        }
        return core::Result<void>::success();
    }

private:
    std::shared_ptr<IoState> state_;
};

struct ReleaseLoadOnExit final {
    std::shared_ptr<IoState> state;
    bool wait() {
        std::unique_lock lock(state->mutex);
        return state->changed.wait_for(lock, std::chrono::seconds{2}, [&] { return state->loadCalled; });
    }
    void release() {
        std::lock_guard lock(state->mutex);
        state->loadReleased = true;
        state->changed.notify_all();
    }
    ~ReleaseLoadOnExit() { release(); }
};

TEST(StartupPreferencesService, AcceptedSaveFailsWithoutWritingAfterUnpreservedLoad) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    state->unpreservedLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());
    ASSERT_TRUE(service.postSave(7U, preferences()).hasValue());
    service.requestStop();
    release.release();
    service.join();
    EXPECT_TRUE(state->savedSerials.empty());
    const auto status = service.latestStatus();
    EXPECT_EQ(status->latestAttemptedSaveRevision, 7U);
    EXPECT_FALSE(status->latestSavedRevision.has_value());
    ASSERT_TRUE(status->warning.has_value());
    EXPECT_EQ(status->warning->code, "startup_save_source_unsafe");
}

TEST(StartupPreferencesService, StopDuringLoadDrainsAcceptedSaveUsingTheLoadedDocument) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_FALSE(service.postSave(1U, preferences()).hasValue());
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());
    auto confirmed = preferences();
    confirmed.identity.serial = "NEW-CONFIRMED";
    ASSERT_TRUE(service.postSave(1U, confirmed).hasValue());
    EXPECT_FALSE(service.latestStatus()->loadCompleted);
    EXPECT_FALSE(service.latestStatus()->latestAttemptedSaveRevision.has_value());
    service.requestStop();
    EXPECT_FALSE(service.postSave(2U, preferences()).hasValue());
    release.release();
    service.join();
    ASSERT_EQ(state->savedSerials, (std::vector<std::string>{"NEW-CONFIRMED"}));
    ASSERT_TRUE(state->savedDocument.has_value());
    EXPECT_EQ(state->savedDocument->application.value("theme"), "retained-theme");
    EXPECT_EQ(state->saveThreads.front(), state->loadThread);
    EXPECT_NE(state->loadThread, std::this_thread::get_id());
    const auto status = service.latestStatus();
    EXPECT_EQ(status->latestAttemptedSaveRevision, 1U);
    EXPECT_EQ(status->latestSavedRevision, 1U);
    ASSERT_TRUE(status->loadedPreferences.has_value());
    EXPECT_EQ(status->loadedPreferences->identity.serial, "SIM-1");
}

TEST(StartupPreferencesService, SlowLoadCoalescesOnlyTheNewestValidIncreasingConfirmation) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());
    auto record = preferences();
    for (std::uint64_t revision = 1U; revision <= 100U; ++revision) {
        record.identity.serial = "CONFIRMED-" + std::to_string(revision);
        ASSERT_TRUE(service.postSave(revision, record).hasValue());
    }
    EXPECT_FALSE(service.postSave(100U, preferences()).hasValue());
    record.confirmed = false;
    EXPECT_FALSE(service.postSave(101U, record).hasValue());
    EXPECT_FALSE(service.latestStatus()->latestAttemptedSaveRevision.has_value());
    service.requestStop();
    release.release();
    service.join();
    EXPECT_EQ(state->savedSerials, (std::vector<std::string>{"CONFIRMED-100"}));
    EXPECT_EQ(service.latestStatus()->latestAttemptedSaveRevision, 100U);
    EXPECT_EQ(service.latestStatus()->latestSavedRevision, 100U);
}

TEST(StartupPreferencesService, AcceptedSaveFailsWithoutWritingAfterFailedLoad) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    state->failLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());
    ASSERT_TRUE(service.postSave(8U, preferences()).hasValue());
    service.requestStop();
    release.release();
    service.join();
    EXPECT_TRUE(state->savedSerials.empty());
    const auto status = service.latestStatus();
    EXPECT_EQ(status->latestAttemptedSaveRevision, 8U);
    EXPECT_FALSE(status->latestSavedRevision.has_value());
    ASSERT_TRUE(status->warning.has_value());
    EXPECT_EQ(status->warning->code, "startup_save_source_unsafe");
}

TEST(StartupPreferencesService, LoadExceptionSettlesNewestAcceptedSaveAsFailed) {
    for (const int exceptionKind : {1, 2}) {
        auto state = std::make_shared<IoState>();
        state->blockLoad = true;
        state->loadException = exceptionKind;
        StartupPreferencesService service(std::make_unique<RecordingIo>(state));
        ReleaseLoadOnExit release{state};
        ASSERT_TRUE(service.start().hasValue());
        ASSERT_TRUE(release.wait());
        ASSERT_TRUE(service.postSave(9U, preferences()).hasValue());
        service.requestStop();
        release.release();
        service.join();
        const auto status = service.latestStatus();
        EXPECT_EQ(status->latestAttemptedSaveRevision, 9U);
        EXPECT_FALSE(status->latestSavedRevision.has_value());
        EXPECT_TRUE(state->savedSerials.empty());
        ASSERT_TRUE(status->warning.has_value());
        EXPECT_EQ(status->warning->code, "startup_service_worker_exception");
        EXPECT_FALSE(service.postSave(10U, preferences()).hasValue());
    }
}

TEST(StartupPreferencesService, LoadsOnBackgroundWorkerAndPublishesInitialStatus) {
    using namespace std::chrono_literals;
    auto state = std::make_shared<IoState>();
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));

    const auto started = service.start();
    ASSERT_TRUE(started.hasValue());
    {
        std::unique_lock lock(state->mutex);
        ASSERT_TRUE(state->changed.wait_for(lock, 2s, [&] { return state->loadCalled; }));
        EXPECT_NE(state->loadThread, std::this_thread::get_id());
    }
    std::shared_ptr<const application::StartupPreferencesStatus> status;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        status = service.latestStatus();
        std::this_thread::yield();
    } while (!status->loadCompleted && std::chrono::steady_clock::now() < deadline);
    ASSERT_TRUE(status->loadCompleted);
    ASSERT_TRUE(status->loadedPreferences.has_value());
    EXPECT_EQ(status->loadedPreferences->cameraId.value, "camera-1");
    EXPECT_FALSE(status->latestAttemptedSaveRevision.has_value());
    EXPECT_FALSE(status->latestSavedRevision.has_value());
    service.requestStop();
    service.join();
}

TEST(StartupPreferencesService, CoalescesPendingSaveAndDrainsNewestOnShutdown) {
    using namespace std::chrono_literals;
    auto state = std::make_shared<IoState>();
    state->blockFirstSave = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    struct ReleaseOnExit final {
        std::shared_ptr<IoState> state;
        ~ReleaseOnExit() {
            {
                std::lock_guard lock(state->mutex);
                state->savesReleased = true;
            }
            state->changed.notify_all();
        }
    } releaseOnExit{state};

    ASSERT_TRUE(service.start().hasValue());
    const auto loadDeadline = std::chrono::steady_clock::now() + 2s;
    while (!service.latestStatus()->loadCompleted
        && std::chrono::steady_clock::now() < loadDeadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(service.latestStatus()->loadCompleted);

    auto first = preferences();
    first.identity.serial = "SIM-1";
    ASSERT_TRUE(service.postSave(1U, first).hasValue());
    {
        std::unique_lock lock(state->mutex);
        ASSERT_TRUE(state->changed.wait_for(
            lock, 2s, [&] { return state->savedSerials.size() == 1U; }));
    }
    const auto activeStatus = service.latestStatus();
    EXPECT_EQ(activeStatus->latestAttemptedSaveRevision, 1U);
    EXPECT_FALSE(activeStatus->latestSavedRevision.has_value());
    auto second = preferences();
    second.identity.serial = "SIM-2";
    auto newest = preferences();
    newest.identity.serial = "SIM-3";
    ASSERT_TRUE(service.postSave(2U, second).hasValue());
    ASSERT_TRUE(service.postSave(3U, newest).hasValue());
    service.requestStop();
    {
        std::lock_guard lock(state->mutex);
        state->savesReleased = true;
    }
    state->changed.notify_all();
    service.join();

    ASSERT_EQ(state->savedSerials, (std::vector<std::string>{"SIM-1", "SIM-3"}));
    ASSERT_EQ(state->saveThreads.size(), 2U);
    EXPECT_EQ(state->saveThreads[0], state->loadThread);
    EXPECT_EQ(state->saveThreads[1], state->loadThread);
    const auto status = service.latestStatus();
    EXPECT_EQ(status->latestAttemptedSaveRevision, 3U);
    EXPECT_EQ(status->latestSavedRevision, 3U);
    ASSERT_TRUE(status->loadedPreferences.has_value());
    EXPECT_EQ(status->loadedPreferences->identity.serial, "SIM-1");
}

TEST(StartupPreferencesService, SaveFailureUpdatesAttemptWithoutClaimingPersistence) {
    using namespace std::chrono_literals;
    auto state = std::make_shared<IoState>();
    state->failSave = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ASSERT_TRUE(service.start().hasValue());
    const auto loadDeadline = std::chrono::steady_clock::now() + 2s;
    while (!service.latestStatus()->loadCompleted
        && std::chrono::steady_clock::now() < loadDeadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(service.postSave(9U, preferences()).hasValue());
    std::shared_ptr<const application::StartupPreferencesStatus> status;
    const auto saveDeadline = std::chrono::steady_clock::now() + 2s;
    do {
        status = service.latestStatus();
        std::this_thread::yield();
    } while ((!status->warning || status->warning->code != "scripted_save_failure")
        && std::chrono::steady_clock::now() < saveDeadline);
    ASSERT_EQ(status->latestAttemptedSaveRevision, 9U);
    EXPECT_FALSE(status->latestSavedRevision.has_value());
    ASSERT_TRUE(status->warning.has_value());
    EXPECT_EQ(status->warning->code, "scripted_save_failure");
    service.requestStop();
    service.join();
}

class UnsafeLoadIo final : public IStartupPreferencesIo {
public:
    core::Result<ApplicationConfiguration> load() override {
        ApplicationConfiguration configuration;
        configuration.usedDefaults = true;
        configuration.loadWarning = core::Error{core::ErrorCategory::Configuration,
            "configuration_invalid_preservation_failed",
            "Invalid configuration could not be preserved; defaults were loaded.",
            "Scripted preservation failure.", true};
        return core::Result<ApplicationConfiguration>::success(std::move(configuration));
    }

    core::Result<void> save(const ApplicationConfiguration&) override {
        ++saveCalls;
        return core::Result<void>::success();
    }

    int saveCalls{0};
};

TEST(StartupPreferencesService, UnpreservedCorruptionRejectsGuessedDefaultSave) {
    using namespace std::chrono_literals;
    auto io = std::make_unique<UnsafeLoadIo>();
    auto* ioObserver = io.get();
    StartupPreferencesService service(std::move(io));
    ASSERT_TRUE(service.start().hasValue());
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!service.latestStatus()->loadCompleted
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    const auto submitted = service.postSave(1U, preferences());

    ASSERT_FALSE(submitted.hasValue());
    EXPECT_EQ(submitted.error().code, "startup_save_source_unsafe");
    EXPECT_EQ(ioObserver->saveCalls, 0);
    service.requestStop();
    service.join();
}

class FailingLoadIo final : public IStartupPreferencesIo {
public:
    core::Result<ApplicationConfiguration> load() override {
        return core::Result<ApplicationConfiguration>::failure({
            core::ErrorCategory::Configuration, "scripted_read_failure",
            "The configuration file could not be read.",
            "Scripted unreadable source.", true});
    }

    core::Result<void> save(const ApplicationConfiguration&) override {
        return core::Result<void>::success();
    }
};

TEST(StartupPreferencesService, FailedLoadPublishesWarningAndDisablesSaves) {
    using namespace std::chrono_literals;
    StartupPreferencesService service(std::make_unique<FailingLoadIo>());
    ASSERT_TRUE(service.start().hasValue());
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!service.latestStatus()->loadCompleted
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const auto status = service.latestStatus();
    ASSERT_TRUE(status->loadCompleted);
    ASSERT_TRUE(status->warning.has_value());
    EXPECT_EQ(status->warning->code, "scripted_read_failure");
    EXPECT_FALSE(service.postSave(1U, preferences()).hasValue());
    service.requestStop();
    service.join();
}

class ThrowingIo final : public IStartupPreferencesIo {
public:
    explicit ThrowingIo(bool unknown) : unknown_(unknown) {}

    core::Result<ApplicationConfiguration> load() override {
        if (unknown_) {
            throw 7;
        }
        throw std::runtime_error("scripted load exception");
    }

    core::Result<void> save(const ApplicationConfiguration&) override {
        return core::Result<void>::success();
    }

private:
    bool unknown_;
};

TEST(StartupPreferencesService, WorkerBoundaryContainsStandardAndUnknownExceptions) {
    using namespace std::chrono_literals;
    for (const bool unknown : {false, true}) {
        StartupPreferencesService service(std::make_unique<ThrowingIo>(unknown));
        ASSERT_TRUE(service.start().hasValue());
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!service.latestStatus()->loadCompleted
            && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        const auto status = service.latestStatus();
        ASSERT_TRUE(status->loadCompleted);
        ASSERT_TRUE(status->warning.has_value());
        EXPECT_EQ(status->warning->code, "startup_service_worker_exception");
        service.requestStop();
        service.join();
    }
}

}  // namespace
}  // namespace lumora::configuration
