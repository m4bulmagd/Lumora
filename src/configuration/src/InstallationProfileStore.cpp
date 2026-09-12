#include <lumora/configuration/InstallationProfileStore.hpp>
#include <lumora/configuration/InstallationProfileCodec.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <optional>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace lumora::configuration {
namespace {
using Profiles = std::vector<application::InstallationCameraProfile>;
constexpr qint64 MaximumDocumentBytes = 4 * 1024 * 1024;
core::Error error(std::string code, std::string summary, std::string detail = {}) {
    return {core::ErrorCategory::Storage, std::move(code), std::move(summary), std::move(detail), true};
}
QString qPath(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return QString::fromUtf8(path.native());
#endif
}
core::Result<std::optional<QByteArray>> readOpenedFile(QFile& file) {
    using Read = core::Result<std::optional<QByteArray>>;
    if (file.size() > MaximumDocumentBytes) {
        return Read::failure(error("installation_read_failed", "The installation file exceeds the supported size."));
    }
    auto bytes = file.read(MaximumDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaximumDocumentBytes) {
        return Read::failure(error("installation_read_failed", "The installation file could not be read completely."));
    }
    return Read::success(std::move(bytes));
}
core::Result<std::optional<QByteArray>> readBytes(const std::filesystem::path& path) {
    using Read = core::Result<std::optional<QByteArray>>;
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory
        || (!ec && status.type() == std::filesystem::file_type::not_found)) {
        return Read::success(std::nullopt);
    }
    if (ec || !std::filesystem::is_regular_file(status)) {
        return Read::failure(error("installation_read_failed",
            "The installation file cannot be read. Check its path and permissions."));
    }
    QFile file(qPath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return Read::failure(error("installation_read_failed", "The installation file cannot be read.",
            file.errorString().toStdString()));
    }
    return readOpenedFile(file);
}

#ifdef _WIN32
class MachineSecurity final {
public:
    MachineSecurity() {
        // Protected DACL: administrators/System write, built-in Users read/execute.
        valid_ = ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",
            SDDL_REVISION_1, &descriptor_, nullptr) != FALSE;
    }
    ~MachineSecurity() { if (descriptor_) LocalFree(descriptor_); }
    bool valid() const { return valid_; }
    PSECURITY_DESCRIPTOR descriptor() const { return descriptor_; }
    bool createProtectedDirectory(const std::filesystem::path& path) const {
        PACL dacl = nullptr; BOOL present = FALSE; BOOL defaulted = FALSE;
        PSID owner = nullptr; BOOL ownerDefaulted = FALSE;
        if (!valid_ || !GetSecurityDescriptorDacl(descriptor_, &present, &dacl, &defaulted) || !present
            || !GetSecurityDescriptorOwner(descriptor_, &owner, &ownerDefaulted)) return false;
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
        if (!CreateDirectoryW(path.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        // Refuse reparse points and hold a handle without delete-sharing while
        // establishing owner/DACL; an operator cannot swap the directory here.
        HANDLE directory = CreateFileW(path.c_str(), READ_CONTROL | WRITE_DAC | WRITE_OWNER,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (directory == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        const bool ordinaryDirectory = GetFileInformationByHandle(directory, &info)
            && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
        const bool protectedDirectory = ordinaryDirectory && SetSecurityInfo(directory, SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            owner, nullptr, dacl, nullptr) == ERROR_SUCCESS;
        CloseHandle(directory);
        return protectedDirectory;
    }
private:
    PSECURITY_DESCRIPTOR descriptor_{nullptr};
    bool valid_{false};
};

core::Result<void> writeWindowsFile(const QString& destination, const QByteArray& bytes, bool replace) {
    MachineSecurity security;
    if (!security.valid()) return core::Result<void>::failure(error("installation_acl_failed", "Machine file permissions could not be prepared."));
    const auto temporary = replace ? destination + "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp" : destination;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.descriptor(), FALSE};
    const auto wide = temporary.toStdWString();
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return core::Result<void>::failure(error("installation_write_failed", "The installation file could not be created."));
    DWORD written = 0;
    const bool complete = WriteFile(file, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == static_cast<DWORD>(bytes.size()) && FlushFileBuffers(file);
    CloseHandle(file);
    if (!complete) {
        DeleteFileW(wide.c_str());
        return core::Result<void>::failure(error("installation_write_failed", "The complete installation file could not be preserved."));
    }
    if (replace && !MoveFileExW(wide.c_str(), destination.toStdWString().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(wide.c_str());
        return core::Result<void>::failure(error("installation_replace_failed", "Atomic installation file replacement failed."));
    }
    return core::Result<void>::success();
}
#endif

core::Error unsafeAuthority() {
    return error("installation_authority_untrusted",
        "The machine installation path is not protected from operator changes. Ask an administrator to correct its owner and permissions.");
}

core::Result<std::vector<std::filesystem::path>> authorityDirectories(
    const std::filesystem::path& path, const std::filesystem::path& protectedRoot) {
    const auto parent = path.parent_path();
    const auto root = protectedRoot.empty() ? parent : protectedRoot;
    const auto relative = parent.lexically_relative(root);
    if (root.empty() || relative.empty() || path.filename().empty()
        || std::any_of(relative.begin(), relative.end(), [](const auto& component) { return component == ".."; })) {
        return core::Result<std::vector<std::filesystem::path>>::failure(unsafeAuthority());
    }
    std::vector<std::filesystem::path> directories{root};
    for (const auto& component : relative) {
        if (component != ".") directories.push_back(directories.back() / component);
    }
    return core::Result<std::vector<std::filesystem::path>>::success(std::move(directories));
}

#ifdef _WIN32
class ReadHandles final {
public:
    ~ReadHandles() { for (auto handle : values) CloseHandle(handle); }
    std::vector<HANDLE> values;
};

bool trustedWindowsObject(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)
        || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
        || ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) return false;
    PSID owner = nullptr; PACL dacl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            &owner, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS) return false;
    const auto administrator = [](PSID sid) {
        return sid && IsValidSid(sid)
            && (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) || IsWellKnownSid(sid, WinLocalSystemSid));
    };
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    bool trusted = administrator(owner) && dacl != nullptr
        && GetSecurityDescriptorControl(descriptor, &control, &revision)
        && (control & SE_DACL_PROTECTED) != 0;
    // Unknown/callback/object ACEs are intentionally rejected. Ordinary allow
    // entries may grant read/execute only; every write-capable principal must
    // be Administrators or System, including inherit-only entries.
    constexpr DWORD readOnly = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE | GENERIC_READ | GENERIC_EXECUTE;
    for (DWORD index = 0; trusted && index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!GetAce(dacl, index, &raw)) { trusted = false; break; }
        const auto* header = static_cast<const ACE_HEADER*>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { trusted = false; break; }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        PSID sid = const_cast<DWORD*>(&ace->SidStart);
        trusted = IsValidSid(sid) && (administrator(sid) || (ace->Mask & ~readOnly) == 0);
    }
    LocalFree(descriptor);
    return trusted;
}

core::Result<std::optional<QByteArray>> readTrustedBytes(
    const std::filesystem::path& path, const std::filesystem::path& protectedRoot) {
    using Read = core::Result<std::optional<QByteArray>>;
    const auto directories = authorityDirectories(path, protectedRoot);
    if (!directories.hasValue()) return Read::failure(directories.error());
    ReadHandles held;
    held.values.reserve(directories.value().size() + 1);
    for (const auto& directory : directories.value()) {
        HANDLE handle = CreateFileW(directory.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return Read::success(std::nullopt);
            return Read::failure(unsafeAuthority());
        }
        held.values.push_back(handle);
        if (!trustedWindowsObject(handle, true)) return Read::failure(unsafeAuthority());
    }
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return Read::success(std::nullopt);
        return Read::failure(unsafeAuthority());
    }
    held.values.push_back(handle);
    if (!trustedWindowsObject(handle, false)) return Read::failure(unsafeAuthority());
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 || size.QuadPart > MaximumDocumentBytes)
        return Read::failure(error("installation_read_failed", "The installation file size is unsupported."));
    QByteArray bytes(static_cast<qsizetype>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
        || read != static_cast<DWORD>(bytes.size()))
        return Read::failure(error("installation_read_failed", "The installation file could not be read completely."));
    return Read::success(std::move(bytes));
}
#else
class ReadHandles final {
public:
    ~ReadHandles() { for (auto descriptor : values) ::close(descriptor); }
    std::vector<int> values;
};

core::Result<std::optional<QByteArray>> readTrustedBytes(
    const std::filesystem::path& path, const std::filesystem::path& protectedRoot) {
    using Read = core::Result<std::optional<QByteArray>>;
    const auto directories = authorityDirectories(path, protectedRoot);
    if (!directories.hasValue()) return Read::failure(directories.error());
    ReadHandles held;
    held.values.reserve(directories.value().size() + 1);
    for (const auto& directory : directories.value()) {
        const int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
        const int descriptor = held.values.empty() ? ::open(directory.c_str(), flags)
            : ::openat(held.values.back(), directory.filename().c_str(), flags);
        if (descriptor < 0) {
            if (errno == ENOENT) return Read::success(std::nullopt);
            return Read::failure(unsafeAuthority());
        }
        held.values.push_back(descriptor);
        struct stat metadata{};
        if (::fstat(descriptor, &metadata) != 0 || !S_ISDIR(metadata.st_mode)
            || !validatePosixInstallationAuthority(metadata.st_uid, metadata.st_mode).hasValue())
            return Read::failure(unsafeAuthority());
    }
    const int descriptor = ::openat(held.values.back(), path.filename().c_str(),
        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        if (errno == ENOENT) return Read::success(std::nullopt);
        return Read::failure(unsafeAuthority());
    }
    held.values.push_back(descriptor);
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode)
        || !validatePosixInstallationAuthority(metadata.st_uid, metadata.st_mode).hasValue())
        return Read::failure(unsafeAuthority());
    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
        return Read::failure(error("installation_read_failed", "The protected installation file cannot be read."));
    return readOpenedFile(file);
}
#endif

core::Result<void> prepareDirectory(const std::filesystem::path& path, bool enforceMachinePermissions,
    const std::filesystem::path& protectedDirectoryRoot) {
    const auto parent = path.parent_path();
    if (parent.empty()) return core::Result<void>::failure(error("installation_path_invalid", "The installation file requires an explicit parent directory."));
    std::error_code ec;
    const auto existing = std::filesystem::symlink_status(parent, ec);
    if (std::filesystem::is_symlink(existing)) {
        return core::Result<void>::failure(error("installation_path_invalid", "The installation directory must not be a symbolic link."));
    }
    if (!enforceMachinePermissions) {
        if (!QDir().mkpath(qPath(parent))) return core::Result<void>::failure(error("installation_directory_failed", "The installation directory is unavailable."));
        return core::Result<void>::success();
    }
#ifdef _WIN32
    const auto root = protectedDirectoryRoot.empty() ? parent : protectedDirectoryRoot;
    const auto relative = parent.lexically_relative(root);
    if (relative.empty() || std::any_of(relative.begin(), relative.end(), [](const auto& component) {
            return component == "..";
        })) return core::Result<void>::failure(error("installation_path_invalid", "The protected directory root must contain the installation path."));
    MachineSecurity security;
    auto directory = root;
    if (!security.createProtectedDirectory(directory)) {
        return core::Result<void>::failure(error("installation_acl_failed", "The application machine directory could not be protected."));
    }
    for (const auto& component : relative) {
        if (component == ".") continue;
        directory /= component;
        if (!security.createProtectedDirectory(directory)) {
            return core::Result<void>::failure(error("installation_acl_failed", "Administrator-write/operator-read directory permissions could not be applied."));
        }
    }
#else
    (void)protectedDirectoryRoot;
    if (!QDir().mkpath(qPath(parent))) return core::Result<void>::failure(error("installation_directory_failed", "The installation directory is unavailable."));
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) return core::Result<void>::failure(error("installation_permissions_failed",
        "The machine directory could not be opened safely."));
    struct stat metadata{};
    const bool ownedByRoot = ::fstat(directory, &metadata) == 0 && metadata.st_uid == 0;
    if (!ownedByRoot) {
        ::close(directory);
        return core::Result<void>::failure(error("installation_owner_invalid",
            "The machine installation directory must be owned by root. Correct its ownership before saving."));
    }
    const bool protectedMode = ::fchmod(directory, 0755) == 0;
    ::close(directory);
    if (!protectedMode) return core::Result<void>::failure(error("installation_permissions_failed", "Machine directory permissions could not be applied."));
#endif
    return core::Result<void>::success();
}

core::Result<void> preserveBackup(const QString& destination, const QByteArray& bytes, bool enforceMachinePermissions) {
#ifdef _WIN32
    if (enforceMachinePermissions) return writeWindowsFile(destination, bytes, false);
#else
    (void)enforceMachinePermissions;
#endif
    QFile backup(destination);
    if (!backup.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return core::Result<void>::failure(error("installation_backup_failed", "Repair requires a new preserved backup; an existing backup will not be overwritten."));
    }
    if (!backup.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther)
        || backup.write(bytes) != bytes.size() || !backup.flush()
#ifndef _WIN32
        || ::fsync(backup.handle()) != 0
#endif
        ) {
        // A partial backup is retained for inspection; original remains untouched.
        return core::Result<void>::failure(error("installation_backup_failed", "The invalid installation file could not be backed up completely."));
    }
    return core::Result<void>::success();
}

core::Result<void> replaceFile(const QString& destination, const QByteArray& bytes, bool enforceMachinePermissions) {
#ifdef _WIN32
    if (enforceMachinePermissions) return writeWindowsFile(destination, bytes, true);
#else
    (void)enforceMachinePermissions;
#endif
    QSaveFile file(destination);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        return core::Result<void>::failure(error("installation_replace_failed", "Atomic installation file replacement failed.", file.errorString().toStdString()));
    }
    return core::Result<void>::success();
}
}  // namespace

InstallationProfileStore::InstallationProfileStore(std::filesystem::path path, bool administratorMode,
    bool enforceMachinePermissions, std::filesystem::path protectedDirectoryRoot)
    : path_(std::move(path)), administratorMode_(administratorMode),
      enforceMachinePermissions_(enforceMachinePermissions),
      protectedDirectoryRoot_(std::move(protectedDirectoryRoot)) {}

core::Result<Profiles> InstallationProfileStore::load() {
    auto bytes = enforceMachinePermissions_ ? readTrustedBytes(path_, protectedDirectoryRoot_) : readBytes(path_);
    if (!bytes.hasValue()) return core::Result<Profiles>::failure(bytes.error());
    if (!bytes.value()) return core::Result<Profiles>::success({});
    return InstallationProfileCodec::decode(*bytes.value());
}

core::Result<application::InstallationCameraProfile> InstallationProfileStore::save(
    application::InstallationCameraProfile profile, bool repairInvalid) {
    using Saved = core::Result<application::InstallationCameraProfile>;
    if (!administratorMode_) return Saved::failure(error("installation_administrator_required", "Launch installation mode deliberately with administrator authority to save."));
    const auto valid = application::validateInstallationProfile(profile);
    if (!valid.hasValue()) return Saved::failure(valid.error());
    if (enforceMachinePermissions_) {
        // Never secure a formerly writable source and thereby bless records
        // an operator could have forged. Unsafe authority needs external admin
        // correction; JSON repair consent does not authorize trusting its data.
        const auto trustedSource = readTrustedBytes(path_, protectedDirectoryRoot_);
        if (!trustedSource.hasValue()) return Saved::failure(trustedSource.error());
    }
    const auto directory = prepareDirectory(path_, enforceMachinePermissions_, protectedDirectoryRoot_);
    if (!directory.hasValue()) return Saved::failure(directory.error());
    QLockFile lock(qPath(path_) + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return Saved::failure(error("installation_lock_busy", "Another installation update is in progress. Retry after it finishes."));
    const auto bytes = enforceMachinePermissions_ ? readTrustedBytes(path_, protectedDirectoryRoot_) : readBytes(path_);
    if (!bytes.hasValue()) return Saved::failure(bytes.error());
    Profiles profiles;
    bool needsBackup = false;
    if (bytes.value()) {
        auto loaded = InstallationProfileCodec::decode(*bytes.value());
        if (!loaded.hasValue()) {
            if (!repairInvalid) return Saved::failure(loaded.error());
            needsBackup = true;
        } else {
            profiles = std::move(loaded).value();
        }
    }
    const auto found = std::find_if(profiles.begin(), profiles.end(), [&](const auto& item) {
        return application::cameraIdentityKeysEqual(item.identity, profile.identity);
    });
    const auto expectedRevision = profile.revision - 1;
    const auto actualRevision = found == profiles.end() ? 0 : found->revision;
    if (actualRevision != expectedRevision) return Saved::failure(error("installation_revision_conflict", "The installation profile changed. Reload and review it before saving again."));
    if (found == profiles.end()) {
        if (profiles.size() >= application::MaximumInstallationProfiles) return Saved::failure(error("installation_capacity_exceeded", "The machine installation collection already contains 64 camera identities."));
        profiles.push_back(profile);
    } else {
        *found = profile;
    }
    const auto encoded = InstallationProfileCodec::encode(profiles);
    if (!encoded.hasValue()) return Saved::failure(encoded.error());
    if (needsBackup) {
        const auto preserved = preserveBackup(qPath(path_) + ".invalid-backup", *bytes.value(), enforceMachinePermissions_);
        if (!preserved.hasValue()) return Saved::failure(preserved.error());
    }
    const auto replaced = replaceFile(qPath(path_), encoded.value(), enforceMachinePermissions_);
    if (!replaced.hasValue()) return Saved::failure(replaced.error());
    return Saved::success(std::move(profile));
}

core::Result<void> validatePosixInstallationAuthority(std::uint64_t ownerUserId, std::uint32_t mode) {
    if (ownerUserId != 0 || (mode & 0022U) != 0) return core::Result<void>::failure(unsafeAuthority());
    return core::Result<void>::success();
}

std::filesystem::path installationProfilesPathUnderProgramData(
    const std::filesystem::path& programDataDirectory) {
    return programDataDirectory / "Lumora" / "Config" / "installation-profiles.json";
}

core::Result<std::filesystem::path> machineInstallationProfilesPath() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &raw);
    if (FAILED(result) || raw == nullptr) {
        if (raw) CoTaskMemFree(raw);
        return core::Result<std::filesystem::path>::failure(error("installation_machine_path_failed", "The machine ProgramData directory is unavailable."));
    }
    const auto path = installationProfilesPathUnderProgramData(std::filesystem::path(raw));
    CoTaskMemFree(raw);
    return core::Result<std::filesystem::path>::success(path);
#else
    return core::Result<std::filesystem::path>::success("/etc/lumora/installation-profiles.json");
#endif
}

bool hasInstallationAdministratorAuthority() noexcept {
#ifdef _WIN32
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{}; DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)
        && elevation.TokenIsElevated != 0;
    CloseHandle(token);
    BYTE administratorsSid[SECURITY_MAX_SID_SIZE]{};
    DWORD sidSize = sizeof(administratorsSid); BOOL administrator = FALSE;
    return elevated
        && CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administratorsSid, &sidSize)
        && CheckTokenMembership(nullptr, administratorsSid, &administrator)
        && administrator != FALSE;
#else
    return ::geteuid() == 0;
#endif
}
}  // namespace lumora::configuration
