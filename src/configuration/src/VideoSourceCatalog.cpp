#include <lumora/configuration/VideoSourceCatalog.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <set>

namespace lumora::configuration {
namespace {
core::Error catalogError(std::string code, std::string message) {
    // No endpoint or backend error string enters an operator message/log.
    return {core::ErrorCategory::Configuration, std::move(code), std::move(message), {}, true};
}
core::Result<void> invalid(std::string message) {
    return core::Result<void>::failure(catalogError("video_source_invalid", std::move(message)));
}
constexpr qint64 MaximumFileBytes = 1024 * 1024;
}
core::Result<void> validateVideoSources(const std::vector<VideoSourceDefinition>& sources) {
    if (sources.size() > 32) return invalid("At most 32 network sources can be saved.");
    std::set<std::string> ids;
    std::set<QString> addresses;
    for (const auto& source : sources) {
        const auto id = QString::fromStdString(source.id);
        const QUuid uuid(id);
        if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != id || !ids.insert(source.id).second)
            return invalid("Each network source needs a unique valid identity.");
        const auto name = QString::fromStdString(source.name);
        if (name.trimmed().isEmpty() || name.size() > 128
            || std::any_of(name.begin(), name.end(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; }))
            return invalid("Enter a source name of 1 to 128 characters.");
        const auto text = QString::fromStdString(source.url);
        const QUrl url(text, QUrl::StrictMode);
        if (text.size() > 4096 || text != text.trimmed() || !url.isValid()
            || (url.scheme() != "rtsp" && url.scheme() != "rtsps") || url.host().isEmpty()
            || url.port(-1) == 0 || url.hasFragment())
            return invalid("Enter a valid rtsp:// or rtsps:// address with a camera hostname.");
        if (!url.userInfo().isEmpty() || text.contains('@'))
            return invalid("Enter camera credentials in the separate username and password fields.");
        static const std::set<QString> credentialKeys{"user", "username", "pass", "password", "pwd",
            "token", "access_token", "auth", "authorization", "api_key", "apikey", "key"};
        const auto parameters = QUrlQuery(url).queryItems(QUrl::FullyDecoded);
        if (std::any_of(parameters.begin(), parameters.end(), [](const auto& parameter) {
                return credentialKeys.contains(parameter.first.toLower());
            })) return invalid("Keep credentials out of the address. Use the username and password fields for session authentication.");
        if (!addresses.insert(url.toString(QUrl::FullyEncoded)).second)
            return invalid("This network address has already been added.");
    }
    return core::Result<void>::success();
}
VideoSourceCatalog::VideoSourceCatalog(QString path) : path_(std::move(path)) {}
QString VideoSourceCatalog::defaultPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("video-sources.json");
}
core::Result<std::vector<VideoSourceDefinition>> VideoSourceCatalog::load() const {
    using Result = core::Result<std::vector<VideoSourceDefinition>>;
    QFile file(path_);
    if (!file.exists()) return Result::success({});
    if (!file.open(QIODevice::ReadOnly)) return Result::failure(catalogError(
        "video_sources_unreadable", "Saved video sources could not be read. The catalog was left unchanged."));
    const auto bytes = file.read(MaximumFileBytes + 1);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    const auto root = document.object();
    if (bytes.size() > MaximumFileBytes || parseError.error != QJsonParseError::NoError
        || !document.isObject() || root.value("schemaVersion").toDouble(-1) != 1.0
        || !root.value("sources").isArray() || root.size() != 2)
        return Result::failure(catalogError("video_sources_invalid",
            "Saved video sources are invalid or from a newer version. The catalog was left unchanged."));
    std::vector<VideoSourceDefinition> sources;
    for (const auto& value : root.value("sources").toArray()) {
        const auto item = value.toObject();
        if (!value.isObject() || item.size() != 3 || !item.value("id").isString()
            || !item.value("name").isString() || !item.value("url").isString())
            return Result::failure(catalogError("video_sources_invalid", "Saved video sources are invalid. The catalog was left unchanged."));
        sources.push_back({item.value("id").toString().toStdString(),
            item.value("name").toString().toStdString(), item.value("url").toString().toStdString()});
    }
    const auto valid = validateVideoSources(sources);
    if (!valid.hasValue()) return Result::failure(valid.error());
    return Result::success(std::move(sources));
}
core::Result<void> VideoSourceCatalog::save(const std::vector<VideoSourceDefinition>& sources) const {
    const auto valid = validateVideoSources(sources);
    if (!valid.hasValue()) return valid;
    const auto directory = QFileInfo(path_).absolutePath();
    if (!QDir().mkpath(directory)) return core::Result<void>::failure(catalogError(
        "video_sources_save_failed", "The video source directory could not be created."));
    QJsonArray entries;
    for (const auto& source : sources) entries.append(QJsonObject{
        {"id", QString::fromStdString(source.id)}, {"name", QString::fromStdString(source.name)},
        {"url", QString::fromStdString(source.url)}});
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"sources", entries}}).toJson();
    QSaveFile file(path_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return core::Result<void>::failure(catalogError("video_sources_save_failed",
            "Video sources could not be saved. The previous catalog was kept."));
    return core::Result<void>::success();
}
}
