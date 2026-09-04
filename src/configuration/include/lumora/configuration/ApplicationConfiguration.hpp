#pragma once

#include <lumora/core/Error.hpp>

#include <QJsonObject>

#include <filesystem>
#include <optional>

namespace lumora::configuration {

struct ApplicationConfiguration final {
    static constexpr int CurrentSchemaVersion = 1;

    int schemaVersion{CurrentSchemaVersion};
    QJsonObject application;
    QJsonObject cameraProfiles;
    QJsonObject processing;
    QJsonObject presets;
    QJsonObject capture;
    QJsonObject ui;

    // Load metadata is deliberately not serialized.
    bool usedDefaults{false};
    std::optional<core::Error> loadWarning{};
    std::optional<std::filesystem::path> preservedInvalidFile{};
};

}  // namespace lumora::configuration
