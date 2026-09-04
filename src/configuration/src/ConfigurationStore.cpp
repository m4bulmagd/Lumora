#include <lumora/configuration/ConfigurationStore.hpp>

#include <lumora/configuration/ConfigurationCodec.hpp>
#include <lumora/core/Error.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstddef>
#include <string>
#include <utility>

namespace lumora::configuration {
namespace {

[[nodiscard]] QString toQString(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return QString::fromUtf8(path.native());
#endif
}

[[nodiscard]] std::filesystem::path toPath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    const auto encoded = path.toUtf8();
    return std::filesystem::path(
        std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())));
#endif
}

[[nodiscard]] std::string pathForDiagnostic(const std::filesystem::path& path) {
    return toQString(path).toStdString();
}

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

[[nodiscard]] std::filesystem::path invalidFilePath(
    const std::filesystem::path& configurationPath) {
    const auto timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmsszzz'Z'");
    const auto stem = toQString(configurationPath.stem());
    const auto extension = toQString(configurationPath.extension());
    const auto parent = configurationPath.parent_path();

    for (int discriminator = 0;; ++discriminator) {
        auto fileName = stem + ".invalid-" + timestamp;
        if (discriminator != 0) {
            fileName += "-" + QString::number(discriminator);
        }
        fileName += extension;
        auto candidate = parent / toPath(fileName);
        std::error_code existenceError;
        if (!std::filesystem::exists(candidate, existenceError) || existenceError) {
            return candidate;
        }
    }
}

[[nodiscard]] ApplicationConfiguration defaults() {
    ApplicationConfiguration configuration;
    configuration.usedDefaults = true;
    return configuration;
}

}  // namespace

ConfigurationStore::ConfigurationStore()
    : ConfigurationStore(defaultConfigurationPath()) {}

ConfigurationStore::ConfigurationStore(std::filesystem::path configurationPath)
    : configurationPath_(std::move(configurationPath)) {}

core::Result<ApplicationConfiguration> ConfigurationStore::load() const {
    const auto qPath = toQString(configurationPath_);
    const QFileInfo fileInfo(qPath);
    if (!fileInfo.exists()) {
        return core::Result<ApplicationConfiguration>::success(defaults());
    }

    QFile file(qPath);
    if (!fileInfo.isFile() || !file.open(QIODevice::ReadOnly)) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_read_failed",
            "The configuration file could not be read.",
            "Failed to open '" + pathForDiagnostic(configurationPath_) + "': "
                + file.errorString().toStdString()));
    }
    const auto contents = file.readAll();
    file.close();

    auto decoded = ConfigurationCodec::decode(contents);
    if (decoded.hasValue()) {
        return decoded;
    }

    auto fallback = defaults();
    const auto preservedPath = invalidFilePath(configurationPath_);
    if (QFile::rename(qPath, toQString(preservedPath))) {
        fallback.preservedInvalidFile = preservedPath;
        fallback.loadWarning = configurationError(
            "configuration_invalid_preserved",
            "Invalid configuration was preserved and defaults were loaded.",
            decoded.error().diagnosticDetail + " Preserved as '"
                + pathForDiagnostic(preservedPath) + "'.");
    } else {
        fallback.loadWarning = configurationError(
            "configuration_invalid_preservation_failed",
            "Invalid configuration could not be preserved; defaults were loaded.",
            decoded.error().diagnosticDetail + " Failed to rename '"
                + pathForDiagnostic(configurationPath_) + "'.");
    }
    return core::Result<ApplicationConfiguration>::success(std::move(fallback));
}

core::Result<void> ConfigurationStore::save(
    const ApplicationConfiguration& configuration) const {
    const auto encoded = ConfigurationCodec::encode(configuration);
    if (!encoded.hasValue()) {
        return core::Result<void>::failure(encoded.error());
    }

    const auto parent = configurationPath_.parent_path();
    const auto qParent = parent.empty() ? QDir::currentPath() : toQString(parent);
    const QFileInfo parentInfo(qParent);
    if ((parentInfo.exists() && !parentInfo.isDir()) || !QDir().mkpath(qParent)) {
        return core::Result<void>::failure(configurationError(
            "configuration_directory_unavailable",
            "The configuration directory is unavailable.",
            "Failed to create or access configuration directory '"
                + pathForDiagnostic(parent) + "'."));
    }

    const auto qPath = toQString(configurationPath_);
    if (QFileInfo(qPath).isDir()) {
        return core::Result<void>::failure(configurationError(
            "configuration_replace_failed",
            "The configuration file could not be replaced.",
            "The configuration target is a directory: '"
                + pathForDiagnostic(configurationPath_) + "'."));
    }

    QSaveFile file(qPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return core::Result<void>::failure(configurationError(
            "configuration_write_failed",
            "The configuration file could not be written.",
            "Failed to open a sibling temporary file for '"
                + pathForDiagnostic(configurationPath_)
                + "': " + file.errorString().toStdString()));
    }
    if (file.write(encoded.value()) != encoded.value().size()) {
        const auto detail = file.errorString().toStdString();
        file.cancelWriting();
        return core::Result<void>::failure(configurationError(
            "configuration_write_failed",
            "The configuration file could not be written.",
            "Failed to write the complete temporary configuration: " + detail));
    }
    if (!file.commit()) {
        return core::Result<void>::failure(configurationError(
            "configuration_replace_failed",
            "The configuration file could not be replaced.",
            "Atomic replacement failed for '" + pathForDiagnostic(configurationPath_) + "': "
                + file.errorString().toStdString()));
    }
    return core::Result<void>::success();
}

const std::filesystem::path& ConfigurationStore::path() const noexcept {
    return configurationPath_;
}

std::filesystem::path ConfigurationStore::defaultConfigurationPath() {
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return toPath(QDir(root).filePath("Config/config.json"));
}

}  // namespace lumora::configuration
