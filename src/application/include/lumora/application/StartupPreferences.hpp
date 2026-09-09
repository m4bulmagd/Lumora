#pragma once

#include <lumora/application/Preset.hpp>
#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/Error.hpp>
#include <lumora/core/Result.hpp>

#include <cstdint>
#include <optional>

namespace lumora::application {

struct StartupPreferences final {
    std::uint32_t recordVersion{1};
    camera::CameraId cameraId;
    core::CameraIdentity identity;
    camera::CameraCapabilities confirmedCapabilities;
    camera::CameraConfiguration requested;
    camera::CameraConfiguration lastApplied;
    bool confirmed{false};
};

struct StartupPreferencesStatus final {
    bool loadCompleted{false};
    // These remain the initial loaded records. Successful saves are represented
    // by the section revisions below; they do not rewrite load provenance.
    std::optional<StartupPreferences> loadedPreferences;
    std::optional<PresetState> loadedPresets;
    std::optional<std::uint64_t> latestAttemptedSaveRevision;
    std::optional<std::uint64_t> latestSavedRevision;
    std::optional<std::uint64_t> latestAttemptedPresetSaveRevision;
    std::optional<std::uint64_t> latestSavedPresetRevision;
    std::optional<core::Error> warning;
};

[[nodiscard]] core::Result<void> validateStartupPreferences(
    const StartupPreferences& preferences);
[[nodiscard]] bool cameraCapabilitiesEqual(
    const camera::CameraCapabilities& left,
    const camera::CameraCapabilities& right);
[[nodiscard]] bool cameraConfigurationsEqual(
    const camera::CameraConfiguration& left,
    const camera::CameraConfiguration& right);
[[nodiscard]] bool isStartupResumeEligible(
    const StartupPreferences& preferences,
    const camera::CameraId& cameraId,
    const core::CameraIdentity& identity,
    const camera::CameraCapabilities& capabilities);

}  // namespace lumora::application
