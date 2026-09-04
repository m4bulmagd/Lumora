#include <lumora/configuration/ConfigurationCodec.hpp>

#include <lumora/core/Error.hpp>

#include <QJsonDocument>
#include <QJsonParseError>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace lumora::configuration {
namespace {

[[nodiscard]] core::Error configurationError(
    std::string code,
    std::string operatorSummary,
    std::string diagnosticDetail) {
    return core::Error{
        .category = core::ErrorCategory::Configuration,
        .code = std::move(code),
        .operatorSummary = std::move(operatorSummary),
        .diagnosticDetail = std::move(diagnosticDetail),
        .recoverable = true,
    };
}

[[nodiscard]] core::Result<ApplicationConfiguration> decodeObject(const QJsonObject& root) {
    const auto schemaValue = root.value("schemaVersion");
    if (!schemaValue.isDouble()) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_missing_schema",
            "The configuration version is missing or invalid.",
            "The root schemaVersion must be an integer."));
    }

    const auto schemaNumber = schemaValue.toDouble();
    if (!std::isfinite(schemaNumber) || std::floor(schemaNumber) != schemaNumber
        || schemaNumber < static_cast<double>(std::numeric_limits<int>::min())
        || schemaNumber > static_cast<double>(std::numeric_limits<int>::max())) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_schema",
            "The configuration version is invalid.",
            "The root schemaVersion is not a representable integer."));
    }

    const auto schemaVersion = static_cast<int>(schemaNumber);
    if (schemaVersion > ApplicationConfiguration::CurrentSchemaVersion) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_future_schema",
            "This configuration was created by a newer Lumora version.",
            "The stored schemaVersion is newer than the supported schema."));
    }
    if (schemaVersion < ApplicationConfiguration::CurrentSchemaVersion) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_unsupported_schema",
            "This configuration version is not supported.",
            "No migration is available for the stored schemaVersion."));
    }

    constexpr std::array<std::string_view, 6> sectionNames{
        "application", "cameraProfiles", "processing", "presets", "capture", "ui"};
    for (const auto sectionName : sectionNames) {
        const auto section = root.value(
            QString::fromLatin1(sectionName.data(), static_cast<qsizetype>(sectionName.size())));
        if (!section.isObject()) {
            return core::Result<ApplicationConfiguration>::failure(configurationError(
                "configuration_invalid_section",
                "A configuration section is missing or invalid.",
                "The '" + std::string(sectionName) + "' section must be a JSON object."));
        }
    }

    ApplicationConfiguration configuration;
    configuration.schemaVersion = schemaVersion;
    configuration.application = root.value("application").toObject();
    configuration.cameraProfiles = root.value("cameraProfiles").toObject();
    configuration.processing = root.value("processing").toObject();
    configuration.presets = root.value("presets").toObject();
    configuration.capture = root.value("capture").toObject();
    configuration.ui = root.value("ui").toObject();
    return core::Result<ApplicationConfiguration>::success(std::move(configuration));
}

}  // namespace

core::Result<ApplicationConfiguration> ConfigurationCodec::decode(const QByteArray& contents) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_json",
            "The configuration file is not valid JSON.",
            "JSON parse error at byte " + std::to_string(parseError.offset) + ": "
                + parseError.errorString().toStdString()));
    }
    if (!document.isObject()) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_root",
            "The configuration file has an invalid structure.",
            "The JSON document root must be an object."));
    }
    return decodeObject(document.object());
}

core::Result<QByteArray> ConfigurationCodec::encode(
    const ApplicationConfiguration& configuration) {
    QJsonObject root{
        {"schemaVersion", configuration.schemaVersion},
        {"application", configuration.application},
        {"cameraProfiles", configuration.cameraProfiles},
        {"processing", configuration.processing},
        {"presets", configuration.presets},
        {"capture", configuration.capture},
        {"ui", configuration.ui},
    };

    const auto validated = decodeObject(root);
    if (!validated.hasValue()) {
        return core::Result<QByteArray>::failure(validated.error());
    }
    return core::Result<QByteArray>::success(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace lumora::configuration
