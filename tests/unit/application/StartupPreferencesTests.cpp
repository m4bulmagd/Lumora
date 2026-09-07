#include <lumora/application/StartupPreferences.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace lumora::application {
namespace {

core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
}

core::SourcePixelFormat mono16() {
    return {"Mono16", 0x01100007U, 16U, 65535U, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt16};
}

camera::CameraCapabilities capabilities() {
    return {{mono8(), mono16()},
        {{0U, 0U, 8U, 8U}, {0U, 0U, 640U, 480U}, {1U, 1U, 8U, 8U}},
        {1.0, 60.0, 0.1, false},
        {10.0, 10000.0, 1.0, true},
        {camera::ExposureMode::Manual, camera::ExposureMode::Auto},
        {0.0, 24.0, 0.1, true},
        {camera::GainMode::Manual, camera::GainMode::Auto}};
}

camera::CameraConfiguration configuration() {
    return {mono8(), {0U, 0U, 640U, 480U}, 30.0,
        {camera::ExposureMode::Manual, 1000.0},
        {camera::GainMode::Manual, 2.0},
        camera::AcquisitionMode::Continuous};
}

StartupPreferences preferences() {
    return {1U, {"camera-1"}, {"Lumora", "Simulator", "SIM-1", "virtual", "1.0"},
        capabilities(), configuration(), configuration(), true};
}

TEST(StartupPreferences, CapabilityComparisonIgnoresSetOrdering) {
    auto reordered = capabilities();
    std::reverse(reordered.pixelFormats.begin(), reordered.pixelFormats.end());
    std::reverse(reordered.exposureModes.begin(), reordered.exposureModes.end());
    std::reverse(reordered.gainModes.begin(), reordered.gainModes.end());

    EXPECT_TRUE(cameraCapabilitiesEqual(capabilities(), reordered));
}

TEST(StartupPreferences, CapabilityComparisonCoversEveryStructuralGroup) {
    const auto original = capabilities();

    auto descriptor = original;
    descriptor.pixelFormats.front().sampleMaximum = 127U;
    EXPECT_FALSE(cameraCapabilitiesEqual(original, descriptor));

    auto roi = original;
    roi.roi.maximum.width = 648U;
    EXPECT_FALSE(cameraCapabilitiesEqual(original, roi));

    auto numeric = original;
    numeric.exposure.maximum = 9999.0;
    EXPECT_FALSE(cameraCapabilitiesEqual(original, numeric));

    auto modes = original;
    modes.gainModes.pop_back();
    EXPECT_FALSE(cameraCapabilitiesEqual(original, modes));
}

TEST(StartupPreferences, ValidationRejectsDuplicateAndNonfiniteCapabilities) {
    auto duplicate = preferences();
    duplicate.confirmedCapabilities.pixelFormats.push_back(mono8());
    const auto duplicateResult = validateStartupPreferences(duplicate);
    ASSERT_FALSE(duplicateResult.hasValue());
    EXPECT_EQ(duplicateResult.error().code, "startup_capabilities_duplicate");

    auto nonfinite = preferences();
    nonfinite.confirmedCapabilities.frameRate.maximum =
        std::numeric_limits<double>::infinity();
    const auto nonfiniteResult = validateStartupPreferences(nonfinite);
    ASSERT_FALSE(nonfiniteResult.hasValue());
    EXPECT_EQ(nonfiniteResult.error().code, "startup_capabilities_invalid");
}

TEST(StartupPreferences, ResumeEligibilityRequiresConfirmationAndStableIdentity) {
    const auto saved = preferences();
    EXPECT_TRUE(isStartupResumeEligible(
        saved, {"camera-1"}, {"Lumora", "Simulator", "SIM-1", "usb:changed", "2.0"},
        capabilities()));

    auto unconfirmed = saved;
    unconfirmed.confirmed = false;
    EXPECT_FALSE(isStartupResumeEligible(
        unconfirmed, saved.cameraId, saved.identity, saved.confirmedCapabilities));

    auto wrongIdentity = saved.identity;
    wrongIdentity.serial = "SIM-2";
    EXPECT_FALSE(isStartupResumeEligible(
        saved, saved.cameraId, wrongIdentity, saved.confirmedCapabilities));
}

TEST(StartupPreferences, ConfigurationComparisonIncludesRequestedAndModeValues) {
    const auto original = configuration();
    auto changed = original;
    changed.exposure.requestedMicroseconds = 1001.0;
    EXPECT_FALSE(cameraConfigurationsEqual(original, changed));
    changed = original;
    changed.acquisitionMode = camera::AcquisitionMode::Triggered;
    EXPECT_FALSE(cameraConfigurationsEqual(original, changed));
    EXPECT_TRUE(cameraConfigurationsEqual(original, original));
}

TEST(StartupPreferences, ValidationRejectsUnknownCapabilityModes) {
    auto invalid = preferences();
    invalid.confirmedCapabilities.exposureModes.push_back(
        static_cast<camera::ExposureMode>(99));

    const auto result = validateStartupPreferences(invalid);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "startup_capabilities_invalid");
}

TEST(StartupPreferences, ValidationRejectsUnknownConfigurationModes) {
    auto invalid = preferences();
    invalid.requested.gain.mode = static_cast<camera::GainMode>(99);
    invalid.confirmedCapabilities.gainModes.push_back(
        static_cast<camera::GainMode>(99));

    const auto result = validateStartupPreferences(invalid);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "startup_capabilities_invalid");
}

TEST(StartupPreferences, ValidationRequiresPositiveFiniteAppliedFrameRate) {
    auto missing = preferences();
    missing.lastApplied.requestedFps.reset();
    const auto missingResult = validateStartupPreferences(missing);
    ASSERT_FALSE(missingResult.hasValue());
    EXPECT_EQ(missingResult.error().code, "startup_applied_frame_rate_invalid");

    auto nonpositive = preferences();
    nonpositive.lastApplied.requestedFps = 0.0;
    const auto nonpositiveResult = validateStartupPreferences(nonpositive);
    ASSERT_FALSE(nonpositiveResult.hasValue());
    EXPECT_EQ(nonpositiveResult.error().code, "startup_applied_frame_rate_invalid");
}

}  // namespace
}  // namespace lumora::application
