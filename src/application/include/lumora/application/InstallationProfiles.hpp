#pragma once

#include <lumora/application/CameraProfile.hpp>
#include <lumora/application/StartupPreferences.hpp>
#include <lumora/core/Frame.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace lumora::application {

struct InstallationCameraProfile final {
    std::uint32_t recordVersion{1};
    std::uint64_t revision{0};
    core::CameraIdentity identity;
    std::uint32_t capabilityFingerprintVersion{
        CurrentCameraCapabilityFingerprintVersion};
    camera::CameraCapabilities capabilities;
    core::Orientation orientation{false, false, core::Rotation::Degrees0};
    bool confirmed{false};
};

enum class InstallationProfilePolicy { Required, SimulatorIdentityFallback };

struct InstallationSaveOutcome final {
    std::uint64_t requestId{0};
    std::optional<InstallationCameraProfile> savedProfile;
    std::optional<core::Error> error;
};

struct InstallationProfilesSnapshot final {
    bool loadCompleted{false};
    bool administratorMode{false};
    InstallationProfilePolicy policy{InstallationProfilePolicy::Required};
    std::vector<InstallationCameraProfile> profiles;
    std::optional<core::Error> loadError;
    bool savePending{false};
    std::optional<InstallationSaveOutcome> latestSaveOutcome;
};

class IInstallationProfiles {
public:
    virtual ~IInstallationProfiles() = default;
    [[nodiscard]] virtual std::shared_ptr<const InstallationProfilesSnapshot>
        latestStatus() const = 0;
    [[nodiscard]] virtual core::Result<void> postSave(
        std::uint64_t requestId, InstallationCameraProfile profile,
        bool repairInvalid = false) = 0;
};

inline constexpr std::size_t MaximumInstallationProfiles = 64;
[[nodiscard]] core::Result<void> validateInstallationProfile(
    const InstallationCameraProfile& profile);
[[nodiscard]] core::Result<void> validateInstallationProfiles(
    const std::vector<InstallationCameraProfile>& profiles);
[[nodiscard]] const InstallationCameraProfile* findInstallationProfile(
    const std::vector<InstallationCameraProfile>& profiles,
    const core::CameraIdentity& identity);
// Empty success is permitted only for missing simulator-policy profiles.
[[nodiscard]] core::Result<std::optional<InstallationCameraProfile>> resolveInstallationProfile(
    const InstallationProfilesSnapshot& snapshot, const core::CameraIdentity& identity,
    const camera::CameraCapabilities& capabilities);
[[nodiscard]] InstallationProfileReference installationProfileReference(
    const InstallationCameraProfile& profile);

}  // namespace lumora::application
