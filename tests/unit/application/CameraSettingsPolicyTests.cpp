#include <lumora/application/CameraSettingsPolicy.hpp>

#include <gtest/gtest.h>

namespace lumora::application {
namespace {

camera::CameraConfiguration preparedConfiguration() {
    return {{"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
                core::BitAlignment::LeastSignificant, core::StorageType::UInt8},
        {4U, 6U, 640U, 480U}, 30.0,
        {camera::ExposureMode::Manual, 1000.0},
        {camera::GainMode::Manual, 2.0},
        camera::AcquisitionMode::Continuous};
}

TEST(CameraSettingsPolicy, PermitsExposureAndGainModeAndValueDifferences) {
    const auto prepared = preparedConfiguration();

    auto differentValues = prepared;
    differentValues.exposure.requestedMicroseconds = 2500.5;
    differentValues.gain.requestedDb = 4.25;
    EXPECT_TRUE(isCameraSettingsCompatible(differentValues, prepared));

    auto automatic = prepared;
    automatic.exposure = {camera::ExposureMode::Auto, std::nullopt};
    automatic.gain = {camera::GainMode::Auto, std::nullopt};
    EXPECT_TRUE(isCameraSettingsCompatible(automatic, prepared));

    auto differentModeAndValues = prepared;
    differentModeAndValues.exposure = {camera::ExposureMode::Auto, 1500.0};
    differentModeAndValues.gain = {camera::GainMode::Auto, 3.5};
    EXPECT_TRUE(isCameraSettingsCompatible(differentModeAndValues, prepared));
}

TEST(CameraSettingsPolicy, RejectsEverySourceFormatMetadataDifference) {
    const auto prepared = preparedConfiguration();

    auto canonicalName = prepared;
    canonicalName.pixelFormat.canonicalName = "Mono8Vendor";
    EXPECT_FALSE(isCameraSettingsCompatible(canonicalName, prepared));

    auto canonicalEncoding = prepared;
    canonicalEncoding.pixelFormat.canonicalEncoding = 0x01080002U;
    EXPECT_FALSE(isCameraSettingsCompatible(canonicalEncoding, prepared));

    auto validBits = prepared;
    validBits.pixelFormat.validBits = 7U;
    EXPECT_FALSE(isCameraSettingsCompatible(validBits, prepared));

    auto sampleMaximum = prepared;
    sampleMaximum.pixelFormat.sampleMaximum = 254U;
    EXPECT_FALSE(isCameraSettingsCompatible(sampleMaximum, prepared));

    auto packing = prepared;
    packing.pixelFormat.packing = core::SourcePacking::Packed;
    EXPECT_FALSE(isCameraSettingsCompatible(packing, prepared));

    auto alignment = prepared;
    alignment.pixelFormat.alignment = core::BitAlignment::MostSignificant;
    EXPECT_FALSE(isCameraSettingsCompatible(alignment, prepared));

    auto applicationStorage = prepared;
    applicationStorage.pixelFormat.applicationStorage = core::StorageType::UInt16;
    EXPECT_FALSE(isCameraSettingsCompatible(applicationStorage, prepared));
}

TEST(CameraSettingsPolicy, RejectsEveryRoiCoordinateAndExtentDifference) {
    const auto prepared = preparedConfiguration();

    auto x = prepared;
    ++x.roi.x;
    EXPECT_FALSE(isCameraSettingsCompatible(x, prepared));

    auto y = prepared;
    ++y.roi.y;
    EXPECT_FALSE(isCameraSettingsCompatible(y, prepared));

    auto width = prepared;
    --width.roi.width;
    EXPECT_FALSE(isCameraSettingsCompatible(width, prepared));

    auto height = prepared;
    --height.roi.height;
    EXPECT_FALSE(isCameraSettingsCompatible(height, prepared));
}

TEST(CameraSettingsPolicy, RequiresIdenticalOptionalFrameRate) {
    const auto prepared = preparedConfiguration();

    auto missing = prepared;
    missing.requestedFps.reset();
    EXPECT_FALSE(isCameraSettingsCompatible(missing, prepared));

    auto different = prepared;
    different.requestedFps = 29.97;
    EXPECT_FALSE(isCameraSettingsCompatible(different, prepared));

    auto noPreparedFrameRate = prepared;
    noPreparedFrameRate.requestedFps.reset();
    EXPECT_FALSE(isCameraSettingsCompatible(prepared, noPreparedFrameRate));

    auto bothMissing = prepared;
    bothMissing.requestedFps.reset();
    EXPECT_TRUE(isCameraSettingsCompatible(bothMissing, noPreparedFrameRate));
}

TEST(CameraSettingsPolicy, RequiresIdenticalAcquisitionMode) {
    const auto prepared = preparedConfiguration();
    auto triggered = prepared;
    triggered.acquisitionMode = camera::AcquisitionMode::Triggered;

    EXPECT_FALSE(isCameraSettingsCompatible(triggered, prepared));
}

}  // namespace
}  // namespace lumora::application
