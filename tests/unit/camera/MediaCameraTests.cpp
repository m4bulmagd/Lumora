#include "MediaFrameHandoff.hpp"
#include <lumora/camera/media/MediaCameraProvider.hpp>
#include <QCoreApplication>
#include <lumora/camera/CameraConfigurationValidator.hpp>
#include <gtest/gtest.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

namespace lumora::camera::media {
namespace {
using namespace std::chrono_literals;
core::CameraIdentity identity() { return {"Qt Multimedia", "Test source", "media:test", "test", std::nullopt}; }

TEST(MediaCamera, CopiesConvertedColorRowsIntoOwnedMono8PoolWithoutPadding) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {3, 2, 25.0});
    auto pool = core::BufferPool::create(2, 6).value();
    QImage color(3, 2, QImage::Format_RGB32);
    color.setPixel(0, 0, qRgb(255, 0, 0)); color.setPixel(1, 0, qRgb(0, 255, 0));
    color.setPixel(2, 0, qRgb(0, 0, 255)); color.setPixel(0, 1, qRgb(255, 255, 255));
    color.setPixel(1, 1, qRgb(0, 0, 0)); color.setPixel(2, 1, qRgb(128, 128, 128));
    handoff.start();
    handoff.publish(QVideoFrame(color));
    auto frame = handoff.retrieve(100ms, *pool);
    ASSERT_TRUE(frame.hasValue());
    EXPECT_EQ(frame.value()->layout.strideBytes(), 3U);
    const std::array<unsigned, 6> expected{87, 127, 39, 255, 0, 128};
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(std::to_integer<unsigned>(frame.value()->pixels.bytes()[i]), expected[i]);
    color.fill(Qt::black);
    EXPECT_EQ(std::to_integer<unsigned>(frame.value()->pixels.bytes()[0]), 87U);
    EXPECT_EQ(pool->stats().inUse, 1U);
    EXPECT_EQ(frame.value()->metadata.acquisitionSettings.actualFps, 25.0);
    EXPECT_EQ(frame.value()->metadata.acquisitionSettings.sourceFormat.canonicalName, "Mono8");
    EXPECT_FALSE(frame.value()->metadata.cameraFrameId.has_value());
}

TEST(MediaCamera, ConvertsNativeNv12FramesAndHonorsTheirPlaneStrides) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {4, 2, 25});
    auto pool = core::BufferPool::create(1, 8).value();
    QVideoFrameFormat format(QSize(4, 2), QVideoFrameFormat::Format_NV12);
    format.setColorRange(QVideoFrameFormat::ColorRange_Full);
    format.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
    QVideoFrame video(format);
    ASSERT_TRUE(video.map(QVideoFrame::WriteOnly));
    for (int row = 0; row < 2; ++row) {
        std::fill_n(video.bits(0) + row * video.bytesPerLine(0), 4, static_cast<uchar>(row == 0 ? 0 : 255));
    }
    std::fill_n(video.bits(1), 4, static_cast<uchar>(128));
    video.unmap();
    handoff.start(); handoff.publish(video);
    auto result = handoff.retrieve(100ms, *pool);
    ASSERT_TRUE(result.hasValue());
    const auto bytes = result.value()->pixels.bytes();
    for (std::size_t i = 0; i < 4; ++i) EXPECT_LE(std::to_integer<unsigned>(bytes[i]), 1U);
    for (std::size_t i = 4; i < 8; ++i) EXPECT_GE(std::to_integer<unsigned>(bytes[i]), 254U);
}

TEST(MediaCamera, RejectsMissingFramesChangedRateAndInsufficientPoolStorage) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {3, 2, 25});
    auto pool = core::BufferPool::create(1, 5).value();
    handoff.start(); handoff.publish({});
    EXPECT_EQ(handoff.retrieve(100ms, *pool).error().code, "media_stream_invalid");
    handoff.stop(); handoff.start();
    QVideoFrame frame(QImage(3, 2, QImage::Format_Grayscale8));
    frame.setStreamFrameRate(30); handoff.publish(frame);
    EXPECT_EQ(handoff.retrieve(100ms, *pool).error().code, "media_stream_invalid");
    handoff.stop(); handoff.start();
    frame.setStreamFrameRate(25); handoff.publish(frame);
    EXPECT_EQ(handoff.retrieve(100ms, *pool).error().code, "invalid_frame");
    EXPECT_EQ(pool->stats().inUse, 0U);
}

TEST(MediaCamera, RetainsOnlyNewestFrameAndDropsFramesOutsideStreaming) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {3, 2, 30});
    auto pool = core::BufferPool::create(1, 6).value();
    QImage image(3, 2, QImage::Format_Grayscale8);
    image.fill(10); handoff.publish(QVideoFrame(image));
    handoff.start();
    EXPECT_EQ(handoff.retrieve(1ms, *pool).error().code, "acquisition_timeout");
    image.fill(20); handoff.publish(QVideoFrame(image));
    image.fill(30); handoff.publish(QVideoFrame(image));
    auto frame = handoff.retrieve(100ms, *pool);
    ASSERT_TRUE(frame.hasValue());
    EXPECT_EQ(std::to_integer<unsigned>(frame.value()->pixels.bytes()[0]), 30U);
    EXPECT_EQ(frame.value()->frameId, 2U);
    handoff.stop(); handoff.stop(); handoff.start(); handoff.start();
    EXPECT_EQ(handoff.retrieve(1ms, *pool).error().code, "acquisition_timeout");
}

TEST(MediaCamera, WaitingRetrievalIsCancellableAndBounded) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {3, 2, 30});
    auto pool = core::BufferPool::create(1, 6).value();
    handoff.start();
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&] { return handoff.retrieve(10s, *pool, stop.get_token()); });
    stop.request_stop();
    ASSERT_EQ(pending.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(pending.get().error().code, "cancelled");
    const auto before = std::chrono::steady_clock::now();
    EXPECT_EQ(handoff.retrieve(5ms, *pool).error().code, "acquisition_timeout");
    EXPECT_LT(std::chrono::steady_clock::now() - before, 500ms);
}

TEST(MediaCamera, StopWakesWaitingRetrievalAndClearsStaleFrames) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {3, 2, 30});
    auto pool = core::BufferPool::create(1, 6).value();
    handoff.start();
    auto pending = std::async(std::launch::async, [&] { return handoff.retrieve(10s, *pool); });
    handoff.stop();
    ASSERT_EQ(pending.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(pending.get().error().code, "stream_not_started");
}

TEST(MediaCamera, RejectsChangedLayoutAndLatchesFailureUntilRestart) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {3, 2, 30});
    auto pool = core::BufferPool::create(1, 6).value();
    handoff.start();
    handoff.publish(QVideoFrame(QImage(4, 2, QImage::Format_Grayscale8)));
    handoff.publish(QVideoFrame(QImage(3, 2, QImage::Format_Grayscale8)));
    EXPECT_EQ(handoff.retrieve(100ms, *pool).error().code, "media_stream_invalid");
}

TEST(MediaCamera, LatchedSourceFailuresRequireReconnectInsteadOfDroppingForever) {
    core::ManualClock clock;
    detail::MediaFrameHandoff handoff(clock, identity(), {3, 2, 30});
    auto pool = core::BufferPool::create(1, 6).value();
    handoff.start();
    handoff.publish(QVideoFrame(QImage(4, 2, QImage::Format_Grayscale8)));
    auto first = handoff.retrieve(100ms, *pool);
    ASSERT_FALSE(first.hasValue());
    EXPECT_EQ(first.error().category, core::ErrorCategory::CameraConnection);
    EXPECT_EQ(first.error().code, "media_stream_invalid");
    EXPECT_TRUE(first.error().recoverable);
    handoff.publish(QVideoFrame(QImage(3, 2, QImage::Format_Grayscale8)));
    auto stillFailed = handoff.retrieve(100ms, *pool);
    ASSERT_FALSE(stillFailed.hasValue());
    EXPECT_EQ(stillFailed.error().category, core::ErrorCategory::CameraConnection);
    EXPECT_EQ(pool->stats().inUse, 0U);
}

TEST(MediaCamera, ValidatesRealFactsAndExposesOnlyFixedControls) {
    EXPECT_FALSE(detail::validateFacts({0, 2, 30}).hasValue());
    EXPECT_FALSE(detail::validateFacts({1921, 1080, 30}).hasValue());
    EXPECT_FALSE(detail::validateFacts({1920, 1081, 30}).hasValue());
    EXPECT_FALSE(detail::validateFacts({640, 480, 0}).hasValue());
    auto facts = detail::StreamFacts{640, 480, 29.97};
    const auto caps = detail::capabilitiesFor(facts);
    const auto config = detail::configurationFor(facts);
    ASSERT_TRUE(validateCameraCapabilities(caps).hasValue());
    ASSERT_TRUE(validateCameraConfigurationReadback(config, caps).hasValue());
    EXPECT_EQ(caps.roi.access, ControlAccess::ReadOnly);
    EXPECT_EQ(caps.frameRate.access, ControlAccess::ReadOnly);
    EXPECT_EQ(caps.pixelFormatAccess, ControlAccess::ReadOnly);
    EXPECT_EQ(caps.exposure.access, ControlAccess::Unavailable);
    EXPECT_EQ(caps.gain.access, ControlAccess::Unavailable);
    EXPECT_FALSE(config.exposure.mode.has_value());
    EXPECT_FALSE(config.gain.requestedDb.has_value());
    EXPECT_TRUE(planCameraConfigurationChange(config, config, caps, false).hasValue());
    auto changed = config; changed.requestedFps = 60;
    EXPECT_FALSE(planCameraConfigurationChange(changed, config, caps, false).hasValue());
}
QCoreApplication& application() {
    static int argc = 1;
    static char name[] = "lumora-media-tests";
    static char* argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
    return app;
}

TEST(MediaCamera, NetworkSourcesAreValidatedAtomicallyAndNeverExposeCredentials) {
    (void)application();
    core::SystemClock clock;
    MediaCameraProvider provider(clock);
    const std::string uuid = "047a87e1-0ab3-4b56-bad9-91c2c4532a54";
    ASSERT_TRUE(provider.setNetworkSources({{uuid, "Lab feed", "rtsp://operator:private-password@127.0.0.1:8554/camera?token=secret"}}).hasValue());
    auto sources = provider.discover();
    ASSERT_TRUE(sources.hasValue());
    auto source = std::find_if(sources.value().begin(), sources.value().end(), [&](const auto& entry) { return entry.id.value == "media:rtsp:" + uuid; });
    ASSERT_NE(source, sources.value().end());
    EXPECT_EQ(source->identity.model, "Lab feed");
    EXPECT_EQ(source->identity.serial, uuid);
    EXPECT_EQ(source->identity.transport, "RTSP / Qt Multimedia");
    const auto invalid = provider.setNetworkSources({{uuid, "Bad", "https://operator:private-password@example.com"}});
    ASSERT_FALSE(invalid.hasValue());
    EXPECT_EQ(invalid.error().diagnosticDetail.find("private-password"), std::string::npos);
    auto retained = provider.create({"media:rtsp:" + uuid});
    ASSERT_TRUE(retained.hasValue());
    EXPECT_FALSE(provider.setNetworkSources({{uuid, "A", "rtsp://host/a"}, {uuid, "B", "rtsp://host/b"}}).hasValue());
    EXPECT_FALSE(provider.setNetworkSources({{"not-a-uuid", "A", "rtsp://host/a"}}).hasValue());
    EXPECT_FALSE(provider.setNetworkSources({{uuid, "A", "rtsp:///missing-host"}}).hasValue());
}

TEST(MediaCamera, NetworkSourceNamesMatchTheCatalogLimitAndRejectControls) {
    (void)application();
    core::SystemClock clock;
    MediaCameraProvider provider(clock);
    const std::string uuid = "047a87e1-0ab3-4b56-bad9-91c2c4532a54";
    EXPECT_TRUE(provider.setNetworkSources({{uuid, std::string(128, 'a'), "rtsp://host/video"}}).hasValue());
    EXPECT_FALSE(provider.setNetworkSources({{uuid, std::string(129, 'a'), "rtsp://host/video"}}).hasValue());
    EXPECT_FALSE(provider.setNetworkSources({{uuid, "Lab\nfeed", "rtsp://host/video"}}).hasValue());
}

TEST(MediaCamera, CreatedDeviceRemainsClosedAndCleanupDoesNotNeedGuiEventLoop) {
    (void)application();
    core::SystemClock clock;
    MediaCameraProvider provider(clock);
    const std::string uuid = "047a87e1-0ab3-4b56-bad9-91c2c4532a54";
    ASSERT_TRUE(provider.setNetworkSources({{uuid, "Lab feed", "rtsp://127.0.0.1:1/no-server"}}).hasValue());
    auto operation = std::async(std::launch::async, [&] {
        auto device = provider.create({"media:rtsp:" + uuid});
        if (!device.hasValue()) return false;
        return !device.value()->capabilities().hasValue() && !device.value()->startStream().hasValue()
            && device.value()->stopStream().hasValue() && device.value()->close().hasValue()
            && device.value()->close().hasValue();
    });
    ASSERT_EQ(operation.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(operation.get());
    std::stop_source stop; stop.request_stop();
    EXPECT_EQ(provider.discover(stop.get_token()).error().code, "cancelled");
}

TEST(MediaCamera, UnreachableNetworkOpenFailsWithoutGuiLoopOrCredentialErrors) {
    (void)application();
    core::SystemClock clock;
    MediaCameraProvider provider(clock);
    const std::string uuid = "047a87e1-0ab3-4b56-bad9-91c2c4532a54";
    ASSERT_TRUE(provider.setNetworkSources({{uuid, "Lab feed", "rtsp://operator:private-password@127.0.0.1:1/no-server"}}).hasValue());
    auto operation = std::async(std::launch::async, [&] {
        auto device = provider.create({"media:rtsp:" + uuid});
        return device.value()->open();
    });
    ASSERT_EQ(operation.wait_for(8s), std::future_status::ready);
    const auto result = operation.get();
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().diagnosticDetail.find("private-password"), std::string::npos);
    EXPECT_EQ(result.error().diagnosticDetail.find("rtsp://"), std::string::npos);
}

}
}
