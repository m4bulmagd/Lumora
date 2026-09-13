#pragma once

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/configuration/PresetIssue.hpp>
#include <lumora/core/Error.hpp>

#include <QJsonObject>

#include <filesystem>
#include <optional>
#include <vector>

namespace lumora::configuration {

struct ApplicationConfiguration final {
    static constexpr int CurrentSchemaVersion = 5;

    int schemaVersion{CurrentSchemaVersion};
    QJsonObject application;
    application::CameraPreferences cameraProfiles;
    QJsonObject legacyCameraProfiles;
    QJsonObject processing;
    application::PresetState presets;
    QJsonObject legacyPresets;
    QJsonObject capture;
    QJsonObject ui;
    std::optional<application::StartupPreferences> startup;

    // Load metadata is deliberately not serialized.
    std::vector<PresetIssue> presetIssues;
    bool usedDefaults{false};
    std::optional<core::Error> loadWarning{};
    std::optional<std::filesystem::path> preservedInvalidFile{};
};

}  // namespace lumora::configuration
