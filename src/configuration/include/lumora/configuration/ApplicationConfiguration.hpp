#pragma once

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/core/Error.hpp>

#include <QJsonObject>

#include <filesystem>
#include <optional>

namespace lumora::configuration {

struct ApplicationConfiguration final {
    static constexpr int CurrentSchemaVersion = 2;

    int schemaVersion{CurrentSchemaVersion};
    QJsonObject application;
    QJsonObject cameraProfiles;
    QJsonObject processing;
    QJsonObject presets;
    QJsonObject capture;
    QJsonObject ui;
    std::optional<application::StartupPreferences> startup;

    // Load metadata is deliberately not serialized.
    bool usedDefaults{false};
    std::optional<core::Error> loadWarning{};
    std::optional<std::filesystem::path> preservedInvalidFile{};
};

}  // namespace lumora::configuration
