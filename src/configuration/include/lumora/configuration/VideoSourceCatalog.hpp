#pragma once

#include <lumora/core/Result.hpp>
#include <QString>
#include <string>
#include <vector>

namespace lumora::configuration {
struct VideoSourceDefinition final {
    std::string id;
    std::string name;
    std::string url;
    bool operator==(const VideoSourceDefinition&) const = default;
};
[[nodiscard]] core::Result<void> validateVideoSources(const std::vector<VideoSourceDefinition>& sources);

// Separate atomic catalog: preference-worker saves cannot overwrite source edits.
// Addresses contain no user info; authentication is owned by the UI for one run.
class VideoSourceCatalog final {
public:
    explicit VideoSourceCatalog(QString path = defaultPath());
    [[nodiscard]] static QString defaultPath();
    [[nodiscard]] core::Result<std::vector<VideoSourceDefinition>> load() const;
    [[nodiscard]] core::Result<void> save(const std::vector<VideoSourceDefinition>& sources) const;
private:
    QString path_;
};
}
