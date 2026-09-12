#include <lumora/application/InstallationProfiles.hpp>

#include <algorithm>
#include <cctype>

namespace lumora::application {
namespace {
core::Error invalid(std::string code, std::string summary) {
    return {core::ErrorCategory::Configuration, std::move(code), std::move(summary),
        "Review the machine installation profile in administrator installation mode.", true};
}
bool blank(const std::string& text) {
    return text.empty() || std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
}
}

core::Result<void> validateInstallationProfile(const InstallationCameraProfile& profile) {
    if (profile.recordVersion != 1 || profile.capabilityFingerprintVersion != 1
        || profile.revision == 0 || !profile.confirmed
        || blank(profile.identity.manufacturer) || blank(profile.identity.model)
        || blank(profile.identity.serial)) {
        return core::Result<void>::failure(invalid("installation_invalid_record",
            "The installation profile requires a valid identity, version, revision and confirmation."));
    }
    switch (profile.orientation.rotation) {
    case core::Rotation::Degrees0:
    case core::Rotation::Degrees90:
    case core::Rotation::Degrees180:
    case core::Rotation::Degrees270: break;
    default:
        return core::Result<void>::failure(invalid("installation_invalid_orientation",
            "The installation rotation is invalid."));
    }
    const auto capabilities = validateCameraCapabilities(profile.capabilities);
    if (!capabilities.hasValue()) {
        return core::Result<void>::failure(invalid("installation_invalid_capabilities",
            "The installation profile contains invalid camera capabilities."));
    }
    return core::Result<void>::success();
}

core::Result<void> validateInstallationProfiles(
    const std::vector<InstallationCameraProfile>& profiles) {
    if (profiles.size() > MaximumInstallationProfiles) {
        return core::Result<void>::failure(invalid("installation_invalid_capacity",
            "The installation profile collection exceeds its capacity."));
    }
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        const auto valid = validateInstallationProfile(profiles[i]);
        if (!valid.hasValue()) return valid;
        for (std::size_t j = 0; j < i; ++j) {
            if (cameraIdentityKeysEqual(profiles[i].identity, profiles[j].identity)) {
                return core::Result<void>::failure(invalid("installation_invalid_duplicate",
                    "The installation file contains duplicate camera identities."));
            }
        }
    }
    return core::Result<void>::success();
}

const InstallationCameraProfile* findInstallationProfile(
    const std::vector<InstallationCameraProfile>& profiles,
    const core::CameraIdentity& identity) {
    const auto found = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return cameraIdentityKeysEqual(profile.identity, identity);
    });
    return found == profiles.end() ? nullptr : &*found;
}

core::Result<std::optional<InstallationCameraProfile>> resolveInstallationProfile(
    const InstallationProfilesSnapshot& snapshot, const core::CameraIdentity& identity,
    const camera::CameraCapabilities& capabilities) {
    using Resolution = core::Result<std::optional<InstallationCameraProfile>>;
    if (!snapshot.loadCompleted || snapshot.savePending) {
        return Resolution::failure(invalid("installation_pending",
            "Wait for the installation profile operation to finish."));
    }
    if (snapshot.loadError) return Resolution::failure(*snapshot.loadError);
    const auto valid = validateInstallationProfiles(snapshot.profiles);
    if (!valid.hasValue()) return Resolution::failure(valid.error());
    if (blank(identity.manufacturer) || blank(identity.model) || blank(identity.serial)
        || !validateCameraCapabilities(capabilities).hasValue()) {
        return Resolution::failure(invalid("installation_camera_invalid",
            "The discovered camera identity or capabilities are invalid."));
    }
    const auto* profile = findInstallationProfile(snapshot.profiles, identity);
    if (!profile) {
        if (snapshot.policy == InstallationProfilePolicy::SimulatorIdentityFallback) {
            return Resolution::success(std::nullopt);
        }
        return Resolution::failure(invalid("installation_profile_required",
            "An administrator must save an installation profile for this camera."));
    }
    if (!cameraCapabilitiesEqual(profile->capabilities, capabilities)) {
        return Resolution::failure(invalid("installation_capabilities_changed",
            "Camera capabilities changed. Review and save the installation profile again."));
    }
    return Resolution::success(*profile);
}

InstallationProfileReference installationProfileReference(const InstallationCameraProfile& profile) {
    return {profile.recordVersion, profile.revision, profile.orientation};
}
}  // namespace lumora::application
