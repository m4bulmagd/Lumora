#include <lumora/configuration/ApplicationConfiguration.hpp>
#include <lumora/configuration/CameraProfileCodec.hpp>
#include <lumora/configuration/ConfigurationCodec.hpp>
#include <lumora/configuration/ConfigurationStore.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>

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

ApplicationConfiguration profileDocument(std::size_t count) {
    ApplicationConfiguration document;
    for (std::size_t index = 0U; index < count; ++index) {
        auto profile = preferences();
        profile.identity.serial = "LOADED-" + std::to_string(index);
        profile.cameraId = {"loaded-logical-" + std::to_string(index)};
        document.cameraProfiles.profiles.push_back(std::move(profile));
    }
    if (count > 0U) {
        document.cameraProfiles.lastSelectedCameraId = {"loaded-logical-0"};
    }
    return document;
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
        configuration.legacyCameraProfiles.insert("profile", "legacy");
        configuration.processing.insert("pipeline", "standard");
        configuration.presets.selectedId = {"standard"};
        configuration.presets.activePipeline = processing::standardPipeline();
        configuration.legacyPresets.insert("selected", "Standard");
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
    EXPECT_EQ(loaded.value().schemaVersion, 4);
    EXPECT_EQ(loaded.value().presets.selectedId.value, "original");
    EXPECT_EQ(loaded.value().legacyPresets.value("selected"), "Standard");
    EXPECT_FALSE(loaded.value().startup.has_value());
    EXPECT_EQ(loaded.value().application.value("theme"), "dark");
    EXPECT_EQ(loaded.value().legacyCameraProfiles.value("legacy"), true);
    EXPECT_TRUE(loaded.value().cameraProfiles.profiles.empty());
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
    EXPECT_EQ(loaded.value().legacyCameraProfiles.value("profile"), "legacy");
    ASSERT_EQ(loaded.value().cameraProfiles.profiles.size(), 1U);
    ASSERT_TRUE(loaded.value().cameraProfiles.lastSelectedCameraId.has_value());
    EXPECT_EQ(loaded.value().cameraProfiles.lastSelectedCameraId->value, "camera-1");
    EXPECT_EQ(loaded.value().processing.value("pipeline"), "standard");
    EXPECT_EQ(loaded.value().presets.selectedId.value, "standard");
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        loaded.value().presets.activePipeline, processing::standardPipeline()));
    EXPECT_EQ(loaded.value().legacyPresets.value("selected"), "Standard");
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
    auto cameraProfiles = root.value("cameraProfiles").toObject();
    auto profileArray = cameraProfiles.value("profiles").toArray();
    auto startup = profileArray.at(0).toObject();
    auto capabilityObject = startup.value("confirmedCapabilities").toObject();
    auto formats = capabilityObject.value("pixelFormats").toArray();
    formats.append(formats.at(0));
    capabilityObject.insert("pixelFormats", formats);
    startup.insert("confirmedCapabilities", capabilityObject);
    profileArray.replace(0, startup);
    cameraProfiles.insert("profiles", profileArray);
    root.insert("cameraProfiles", cameraProfiles);

    const auto decoded = ConfigurationCodec::decode(QJsonDocument(root).toJson());

    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.error().code, "configuration_invalid_camera_profiles");
}

TEST_F(StartupPreferencesTest, FutureStartupRecordVersionIsRejected) {
    ApplicationConfiguration configuration;
    configuration.startup = preferences();
    const auto encoded = ConfigurationCodec::encode(configuration);
    ASSERT_TRUE(encoded.hasValue());
    auto root = QJsonDocument::fromJson(encoded.value()).object();
    auto cameraProfiles = root.value("cameraProfiles").toObject();
    auto profileArray = cameraProfiles.value("profiles").toArray();
    auto startup = profileArray.at(0).toObject();
    startup.insert("recordVersion", 2);
    profileArray.replace(0, startup);
    cameraProfiles.insert("profiles", profileArray);
    root.insert("cameraProfiles", cameraProfiles);

    const auto decoded = ConfigurationCodec::decode(QJsonDocument(root).toJson());

    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.error().code, "configuration_invalid_camera_profiles");
}

TEST_F(StartupPreferencesTest, Schema3MigratesStartupAndOpaqueCameraProfilesToSchema4) {
    ApplicationConfiguration current;
    current.startup = preferences();
    current.legacyCameraProfiles.insert("opaque", 17);
    const auto encoded = ConfigurationCodec::encode(current);
    ASSERT_TRUE(encoded.hasValue());
    auto root = QJsonDocument::fromJson(encoded.value()).object();
    const auto typedProfiles = root.value("cameraProfiles").toObject();
    const auto migratedStartup = typedProfiles.value("profiles").toArray().at(0);
    root.insert("schemaVersion", 3);
    root.insert("cameraProfiles", QJsonObject{{"opaque", 17}});
    root.insert("startup", migratedStartup);
    root.remove("legacyCameraProfiles");
    auto oldStartup = root.value("startup").toObject();
    oldStartup.remove("capabilityFingerprintVersion");
    oldStartup.remove("installationProfile");
    root.insert("startup", oldStartup);

    const auto decoded = ConfigurationCodec::decode(QJsonDocument(root).toJson());

    ASSERT_TRUE(decoded.hasValue()) << decoded.error().diagnosticDetail;
    EXPECT_EQ(decoded.value().schemaVersion, 4);
    EXPECT_EQ(decoded.value().legacyCameraProfiles.value("opaque"), 17);
    ASSERT_EQ(decoded.value().cameraProfiles.profiles.size(), 1U);
    EXPECT_EQ(decoded.value().cameraProfiles.profiles.front().identity.serial, "SIM-1");
    EXPECT_EQ(decoded.value().cameraProfiles.profiles.front().capabilityFingerprintVersion, 1U);
    EXPECT_FALSE(decoded.value().cameraProfiles.profiles.front().installationProfile.has_value());
}

TEST_F(StartupPreferencesTest, Schema2And3RejectMissingOrWrongTypedStartup) {
    ApplicationConfiguration current;
    current.startup = preferences();
    const auto encoded = ConfigurationCodec::encode(current);
    ASSERT_TRUE(encoded.hasValue());
    const auto schema4 = QJsonDocument::fromJson(encoded.value()).object();
    const auto profiles = schema4.value("cameraProfiles").toObject()
        .value("profiles").toArray();
    ASSERT_EQ(profiles.size(), 1);
    auto legacyStartup = profiles.at(0).toObject();
    legacyStartup.remove("capabilityFingerprintVersion");
    legacyStartup.remove("installationProfile");

    const std::vector<QJsonValue> malformed{
        QJsonArray{}, QStringLiteral("invalid"), true, 7.0};
    for (const int version : {2, 3}) {
        auto base = schema4;
        base.insert("schemaVersion", version);
        base.insert("cameraProfiles", QJsonObject{{"opaque", true}});
        base.remove("legacyCameraProfiles");
        for (const auto& value : malformed) {
            auto root = base;
            root.insert("startup", value);
            const auto decoded =
                ConfigurationCodec::decode(QJsonDocument(root).toJson());
            ASSERT_FALSE(decoded.hasValue()) << "schema " << version;
            EXPECT_EQ(decoded.error().code, "configuration_invalid_startup");
        }
        auto missing = base;
        missing.remove("startup");
        const auto missingResult =
            ConfigurationCodec::decode(QJsonDocument(missing).toJson());
        ASSERT_FALSE(missingResult.hasValue()) << "schema " << version;
        EXPECT_EQ(missingResult.error().code, "configuration_invalid_startup");

        auto valid = base;
        valid.insert("startup", legacyStartup);
        EXPECT_TRUE(ConfigurationCodec::decode(
            QJsonDocument(valid).toJson()).hasValue());
        valid.insert("startup", QJsonValue{});
        EXPECT_TRUE(ConfigurationCodec::decode(
            QJsonDocument(valid).toJson()).hasValue());
    }
}

TEST_F(StartupPreferencesTest, StorePreservesMalformedSchema3Startup) {
    ApplicationConfiguration current;
    const auto encoded = ConfigurationCodec::encode(current);
    ASSERT_TRUE(encoded.hasValue());
    auto root = QJsonDocument::fromJson(encoded.value()).object();
    root.insert("schemaVersion", 3);
    root.insert("cameraProfiles", QJsonObject{});
    root.insert("startup", QJsonArray{});
    root.remove("legacyCameraProfiles");
    const auto original = QJsonDocument(root).toJson();
#ifdef _WIN32
    QFile file(QString::fromStdWString(path_.native()));
#else
    QFile file(QString::fromUtf8(path_.native()));
#endif
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(original), original.size());
    file.close();

    const auto loaded = ConfigurationStore(path_).load();

    ASSERT_TRUE(loaded.hasValue());
    EXPECT_TRUE(loaded.value().usedDefaults);
    ASSERT_TRUE(loaded.value().preservedInvalidFile.has_value());
    EXPECT_TRUE(std::filesystem::exists(*loaded.value().preservedInvalidFile));
}

TEST_F(StartupPreferencesTest, Schema4RoundTripOmitsLegacyStartupAuthority) {
    ApplicationConfiguration configuration;
    auto cameraA = preferences();
    cameraA.cameraId = {"logical-a"};
    cameraA.identity.serial = "SERIAL-A";
    cameraA.installationProfile = application::InstallationProfileReference{
        1U, 9007199254740993ULL,
        {true, false, core::Rotation::Degrees270}};
    configuration.cameraProfiles.lastSelectedCameraId = cameraA.cameraId;
    configuration.cameraProfiles.profiles.push_back(cameraA);
    auto ignoredLegacyStartup = preferences();
    ignoredLegacyStartup.identity.serial = "IGNORED";
    configuration.startup = ignoredLegacyStartup;

    const auto encoded = ConfigurationCodec::encode(configuration);
    ASSERT_TRUE(encoded.hasValue()) << encoded.error().diagnosticDetail;
    const auto root = QJsonDocument::fromJson(encoded.value()).object();
    EXPECT_EQ(root.value("schemaVersion").toInt(), 4);
    EXPECT_FALSE(root.contains("startup"));
    const auto decoded = ConfigurationCodec::decode(encoded.value());
    ASSERT_TRUE(decoded.hasValue()) << decoded.error().diagnosticDetail;
    ASSERT_EQ(decoded.value().cameraProfiles.profiles.size(), 1U);
    const auto& roundTripped = decoded.value().cameraProfiles.profiles.front();
    EXPECT_EQ(roundTripped.identity.serial, "SERIAL-A");
    ASSERT_TRUE(roundTripped.installationProfile.has_value());
    EXPECT_EQ(roundTripped.installationProfile->revision, 9007199254740993ULL);
    EXPECT_EQ(roundTripped.installationProfile->orientation,
        (core::Orientation{true, false, core::Rotation::Degrees270}));
}

TEST_F(StartupPreferencesTest, FutureSchema5IsRejected) {
    const auto encoded = ConfigurationCodec::encode(ApplicationConfiguration{});
    ASSERT_TRUE(encoded.hasValue());
    auto root = QJsonDocument::fromJson(encoded.value()).object();
    root.insert("schemaVersion", 5);

    const auto decoded = ConfigurationCodec::decode(QJsonDocument(root).toJson());

    ASSERT_FALSE(decoded.hasValue());
    EXPECT_EQ(decoded.error().code, "configuration_future_schema");
}

TEST_F(StartupPreferencesTest, DuplicateStableIdentitiesAreRejectedWithoutEviction) {
    ApplicationConfiguration configuration;
    auto original = preferences();
    auto duplicate = original;
    duplicate.cameraId = {"different-logical-id"};
    duplicate.identity.transport = "different-transport";
    duplicate.identity.firmware = "different-firmware";
    configuration.cameraProfiles.profiles = {original, duplicate};

    const auto encoded = ConfigurationCodec::encode(configuration);

    ASSERT_FALSE(encoded.hasValue());
    EXPECT_EQ(encoded.error().code, "configuration_duplicate_camera_profile");
}

TEST_F(StartupPreferencesTest, SharedIdentityAndCapabilityCodecsRoundTripStructuralData) {
    const auto identity = preferences().identity;
    const auto decodedIdentity = decodeCameraIdentity(encodeCameraIdentity(identity));
    ASSERT_TRUE(decodedIdentity.hasValue());
    EXPECT_TRUE(application::cameraIdentityKeysEqual(
        decodedIdentity.value(), identity));
    EXPECT_EQ(decodedIdentity.value().transport, "virtual");
    EXPECT_EQ(decodedIdentity.value().firmware, "1.0");

    auto reordered = capabilities();
    std::reverse(reordered.exposureModes.begin(), reordered.exposureModes.end());
    std::reverse(reordered.gainModes.begin(), reordered.gainModes.end());
    const auto decodedCapabilities =
        decodeCameraCapabilities(encodeCameraCapabilities(reordered));
    ASSERT_TRUE(decodedCapabilities.hasValue())
        << decodedCapabilities.error().diagnosticDetail;
    EXPECT_TRUE(application::cameraCapabilitiesEqual(
        decodedCapabilities.value(), capabilities()));
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
    int saveException{0};
    bool recoveredPresets{false};
    bool savesReleased{false};
    std::vector<std::string> savedSerials;
    std::vector<std::thread::id> saveThreads;
    std::optional<ApplicationConfiguration> savedDocument;
    std::vector<ApplicationConfiguration> savedDocuments;
    std::optional<ApplicationConfiguration> loadedDocument;
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
        ApplicationConfiguration configuration = state_->loadedDocument.value_or(
            ApplicationConfiguration{});
        if (!state_->loadedDocument) configuration.startup = preferences();
        configuration.application.insert("theme", "retained-theme");
        if (state_->recoveredPresets) {
            application::Preset recipe{{"saved-user"}, "Retained user recipe", "", false, 1, 23, processing::standardPipeline()};
            configuration.presets.customPresets.push_back(recipe);
            configuration.presets.selectedId = recipe.id;
            configuration.presets.activePipeline = recipe.pipeline;
            configuration.legacyPresets.insert("old-selection", "keep");
            configuration.loadWarning = core::Error{core::ErrorCategory::Configuration,
                "preset_entries_recovered", "A malformed entry was skipped.", {}, true};
            configuration.presetIssues.push_back({2U, application::PresetId{"invalid-entry"}, *configuration.loadWarning});
        }
        if (state_->unpreservedLoad) {
            configuration.usedDefaults = true;
            configuration.loadWarning = core::Error{core::ErrorCategory::Configuration,
                "configuration_invalid_preservation_failed", "Source was not preserved.", {}, true};
        }
        return core::Result<ApplicationConfiguration>::success(std::move(configuration));
    }

    core::Result<void> save(const ApplicationConfiguration& configuration) override {
        std::unique_lock lock(state_->mutex);
        state_->savedSerials.push_back(configuration.startup
                ? configuration.startup->identity.serial : std::string{});
        state_->saveThreads.push_back(std::this_thread::get_id());
        state_->savedDocument = configuration;
        state_->savedDocuments.push_back(configuration);
        state_->changed.notify_all();
        if (state_->blockFirstSave && state_->savedSerials.size() == 1U) {
            state_->changed.wait(lock, [&] { return state_->savesReleased; });
        }
        if (state_->saveException == 1) { throw std::runtime_error("save failed"); }
        if (state_->saveException == 2) { throw 7; }
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

TEST(StartupPreferencesService, DistinctCameraSavesSurviveCoalescingWhileLoadIsBlocked) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());

    auto cameraA = preferences();
    cameraA.cameraId = {"logical-a"};
    cameraA.identity.serial = "SERIAL-A";
    auto cameraB = preferences();
    cameraB.cameraId = {"logical-b"};
    cameraB.identity.serial = "SERIAL-B";
    ASSERT_TRUE(service.postSave(1U, cameraA).hasValue());
    ASSERT_TRUE(service.postSave(2U, cameraB).hasValue());

    service.requestStop();
    release.release();
    service.join();

    ASSERT_TRUE(state->savedDocument.has_value());
    const auto& savedProfiles = state->savedDocument->cameraProfiles.profiles;
    const auto hasSerial = [&](const std::string& serial) {
        return std::any_of(savedProfiles.begin(), savedProfiles.end(),
            [&](const auto& profile) { return profile.identity.serial == serial; });
    };
    EXPECT_TRUE(hasSerial("SERIAL-A"));
    EXPECT_TRUE(hasSerial("SERIAL-B"));
}

TEST(StartupPreferencesService, CoalescedABAUsesRevisionOrderAfterBlockedLoad) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());
    auto cameraA = preferences();
    cameraA.cameraId = {"shared-logical"};
    cameraA.identity.serial = "SERIAL-A";
    auto cameraB = preferences();
    cameraB.cameraId = {"shared-logical"};
    cameraB.identity.serial = "SERIAL-B";
    ASSERT_TRUE(service.postSave(1U, cameraA).hasValue());
    ASSERT_TRUE(service.postSave(2U, cameraB).hasValue());
    cameraA.identity.transport = "newest-a-transport";
    ASSERT_TRUE(service.postSave(3U, cameraA).hasValue());
    service.requestStop();
    release.release();
    service.join();

    ASSERT_TRUE(state->savedDocument.has_value());
    const auto& saved = state->savedDocument->cameraProfiles.profiles;
    const auto a = std::find_if(saved.begin(), saved.end(), [](const auto& profile) {
        return profile.identity.serial == "SERIAL-A";
    });
    const auto b = std::find_if(saved.begin(), saved.end(), [](const auto& profile) {
        return profile.identity.serial == "SERIAL-B";
    });
    ASSERT_NE(a, saved.end());
    ASSERT_NE(b, saved.end());
    EXPECT_LT(std::distance(saved.begin(), b), std::distance(saved.begin(), a));
    EXPECT_EQ(a->identity.transport, "newest-a-transport");
    const auto encoded = ConfigurationCodec::encode(*state->savedDocument);
    ASSERT_TRUE(encoded.hasValue());
    const auto decoded = ConfigurationCodec::decode(encoded.value());
    ASSERT_TRUE(decoded.hasValue());
    ASSERT_TRUE(decoded.value().startup.has_value());
    EXPECT_EQ(decoded.value().startup->identity.serial, "SERIAL-A");
    EXPECT_EQ(decoded.value().startup->identity.transport, "newest-a-transport");
}

TEST(StartupPreferencesService, SelectionIsIndependentAndDoesNotCreateConfirmation) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());

    auto cameraA = preferences();
    cameraA.cameraId = {"logical-a"};
    cameraA.identity.serial = "SERIAL-A";
    ASSERT_TRUE(service.postSave(1U, cameraA, false).hasValue());
    ASSERT_TRUE(service.postSelection(2U, {"logical-b"}).hasValue());
    EXPECT_FALSE(service.postSave(2U, cameraA, false).hasValue());
    service.requestStop();
    release.release();
    service.join();

    ASSERT_TRUE(state->savedDocument.has_value());
    ASSERT_TRUE(state->savedDocument->cameraProfiles.lastSelectedCameraId.has_value());
    EXPECT_EQ(state->savedDocument->cameraProfiles.lastSelectedCameraId->value,
        "logical-b");
    EXPECT_TRUE(std::none_of(state->savedDocument->cameraProfiles.profiles.begin(),
        state->savedDocument->cameraProfiles.profiles.end(), [](const auto& profile) {
            return profile.cameraId.value == "logical-b";
        }));
    EXPECT_FALSE(state->savedDocument->startup.has_value());
}

TEST(StartupPreferencesService, CapacityRejectionRetainsAcceptedExistingUpdate) {
    auto state = std::make_shared<IoState>();
    ApplicationConfiguration loaded;
    for (std::size_t index = 0U;
         index < application::CameraPreferences::MaximumProfiles; ++index) {
        auto profile = preferences();
        profile.identity.serial = "SERIAL-" + std::to_string(index);
        profile.cameraId = {"logical-" + std::to_string(index)};
        loaded.cameraProfiles.profiles.push_back(std::move(profile));
    }
    loaded.cameraProfiles.lastSelectedCameraId = {"logical-0"};
    state->loadedDocument = loaded;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ASSERT_TRUE(service.start().hasValue());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!service.latestStatus()->loadCompleted
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(service.latestStatus()->loadCompleted);

    auto updated = loaded.cameraProfiles.profiles.front();
    updated.requested.requestedFps = 31.0;
    ASSERT_TRUE(service.postSave(1U, updated, false).hasValue());
    auto newIdentity = preferences();
    newIdentity.identity.serial = "SERIAL-OVER-CAPACITY";
    const auto rejected = service.postSave(2U, newIdentity, false);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, "startup_save_capacity_reached");
    service.requestStop();
    service.join();

    ASSERT_TRUE(state->savedDocument.has_value());
    const auto saved = std::find_if(
        state->savedDocument->cameraProfiles.profiles.begin(),
        state->savedDocument->cameraProfiles.profiles.end(), [](const auto& profile) {
            return profile.identity.serial == "SERIAL-0";
        });
    ASSERT_NE(saved, state->savedDocument->cameraProfiles.profiles.end());
    EXPECT_EQ(saved->requested.requestedFps, 31.0);
    EXPECT_EQ(state->savedDocument->cameraProfiles.profiles.size(),
        application::CameraPreferences::MaximumProfiles);
}

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
    record.identity.serial = "COALESCED-IDENTITY";
    for (std::uint64_t revision = 1U; revision <= 100U; ++revision) {
        record.cameraId.value = "camera-" + std::to_string(revision);
        record.identity.transport = "virtual:" + std::to_string(revision);
        ASSERT_TRUE(service.postSave(revision, record).hasValue());
    }
    EXPECT_FALSE(service.postSave(100U, preferences()).hasValue());
    record.confirmed = false;
    EXPECT_FALSE(service.postSave(101U, record).hasValue());
    EXPECT_FALSE(service.latestStatus()->latestAttemptedSaveRevision.has_value());
    service.requestStop();
    release.release();
    service.join();
    EXPECT_EQ(state->savedSerials,
        (std::vector<std::string>{"COALESCED-IDENTITY"}));
    ASSERT_TRUE(state->savedDocument.has_value());
    ASSERT_TRUE(state->savedDocument->startup.has_value());
    EXPECT_EQ(state->savedDocument->startup->cameraId.value, "camera-100");
    EXPECT_EQ(state->savedDocument->startup->identity.transport, "virtual:100");
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

TEST(StartupPreferencesService, RecoveryWarningStillAllowsSaveAndPreservesCompleteTypedPresetState) {
    auto state = std::make_shared<IoState>();
    state->recoveredPresets = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(service.postSave(1U, preferences()).hasValue());
    service.requestStop();
    service.join();

    ASSERT_TRUE(state->savedDocument.has_value());
    const auto& saved = *state->savedDocument;
    EXPECT_FALSE(saved.usedDefaults);
    EXPECT_EQ(saved.presets.selectedId.value, "saved-user");
    ASSERT_EQ(saved.presets.customPresets.size(), 1U);
    EXPECT_EQ(saved.presets.customPresets[0].revision, 23U);
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(saved.presets.activePipeline, processing::standardPipeline()));
    EXPECT_EQ(saved.legacyPresets.value("old-selection"), "keep");
    ASSERT_EQ(saved.presetIssues.size(), 1U);
    EXPECT_EQ(service.latestStatus()->latestSavedRevision, 1U);
    ASSERT_TRUE(service.latestStatus()->warning.has_value());
    EXPECT_EQ(service.latestStatus()->warning->code, "preset_entries_recovered");
    const auto encoded = ConfigurationCodec::encode(saved);
    ASSERT_TRUE(encoded.hasValue());
    const auto root = QJsonDocument::fromJson(encoded.value()).object();
    EXPECT_EQ(root.value("presets").toObject().value("selectedId"), "saved-user");
    EXPECT_FALSE(root.contains("presetIssues"));
    EXPECT_FALSE(root.value("presets").toObject().contains("issues"));
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


application::PresetState standardPresets() {
    application::PresetState state;
    state.selectedId = {"standard"};
    state.activePipeline = processing::standardPipeline();
    return state;
}

struct ReleaseSaveOnExit final {
    std::shared_ptr<IoState> state;
    bool wait() {
        std::unique_lock lock(state->mutex);
        return state->changed.wait_for(lock, std::chrono::seconds{2}, [&] {
            return !state->savedDocuments.empty();
        });
    }
    void release() {
        std::lock_guard lock(state->mutex);
        state->savesReleased = true;
        state->changed.notify_all();
    }
    ~ReleaseSaveOnExit() { release(); }
};

TEST(StartupPreferencesService, InFlightIdentityReservesFinalCapacitySlot) {
    auto state = std::make_shared<IoState>();
    state->loadedDocument = profileDocument(
        application::CameraPreferences::MaximumProfiles - 1U);
    state->blockFirstSave = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseSaveOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    const auto loadDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!service.latestStatus()->loadCompleted
        && std::chrono::steady_clock::now() < loadDeadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(service.latestStatus()->loadCompleted);
    auto finalIdentity = preferences();
    finalIdentity.identity.serial = "FINAL-CAPACITY-IDENTITY";
    finalIdentity.cameraId = {"final-capacity-logical"};
    ASSERT_TRUE(service.postSave(1U, finalIdentity, false).hasValue());
    ASSERT_TRUE(release.wait());

    auto overflow = preferences();
    overflow.identity.serial = "OVERFLOW-IDENTITY";
    overflow.cameraId = {"overflow-logical"};
    const auto rejected = service.postSave(2U, overflow, false);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, "startup_save_capacity_reached");
    auto existingUpdate = state->loadedDocument->cameraProfiles.profiles.front();
    existingUpdate.requested.requestedFps = 31.0;
    ASSERT_TRUE(service.postSave(2U, existingUpdate, false).hasValue());
    service.requestStop();
    release.release();
    service.join();

    ASSERT_EQ(state->savedDocuments.size(), 2U);
    const auto& saved = state->savedDocuments.back().cameraProfiles.profiles;
    EXPECT_EQ(saved.size(), application::CameraPreferences::MaximumProfiles);
    EXPECT_TRUE(std::any_of(saved.begin(), saved.end(), [](const auto& profile) {
        return profile.identity.serial == "FINAL-CAPACITY-IDENTITY";
    }));
    EXPECT_FALSE(std::any_of(saved.begin(), saved.end(), [](const auto& profile) {
        return profile.identity.serial == "OVERFLOW-IDENTITY";
    }));
    const auto updated = std::find_if(saved.begin(), saved.end(), [](const auto& profile) {
        return profile.identity.serial == "LOADED-0";
    });
    ASSERT_NE(updated, saved.end());
    EXPECT_EQ(updated->requested.requestedFps, 31.0);
    EXPECT_EQ(service.latestStatus()->latestSavedRevision, 2U);
}

TEST(StartupPreferencesService, OverflowAfterBlockedFullLoadStaysFailedAcrossPresetSave) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    state->loadedDocument =
        profileDocument(application::CameraPreferences::MaximumProfiles);
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit releaseLoad{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(releaseLoad.wait());
    auto existingUpdate = state->loadedDocument->cameraProfiles.profiles.front();
    existingUpdate.requested.requestedFps = 31.0;
    ASSERT_TRUE(service.postSave(6U, existingUpdate, false).hasValue());
    auto overflow = preferences();
    overflow.identity.serial = "OVERFLOW-IDENTITY";
    overflow.cameraId = {"overflow-logical"};
    ASSERT_TRUE(service.postSave(7U, overflow).hasValue());
    releaseLoad.release();
    const auto cameraDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while ((!service.latestStatus()->warning
            || service.latestStatus()->warning->code
                != "startup_save_capacity_reached")
        && std::chrono::steady_clock::now() < cameraDeadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(service.latestStatus()->warning.has_value());
    ASSERT_TRUE(service.postPresetSave(1U, standardPresets()).hasValue());
    const auto presetDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (service.latestStatus()->latestSavedPresetRevision != 1U
        && std::chrono::steady_clock::now() < presetDeadline) {
        std::this_thread::yield();
    }
    service.requestStop();
    service.join();

    ASSERT_TRUE(state->savedDocument.has_value());
    const auto& saved = state->savedDocument->cameraProfiles;
    EXPECT_EQ(saved.profiles.size(), application::CameraPreferences::MaximumProfiles);
    EXPECT_FALSE(std::any_of(saved.profiles.begin(), saved.profiles.end(),
        [](const auto& profile) {
            return profile.identity.serial == "OVERFLOW-IDENTITY";
        }));
    const auto updated = std::find_if(saved.profiles.begin(), saved.profiles.end(),
        [](const auto& profile) { return profile.identity.serial == "LOADED-0"; });
    ASSERT_NE(updated, saved.profiles.end());
    EXPECT_EQ(updated->requested.requestedFps, 31.0);
    ASSERT_TRUE(saved.lastSelectedCameraId.has_value());
    EXPECT_EQ(saved.lastSelectedCameraId->value, "loaded-logical-0");
    const auto status = service.latestStatus();
    EXPECT_EQ(status->latestSavedRevision, 6U);
    EXPECT_EQ(status->latestSavedPresetRevision, 1U);
    ASSERT_TRUE(status->warning.has_value());
    EXPECT_EQ(status->warning->code, "startup_save_capacity_reached");
}

TEST(StartupPreferencesService,
    SuppressedEarlierSelectionStopsDurabilityPrefixAcrossPresetSave) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    state->loadedDocument =
        profileDocument(application::CameraPreferences::MaximumProfiles);
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit releaseLoad{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(releaseLoad.wait());

    auto newCamera = preferences();
    newCamera.identity.serial = "NEW-CAMERA";
    newCamera.cameraId = {"new-logical"};
    ASSERT_TRUE(service.postSave(1U, newCamera, true).hasValue());

    auto existingUpdate = state->loadedDocument->cameraProfiles.profiles.at(1U);
    existingUpdate.requested.requestedFps = 31.0;
    ASSERT_TRUE(service.postSave(2U, existingUpdate, false).hasValue());

    newCamera.requested.requestedFps = 32.0;
    ASSERT_TRUE(service.postSave(3U, newCamera, false).hasValue());
    releaseLoad.release();

    const auto cameraDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while ((!service.latestStatus()->warning
            || service.latestStatus()->warning->code
                != "startup_save_capacity_reached")
        && std::chrono::steady_clock::now() < cameraDeadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(service.latestStatus()->warning.has_value());
    ASSERT_TRUE(service.postPresetSave(1U, standardPresets()).hasValue());
    const auto presetDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (service.latestStatus()->latestSavedPresetRevision != 1U
        && std::chrono::steady_clock::now() < presetDeadline) {
        std::this_thread::yield();
    }
    service.requestStop();
    service.join();

    ASSERT_TRUE(state->savedDocument.has_value());
    const auto& saved = state->savedDocument->cameraProfiles;
    EXPECT_FALSE(std::any_of(saved.profiles.begin(), saved.profiles.end(),
        [](const auto& profile) {
            return profile.identity.serial == "NEW-CAMERA";
        }));
    const auto updated = std::find_if(saved.profiles.begin(), saved.profiles.end(),
        [](const auto& profile) { return profile.identity.serial == "LOADED-1"; });
    ASSERT_NE(updated, saved.profiles.end());
    EXPECT_EQ(updated->requested.requestedFps, 31.0);
    ASSERT_TRUE(saved.lastSelectedCameraId.has_value());
    EXPECT_EQ(saved.lastSelectedCameraId->value, "loaded-logical-0");

    const auto status = service.latestStatus();
    EXPECT_FALSE(status->latestSavedRevision.has_value());
    EXPECT_EQ(status->latestSavedPresetRevision, 1U);
    ASSERT_TRUE(status->warning.has_value());
    EXPECT_EQ(status->warning->code, "startup_save_capacity_reached");
}

TEST(StartupPreferencesService, CoalescedABAUsesRevisionOrderBehindBlockedSave) {
    auto state = std::make_shared<IoState>();
    state->blockFirstSave = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseSaveOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(service.postPresetSave(1U, standardPresets()).hasValue());
    ASSERT_TRUE(release.wait());
    auto cameraA = preferences();
    cameraA.cameraId = {"shared-logical"};
    cameraA.identity.serial = "SERIAL-A";
    auto cameraB = preferences();
    cameraB.cameraId = {"shared-logical"};
    cameraB.identity.serial = "SERIAL-B";
    ASSERT_TRUE(service.postSave(1U, cameraA).hasValue());
    ASSERT_TRUE(service.postSave(2U, cameraB).hasValue());
    cameraA.identity.transport = "newest-a-transport";
    ASSERT_TRUE(service.postSave(3U, cameraA).hasValue());
    service.requestStop();
    release.release();
    service.join();

    ASSERT_EQ(state->savedDocuments.size(), 2U);
    const auto& document = state->savedDocuments.back();
    const auto& saved = document.cameraProfiles.profiles;
    const auto a = std::find_if(saved.begin(), saved.end(), [](const auto& profile) {
        return profile.identity.serial == "SERIAL-A";
    });
    const auto b = std::find_if(saved.begin(), saved.end(), [](const auto& profile) {
        return profile.identity.serial == "SERIAL-B";
    });
    ASSERT_NE(a, saved.end());
    ASSERT_NE(b, saved.end());
    EXPECT_LT(std::distance(saved.begin(), b), std::distance(saved.begin(), a));
    EXPECT_EQ(a->identity.transport, "newest-a-transport");
    const auto encoded = ConfigurationCodec::encode(document);
    ASSERT_TRUE(encoded.hasValue());
    const auto decoded = ConfigurationCodec::decode(encoded.value());
    ASSERT_TRUE(decoded.hasValue());
    ASSERT_TRUE(decoded.value().startup.has_value());
    EXPECT_EQ(decoded.value().startup->identity.serial, "SERIAL-A");
}

TEST(StartupPreferencesService, PublishesInitialPresetsWithoutAnySaveOrCameraIntent) {
    auto state = std::make_shared<IoState>();
    state->recoveredPresets = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    EXPECT_FALSE(service.latestStatus()->loadedPresets.has_value());
    ASSERT_TRUE(service.start().hasValue());
    service.requestStop();
    service.join();
    const auto status = service.latestStatus();
    ASSERT_TRUE(status->loadCompleted);
    ASSERT_TRUE(status->loadedPresets.has_value());
    EXPECT_EQ(status->loadedPresets->selectedId.value, "saved-user");
    ASSERT_EQ(status->loadedPresets->customPresets.size(), 1U);
    EXPECT_EQ(status->loadedPresets->customPresets.front().revision, 23U);
    EXPECT_FALSE(status->latestAttemptedPresetSaveRevision.has_value());
    EXPECT_FALSE(status->latestSavedPresetRevision.has_value());
    EXPECT_TRUE(state->savedDocuments.empty());
}

TEST(StartupPreferencesService, MergesPendingSectionsAndKeepsInitialLoadMetadata) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    state->recoveredPresets = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    EXPECT_FALSE(service.postPresetSave(1U, standardPresets()).hasValue());
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());
    auto camera = preferences();
    camera.identity.serial = "NEW-CAMERA";
    bool cameraAccepted = false;
    bool presetsAccepted = false;
    {
        std::jthread cameraCaller([&] {
            cameraAccepted = service.postSave(100U, camera).hasValue();
        });
        std::jthread presetCaller([&] {
            presetsAccepted = service.postPresetSave(1U, standardPresets()).hasValue();
        });
    }
    ASSERT_TRUE(cameraAccepted);
    ASSERT_TRUE(presetsAccepted);
    EXPECT_FALSE(service.latestStatus()->latestAttemptedPresetSaveRevision.has_value());
    service.requestStop();
    EXPECT_FALSE(service.postPresetSave(2U, standardPresets()).hasValue());
    release.release();
    service.join();
    ASSERT_EQ(state->savedDocuments.size(), 1U);
    const auto& saved = state->savedDocuments.front();
    EXPECT_EQ(saved.startup->identity.serial, "NEW-CAMERA");
    EXPECT_EQ(saved.presets.selectedId.value, "standard");
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        saved.presets.activePipeline, processing::standardPipeline()));
    EXPECT_EQ(saved.application.value("theme"), "retained-theme");
    EXPECT_EQ(saved.legacyPresets.value("old-selection"), "keep");
    ASSERT_EQ(saved.presetIssues.size(), 1U);
    EXPECT_FALSE(saved.usedDefaults);
    ASSERT_TRUE(saved.loadWarning.has_value());
    EXPECT_EQ(saved.loadWarning->code, "preset_entries_recovered");
    EXPECT_EQ(state->saveThreads.front(), state->loadThread);
    EXPECT_NE(state->loadThread, std::this_thread::get_id());
    const auto status = service.latestStatus();
    EXPECT_EQ(status->latestAttemptedSaveRevision, 100U);
    EXPECT_EQ(status->latestSavedRevision, 100U);
    EXPECT_EQ(status->latestAttemptedPresetSaveRevision, 1U);
    EXPECT_EQ(status->latestSavedPresetRevision, 1U);
    ASSERT_TRUE(status->loadedPreferences.has_value());
    EXPECT_EQ(status->loadedPreferences->identity.serial, "SIM-1");
    ASSERT_TRUE(status->loadedPresets.has_value());
    EXPECT_EQ(status->loadedPresets->selectedId.value, "saved-user");
    ASSERT_TRUE(status->warning.has_value());
    EXPECT_EQ(status->warning->code, "preset_entries_recovered");
}

TEST(StartupPreferencesService, PresetValidationDoesNotConsumeIndependentRevision) {
    auto state = std::make_shared<IoState>();
    state->blockLoad = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseLoadOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(release.wait());
    EXPECT_FALSE(service.postPresetSave(0U, standardPresets()).hasValue());
    auto invalid = standardPresets();
    invalid.selectedId = {"missing"};
    EXPECT_FALSE(service.postPresetSave(1U, invalid).hasValue());
    invalid = standardPresets();
    invalid.selectedId = {"original"}; // Named selection must match the active definition.
    EXPECT_FALSE(service.postPresetSave(1U, invalid).hasValue());
    invalid = standardPresets();
    invalid.customPresets.push_back({{"standard"}, "Reserved", "", false, 1U, 1U,
        processing::standardPipeline()});
    EXPECT_FALSE(service.postPresetSave(1U, invalid).hasValue());
    ASSERT_TRUE(service.postPresetSave(1U, standardPresets()).hasValue());
    EXPECT_FALSE(service.postPresetSave(1U, standardPresets()).hasValue());
    ASSERT_TRUE(service.postPresetSave(2U, application::PresetState{}).hasValue());
    EXPECT_FALSE(service.postPresetSave(1U, standardPresets()).hasValue());
    ASSERT_TRUE(service.postSave(1U, preferences()).hasValue());
    service.requestStop();
    release.release();
    service.join();
    ASSERT_EQ(state->savedDocuments.size(), 1U);
    EXPECT_EQ(state->savedDocuments.front().presets.selectedId.value, "original");
    EXPECT_EQ(service.latestStatus()->latestSavedRevision, 1U);
    EXPECT_EQ(service.latestStatus()->latestSavedPresetRevision, 2U);
}

TEST(StartupPreferencesService, CoalescesBothSectionsBehindAnExecutingPresetSaveAndDrains) {
    auto state = std::make_shared<IoState>();
    state->blockFirstSave = true;
    StartupPreferencesService service(std::make_unique<RecordingIo>(state));
    ReleaseSaveOnExit release{state};
    ASSERT_TRUE(service.start().hasValue());
    ASSERT_TRUE(service.postPresetSave(1U, standardPresets()).hasValue());
    ASSERT_TRUE(release.wait());
    const auto inFlight = service.latestStatus();
    EXPECT_EQ(inFlight->latestAttemptedPresetSaveRevision, 1U);
    EXPECT_FALSE(inFlight->latestSavedPresetRevision.has_value());
    EXPECT_FALSE(inFlight->latestAttemptedSaveRevision.has_value());
    auto camera = preferences();
    camera.identity.serial = "SUPERSEDED";
    ASSERT_TRUE(service.postSave(1U, camera).hasValue());
    ASSERT_TRUE(service.postPresetSave(2U, standardPresets()).hasValue());
    camera.identity.serial = "NEWEST";
    ASSERT_TRUE(service.postSave(2U, camera).hasValue());
    ASSERT_TRUE(service.postPresetSave(3U, application::PresetState{}).hasValue());
    service.requestStop();
    release.release();
    service.join();
    ASSERT_EQ(state->savedDocuments.size(), 2U);
    EXPECT_EQ(state->savedDocuments[0].startup->identity.serial, "SIM-1");
    EXPECT_EQ(state->savedDocuments[0].presets.selectedId.value, "standard");
    EXPECT_EQ(state->savedDocuments[1].startup->identity.serial, "NEWEST");
    EXPECT_EQ(state->savedDocuments[1].presets.selectedId.value, "original");
    EXPECT_EQ(state->saveThreads[0], state->saveThreads[1]);
    const auto status = service.latestStatus();
    EXPECT_EQ(status->latestSavedRevision, 2U);
    EXPECT_EQ(status->latestSavedPresetRevision, 3U);
    EXPECT_EQ(status->latestAttemptedSaveRevision, 2U);
    EXPECT_EQ(status->latestAttemptedPresetSaveRevision, 3U);
    EXPECT_FALSE(inFlight->latestSavedPresetRevision.has_value());
}

TEST(StartupPreferencesService, UnsafeLoadSettlesBothAcceptedSectionsWithoutWriting) {
    for (const bool failedLoad : {false, true}) {
        auto state = std::make_shared<IoState>();
        state->blockLoad = true;
        state->failLoad = failedLoad;
        state->unpreservedLoad = !failedLoad;
        StartupPreferencesService service(std::make_unique<RecordingIo>(state));
        ReleaseLoadOnExit release{state};
        ASSERT_TRUE(service.start().hasValue());
        ASSERT_TRUE(release.wait());
        ASSERT_TRUE(service.postSave(8U, preferences()).hasValue());
        ASSERT_TRUE(service.postPresetSave(9U, standardPresets()).hasValue());
        release.release();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!service.latestStatus()->latestAttemptedPresetSaveRevision.has_value()
            && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        EXPECT_FALSE(service.postPresetSave(10U, standardPresets()).hasValue());
        service.requestStop();
        service.join();
        const auto status = service.latestStatus();
        EXPECT_EQ(status->latestAttemptedSaveRevision, 8U);
        EXPECT_EQ(status->latestAttemptedPresetSaveRevision, 9U);
        EXPECT_FALSE(status->latestSavedRevision.has_value());
        EXPECT_FALSE(status->latestSavedPresetRevision.has_value());
        EXPECT_TRUE(state->savedDocuments.empty());
        ASSERT_TRUE(status->warning.has_value());
        EXPECT_EQ(status->warning->code, "startup_save_source_unsafe");
        if (failedLoad) { EXPECT_FALSE(status->loadedPresets.has_value()); }
    }
}

TEST(StartupPreferencesService, FailedCombinedWriteDoesNotClaimEitherSectionDurable) {
    for (const int exceptionKind : {0, 1, 2}) {
        auto state = std::make_shared<IoState>();
        state->blockLoad = true;
        state->failSave = exceptionKind == 0;
        state->saveException = exceptionKind;
        StartupPreferencesService service(std::make_unique<RecordingIo>(state));
        ReleaseLoadOnExit release{state};
        ASSERT_TRUE(service.start().hasValue());
        ASSERT_TRUE(release.wait());
        ASSERT_TRUE(service.postSave(5U, preferences()).hasValue());
        ASSERT_TRUE(service.postPresetSave(6U, standardPresets()).hasValue());
        service.requestStop();
        release.release();
        service.join();
        ASSERT_EQ(state->savedDocuments.size(), 1U);
        const auto status = service.latestStatus();
        EXPECT_EQ(status->latestAttemptedSaveRevision, 5U);
        EXPECT_EQ(status->latestAttemptedPresetSaveRevision, 6U);
        EXPECT_FALSE(status->latestSavedRevision.has_value());
        EXPECT_FALSE(status->latestSavedPresetRevision.has_value());
        ASSERT_TRUE(status->warning.has_value());
        EXPECT_EQ(status->warning->code, exceptionKind == 0
            ? "scripted_save_failure" : "startup_service_worker_exception");
    }
}

TEST(StartupPreferencesService, LaterWholeDocumentSuccessSettlesPreviouslyFailedSection) {
    for (const bool presetFirst : {false, true}) {
        auto state = std::make_shared<IoState>();
        state->failSave = true;
        StartupPreferencesService service(std::make_unique<RecordingIo>(state));
        ASSERT_TRUE(service.start().hasValue());
        auto camera = preferences();
        camera.identity.serial = "RETAINED-CHANGE";
        ASSERT_TRUE((presetFirst ? service.postPresetSave(7U, standardPresets())
                                 : service.postSave(8U, camera)).hasValue());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while ((!service.latestStatus()->warning
                || service.latestStatus()->warning->code != "scripted_save_failure")
            && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        const auto failed = service.latestStatus();
        ASSERT_TRUE(failed->warning.has_value());
        ASSERT_EQ(failed->warning->code, "scripted_save_failure");
        EXPECT_FALSE(failed->latestSavedPresetRevision.has_value());
        EXPECT_FALSE(failed->latestSavedRevision.has_value());
        {
            std::lock_guard lock(state->mutex);
            state->failSave = false;
        }
        ASSERT_TRUE((presetFirst ? service.postSave(8U, camera)
                                 : service.postPresetSave(7U, standardPresets())).hasValue());
        service.requestStop();
        service.join();
        ASSERT_EQ(state->savedDocuments.size(), 2U);
        const auto& saved = state->savedDocuments.back();
        EXPECT_EQ(saved.startup->identity.serial, "RETAINED-CHANGE");
        EXPECT_EQ(saved.presets.selectedId.value, "standard");
        const auto status = service.latestStatus();
        EXPECT_EQ(status->latestAttemptedSaveRevision, 8U);
        EXPECT_EQ(status->latestSavedRevision, 8U);
        EXPECT_EQ(status->latestAttemptedPresetSaveRevision, 7U);
        EXPECT_EQ(status->latestSavedPresetRevision, 7U);
        EXPECT_FALSE(status->warning.has_value());
        EXPECT_FALSE(failed->latestSavedPresetRevision.has_value());
        EXPECT_FALSE(failed->latestSavedRevision.has_value());
    }
}

TEST(StartupPreferencesService, LoadExceptionSettlesBothPendingSections) {
    for (const int exceptionKind : {1, 2}) {
        auto state = std::make_shared<IoState>();
        state->blockLoad = true;
        state->loadException = exceptionKind;
        StartupPreferencesService service(std::make_unique<RecordingIo>(state));
        ReleaseLoadOnExit release{state};
        ASSERT_TRUE(service.start().hasValue());
        ASSERT_TRUE(release.wait());
        ASSERT_TRUE(service.postSave(3U, preferences()).hasValue());
        ASSERT_TRUE(service.postPresetSave(4U, standardPresets()).hasValue());
        service.requestStop();
        release.release();
        service.join();
        const auto status = service.latestStatus();
        EXPECT_EQ(status->latestAttemptedSaveRevision, 3U);
        EXPECT_EQ(status->latestAttemptedPresetSaveRevision, 4U);
        EXPECT_FALSE(status->latestSavedPresetRevision.has_value());
        EXPECT_FALSE(status->loadedPresets.has_value());
        EXPECT_TRUE(state->savedDocuments.empty());
        ASSERT_TRUE(status->warning.has_value());
        EXPECT_EQ(status->warning->code, "startup_service_worker_exception");
    }
}

}  // namespace
}  // namespace lumora::configuration
