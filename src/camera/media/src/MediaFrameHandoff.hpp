#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/Frame.hpp>

#include <QImage>
#include <QVideoFrame>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>

namespace lumora::camera::media::detail {
struct StreamFacts final {
    int width;
    int height;
    double fps;
};

[[nodiscard]] core::Result<void> validateFacts(StreamFacts facts);
[[nodiscard]] CameraConfiguration configurationFor(StreamFacts facts);
[[nodiscard]] CameraCapabilities capabilitiesFor(StreamFacts facts);

// The event-thread producer retains only one converted image. The acquisition
// worker always copies into its own pool; Qt memory never enters RawFrame.
class MediaFrameHandoff final {
public:
    MediaFrameHandoff(core::IClock& clock, core::CameraIdentity identity, StreamFacts facts,
        std::shared_ptr<std::atomic<std::uint64_t>> sequence = std::make_shared<std::atomic<std::uint64_t>>(1));
    void start();
    void stop();
    void fail(core::Error error);
    void publish(const QVideoFrame& frame);
    [[nodiscard]] core::Result<std::shared_ptr<const core::RawFrame>> retrieve(
        std::chrono::milliseconds timeout, core::BufferPool& pool, std::stop_token stopToken = {});
private:
    struct PendingFrame {
        QImage image;
        std::chrono::steady_clock::time_point receipt;
        std::chrono::system_clock::time_point utc;
        std::uint64_t id;
    };
    core::IClock& clock_;
    core::CameraIdentity identity_;
    StreamFacts facts_;
    std::mutex mutex_;
    std::condition_variable_any changed_;
    bool streaming_{};
    std::shared_ptr<std::atomic<std::uint64_t>> sequence_;
    std::uint64_t lastPublishedId_{};
    std::uint64_t generation_{};
    std::optional<PendingFrame> latest_;
    std::optional<core::Error> error_;
};
}
