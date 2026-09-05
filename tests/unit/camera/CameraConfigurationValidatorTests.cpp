#include <lumora/camera/CameraConfigurationValidator.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

namespace lumora::camera {
namespace {

using core::BitAlignment;
using core::RegionOfInterest;
using core::SourcePacking;
using core::SourcePixelFormat;
using core::StorageType;

SourcePixelFormat mono12() {
    return {
        .canonicalName = "Mono12",
        .canonicalEncoding = 0x01100005U,
        .validBits = 12,
        .sampleMaximum = 4095,
        .packing = SourcePacking::Unpacked,
        .alignment = BitAlignment::LeastSignificant,
        .applicationStorage = StorageType::UInt16,
    };
}

CameraCapabilities mono12Capabilities(std::uint32_t widthIncrement = 8U) {
    return {
        .pixelFormats = {mono12()},
        .roi = {
            .minimum = {.x = 0, .y = 0, .width = 16, .height = 16},
            .maximum = {.x = 2047, .y = 1023, .width = 2048, .height = 1024},
            .increment = {.x = 4, .y = 2, .width = widthIncrement, .height = 4},
        },
        .frameRate = {.minimum = 1.0, .maximum = 60.0, .increment = 0.1,
                      .writableWhileStreaming = true},
        .exposure = {.minimum = 10.0, .maximum = 20000.0, .increment = 1.0,
                     .writableWhileStreaming = true},
        .exposureModes = {ExposureMode::Manual, ExposureMode::Auto},
        .gain = {.minimum = 0.0, .maximum = 24.0, .increment = 0.1,
                 .writableWhileStreaming = true},
        .gainModes = {GainMode::Manual, GainMode::Auto},
    };
}

CameraConfiguration validConfiguration() {
    return {
        .pixelFormat = mono12(),
        .roi = {.x = 8, .y = 4, .width = 1024, .height = 512},
        .requestedFps = 30.0,
        .exposure = {.mode = ExposureMode::Manual, .requestedMicroseconds = 1000.0},
        .gain = {.mode = GainMode::Manual, .requestedDb = 6.0},
        .acquisitionMode = AcquisitionMode::Continuous,
    };
}

TEST(CameraConfigurationValidator, RejectsRoiThatMissesCameraIncrement) {
    auto capabilities = mono12Capabilities(/* widthIncrement = */ 8U);
    auto requested = validConfiguration();
    requested.roi.width = 1025;

    const auto result = validateCameraConfiguration(requested, capabilities);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "roi_width_increment");
}

TEST(CameraConfigurationValidator, AcceptsInRangeNumericRequestsWithoutIncrementAlignment) {
    const auto capabilities = mono12Capabilities();
    auto requested = validConfiguration();
    requested.requestedFps = 30.07;
    requested.exposure.requestedMicroseconds = 1000.4;
    requested.gain.requestedDb = 6.03;

    const auto result = validateCameraConfiguration(requested, capabilities);

    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(requested.requestedFps, 30.07);
    EXPECT_EQ(requested.exposure.requestedMicroseconds, 1000.4);
    EXPECT_EQ(requested.gain.requestedDb, 6.03);
}

TEST(CameraConfigurationValidator, RejectsTriggeredAcquisitionAsReserved) {
    const auto capabilities = mono12Capabilities();
    auto requested = validConfiguration();
    requested.acquisitionMode = AcquisitionMode::Triggered;

    const auto result = validateCameraConfiguration(requested, capabilities);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "acquisition_mode_unsupported");
}

TEST(CameraConfigurationValidator, RequiresCompleteSupportedPixelFormatDescriptor) {
    const auto capabilities = mono12Capabilities();
    auto requested = validConfiguration();
    requested.pixelFormat.sampleMaximum = 4094;

    const auto result = validateCameraConfiguration(requested, capabilities);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "pixel_format_unsupported");
}

TEST(CameraConfigurationValidator, ReturnsFirstViolationAndAllViolationsInFieldOrder) {
    auto capabilities = mono12Capabilities();
    capabilities.frameRate.increment = 0.0;
    auto requested = validConfiguration();
    requested.pixelFormat.sampleMaximum = 4094;
    requested.roi.x = 3;
    requested.roi.y = 3;
    requested.roi.width = 1025;
    requested.requestedFps = std::numeric_limits<double>::quiet_NaN();
    requested.exposure.requestedMicroseconds = 20001.0;
    requested.gain.requestedDb = -1.0;
    requested.acquisitionMode = AcquisitionMode::Triggered;

    const auto result = validateCameraConfiguration(requested, capabilities);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "capability_frame_rate_increment");
    EXPECT_EQ(
        result.error().diagnosticDetail,
        "capability_frame_rate_increment\n"
        "pixel_format_unsupported\n"
        "roi_x_increment\n"
        "roi_y_increment\n"
        "roi_width_increment\n"
        "frame_rate_finite\n"
        "exposure_range\n"
        "gain_range\n"
        "acquisition_mode_unsupported");
}

TEST(CameraConfigurationValidator, RejectsInvalidCapabilityMetadata) {
    auto capabilities = mono12Capabilities();
    capabilities.exposure.minimum = 20000.0;
    capabilities.exposure.maximum = 10.0;
    auto requested = validConfiguration();

    const auto result = validateCameraConfiguration(requested, capabilities);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "capability_exposure_range");
}

}  // namespace
}  // namespace lumora::camera
