#pragma once

#include <lumora/application/InstallationProfiles.hpp>
#include <filesystem>
#include <vector>

namespace lumora::configuration {

class IInstallationProfilesIo {
public:
    virtual ~IInstallationProfilesIo() = default;
    [[nodiscard]] virtual core::Result<std::vector<application::InstallationCameraProfile>> load() = 0;
    // Must lock, reread, validate expected revision, preserve other records and atomically save.
    [[nodiscard]] virtual core::Result<application::InstallationCameraProfile> save(
        application::InstallationCameraProfile profile, bool repairInvalid) = 0;
};

class InstallationProfileStore final : public IInstallationProfilesIo {
public:
    // Explicit injected path/authority for composition and temporary-directory tests.
    // Windows production supplies the application-owned directory root so every
    // directory below it is protected without changing unrelated ancestors.
    InstallationProfileStore(std::filesystem::path path, bool administratorMode,
        bool enforceMachinePermissions = false,
        std::filesystem::path protectedDirectoryRoot = {});
    [[nodiscard]] core::Result<std::vector<application::InstallationCameraProfile>> load() override;
    [[nodiscard]] core::Result<application::InstallationCameraProfile> save(
        application::InstallationCameraProfile profile, bool repairInvalid = false) override;
private:
    std::filesystem::path path_;
    bool administratorMode_;
    bool enforceMachinePermissions_;
    std::filesystem::path protectedDirectoryRoot_;
};

// Pure path adapter; never reads or creates the injected directory.
[[nodiscard]] std::filesystem::path installationProfilesPathUnderProgramData(
    const std::filesystem::path& programDataDirectory);
[[nodiscard]] core::Result<std::filesystem::path> machineInstallationProfilesPath();
[[nodiscard]] bool hasInstallationAdministratorAuthority() noexcept;

}  // namespace lumora::configuration
