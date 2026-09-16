#include "MediaFrameHandoff.hpp"
#include <lumora/camera/media/MediaCameraProvider.hpp>

#include <cmath>
#include <cstring>
#include <utility>

namespace lumora::camera::media::detail {
namespace {
using FrameResult = core::Result<std::shared_ptr<const core::RawFrame>>;
core::Error frameError(std::string detail) {
    return {core::ErrorCategory::InvalidFrame, "invalid_frame", "The video source supplied an unsupported frame.", std::move(detail), true};
}
core::Error streamError(std::string detail) {
    // A latched source failure cannot recover by dropping one frame. The
    // acquisition worker must retire the session and require manual Retry.
    return {core::ErrorCategory::CameraConnection, "media_stream_invalid",
        "The video source no longer matches its configured stream.", std::move(detail), true};
}
core::Error cancelled() {
    return {core::ErrorCategory::Cancelled, "cancelled", "Frame retrieval was cancelled.", "The acquisition stop token was requested.", true};
}
core::Error timeout() {
    return {core::ErrorCategory::Acquisition, "acquisition_timeout", "No video frame arrived before the timeout.", "The retrieval deadline expired.", true};
}
core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8, 255, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
}
}

core::Result<void> validateFacts(StreamFacts facts) {
    if (facts.width <= 0 || facts.height <= 0 || !std::isfinite(facts.fps) || facts.fps <= 0.0)
        return core::Result<void>::failure({core::ErrorCategory::CameraConfiguration,
            "media_metadata_missing", "The video source did not report usable dimensions and frame rate.",
            "Positive source width, height and nominal FPS are required before Start.", true});
    if (facts.width > maximumMediaWidth || facts.height > maximumMediaHeight)
        return core::Result<void>::failure({core::ErrorCategory::CameraConfiguration,
            "media_mode_unsupported", "The video source exceeds the supported 1920 by 1080 size.",
            "Select a smaller source mode; this adapter never silently scales video.", true});
    return core::Result<void>::success();
}

CameraConfiguration configurationFor(StreamFacts facts) {
    return {mono8(), {0, 0, static_cast<std::uint32_t>(facts.width), static_cast<std::uint32_t>(facts.height)},
        facts.fps, {std::nullopt, std::nullopt}, {std::nullopt, std::nullopt}, AcquisitionMode::Continuous};
}
CameraCapabilities capabilitiesFor(StreamFacts facts) {
    const auto roi = configurationFor(facts).roi;
    return {{mono8()}, {roi, roi, {1, 1, 1, 1}, ControlAccess::ReadOnly},
        {facts.fps, facts.fps, 1, ControlAccess::ReadOnly}, {0, 0, 0, ControlAccess::Unavailable}, {},
        {0, 0, 0, ControlAccess::Unavailable}, {}, ControlAccess::ReadOnly,
        ControlAccess::Unavailable, ControlAccess::Unavailable};
}

MediaFrameHandoff::MediaFrameHandoff(core::IClock& clock, core::CameraIdentity identity, StreamFacts facts,
    std::shared_ptr<std::atomic<std::uint64_t>> sequence)
    : clock_(clock), identity_(std::move(identity)), facts_(facts), sequence_(std::move(sequence)) {}

void MediaFrameHandoff::start() {
    std::lock_guard lock(mutex_);
    if (streaming_) return;
    latest_.reset(); error_.reset(); streaming_ = true; ++generation_;
}
void MediaFrameHandoff::stop() {
    { std::lock_guard lock(mutex_); streaming_ = false; latest_.reset(); error_.reset(); ++generation_; }
    changed_.notify_all();
}
void MediaFrameHandoff::fail(core::Error error) {
    { std::lock_guard lock(mutex_); if (!streaming_ || error_) return; latest_.reset(); error_ = std::move(error); }
    changed_.notify_all();
}
void MediaFrameHandoff::publish(const QVideoFrame& frame) {
    // Conversion runs outside the slot mutex so a decoder/GPU readback cannot
    // hold up cancellation or the acquisition timeout. Epochs retire work that
    // was already converting when Stop or a subsequent Start happened.
    std::uint64_t generation;
    std::uint64_t frameId;
    {
        std::lock_guard lock(mutex_);
        if (!streaming_ || error_) return;
        generation = generation_;
        frameId = sequence_->fetch_add(1, std::memory_order_relaxed);
    }
    const auto receipt = clock_.steadyNow();
    const auto utc = clock_.utcNow();
    std::optional<core::Error> error;
    QImage gray;
    if (!frame.isValid() || frame.width() != facts_.width || frame.height() != facts_.height) {
        error = streamError("Source dimensions changed or the decoded frame is invalid.");
    } else if (frame.streamFrameRate() > 0 && std::abs(frame.streamFrameRate() - facts_.fps) > 0.01) {
        error = streamError("The source frame rate changed after configuration.");
    } else {
        // Qt decodes native YUV/RGB. Explicit qGray conversion defines our
        // eight-bit intensity mapping independently of QImage color profiles.
        const auto image = frame.toImage();
        if (image.isNull() || image.width() != facts_.width || image.height() != facts_.height) {
            error = streamError("Qt could not convert the source frame to the negotiated Mono8 layout.");
        } else if (image.format() == QImage::Format_Grayscale8) {
            gray = image;
        } else {
            const auto rgb = image.convertToFormat(QImage::Format_RGB32);
            if (!rgb.isNull()) gray = QImage(facts_.width, facts_.height, QImage::Format_Grayscale8);
            if (!gray.isNull()) {
                for (int y = 0; y < facts_.height; ++y) {
                    const auto* input = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
                    auto* output = gray.scanLine(y);
                    for (int x = 0; x < facts_.width; ++x)
                        output[x] = static_cast<uchar>(qGray(input[x]));
                }
            }
        }
        if (gray.isNull()) error = streamError("Converting the source image failed.");
    }
    {
        std::lock_guard lock(mutex_);
        if (!streaming_ || error_ || generation_ != generation || frameId <= lastPublishedId_) return;
        if (error) { error_ = std::move(error); latest_.reset(); }
        else {
            lastPublishedId_ = frameId;
            latest_ = PendingFrame{std::move(gray), receipt, utc, frameId};
        }
    }
    changed_.notify_all();
}

core::Result<std::shared_ptr<const core::RawFrame>> MediaFrameHandoff::retrieve(
    std::chrono::milliseconds wait, core::BufferPool& pool, std::stop_token token) {
    if (token.stop_requested()) return FrameResult::failure(cancelled());
    if (wait <= std::chrono::milliseconds::zero()) return FrameResult::failure(timeout());
    const auto now = std::chrono::steady_clock::now();
    const auto maxWait = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::time_point::max() - now);
    const auto deadline = wait >= maxWait ? std::chrono::steady_clock::time_point::max() : now + wait;
    std::unique_lock lock(mutex_);
    changed_.wait_until(lock, token, deadline, [&] { return !streaming_ || error_ || latest_; });
    if (token.stop_requested()) return FrameResult::failure(cancelled());
    if (!streaming_) return FrameResult::failure({core::ErrorCategory::Acquisition,
        "stream_not_started", "The video stream is not running.", "Start the source before retrieving frames.", true});
    if (error_) return FrameResult::failure(*error_);
    if (!latest_ || std::chrono::steady_clock::now() >= deadline) return FrameResult::failure(timeout());
    auto pending = std::move(*latest_); latest_.reset();
    lock.unlock();
    auto lease = pool.tryAcquire();
    if (!lease) return FrameResult::failure({core::ErrorCategory::ResourceExhaustion,
        "buffer_pool_exhausted", "No image buffer is available.", "The acquisition buffer pool is exhausted.", true});
    const auto width = static_cast<std::size_t>(facts_.width);
    const auto bytes = width * static_cast<std::size_t>(facts_.height);
    if (lease->bytes().size() < bytes) return FrameResult::failure(frameError("The supplied buffer cannot hold the negotiated image."));
    for (int row = 0; row < facts_.height; ++row) {
        if (token.stop_requested()) return FrameResult::failure(cancelled());
        if (std::chrono::steady_clock::now() >= deadline) return FrameResult::failure(timeout());
        std::memcpy(lease->bytes().data() + static_cast<std::size_t>(row) * width, pending.image.constScanLine(row), width);
    }
    const auto config = configurationFor(facts_);
    auto settings = core::AcquisitionSettingsSnapshot::create(identity_, config.pixelFormat, config.roi,
        facts_.fps, facts_.fps, std::nullopt, std::nullopt);
    if (!settings.hasValue()) return FrameResult::failure(frameError("The source acquisition metadata is invalid."));
    auto layout = core::ImageLayout::create(config.roi.width, config.roi.height, width, core::StorageType::UInt8, bytes);
    if (!layout.hasValue()) return FrameResult::failure(frameError("The source image layout is invalid."));
    auto result = core::RawFrame::create(pending.id, layout.value(), std::move(*lease).seal(),
        {std::nullopt, pending.receipt, pending.utc, std::nullopt, settings.value()});
    if (token.stop_requested()) return FrameResult::failure(cancelled());
    if (std::chrono::steady_clock::now() >= deadline) return FrameResult::failure(timeout());
    return result;
}
}
