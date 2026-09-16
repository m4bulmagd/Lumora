#include <lumora/camera/media/MediaCameraProvider.hpp>
#include <lumora/camera/CameraConfigurationValidator.hpp>
#include "MediaFrameHandoff.hpp"

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPlaybackOptions>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QVideoSink>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace lumora::camera::media {
namespace {
using detail::StreamFacts;
using detail::MediaFrameHandoff;
using namespace std::chrono_literals;

core::Error mediaError(core::ErrorCategory category, std::string code, std::string summary,
    std::string detail, std::optional<std::int64_t> native = {}) {
    return {category, std::move(code), std::move(summary), std::move(detail), true, native};
}
core::Error closedError() {
    return mediaError(core::ErrorCategory::CameraConnection, "camera_not_open",
        "The video source is not open.", "Connect to the source before configuring or starting it.");
}
core::Error backendError(std::int64_t code) {
    // Backend errorString can contain complete URLs, including userinfo and
    // query credentials. Only the numeric native error crosses this boundary.
    return mediaError(core::ErrorCategory::CameraConnection, "media_backend_error",
        "The video source could not be read.", "Qt Multimedia reported a source or decoder error. Check the source and its credentials.", code);
}
CameraId localId(const QCameraDevice& camera) {
    return {"media:local:" + camera.id().toHex().toStdString()};
}
core::CameraIdentity localIdentity(const QCameraDevice& camera) {
    return {"Qt Multimedia", camera.description().toStdString(), camera.id().toHex().toStdString(),
        "Local video / Qt Multimedia", std::nullopt};
}
core::CameraIdentity networkIdentity(const NetworkVideoSource& source) {
    return {"Qt Multimedia", source.name, source.id, "RTSP / Qt Multimedia", std::nullopt};
}

// Only this dedicated thread is ever a target of synchronous dispatch. In
// particular, shutdown never waits for the GUI thread (which may join its
// acquisition worker). All multimedia objects die on their own event thread.
class MediaRuntime final {
public:
    MediaRuntime() : context_(new QObject) {
        context_->moveToThread(&thread_);
        QObject::connect(&thread_, &QThread::finished, context_, &QObject::deleteLater);
        thread_.setObjectName(QStringLiteral("Lumora video sources"));
        thread_.start();
    }
    ~MediaRuntime() { thread_.quit(); thread_.wait(); }
    void own(QObject& object) { object.setParent(context_); }
    template<class F> auto invoke(F&& function) -> std::invoke_result_t<F> {
        using Value = std::invoke_result_t<F>;
        if (QThread::currentThread() == &thread_) return function();
        std::exception_ptr failure;
        if constexpr (std::is_void_v<Value>) {
            const bool invoked = QMetaObject::invokeMethod(context_, [&] { try { function(); } catch (...) { failure = std::current_exception(); } }, Qt::BlockingQueuedConnection);
            if (!invoked) throw std::runtime_error("Media event-thread dispatch failed.");
            if (failure) std::rethrow_exception(failure);
        } else {
            std::optional<Value> result;
            const bool invoked = QMetaObject::invokeMethod(context_, [&] { try { result.emplace(function()); } catch (...) { failure = std::current_exception(); } }, Qt::BlockingQueuedConnection);
            if (failure) std::rethrow_exception(failure);
            if (!invoked || !result) throw std::runtime_error("Media event-thread dispatch failed.");
            return std::move(*result);
        }
    }
private:
    QThread thread_;
    QObject* context_;
};

struct Source final {
    std::optional<QCameraDevice> camera;
    std::optional<NetworkVideoSource> network;
    core::CameraIdentity identity;
};
struct Probe final {
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<StreamFacts> facts;
    std::optional<core::Error> error;
};

class MediaBackend final : public QObject {
public:
    MediaBackend(Source source, std::shared_ptr<Probe> probe)
        : source_(std::move(source)), probe_(std::move(probe)) {}
    void prepare() {
        sink_ = std::make_unique<QVideoSink>();
        if (source_.camera) prepareCamera();
        else preparePlayer();
    }
    void attach(std::shared_ptr<MediaFrameHandoff> handoff) {
        handoff_ = std::move(handoff);
        // Direct delivery applies backpressure to the decoder instead of
        // queuing unbounded QVideoFrame events. The value-only handoff is thread
        // safe and the callback owns it, never touching the backend QObject.
        connect(sink_.get(), &QVideoSink::videoFrameChanged, this,
            [handoff = handoff_](const QVideoFrame& frame) { handoff->publish(frame); },
            Qt::DirectConnection);
    }
    core::Result<void> status() {
        std::lock_guard lock(probe_->mutex);
        if (probe_->error) return core::Result<void>::failure(*probe_->error);
        return core::Result<void>::success();
    }
    core::Result<void> start() {
        if (streaming_) return core::Result<void>::success();
        if (!handoff_) return core::Result<void>::failure(closedError());
        { std::lock_guard lock(probe_->mutex); if (probe_->error) return core::Result<void>::failure(*probe_->error); }
        streaming_ = true;
        handoff_->start();
        if (camera_) camera_->start();
        else player_->play();
        return core::Result<void>::success();
    }
    void stop() {
        const bool wasStreaming = std::exchange(streaming_, false);
        if (handoff_) handoff_->stop();
        if (camera_) camera_->stop();
        // Preserve the live demuxer's position. QMediaPlayer::stop resets to
        // time zero, so a subsequent play can attempt an unsupported RTSP seek.
        // Paused output is discarded by the stopped handoff until explicit Start.
        if (player_ && wasStreaming) player_->pause();
    }
    ~MediaBackend() override {
        stop();
        // Detach outputs first: no signal may reach a sink being destroyed.
        if (session_) { session_->setCamera(nullptr); session_->setVideoSink(nullptr); }
        if (player_) { player_->stop(); player_->setVideoSink(nullptr); player_->setSource({}); }
    }
private:
    void report(core::Error error) {
        { std::lock_guard lock(probe_->mutex); probe_->error = error; }
        probe_->changed.notify_all();
        if (handoff_) handoff_->fail(std::move(error));
    }
    void acceptFacts(StreamFacts facts) {
        auto valid = detail::validateFacts(facts);
        if (!valid.hasValue()) { report(valid.error()); return; }
        bool changed = false;
        {
            std::lock_guard lock(probe_->mutex);
            if (probe_->facts) changed = probe_->facts->width != facts.width || probe_->facts->height != facts.height || std::abs(probe_->facts->fps - facts.fps) > 0.01;
            else probe_->facts = facts;
        }
        if (changed) report(mediaError(core::ErrorCategory::CameraConnection, "media_stream_invalid",
            "The video source changed its configured mode.", "Reconnect and Apply the new source configuration."));
        probe_->changed.notify_all();
    }
    void prepareCamera() {
        // QCameraFormat reports a rate interval, not a measured rate. Admit only
        // a fixed nominal rate; claiming its maximum as actual for a variable
        // interval would fabricate the configuration before capture starts.
        std::optional<QCameraFormat> selected;
        for (const auto& format : source_.camera->videoFormats()) {
            StreamFacts facts{format.resolution().width(), format.resolution().height(), format.maxFrameRate()};
            if (!detail::validateFacts(facts).hasValue() || std::abs(format.minFrameRate() - format.maxFrameRate()) > 0.001) continue;
            if (!selected || format.resolution().width() * format.resolution().height() > selected->resolution().width() * selected->resolution().height()
                || (format.resolution() == selected->resolution() && format.maxFrameRate() > selected->maxFrameRate())) selected = format;
        }
        if (!selected) {
            report(mediaError(core::ErrorCategory::CameraConfiguration, "media_mode_unsupported",
                "The camera has no supported fixed-rate mode.", "A reported fixed nominal frame rate and dimensions no larger than 1920 by 1080 are required."));
            return;
        }
        camera_ = std::make_unique<QCamera>(*source_.camera);
        connect(camera_.get(), &QCamera::errorOccurred, this, [this](QCamera::Error code, const QString&) {
            if (code != QCamera::NoError) report(backendError(static_cast<std::int64_t>(code)));
        });
        connect(camera_.get(), &QCamera::activeChanged, this, [this](bool active) {
            if (!active && streaming_) report(mediaError(core::ErrorCategory::CameraConnection, "media_disconnected",
                "The camera stopped delivering video.", "The local camera became inactive while streaming."));
        });
        camera_->setCameraFormat(*selected);
        const auto actual = camera_->cameraFormat();
        if (actual.isNull() || std::abs(actual.minFrameRate() - actual.maxFrameRate()) > 0.001) {
            report(mediaError(core::ErrorCategory::CameraConfiguration, "media_metadata_missing",
                "The camera did not accept a fixed-rate format.", "Qt did not provide authoritative source configuration."));
            return;
        }
        session_ = std::make_unique<QMediaCaptureSession>();
        session_->setCamera(camera_.get());
        session_->setVideoSink(sink_.get());
        acceptFacts({actual.resolution().width(), actual.resolution().height(), actual.maxFrameRate()});
    }
    void readMetadata() {
        // Stream-specific facts take precedence over file-level metadata.
        auto metadata = player_->metaData();
        const auto tracks = player_->videoTracks();
        const int active = player_->activeVideoTrack();
        if (active >= 0 && active < tracks.size()) metadata = tracks.at(active);
        else if (tracks.size() == 1) metadata = tracks.front();
        const auto size = metadata.value(QMediaMetaData::Resolution).toSize();
        const auto fps = metadata.value(QMediaMetaData::VideoFrameRate).toDouble();
        if (size.isEmpty() || !std::isfinite(fps) || fps <= 0) return;
        acceptFacts({size.width(), size.height(), fps});
    }
    void preparePlayer() {
        player_ = std::make_unique<QMediaPlayer>();
        QPlaybackOptions options;
        options.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
        options.setNetworkTimeout(2000ms);
        player_->setPlaybackOptions(options);
        player_->setVideoSink(sink_.get());
        connect(player_.get(), &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error code, const QString&) {
            if (code != QMediaPlayer::NoError) report(backendError(static_cast<std::int64_t>(code)));
        });
        connect(player_.get(), &QMediaPlayer::metaDataChanged, this, [this] { readMetadata(); });
        connect(player_.get(), &QMediaPlayer::tracksChanged, this, [this] { readMetadata(); });
        connect(player_.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) readMetadata();
            if (status == QMediaPlayer::InvalidMedia) report(backendError(static_cast<std::int64_t>(player_->error())));
            if (status == QMediaPlayer::EndOfMedia && streaming_) report(mediaError(core::ErrorCategory::Acquisition,
                "media_stream_ended", "The network video stream ended.", "Reconnect to restart this source."));
        });
        // Metadata probing only. play() is called exclusively by start().
        player_->setSource(QUrl(QString::fromStdString(source_.network->url), QUrl::StrictMode));
    }
    Source source_;
    std::shared_ptr<Probe> probe_;
    std::shared_ptr<MediaFrameHandoff> handoff_;
    bool streaming_{};
    std::unique_ptr<QVideoSink> sink_;
    std::unique_ptr<QCamera> camera_;
    std::unique_ptr<QMediaCaptureSession> session_;
    std::unique_ptr<QMediaPlayer> player_;
};

class MediaCameraDevice final : public ICameraDevice {
public:
    MediaCameraDevice(core::IClock& clock, std::shared_ptr<MediaRuntime> runtime, Source source)
        : clock_(clock), runtime_(std::move(runtime)), source_(std::move(source)) {}
    ~MediaCameraDevice() override { (void)close(); }
    core::Result<void> open() override {
        if (backend_) return core::Result<void>::success();
        auto probe = std::make_shared<Probe>();
        backend_ = runtime_->invoke([&] {
            auto backend = std::make_unique<MediaBackend>(source_, probe);
            backend->prepare();
            runtime_->own(*backend);
            return backend.release();
        });
        std::unique_lock lock(probe->mutex);
        probe->changed.wait_for(lock, 5s, [&] { return probe->facts || probe->error; });
        if (probe->error || !probe->facts) {
            const auto error = probe->error.value_or(mediaError(core::ErrorCategory::CameraConfiguration,
                "media_metadata_timeout", "The video source did not report its dimensions and frame rate.",
                "Metadata probing timed out after five seconds; playback was not started."));
            lock.unlock(); (void)close();
            return core::Result<void>::failure(error);
        }
        facts_ = *probe->facts; lock.unlock();
        handoff_ = std::make_shared<MediaFrameHandoff>(clock_, source_.identity, *facts_, sequence_);
        runtime_->invoke([&] { backend_->attach(handoff_); });
        return core::Result<void>::success();
    }
    core::Result<CameraCapabilities> capabilities() override {
        if (!facts_) return core::Result<CameraCapabilities>::failure(closedError());
        const auto status = runtime_->invoke([&] { return backend_->status(); });
        if (!status.hasValue()) return core::Result<CameraCapabilities>::failure(status.error());
        return core::Result<CameraCapabilities>::success(detail::capabilitiesFor(*facts_));
    }
    core::Result<CameraConfiguration> readConfiguration() override {
        if (!facts_) return core::Result<CameraConfiguration>::failure(closedError());
        const auto status = runtime_->invoke([&] { return backend_->status(); });
        if (!status.hasValue()) return core::Result<CameraConfiguration>::failure(status.error());
        return core::Result<CameraConfiguration>::success(detail::configurationFor(*facts_));
    }
    core::Result<AppliedCameraConfiguration> applyConfiguration(const CameraConfiguration& requested) override {
        if (!facts_) return core::Result<AppliedCameraConfiguration>::failure(closedError());
        const auto status = runtime_->invoke([&] { return backend_->status(); });
        if (!status.hasValue()) return core::Result<AppliedCameraConfiguration>::failure(status.error());
        const auto actual = detail::configurationFor(*facts_);
        const auto plan = planCameraConfigurationChange(requested, actual, detail::capabilitiesFor(*facts_), streaming_);
        if (!plan.hasValue()) return core::Result<AppliedCameraConfiguration>::failure(plan.error());
        return core::Result<AppliedCameraConfiguration>::success({requested, actual});
    }
    core::Result<void> startStream() override {
        if (!backend_ || !facts_) return core::Result<void>::failure(closedError());
        const auto result = runtime_->invoke([&] { return backend_->start(); });
        if (result.hasValue()) streaming_ = true;
        return result;
    }
    core::Result<std::shared_ptr<const core::RawFrame>> retrieve(std::chrono::milliseconds timeout,
        core::BufferPool& pool, std::stop_token stop) override {
        if (!handoff_) return core::Result<std::shared_ptr<const core::RawFrame>>::failure(closedError());
        return handoff_->retrieve(timeout, pool, stop);
    }
    core::Result<void> stopStream() noexcept override {
        try {
            if (handoff_) handoff_->stop();
            streaming_ = false;
            if (backend_) runtime_->invoke([&] { backend_->stop(); });
            return core::Result<void>::success();
        } catch (...) {
            return core::Result<void>::failure(mediaError(core::ErrorCategory::Internal,
                "media_cleanup_failed", "The video source could not finish stopping.",
                "The media event thread could not complete cleanup."));
        }
    }
    core::Result<void> close() noexcept override {
        const auto stopped = stopStream();
        try {
            if (backend_) runtime_->invoke([&] { delete std::exchange(backend_, nullptr); });
            handoff_.reset(); facts_.reset();
            return stopped;
        } catch (...) {
            // The runtime also parents every backend to its event-thread root,
            // so a failed explicit delete is reclaimed when that thread exits.
            return core::Result<void>::failure(mediaError(core::ErrorCategory::Internal,
                "media_cleanup_failed", "The video source could not finish closing.",
                "The media event thread could not complete cleanup."));
        }
    }

private:
    core::IClock& clock_;
    std::shared_ptr<MediaRuntime> runtime_;
    Source source_;
    MediaBackend* backend_{};
    std::optional<StreamFacts> facts_;
    std::shared_ptr<MediaFrameHandoff> handoff_;
    bool streaming_{};
    std::shared_ptr<std::atomic<std::uint64_t>> sequence_{std::make_shared<std::atomic<std::uint64_t>>(1)};
};
}

class MediaCameraProvider::Impl final {
public:
    explicit Impl(core::IClock& clock) : clock(clock) {
        // Initialize Qt's process-wide media integration on the GUI/application
        // thread, before any worker can first access the multimedia backend.
        (void)QMediaDevices::videoInputs();
        runtime = std::make_shared<MediaRuntime>();
    }
    core::IClock& clock;
    std::shared_ptr<MediaRuntime> runtime;
    std::mutex sourcesMutex;
    std::vector<NetworkVideoSource> sources;
};
MediaCameraProvider::MediaCameraProvider(core::IClock& clock) : impl_(std::make_unique<Impl>(clock)) {}
MediaCameraProvider::~MediaCameraProvider() = default;

core::Result<void> MediaCameraProvider::setNetworkSources(std::vector<NetworkVideoSource> sources) {
    const auto invalid = [] { return core::Result<void>::failure(mediaError(core::ErrorCategory::Configuration,
        "invalid_network_source", "The network source definition is invalid.",
        "Provide a unique UUID, a short display name and a valid rtsp or rtsps address with a host; at most 32 sources are supported.")); };
    if (sources.size() > 32) return invalid();
    std::set<std::string> ids;
    for (auto& source : sources) {
        const QUuid uuid(QString::fromStdString(source.id));
        const QUrl url(QString::fromStdString(source.url), QUrl::StrictMode);
        const auto name = QString::fromStdString(source.name).trimmed();
        if (uuid.isNull() || name.isEmpty() || name.size() > 128 || name.contains(QStringLiteral("://"))
            || std::any_of(name.cbegin(), name.cend(), [](QChar character) { return character.category() == QChar::Other_Control; })
            || source.url.size() > 4096 || !url.isValid() || url.host().isEmpty() || url.hasFragment()
            || (url.scheme() != QStringLiteral("rtsp") && url.scheme() != QStringLiteral("rtsps"))) return invalid();
        source.id = uuid.toString(QUuid::WithoutBraces).toStdString();
        source.name = name.toStdString();
        if (!ids.insert(source.id).second) return invalid();
    }
    std::lock_guard lock(impl_->sourcesMutex);
    impl_->sources = std::move(sources);
    return core::Result<void>::success();
}
core::Result<std::vector<CameraDescriptor>> MediaCameraProvider::discover(std::stop_token stop) {
    const auto cancelled = [] { return core::Result<std::vector<CameraDescriptor>>::failure(mediaError(core::ErrorCategory::Cancelled,
        "cancelled", "Video discovery was cancelled.", "Discovery stop requested.")); };
    if (stop.stop_requested()) return cancelled();
    const auto locals = impl_->runtime->invoke([] { return QMediaDevices::videoInputs(); });
    std::vector<CameraDescriptor> result;
    for (const auto& camera : locals) result.push_back({localId(camera), localIdentity(camera), true});
    {
        std::lock_guard lock(impl_->sourcesMutex);
        for (const auto& source : impl_->sources) result.push_back({{"media:rtsp:" + source.id}, networkIdentity(source), true});
    }
    if (stop.stop_requested()) return cancelled();
    return core::Result<std::vector<CameraDescriptor>>::success(std::move(result));
}
core::Result<std::unique_ptr<ICameraDevice>> MediaCameraProvider::create(const CameraId& id) {
    std::optional<Source> selected;
    if (id.value.starts_with("media:local:")) {
        const auto locals = impl_->runtime->invoke([] { return QMediaDevices::videoInputs(); });
        for (const auto& camera : locals) if (localId(camera) == id) {
            selected = Source{camera, std::nullopt, localIdentity(camera)};
            break;
        }
    } else if (id.value.starts_with("media:rtsp:")) {
        std::lock_guard lock(impl_->sourcesMutex);
        for (const auto& source : impl_->sources) if (id.value == "media:rtsp:" + source.id) {
            selected = Source{std::nullopt, source, networkIdentity(source)};
            break;
        }
    }
    if (!selected) return core::Result<std::unique_ptr<ICameraDevice>>::failure(mediaError(core::ErrorCategory::CameraConnection,
        "media_source_not_found", "The video source is no longer available.", "Refresh the source list and choose an available source."));
    return core::Result<std::unique_ptr<ICameraDevice>>::success(std::make_unique<MediaCameraDevice>(impl_->clock, impl_->runtime, std::move(*selected)));
}
}
