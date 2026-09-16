#pragma once

#include <lumora/camera/ICameraProvider.hpp>
#include <lumora/core/Clock.hpp>

#include <memory>
#include <string>
#include <vector>

namespace lumora::camera::media {
inline constexpr int maximumMediaWidth = 1920;
inline constexpr int maximumMediaHeight = 1080;

struct NetworkVideoSource final {
    std::string id;
    std::string name;
    std::string url;
};

// Construct on the Qt application thread before acquisition workers start.
// Discovery, creation and source-list replacement may run on worker threads.
class MediaCameraProvider final : public ICameraProvider {
public:
    explicit MediaCameraProvider(core::IClock& clock);
    ~MediaCameraProvider() override;
    MediaCameraProvider(const MediaCameraProvider&) = delete;
    MediaCameraProvider& operator=(const MediaCameraProvider&) = delete;
    [[nodiscard]] core::Result<void> setNetworkSources(std::vector<NetworkVideoSource> sources);
    [[nodiscard]] core::Result<std::vector<CameraDescriptor>> discover(std::stop_token = {}) override;
    [[nodiscard]] core::Result<std::unique_ptr<ICameraDevice>> create(const CameraId&) override;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
