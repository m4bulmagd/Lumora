#include <lumora/configuration/InstallationProfilesService.hpp>
#include <lumora/configuration/InstallationProfileCodec.hpp>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace lumora::configuration {
namespace {
application::InstallationCameraProfile profile(std::string serial = "SIM-1") {
    core::SourcePixelFormat format{"Mono8", 0x01080001U, 8U, 255U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt8};
    camera::CameraCapabilities caps{{format},
        {{0U, 0U, 8U, 8U}, {0U, 0U, 640U, 480U}, {1U, 1U, 8U, 8U}},
        {1.0, 60.0, 0.1, false}, {10.0, 10000.0, 1.0, true},
        {camera::ExposureMode::Manual, camera::ExposureMode::Auto},
        {0.0, 24.0, 0.1, true}, {camera::GainMode::Manual, camera::GainMode::Auto}};
    return {1U, 1U, {"Lumora", "Simulator", std::move(serial), "virtual", "1"},
        1U, caps, {true, false, core::Rotation::Degrees90}, true};
}
std::filesystem::path path(const QTemporaryDir& temp) {
#ifdef _WIN32
    return std::filesystem::path(temp.path().toStdWString()) / "installation.json";
#else
    return std::filesystem::path(temp.path().toStdString()) / "installation.json";
#endif
}
TEST(InstallationProfilesTest, RejectsUnconfirmedAndInvalidIdentityCapabilitiesAndRevision) {
    auto value = profile();
    EXPECT_TRUE(application::validateInstallationProfile(value).hasValue());
    value.confirmed = false;
    EXPECT_FALSE(application::validateInstallationProfile(value).hasValue());
    value = profile(); value.revision = 0;
    EXPECT_FALSE(application::validateInstallationProfile(value).hasValue());
    value = profile(); value.capabilities.pixelFormats.push_back(value.capabilities.pixelFormats.front());
    EXPECT_FALSE(application::validateInstallationProfile(value).hasValue());
    value = profile(); value.identity.serial.clear();
    EXPECT_FALSE(application::validateInstallationProfile(value).hasValue());
    value = profile(); value.orientation.rotation = static_cast<core::Rotation>(99);
    EXPECT_FALSE(application::validateInstallationProfile(value).hasValue());
}
TEST(InstallationProfilesTest, MissingSimulatorFallbackNeverHidesInvalidOrMismatchedProfile) {
    application::InstallationProfilesSnapshot snapshot;
    snapshot.loadCompleted = true;
    auto value = profile();
    EXPECT_FALSE(application::resolveInstallationProfile(snapshot, value.identity, value.capabilities).hasValue());
    snapshot.policy = application::InstallationProfilePolicy::SimulatorIdentityFallback;
    auto missing = application::resolveInstallationProfile(snapshot, value.identity, value.capabilities);
    ASSERT_TRUE(missing.hasValue()); EXPECT_FALSE(missing.value().has_value());
    snapshot.loadError = core::Error{core::ErrorCategory::Storage, "bad", "bad", {}, true};
    EXPECT_FALSE(application::resolveInstallationProfile(snapshot, value.identity, value.capabilities).hasValue());
    snapshot.loadError.reset(); snapshot.profiles = {value};
    auto changed = value.capabilities; changed.frameRate.maximum += 1.0;
    EXPECT_FALSE(application::resolveInstallationProfile(snapshot, value.identity, changed).hasValue());
    auto resolved = application::resolveInstallationProfile(snapshot, value.identity, value.capabilities);
    ASSERT_TRUE(resolved.hasValue()); ASSERT_TRUE(resolved.value().has_value());
    EXPECT_EQ(application::installationProfileReference(*resolved.value()).orientation, value.orientation);
}
TEST(InstallationProfilesTest, CollectionRejectsDuplicatesAndCapacityOverflow) {
    EXPECT_FALSE(application::validateInstallationProfiles({profile(), profile()}).hasValue());
    std::vector<application::InstallationCameraProfile> profiles;
    for (int i = 0; i < 65; ++i) profiles.push_back(profile(std::to_string(i)));
    EXPECT_FALSE(application::validateInstallationProfiles(profiles).hasValue());
}
TEST(InstallationProfilesStoreTest, ProgramDataPathIncludesProtectedLumoraAndConfigAncestors) {
    const std::filesystem::path injected("injected-machine-data");
    EXPECT_EQ(installationProfilesPathUnderProgramData(injected),
        injected / "Lumora" / "Config" / "installation-profiles.json");
}
TEST(InstallationProfilesStoreTest, SavesAtomicallyPreservingOtherProfilesAndRejectingStaleRevision) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    InstallationProfileStore store(path(temp), true);
    ASSERT_TRUE(store.save(profile()).hasValue());
    ASSERT_TRUE(store.save(profile("SIM-2")).hasValue());
    auto updated = profile(); updated.revision = 2; updated.orientation.flipVertical = true;
    ASSERT_TRUE(store.save(updated).hasValue());
    EXPECT_FALSE(store.save(updated).hasValue());
    const auto loaded = store.load(); ASSERT_TRUE(loaded.hasValue());
    ASSERT_EQ(loaded.value().size(), 2U);
    EXPECT_EQ(application::findInstallationProfile(loaded.value(), updated.identity)->revision, 2U);
}
TEST(InstallationProfilesStoreTest, BusyCrossProcessLockPreventsAWrite) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    QLockFile lock(temp.path() + "/installation.json.lock"); ASSERT_TRUE(lock.tryLock());
    InstallationProfileStore store(path(temp), true);
    EXPECT_FALSE(store.save(profile()).hasValue());
    EXPECT_FALSE(std::filesystem::exists(path(temp)));
}
TEST(InstallationProfilesStoreTest, ReadOnlyLoadPreservesInvalidBytesAndExplicitRepairBacksUpFirst) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const auto filePath = temp.path() + "/installation.json";
    { QFile file(filePath); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); ASSERT_EQ(file.write("broken data"), 11); }
    InstallationProfileStore store(path(temp), true);
    EXPECT_FALSE(store.load().hasValue());
    EXPECT_FALSE(store.save(profile()).hasValue());
    QFile original(filePath); ASSERT_TRUE(original.open(QIODevice::ReadOnly));
    EXPECT_EQ(original.readAll(), QByteArray("broken data")); original.close();
    ASSERT_TRUE(store.save(profile(), true).hasValue());
    QFile backup(filePath + ".invalid-backup"); ASSERT_TRUE(backup.open(QIODevice::ReadOnly));
    EXPECT_EQ(backup.readAll(), QByteArray("broken data"));
    EXPECT_TRUE(store.load().hasValue());
}
TEST(InstallationProfilesStoreTest, RepairDoesNotOverwriteExistingBackupAndRequiresRevisionOne) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const auto filePath = temp.path() + "/installation.json";
    { QFile f(filePath); ASSERT_TRUE(f.open(QIODevice::WriteOnly)); f.write("invalid"); }
    InstallationProfileStore store(path(temp), true);
    auto p = profile(); p.revision = 2;
    EXPECT_FALSE(store.save(p, true).hasValue());
    { QFile f(filePath + ".invalid-backup"); ASSERT_TRUE(f.open(QIODevice::WriteOnly)); f.write("prior backup"); }
    EXPECT_FALSE(store.save(profile(), true).hasValue());
    QFile original(filePath); ASSERT_TRUE(original.open(QIODevice::ReadOnly)); EXPECT_EQ(original.readAll(), "invalid");
}
TEST(InstallationProfilesStoreTest, OperatorCannotWriteAndReadFailureCannotBeRepaired) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    InstallationProfileStore readOnly(path(temp), false);
    EXPECT_FALSE(readOnly.save(profile()).hasValue());
    EXPECT_FALSE(std::filesystem::exists(path(temp)));
    std::filesystem::create_directory(path(temp));
    InstallationProfileStore admin(path(temp), true);
    EXPECT_FALSE(admin.save(profile(), true).hasValue());
    EXPECT_TRUE(std::filesystem::is_directory(path(temp)));
}
#ifndef _WIN32
TEST(InstallationProfilesStoreTest, MachinePermissionsRejectNonRootOwnedDirectoryBeforeWriting) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    // Even privileged CI uses only this disposable root, never /etc or ProgramData.
    if (::geteuid() == 0) {
        ASSERT_EQ(::chown(path(temp).parent_path().c_str(), 65534, 65534), 0);
    }
    struct stat metadata{};
    ASSERT_EQ(::stat(path(temp).parent_path().c_str(), &metadata), 0);
    ASSERT_NE(metadata.st_uid, 0U);
    InstallationProfileStore store(path(temp), true, true);
    EXPECT_FALSE(store.save(profile()).hasValue());
    EXPECT_FALSE(std::filesystem::exists(path(temp)));
    EXPECT_FALSE(QFile::exists(temp.path() + "/installation.json.lock"));
}
#endif
TEST(InstallationProfilesCodecTest, RoundTripsFullUint64RevisionAndRejectsMalformedRecords) {
    auto p = profile(); p.revision = std::numeric_limits<std::uint64_t>::max();
    auto encoded = InstallationProfileCodec::encode({p}); ASSERT_TRUE(encoded.hasValue());
    auto decoded = InstallationProfileCodec::decode(encoded.value()); ASSERT_TRUE(decoded.hasValue());
    ASSERT_EQ(decoded.value().size(), 1U); EXPECT_EQ(decoded.value()[0].revision, p.revision);
    EXPECT_EQ(decoded.value()[0].orientation, p.orientation);
    auto object = QJsonDocument::fromJson(encoded.value()).object();
    auto records = object.value("profiles").toArray(); auto record = records[0].toObject();
    record.insert("confirmed", "true"); records[0] = record; object.insert("profiles", records);
    EXPECT_FALSE(InstallationProfileCodec::decode(QJsonDocument(object).toJson()).hasValue());
    EXPECT_FALSE(InstallationProfileCodec::decode("{\"schemaVersion\":2,\"profiles\":[]}").hasValue());
}
class BlockingIo final : public IInstallationProfilesIo {
public:
    std::mutex mutex;
    std::condition_variable cv;
    bool entered{false};
    bool release{false};
    int writes{0};
    std::vector<application::InstallationCameraProfile> stored;
    core::Result<std::vector<application::InstallationCameraProfile>> load() override {
        std::lock_guard lock(mutex);
        return core::Result<std::vector<application::InstallationCameraProfile>>::success(stored);
    }
    core::Result<application::InstallationCameraProfile> save(application::InstallationCameraProfile p, bool) override {
        std::unique_lock lock(mutex); entered = true; cv.notify_all();
        cv.wait(lock, [&] { return release; }); ++writes; stored = {p};
        return core::Result<application::InstallationCameraProfile>::success(std::move(p));
    }
};
TEST(InstallationProfilesServiceTest, BoundedWorkerPublishesDurabilityWithoutMutatingPriorSnapshotsAndDrainsStop) {
    auto io = std::make_unique<BlockingIo>(); auto* blocking = io.get();
    InstallationProfilesService service(std::move(io), true, application::InstallationProfilePolicy::Required);
    ASSERT_TRUE(service.start().hasValue());
    auto before = service.latestStatus();
    ASSERT_TRUE(service.postSave(7, profile()).hasValue());
    bool entered;
    { std::unique_lock lock(blocking->mutex);
      entered = blocking->cv.wait_for(lock, std::chrono::seconds(3), [&] { return blocking->entered; }); }
    EXPECT_TRUE(entered);
    EXPECT_FALSE(service.postSave(8, profile("SIM-2")).hasValue());
    EXPECT_TRUE(service.latestStatus()->savePending);
    service.requestStop(); EXPECT_FALSE(service.postSave(9, profile()).hasValue());
    { std::lock_guard lock(blocking->mutex); blocking->release = true; blocking->cv.notify_all(); }
    service.join();
    const auto after = service.latestStatus();
    EXPECT_FALSE(before->latestSaveOutcome.has_value());
    ASSERT_TRUE(after->latestSaveOutcome.has_value());
    EXPECT_EQ(after->latestSaveOutcome->requestId, 7U);
    ASSERT_TRUE(after->latestSaveOutcome->savedProfile.has_value());
    EXPECT_FALSE(after->savePending); EXPECT_EQ(blocking->writes, 1);
}
TEST(InstallationProfilesServiceTest, OperatorAdmissionRejectsWithoutInvokingIo) {
    auto io = std::make_unique<BlockingIo>(); auto* blocking = io.get();
    InstallationProfilesService service(std::move(io), false, application::InstallationProfilePolicy::Required);
    ASSERT_TRUE(service.start().hasValue());
    EXPECT_FALSE(service.postSave(1, profile()).hasValue());
    service.requestStop(); service.join(); EXPECT_EQ(blocking->writes, 0);
}
class FailedSaveRefreshIo final : public IInstallationProfilesIo {
public:
    enum class DiskState { ChangedRevision, Invalid, Unreadable, Unchanged };
    explicit FailedSaveRefreshIo(DiskState state) : state_(state) {}
    core::Result<std::vector<application::InstallationCameraProfile>> load() override {
        using Loaded = core::Result<std::vector<application::InstallationCameraProfile>>;
        if (initialLoad_) { initialLoad_ = false; return Loaded::success({profile()}); }
        if (state_ == DiskState::Invalid || state_ == DiskState::Unreadable) {
            return Loaded::failure({core::ErrorCategory::Storage,
                state_ == DiskState::Invalid ? "installation_invalid_document" : "installation_read_failed",
                "The machine file is unsafe.", {}, true});
        }
        auto durable = profile();
        if (state_ == DiskState::ChangedRevision) durable.revision = 2;
        return Loaded::success({durable});
    }
    core::Result<application::InstallationCameraProfile> save(application::InstallationCameraProfile, bool) override {
        return core::Result<application::InstallationCameraProfile>::failure({core::ErrorCategory::Storage,
            state_ == DiskState::ChangedRevision ? "installation_revision_conflict" : "installation_replace_failed",
            "Save failed.", {}, true});
    }
private:
    DiskState state_;
    bool initialLoad_{true};
};
bool waitForInstallationLoad(const InstallationProfilesService& service) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!service.latestStatus()->loadCompleted && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return service.latestStatus()->loadCompleted;
}
TEST(InstallationProfilesServiceTest, FailedConflictPublishesCurrentRevisionAndInvalidatesPriorBinding) {
    InstallationProfilesService service(std::make_unique<FailedSaveRefreshIo>(
        FailedSaveRefreshIo::DiskState::ChangedRevision), true, application::InstallationProfilePolicy::Required);
    ASSERT_TRUE(service.start().hasValue()); ASSERT_TRUE(waitForInstallationLoad(service));
    const auto before = service.latestStatus(); ASSERT_EQ(before->profiles.size(), 1U);
    const auto activeBinding = application::installationProfileReference(before->profiles.front());
    auto requested = profile(); requested.revision = 2;
    ASSERT_TRUE(service.postSave(1, requested).hasValue()); service.requestStop(); service.join();
    const auto after = service.latestStatus();
    ASSERT_TRUE(after->latestSaveOutcome); ASSERT_TRUE(after->latestSaveOutcome->error);
    EXPECT_EQ(after->latestSaveOutcome->error->code, "installation_revision_conflict");
    EXPECT_FALSE(after->savePending); EXPECT_FALSE(after->loadError);
    auto resolved = application::resolveInstallationProfile(*after, requested.identity, requested.capabilities);
    ASSERT_TRUE(resolved.hasValue()); ASSERT_TRUE(resolved.value());
    EXPECT_EQ(resolved.value()->revision, 2U);
    EXPECT_NE(application::installationProfileReference(*resolved.value()), activeBinding);
    EXPECT_EQ(before->profiles.front().revision, 1U);
}
TEST(InstallationProfilesServiceTest, FailedSavePublishesInvalidOrUnreadableDiskAndBlocksResolution) {
    for (const auto disk : {FailedSaveRefreshIo::DiskState::Invalid, FailedSaveRefreshIo::DiskState::Unreadable}) {
        SCOPED_TRACE(disk == FailedSaveRefreshIo::DiskState::Invalid ? "invalid" : "unreadable");
        InstallationProfilesService service(std::make_unique<FailedSaveRefreshIo>(disk), true,
            application::InstallationProfilePolicy::SimulatorIdentityFallback);
        ASSERT_TRUE(service.start().hasValue()); ASSERT_TRUE(waitForInstallationLoad(service));
        auto requested = profile(); requested.revision = 2;
        ASSERT_TRUE(service.postSave(1, requested).hasValue()); service.requestStop(); service.join();
        const auto after = service.latestStatus();
        ASSERT_TRUE(after->latestSaveOutcome); ASSERT_TRUE(after->latestSaveOutcome->error);
        EXPECT_TRUE(after->loadError); EXPECT_FALSE(after->savePending);
        EXPECT_FALSE(application::resolveInstallationProfile(*after, requested.identity, requested.capabilities).hasValue());
    }
}
TEST(InstallationProfilesServiceTest, FailedReplacementWithUnchangedDiskPreservesUsableActiveBinding) {
    InstallationProfilesService service(std::make_unique<FailedSaveRefreshIo>(
        FailedSaveRefreshIo::DiskState::Unchanged), true, application::InstallationProfilePolicy::Required);
    ASSERT_TRUE(service.start().hasValue()); ASSERT_TRUE(waitForInstallationLoad(service));
    const auto activeBinding = application::installationProfileReference(service.latestStatus()->profiles.front());
    auto requested = profile(); requested.revision = 2;
    ASSERT_TRUE(service.postSave(1, requested).hasValue()); service.requestStop(); service.join();
    const auto after = service.latestStatus();
    ASSERT_TRUE(after->latestSaveOutcome); ASSERT_TRUE(after->latestSaveOutcome->error);
    EXPECT_FALSE(after->loadError); EXPECT_FALSE(after->savePending);
    auto resolved = application::resolveInstallationProfile(*after, requested.identity, requested.capabilities);
    ASSERT_TRUE(resolved.hasValue()); ASSERT_TRUE(resolved.value());
    EXPECT_EQ(application::installationProfileReference(*resolved.value()), activeBinding);
}
}  // namespace
}  // namespace lumora::configuration
