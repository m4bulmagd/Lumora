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
                      .access = ControlAccess::WritableStreaming},
        .exposure = {.minimum = 10.0, .maximum = 20000.0, .increment = 1.0,
                     .access = ControlAccess::WritableStreaming},
        .exposureModes = {ExposureMode::Manual, ExposureMode::Auto},
        .gain = {.minimum = 0.0, .maximum = 24.0, .increment = 0.1,
                 .access = ControlAccess::WritableStreaming},
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

TEST(CameraConfigurationValidator, AcceptsAbsentGainAndAutoOnlyExposureWithoutNumericNodes) {
    auto caps = mono12Capabilities();
    caps.gainModes.clear();
    caps.gain = {0.0, 0.0, 0.0, ControlAccess::Unavailable};
    caps.gainModeAccess = ControlAccess::Unavailable;
    caps.exposureModes = {ExposureMode::Auto};
    caps.exposure = {0.0, 0.0, 0.0, ControlAccess::Unavailable};
    caps.exposureModeAccess = ControlAccess::ReadOnly;
    auto current = validConfiguration();
    current.gain = {std::nullopt, std::nullopt};
    current.exposure = {ExposureMode::Auto, std::nullopt};
    EXPECT_TRUE(validateCameraCapabilities(caps).hasValue());
    EXPECT_TRUE(validateCameraConfigurationReadback(current, caps).hasValue());
    auto plan = planCameraConfigurationChange(current, current, caps, false);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_FALSE(plan.value().gainMode || plan.value().gainValue || plan.value().exposureValue);
    caps.exposureModes.clear();
    caps.exposureModeAccess = ControlAccess::Unavailable;
    current.exposure.mode.reset();
    EXPECT_TRUE(validateCameraConfigurationReadback(current, caps).hasValue());
    current.gain.mode = GainMode::Manual;
    EXPECT_FALSE(validateCameraConfiguration(current, caps).hasValue());
    caps.gain.maximum = 10.0;
    EXPECT_FALSE(validateCameraCapabilities(caps).hasValue());
}

TEST(CameraConfigurationValidator, RejectsMalformedAccessForEveryControlAndUnavailableManualValue) {
    for (int field = 0; field != 7; ++field) {
        auto caps = mono12Capabilities();
        ControlAccess* accesses[] = {&caps.pixelFormatAccess, &caps.roi.access, &caps.frameRate.access,
            &caps.exposureModeAccess, &caps.exposure.access, &caps.gainModeAccess, &caps.gain.access};
        *accesses[field] = static_cast<ControlAccess>(999);
        EXPECT_FALSE(validateCameraCapabilities(caps).hasValue()) << field;
    }
    auto caps = mono12Capabilities();
    caps.exposure = {0.0, 0.0, 0.0, ControlAccess::Unavailable};
    EXPECT_FALSE(validateCameraCapabilities(caps).hasValue());
}

TEST(CameraConfigurationValidator, ReadbackRequiresPositiveActualFrameRateEvenWithoutAdjustment) {
    auto caps = mono12Capabilities();
    caps.frameRate.access = ControlAccess::Unavailable;
    caps.pixelFormatAccess = ControlAccess::Unavailable;
    caps.roi.access = ControlAccess::Unavailable;
    auto current = validConfiguration();
    EXPECT_TRUE(validateCameraConfigurationReadback(current, caps).hasValue());
    current.requestedFps.reset();
    EXPECT_FALSE(validateCameraConfigurationReadback(current, caps).hasValue());
    current = validConfiguration();
    current.roi.width = 0U;
    EXPECT_FALSE(validateCameraConfigurationReadback(current, caps).hasValue());
}

TEST(CameraConfigurationValidator, RetainedFixedFactsHaveEmptyMaskAndChangedFixedFactsAreRejected) {
    auto caps = mono12Capabilities();
    caps.pixelFormatAccess = ControlAccess::Unavailable;
    caps.roi.access = ControlAccess::ReadOnly;
    caps.frameRate.access = ControlAccess::ReadOnly;
    caps.exposureModeAccess = ControlAccess::ReadOnly;
    caps.exposure.access = ControlAccess::ReadOnly;
    caps.gainModeAccess = ControlAccess::ReadOnly;
    caps.gain.access = ControlAccess::ReadOnly;
    const auto current = validConfiguration();
    auto result = planCameraConfigurationChange(current, current, caps, false);
    ASSERT_TRUE(result.hasValue());
    const auto mask = result.value();
    EXPECT_FALSE(mask.pixelFormat || mask.roi || mask.frameRate || mask.exposureMode
        || mask.exposureValue || mask.gainMode || mask.gainValue);
    auto requested = current;
    requested.requestedFps = 20.0;
    EXPECT_FALSE(planCameraConfigurationChange(requested, current, caps, false).hasValue());
    requested.requestedFps.reset();
    EXPECT_FALSE(planCameraConfigurationChange(requested, current, caps, false).hasValue());
    requested = current;
    requested.exposure.requestedMicroseconds = 1200.0;
    EXPECT_FALSE(planCameraConfigurationChange(requested, current, caps, false).hasValue());
}

TEST(CameraConfigurationValidator, FixedManualModeAllowsIndependentWritableValue) {
    auto caps = mono12Capabilities();
    caps.exposureModes = {ExposureMode::Manual};
    caps.exposureModeAccess = ControlAccess::ReadOnly;
    caps.exposure.access = ControlAccess::WritableStopped;
    const auto current = validConfiguration();
    auto requested = current;
    requested.exposure.requestedMicroseconds = 1200.4;
    auto plan = planCameraConfigurationChange(requested, current, caps, false);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_TRUE(plan.value().exposureValue);
    EXPECT_FALSE(plan.value().exposureMode);
    EXPECT_FALSE(planCameraConfigurationChange(requested, current, caps, true).hasValue());
    caps.exposure.access = ControlAccess::WritableStreaming;
    EXPECT_TRUE(planCameraConfigurationChange(requested, current, caps, true).hasValue());
}

TEST(CameraConfigurationValidator, AutoReadbackMayReportReadableValueAndAuthorizeRetainingItInManual) {
    auto caps = mono12Capabilities();
    caps.exposure.access = ControlAccess::ReadOnly;
    caps.gain.access = ControlAccess::ReadOnly;
    auto current = validConfiguration();
    current.exposure.mode = ExposureMode::Auto;
    current.gain.mode = GainMode::Auto;
    EXPECT_TRUE(validateCameraConfigurationReadback(current, caps).hasValue());
    EXPECT_FALSE(validateCameraConfiguration(current, caps).hasValue());
    auto requested = validConfiguration();
    auto plan = planCameraConfigurationChange(requested, current, caps, false);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_TRUE(plan.value().exposureMode);
    EXPECT_TRUE(plan.value().gainMode);
    EXPECT_FALSE(plan.value().exposureValue);
    EXPECT_FALSE(plan.value().gainValue);
    requested.gain.requestedDb = 7.0;
    EXPECT_FALSE(planCameraConfigurationChange(requested, current, caps, false).hasValue());
}

TEST(CameraConfigurationValidator, AutoTransitionDoesNotWriteNumericNodeAndCannotFabricateFixedManualValue) {
    auto caps = mono12Capabilities();
    caps.exposure.access = ControlAccess::ReadOnly;
    auto current = validConfiguration();
    auto requested = current;
    requested.exposure = {ExposureMode::Auto, std::nullopt};
    auto plan = planCameraConfigurationChange(requested, current, caps, false);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_TRUE(plan.value().exposureMode);
    EXPECT_FALSE(plan.value().exposureValue);
    current = requested;
    requested.exposure = {ExposureMode::Manual, 1000.0};
    EXPECT_FALSE(planCameraConfigurationChange(requested, current, caps, false).hasValue());
    caps.exposure.access = ControlAccess::WritableStopped;
    plan = planCameraConfigurationChange(requested, current, caps, false);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_TRUE(plan.value().exposureMode);
    EXPECT_TRUE(plan.value().exposureValue);
}

TEST(CameraConfigurationValidator, RejectsDuplicateCapabilityFacts) {
    for (int field = 0; field != 3; ++field) {
        auto caps = mono12Capabilities();
        if (field == 0) caps.pixelFormats.push_back(caps.pixelFormats.front());
        if (field == 1) caps.exposureModes.push_back(caps.exposureModes.front());
        if (field == 2) caps.gainModes.push_back(caps.gainModes.front());
        EXPECT_FALSE(validateCameraConfiguration(validConfiguration(), caps).hasValue()) << field;
    }
}

TEST(CameraConfigurationValidator, RejectsUnknownCapabilityModesEvenWhenRequestUsesValidMode) {
    auto caps = mono12Capabilities();
    caps.exposureModes.push_back(static_cast<ExposureMode>(999));
    EXPECT_FALSE(validateCameraConfiguration(validConfiguration(), caps).hasValue());
    caps = mono12Capabilities();
    caps.gainModes.push_back(static_cast<GainMode>(999));
    EXPECT_FALSE(validateCameraConfiguration(validConfiguration(), caps).hasValue());
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
