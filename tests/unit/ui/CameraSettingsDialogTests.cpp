#include <lumora/ui/CameraSettingsDialog.hpp>
#include <lumora/application/StartupPreferences.hpp>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QPixmap>
#include <QSpinBox>
#include <gtest/gtest.h>
#include <climits>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace lumora::ui {
namespace {
CameraStartupPanelPresentation settingsPresentation() {
    const core::SourcePixelFormat format{"Mono8", 0x01080001U, 8U, 255U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
    camera::CameraConfiguration request{format, {8U, 4U, 640U, 480U}, 30.0,
        {camera::ExposureMode::Manual, 1000.1234567890123},
        {camera::GainMode::Manual, 2.123456789012345}, camera::AcquisitionMode::Continuous};
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->actualIdentity = camera::CameraId{"camera-settings-1"};
    status->sessionGeneration = 17U;
    status->requestedRevision = 3U;
    status->appliedRevision = 3U;
    status->requestedConfiguration = request;
    auto actual = request;
    actual.exposure.requestedMicroseconds = 999.5;
    status->currentConfiguration = actual;
    status->appliedConfiguration = camera::AppliedCameraConfiguration{request, actual};
    status->capabilities = camera::CameraCapabilities{{format},
        {{0U, 0U, 16U, 16U}, {100U, 100U, 1920U, 1080U}, {2U, 2U, 2U, 2U}},
        {1.0, 60.0, 1.0, camera::ControlAccess::WritableStopped},
        {10.0, 10000.0, 0.125, camera::ControlAccess::WritableStopped},
        {camera::ExposureMode::Manual, camera::ExposureMode::Auto},
        {-6.0, 24.0, 0.25, camera::ControlAccess::WritableStopped},
        {camera::GainMode::Manual, camera::GainMode::Auto}};
    CameraStartupPanelPresentation result;
    result.cameraStatus = status;
    result.requestedConfiguration = request;
    result.selectedCameraId = status->actualIdentity;
    result.preferencesLoadCompleted = true;
    return result;
}

TEST(CameraSettingsDialog, EditsRemainLocalAndApplyEmitsCompleteTaggedRequest) {
    CameraSettingsDialog dialog;
    auto presentation = settingsPresentation();
    dialog.setPresentation(presentation);
    int count = 0;
    std::uint64_t generation = 0;
    camera::CameraId id;
    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t value, camera::CameraId source, camera::CameraConfiguration request) {
            ++count; generation = value; id = std::move(source); emitted = std::move(request);
        });
    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* gain = dialog.findChild<QDoubleSpinBox*>("cameraGainValue");
    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure, nullptr); ASSERT_NE(gain, nullptr); ASSERT_NE(frameRate, nullptr); ASSERT_NE(apply, nullptr);
    EXPECT_DOUBLE_EQ(exposure->minimum(), 10.0); EXPECT_DOUBLE_EQ(exposure->maximum(), 10000.0);
    EXPECT_DOUBLE_EQ(exposure->singleStep(), 0.125); EXPECT_DOUBLE_EQ(gain->singleStep(), 0.25);
    EXPECT_DOUBLE_EQ(frameRate->minimum(), 1.0); EXPECT_DOUBLE_EQ(frameRate->maximum(), 60.0);
    EXPECT_DOUBLE_EQ(frameRate->singleStep(), 1.0);
    exposure->setValue(2468.875); gain->setValue(-1.375); frameRate->setValue(1.25);
    EXPECT_EQ(count, 0);
    dialog.setPresentation(presentation);
    EXPECT_DOUBLE_EQ(exposure->value(), 2468.875);
    EXPECT_DOUBLE_EQ(frameRate->value(), 1.25);
    ASSERT_TRUE(apply->isEnabled()); apply->click();
    ASSERT_EQ(count, 1); ASSERT_TRUE(emitted);
    EXPECT_EQ(generation, 17U); EXPECT_EQ(id.value, "camera-settings-1");
    auto expected = *presentation.requestedConfiguration;
    expected.exposure.requestedMicroseconds = 2468.875; expected.gain.requestedDb = -1.375;
    expected.requestedFps = 1.25;
    EXPECT_TRUE(application::cameraConfigurationsEqual(*emitted, expected));
}

TEST(CameraSettingsDialog, AdvertisedFormatAndRoiEditorsEmitExactCompleteRequest) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->pixelFormats.push_back({"Mono12", 0x01100005U, 12U, 4095U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt16});
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* format = dialog.findChild<QComboBox*>("cameraPixelFormat");
    auto* roiX = dialog.findChild<QSpinBox*>("cameraRoiX");
    auto* roiY = dialog.findChild<QSpinBox*>("cameraRoiY");
    auto* roiWidth = dialog.findChild<QSpinBox*>("cameraRoiWidth");
    auto* roiHeight = dialog.findChild<QSpinBox*>("cameraRoiHeight");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(format, nullptr);
    ASSERT_NE(roiX, nullptr);
    ASSERT_NE(roiY, nullptr);
    ASSERT_NE(roiWidth, nullptr);
    ASSERT_NE(roiHeight, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_EQ(format->count(), 2);
    EXPECT_TRUE(format->itemText(1).contains("Mono12"));
    EXPECT_TRUE(format->itemText(1).contains("12"));

    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
            emitted = std::move(request);
        });

    format->setCurrentIndex(1);
    roiX->setValue(12);
    roiY->setValue(8);
    roiWidth->setValue(320);
    roiHeight->setValue(240);

    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(emitted);
    EXPECT_EQ(emitted->roi.x, 12U);
    EXPECT_EQ(emitted->roi.y, 8U);
    EXPECT_EQ(emitted->roi.width, 320U);
    EXPECT_EQ(emitted->roi.height, 240U);
    EXPECT_EQ(emitted->pixelFormat.canonicalName, "Mono12");
    EXPECT_EQ(emitted->pixelFormat.canonicalEncoding, 0x01100005U);
    EXPECT_EQ(emitted->pixelFormat.validBits, 12U);
    EXPECT_EQ(emitted->pixelFormat.sampleMaximum, 4095U);
    EXPECT_EQ(emitted->pixelFormat.packing, core::SourcePacking::Unpacked);
    EXPECT_EQ(emitted->pixelFormat.alignment, core::BitAlignment::LeastSignificant);
    EXPECT_EQ(emitted->pixelFormat.applicationStorage, core::StorageType::UInt16);
}

TEST(CameraSettingsDialog, RoiEditorsExposeIncrementsAndRejectMisalignmentAndContainment) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->roi = {
        {0U, 0U, 16U, 16U}, {100U, 100U, 1920U, 1080U}, {4U, 2U, 8U, 4U}};
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* roiX = dialog.findChild<QSpinBox*>("cameraRoiX");
    auto* roiY = dialog.findChild<QSpinBox*>("cameraRoiY");
    auto* roiWidth = dialog.findChild<QSpinBox*>("cameraRoiWidth");
    auto* roiHeight = dialog.findChild<QSpinBox*>("cameraRoiHeight");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(roiX, nullptr);
    ASSERT_NE(roiY, nullptr);
    ASSERT_NE(roiWidth, nullptr);
    ASSERT_NE(roiHeight, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_EQ(roiX->singleStep(), 4);
    EXPECT_EQ(roiY->singleStep(), 2);
    EXPECT_EQ(roiWidth->singleStep(), 8);
    EXPECT_EQ(roiHeight->singleStep(), 4);

    int emissionCount = 0;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration) {
            ++emissionCount;
        });

    roiWidth->setValue(321);
    EXPECT_FALSE(apply->isEnabled());
    apply->click();
    EXPECT_EQ(emissionCount, 0);

    roiWidth->setValue(320);
    EXPECT_TRUE(apply->isEnabled());
    roiX->setValue(100);
    roiWidth->setValue(1920);
    EXPECT_FALSE(apply->isEnabled());
    apply->click();
    EXPECT_EQ(emissionCount, 0);
}

TEST(CameraSettingsDialog, FixedAndUnavailableFactsNormalizeWithoutBlockingWritableExposure) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->pixelFormatAccess = camera::ControlAccess::ReadOnly;
    status->capabilities->roi.access = camera::ControlAccess::ReadOnly;
    status->capabilities->frameRate.access = camera::ControlAccess::ReadOnly;
    status->capabilities->exposureModeAccess = camera::ControlAccess::ReadOnly;
    status->capabilities->exposure.access = camera::ControlAccess::WritableStopped;
    status->capabilities->gainModes.clear();
    status->capabilities->gainModeAccess = camera::ControlAccess::Unavailable;
    status->capabilities->gain = {0.0, 0.0, 0.0, camera::ControlAccess::Unavailable};
    auto current = *status->currentConfiguration;
    current.roi.x = 10U;
    current.requestedFps = 27.0;
    current.gain = {std::nullopt, std::nullopt};
    status->currentConfiguration = current;
    presentation.cameraStatus = status;

    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);
    auto* format = dialog.findChild<QComboBox*>("cameraPixelFormat");
    auto* roiX = dialog.findChild<QSpinBox*>("cameraRoiX");
    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* exposureMode = dialog.findChild<QComboBox*>("cameraExposureMode");
    auto* exposureValue = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* gainMode = dialog.findChild<QComboBox*>("cameraGainMode");
    auto* gainValue = dialog.findChild<QDoubleSpinBox*>("cameraGainValue");
    auto* frameReason = dialog.findChild<QLabel*>("cameraFrameRateReason");
    auto* exposureReason = dialog.findChild<QLabel*>("cameraExposureReason");
    auto* gainReason = dialog.findChild<QLabel*>("cameraGainReason");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(format, nullptr);
    ASSERT_NE(roiX, nullptr);
    ASSERT_NE(frameRate, nullptr);
    ASSERT_NE(exposureMode, nullptr);
    ASSERT_NE(exposureValue, nullptr);
    ASSERT_NE(gainMode, nullptr);
    ASSERT_NE(gainValue, nullptr);
    ASSERT_NE(frameReason, nullptr);
    ASSERT_NE(exposureReason, nullptr);
    ASSERT_NE(gainReason, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(format->isEnabled());
    EXPECT_FALSE(roiX->isEnabled());
    EXPECT_FALSE(frameRate->isEnabled());
    EXPECT_DOUBLE_EQ(frameRate->value(), 27.0);
    EXPECT_FALSE(exposureMode->isEnabled());
    EXPECT_TRUE(exposureValue->isEnabled());
    EXPECT_FALSE(gainMode->isEnabled());
    EXPECT_FALSE(gainValue->isEnabled());
    EXPECT_TRUE(frameReason->text().contains("read-only", Qt::CaseInsensitive));
    EXPECT_TRUE(exposureReason->text().contains("mode", Qt::CaseInsensitive));
    EXPECT_TRUE(exposureReason->text().contains("value", Qt::CaseInsensitive));
    EXPECT_TRUE(gainReason->text().contains("unavailable", Qt::CaseInsensitive));

    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
            emitted = std::move(request);
        });
    exposureValue->setValue(3456.0);
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(emitted);
    EXPECT_EQ(emitted->roi.x, 10U);
    EXPECT_EQ(emitted->requestedFps, 27.0);
    EXPECT_EQ(emitted->exposure.mode, camera::ExposureMode::Manual);
    EXPECT_EQ(emitted->exposure.requestedMicroseconds, 3456.0);
    EXPECT_FALSE(emitted->gain.mode);
    EXPECT_FALSE(emitted->gain.requestedDb);
}

TEST(CameraSettingsDialog, InvalidWritableDraftCanBeRepairedWithoutReplacingIt) {
    auto presentation = settingsPresentation();
    presentation.requestedConfiguration->exposure.requestedMicroseconds = 20000.0;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(exposure->isEnabled());
    EXPECT_FALSE(apply->isEnabled());

    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
            emitted = std::move(request);
        });
    exposure->setValue(2345.0);
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(emitted);
    EXPECT_EQ(emitted->exposure.requestedMicroseconds, 2345.0);
}

TEST(CameraSettingsDialog, UnavailableFrameRateAdjustmentStillShowsCurrentPacingFact) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->frameRate.access = camera::ControlAccess::Unavailable;
    status->currentConfiguration->requestedFps = 27.0;
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* reason = dialog.findChild<QLabel*>("cameraFrameRateReason");
    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(frameRate, nullptr);
    ASSERT_NE(reason, nullptr);
    ASSERT_NE(exposure, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(frameRate->isEnabled());
    EXPECT_DOUBLE_EQ(frameRate->value(), 27.0);
    EXPECT_TRUE(reason->text().contains("unavailable", Qt::CaseInsensitive));
    EXPECT_TRUE(exposure->isEnabled());
    exposure->setValue(2345.0);
    EXPECT_TRUE(apply->isEnabled());
}

TEST(CameraSettingsDialog, FixedModeCanExposeWritableMissingValueForRepair) {
    auto presentation = settingsPresentation();
    presentation.requestedConfiguration->exposure = {
        camera::ExposureMode::Auto, std::nullopt};
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->exposureModeAccess = camera::ControlAccess::ReadOnly;
    status->capabilities->exposure.access = camera::ControlAccess::WritableStopped;
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* mode = dialog.findChild<QComboBox*>("cameraExposureMode");
    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(exposure, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(mode->isEnabled());
    EXPECT_EQ(mode->currentData().toInt(), static_cast<int>(camera::ExposureMode::Manual));
    EXPECT_TRUE(exposure->isEnabled());
    EXPECT_FALSE(apply->isEnabled());

    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
            emitted = std::move(request);
        });
    exposure->setValue(2345.0);
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(emitted);
    EXPECT_EQ(emitted->exposure.mode, camera::ExposureMode::Manual);
    EXPECT_EQ(emitted->exposure.requestedMicroseconds, 2345.0);
}

TEST(CameraSettingsDialog, CapturedSourceShowsPhysicalCameraIdentity) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->discoveredDescriptors.push_back({{"camera-settings-1"},
        {"Lumora", "Scientific Camera", "SERIAL-42", "USB3", {}}, true});
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* source = dialog.findChild<QLabel*>("cameraSettingsSource");
    ASSERT_NE(source, nullptr);
    EXPECT_TRUE(source->text().contains("Lumora"));
    EXPECT_TRUE(source->text().contains("Scientific Camera"));
    EXPECT_TRUE(source->text().contains("SERIAL-42"));
}

TEST(CameraSettingsDialog, WritableModeAndFixedValueRemainIndependent) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->exposureModeAccess = camera::ControlAccess::WritableStopped;
    status->capabilities->exposure.access = camera::ControlAccess::ReadOnly;
    presentation.cameraStatus = status;

    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);
    auto* mode = dialog.findChild<QComboBox*>("cameraExposureMode");
    auto* value = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* reason = dialog.findChild<QLabel*>("cameraExposureReason");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(reason, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(mode->isEnabled());
    EXPECT_FALSE(value->isEnabled());
    EXPECT_DOUBLE_EQ(value->value(), 999.5);
    EXPECT_TRUE(reason->text().contains("value", Qt::CaseInsensitive));
    EXPECT_TRUE(reason->text().contains("read-only", Qt::CaseInsensitive));

    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
            emitted = std::move(request);
        });
    mode->setCurrentIndex(mode->findData(static_cast<int>(camera::ExposureMode::Auto)));
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(emitted);
    EXPECT_EQ(emitted->exposure.mode, camera::ExposureMode::Auto);
    EXPECT_FALSE(emitted->exposure.requestedMicroseconds);
}

TEST(CameraSettingsDialog, UnrepresentableUnsignedRoiCapabilityFailsClosed) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->roi.maximum.x = static_cast<std::uint32_t>(INT_MAX) + 1U;
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* roiX = dialog.findChild<QSpinBox*>("cameraRoiX");
    auto* roiY = dialog.findChild<QSpinBox*>("cameraRoiY");
    auto* roiWidth = dialog.findChild<QSpinBox*>("cameraRoiWidth");
    auto* roiHeight = dialog.findChild<QSpinBox*>("cameraRoiHeight");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
    ASSERT_NE(roiX, nullptr);
    ASSERT_NE(roiY, nullptr);
    ASSERT_NE(roiWidth, nullptr);
    ASSERT_NE(roiHeight, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(message, nullptr);
    EXPECT_FALSE(roiX->isEnabled());
    EXPECT_FALSE(roiY->isEnabled());
    EXPECT_FALSE(roiWidth->isEnabled());
    EXPECT_FALSE(roiHeight->isEnabled());
    EXPECT_FALSE(apply->isEnabled());
    EXPECT_TRUE(message->text().contains("range", Qt::CaseInsensitive));

    int emissionCount = 0;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration) {
            ++emissionCount;
        });
    apply->click();
    EXPECT_EQ(emissionCount, 0);
}

TEST(CameraSettingsDialog, UntouchedFractionalValuesRoundTripWithoutDisplayRounding) {
    CameraSettingsDialog dialog;
    auto presentation = settingsPresentation();
    presentation.requestedConfiguration->requestedFps = 30.123456789012345;
    dialog.setPresentation(presentation);
    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) { emitted = request; });
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton"); ASSERT_NE(apply, nullptr);
    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue"); ASSERT_NE(frameRate, nullptr);
    EXPECT_DOUBLE_EQ(frameRate->value(), 30.123456789012345);
    apply->click(); ASSERT_TRUE(emitted);
    EXPECT_TRUE(application::cameraConfigurationsEqual(*emitted, *presentation.requestedConfiguration));
}

TEST(CameraSettingsDialog, FrameRateUsesCapabilityBoundsAndSubmitsBothEndpoints) {
    for (const camera::NumericCapability capability : {
             camera::NumericCapability{1.0, 60.0, 1.0, camera::ControlAccess::WritableStopped},
             camera::NumericCapability{2.5, 45.5, 0.5, camera::ControlAccess::WritableStopped}}) {
        SCOPED_TRACE(capability.minimum);
        auto presentation = settingsPresentation();
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        status->capabilities->frameRate = capability;
        presentation.cameraStatus = status;
        CameraSettingsDialog dialog; dialog.setPresentation(presentation);
        auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        ASSERT_NE(frameRate, nullptr); ASSERT_NE(apply, nullptr);
        EXPECT_DOUBLE_EQ(frameRate->minimum(), capability.minimum);
        EXPECT_DOUBLE_EQ(frameRate->maximum(), capability.maximum);
        EXPECT_DOUBLE_EQ(frameRate->singleStep(), capability.increment);
        int count = 0;
        std::optional<camera::CameraConfiguration> emitted;
        QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
            [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) { ++count; emitted = request; });
        for (const double endpoint : {capability.minimum, capability.maximum}) {
            frameRate->setValue(endpoint);
            ASSERT_TRUE(apply->isEnabled()); apply->click(); ASSERT_TRUE(emitted);
            auto expected = *presentation.requestedConfiguration;
            expected.requestedFps = endpoint;
            EXPECT_TRUE(application::cameraConfigurationsEqual(*emitted, expected));
        }
        EXPECT_EQ(count, 2);
    }
}

TEST(CameraSettingsDialog, SubOneFrameRateEditsKeepEveryRequestedDigit) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->frameRate = {0.0001, 60.0, 0.0001,
        camera::ControlAccess::WritableStopped};
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog; dialog.setPresentation(presentation);
    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(frameRate, nullptr); ASSERT_NE(apply, nullptr);
    EXPECT_EQ(frameRate->minimum(), 0.0001);
    EXPECT_EQ(frameRate->maximum(), 60.0);
    EXPECT_EQ(frameRate->singleStep(), 0.0001);
    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) { emitted = request; });

    frameRate->setValue(0.0012345678901234567);

    EXPECT_EQ(frameRate->value(), 0.0012345678901234567);
    ASSERT_TRUE(apply->isEnabled()); apply->click(); ASSERT_TRUE(emitted);
    auto expected = *presentation.requestedConfiguration;
    expected.requestedFps = 0.0012345678901234567;
    EXPECT_EQ(emitted->requestedFps, expected.requestedFps);
    EXPECT_TRUE(application::cameraConfigurationsEqual(*emitted, expected));
}

TEST(CameraSettingsDialog, SmallPositiveFrameRateEndpointsRemainEditable) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->frameRate = {1e-20, 60.0, 1e-20,
        camera::ControlAccess::WritableStopped};
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog; dialog.setPresentation(presentation);
    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(frameRate, nullptr); ASSERT_NE(apply, nullptr);
    EXPECT_EQ(frameRate->minimum(), 1e-20);
    EXPECT_EQ(frameRate->maximum(), 60.0);
    EXPECT_EQ(frameRate->singleStep(), 1e-20);
    ASSERT_TRUE(frameRate->isEnabled());
    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) { emitted = request; });

    frameRate->setValue(1e-20);

    EXPECT_EQ(frameRate->value(), 1e-20);
    ASSERT_TRUE(apply->isEnabled()); apply->click(); ASSERT_TRUE(emitted);
    auto expected = *presentation.requestedConfiguration;
    expected.requestedFps = 1e-20;
    EXPECT_EQ(emitted->requestedFps, expected.requestedFps);
    EXPECT_TRUE(application::cameraConfigurationsEqual(*emitted, expected));
}

TEST(CameraSettingsDialog, UnrepresentableFrameRateRangesPreventSubmission) {
    for (const double minimum : {
             std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min()}) {
        SCOPED_TRACE(minimum);
        auto presentation = settingsPresentation();
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        status->capabilities->frameRate = {minimum, 60.0, minimum,
            camera::ControlAccess::WritableStopped};
        presentation.cameraStatus = status;
        CameraSettingsDialog dialog; dialog.setPresentation(presentation);
        auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
        ASSERT_NE(frameRate, nullptr); ASSERT_NE(apply, nullptr); ASSERT_NE(message, nullptr);
        EXPECT_FALSE(frameRate->isEnabled()); EXPECT_FALSE(apply->isEnabled());
        EXPECT_FALSE(message->text().isEmpty());
        int count = 0;
        QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
            [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration) { ++count; });
        apply->click(); EXPECT_EQ(count, 0);
    }
}

TEST(CameraSettingsDialog, AutoModesOmitManualValuesAndCancelEmitsNothing) {
    CameraSettingsDialog dialog; dialog.setPresentation(settingsPresentation());
    int count = 0;
    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) { ++count; emitted = request; });
    auto* exposure = dialog.findChild<QComboBox*>("cameraExposureMode");
    auto* gain = dialog.findChild<QComboBox*>("cameraGainMode");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure, nullptr); ASSERT_NE(gain, nullptr); ASSERT_NE(apply, nullptr);
    ASSERT_EQ(exposure->count(), 2); ASSERT_EQ(gain->count(), 2);
    exposure->setCurrentIndex(exposure->findData(static_cast<int>(camera::ExposureMode::Auto)));
    gain->setCurrentIndex(gain->findData(static_cast<int>(camera::GainMode::Auto)));
    EXPECT_EQ(count, 0); apply->click(); ASSERT_TRUE(emitted);
    EXPECT_EQ(emitted->exposure.mode, camera::ExposureMode::Auto);
    EXPECT_EQ(emitted->gain.mode, camera::GainMode::Auto);
    EXPECT_FALSE(emitted->exposure.requestedMicroseconds); EXPECT_FALSE(emitted->gain.requestedDb);
    dialog.reject(); EXPECT_EQ(count, 1);
}

TEST(CameraSettingsDialog, StreamingRequiresStopAndPendingOperationDisablesApply) {
    CameraSettingsDialog dialog; auto presentation = settingsPresentation(); dialog.setPresentation(presentation);
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* format = dialog.findChild<QComboBox*>("cameraPixelFormat");
    auto* roiWidth = dialog.findChild<QSpinBox*>("cameraRoiWidth");
    auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
    ASSERT_NE(apply, nullptr); ASSERT_NE(exposure, nullptr); ASSERT_NE(frameRate, nullptr);
    ASSERT_NE(format, nullptr); ASSERT_NE(roiWidth, nullptr); ASSERT_NE(message, nullptr);
    frameRate->setValue(1.25);
    int count = 0;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration) { ++count; });
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->state = application::CameraSessionState::Streaming; presentation.cameraStatus = status;
    dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled()); EXPECT_FALSE(exposure->isEnabled());
    EXPECT_FALSE(frameRate->isEnabled()); EXPECT_FALSE(format->isEnabled());
    EXPECT_FALSE(roiWidth->isEnabled()); apply->click(); EXPECT_EQ(count, 0);
    EXPECT_TRUE(message->text().contains("Stop"));
    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->state = application::CameraSessionState::ConnectedIdle; presentation.cameraStatus = status;
    presentation.ordinaryOperationPending = true; dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled()); EXPECT_FALSE(frameRate->isEnabled());
    EXPECT_FALSE(format->isEnabled()); EXPECT_FALSE(roiWidth->isEnabled());
    apply->click(); EXPECT_EQ(count, 0);
    presentation.ordinaryOperationPending = false; dialog.setPresentation(presentation);
    EXPECT_TRUE(apply->isEnabled()); EXPECT_TRUE(frameRate->isEnabled());
    EXPECT_TRUE(format->isEnabled()); EXPECT_TRUE(roiWidth->isEnabled());
    EXPECT_DOUBLE_EQ(frameRate->value(), 1.25);
}

TEST(CameraSettingsDialog, StreamingMixedAccessExplainsStopBeforeEditing) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(
        *presentation.cameraStatus);
    status->state = application::CameraSessionState::Streaming;
    status->capabilities->exposureModeAccess = camera::ControlAccess::ReadOnly;
    status->capabilities->exposure.access = camera::ControlAccess::WritableStopped;
    presentation.cameraStatus = std::move(status);

    CameraSettingsDialog dialog;
    dialog.setPresentation(std::move(presentation));
    auto* reason = dialog.findChild<QLabel*>("cameraExposureReason");
    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    ASSERT_NE(reason, nullptr);
    ASSERT_NE(exposure, nullptr);
    EXPECT_FALSE(exposure->isEnabled());
    EXPECT_TRUE(reason->text().contains("Stop", Qt::CaseInsensitive));
}

TEST(CameraSettingsDialog, FixedAutomaticModesDoNotClaimNumericValuesAreEditable) {
    for (const auto state : {application::CameraSessionState::ConnectedIdle,
             application::CameraSessionState::Streaming}) {
        auto presentation = settingsPresentation();
        auto status = std::make_shared<application::CameraStatusSnapshot>(
            *presentation.cameraStatus);
        status->state = state;
        status->capabilities->exposureModes = {camera::ExposureMode::Auto};
        status->capabilities->exposureModeAccess = camera::ControlAccess::ReadOnly;
        status->capabilities->exposure.access = camera::ControlAccess::WritableStopped;
        status->capabilities->gainModes = {camera::GainMode::Auto};
        status->capabilities->gainModeAccess = camera::ControlAccess::ReadOnly;
        status->capabilities->gain.access = camera::ControlAccess::WritableStopped;
        status->currentConfiguration->exposure = {camera::ExposureMode::Auto, std::nullopt};
        status->currentConfiguration->gain = {camera::GainMode::Auto, std::nullopt};
        presentation.cameraStatus = std::move(status);

        CameraSettingsDialog dialog;
        dialog.setPresentation(std::move(presentation));
        auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
        auto* gain = dialog.findChild<QDoubleSpinBox*>("cameraGainValue");
        auto* exposureReason = dialog.findChild<QLabel*>("cameraExposureReason");
        auto* gainReason = dialog.findChild<QLabel*>("cameraGainReason");
        ASSERT_NE(exposure, nullptr);
        ASSERT_NE(gain, nullptr);
        ASSERT_NE(exposureReason, nullptr);
        ASSERT_NE(gainReason, nullptr);
        EXPECT_FALSE(exposure->isEnabled());
        EXPECT_FALSE(gain->isEnabled());
        EXPECT_TRUE(exposureReason->text().contains("Automatic", Qt::CaseInsensitive));
        EXPECT_TRUE(gainReason->text().contains("Automatic", Qt::CaseInsensitive));
        EXPECT_FALSE(exposureReason->text().contains("edit", Qt::CaseInsensitive));
        EXPECT_FALSE(gainReason->text().contains("edit", Qt::CaseInsensitive));
    }
}

TEST(CameraSettingsDialog, SourceAndCapabilityReplacementPermanentlyInvalidateOpenDraft) {
    using Change = std::function<void(CameraStartupPanelPresentation&, application::CameraStatusSnapshot&)>;
    const std::vector<Change> changes{
        [](auto&, auto& s) { ++s.sessionGeneration; },
        [](auto&, auto& s) { s.actualIdentity = camera::CameraId{"replacement"}; },
        [](auto& p, auto&) { p.selectedCameraId = camera::CameraId{"replacement"}; },
        [](auto& p, auto&) { p.selectedCameraId.reset(); },
        [](auto&, auto& s) { s.capabilities->gain.maximum = 30; },
        [](auto&, auto& s) { s.capabilities->roi.increment.x = 4; },
        [](auto&, auto& s) { s.capabilities->pixelFormats.front().canonicalEncoding += 1; },
        [](auto&, auto& s) { s.capabilities->frameRate.minimum = 2; },
        [](auto&, auto& s) { s.capabilities->frameRate.maximum = 50; },
        [](auto&, auto& s) { s.capabilities->frameRate.increment = 0.5; },
        [](auto&, auto& s) {
            s.capabilities->frameRate.access = camera::ControlAccess::WritableStreaming;
        }};
    for (const auto& change : changes) {
        CameraSettingsDialog dialog; auto original = settingsPresentation(); dialog.setPresentation(original);
        auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue"); ASSERT_NE(frameRate, nullptr);
        frameRate->setValue(1.25);
        int count = 0;
        QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
            [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration) { ++count; });
        auto presentation = original;
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        change(presentation, *status); presentation.cameraStatus = status; dialog.setPresentation(presentation);
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
        ASSERT_NE(apply, nullptr); ASSERT_NE(message, nullptr);
        EXPECT_FALSE(apply->isEnabled()); EXPECT_TRUE(message->text().contains("reopen"));
        EXPECT_FALSE(frameRate->isEnabled()); apply->click(); EXPECT_EQ(count, 0);
        dialog.setPresentation(original); EXPECT_FALSE(apply->isEnabled());
        EXPECT_FALSE(frameRate->isEnabled()); EXPECT_DOUBLE_EQ(frameRate->value(), 1.25);
    }
}

TEST(CameraSettingsDialog, MissingSelectionCannotSubmitConnectedCameraSettings) {
    CameraSettingsDialog dialog; auto presentation = settingsPresentation();
    presentation.selectedCameraId.reset(); dialog.setPresentation(presentation);
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(apply, nullptr); EXPECT_FALSE(apply->isEnabled());
}

TEST(CameraSettingsDialog, ExternalRevisionInvalidatesButOwnSubmissionPreservesReadback) {
    for (const bool ownSubmission : {false, true}) {
        CameraSettingsDialog dialog; auto presentation = settingsPresentation(); dialog.setPresentation(presentation);
        auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
        auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual");
        ASSERT_NE(exposure, nullptr); ASSERT_NE(frameRate, nullptr); ASSERT_NE(apply, nullptr); ASSERT_NE(actual, nullptr);
        exposure->setValue(2000.5); frameRate->setValue(1.25);
        EXPECT_TRUE(actual->text().contains("30 fps"));
        if (ownSubmission) {
            apply->click();
            presentation.requestedConfiguration->exposure.requestedMicroseconds = 2000.5;
            presentation.requestedConfiguration->requestedFps = 1.25;
            presentation.ordinaryOperationPending = true;
            dialog.setPresentation(presentation);
        }
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        ++status->requestedRevision;
        status->requestedConfiguration->exposure.requestedMicroseconds = 2000.5;
        status->requestedConfiguration->requestedFps = 1.25;
        status->appliedRevision = status->requestedRevision;
        status->appliedConfiguration->requested = *status->requestedConfiguration;
        status->appliedConfiguration->actual.requestedFps = 1.0;
        status->currentConfiguration = status->appliedConfiguration->actual;
        presentation.requestedConfiguration = status->requestedConfiguration;
        presentation.cameraStatus = status; presentation.ordinaryOperationPending = false;
        dialog.setPresentation(presentation);
        EXPECT_EQ(apply->isEnabled(), ownSubmission);
        EXPECT_EQ(frameRate->isEnabled(), ownSubmission);
        EXPECT_DOUBLE_EQ(frameRate->value(), 1.25);
        EXPECT_TRUE(actual->text().contains("999.5"));
        EXPECT_TRUE(actual->text().contains("1 fps"));
        EXPECT_FALSE(actual->text().contains("1.25 fps"));
        EXPECT_FALSE(status->confirmedRevision);
    }
}

TEST(CameraSettingsDialog, OwnSuccessfulRebindShowsNewReadbackAndRequiresReopen) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->pixelFormats.push_back({"Mono12", 0x01100005U, 12U, 4095U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt16});
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* format = dialog.findChild<QComboBox*>("cameraPixelFormat");
    auto* roiWidth = dialog.findChild<QSpinBox*>("cameraRoiWidth");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual");
    auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
    ASSERT_NE(format, nullptr);
    ASSERT_NE(roiWidth, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(message, nullptr);

    std::optional<camera::CameraConfiguration> submitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
            submitted = std::move(request);
        });
    format->setCurrentIndex(1);
    roiWidth->setValue(320);
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(submitted);

    // The controller publishes admission and the desired request together.
    presentation.requestedConfiguration = submitted;
    presentation.ordinaryOperationPending = true;
    dialog.setPresentation(presentation);

    auto rebound = std::make_shared<application::CameraStatusSnapshot>(*status);
    rebound->sessionGeneration = 18U;
    rebound->requestedRevision = 4U;
    rebound->appliedRevision = 4U;
    rebound->confirmedRevision.reset();
    rebound->requestedConfiguration = submitted;
    rebound->appliedConfiguration = camera::AppliedCameraConfiguration{*submitted, *submitted};
    rebound->currentConfiguration = *submitted;
    presentation.cameraStatus = rebound;
    presentation.ordinaryOperationPending = false;
    dialog.setPresentation(presentation);

    EXPECT_FALSE(apply->isEnabled());
    EXPECT_FALSE(format->isEnabled());
    EXPECT_FALSE(roiWidth->isEnabled());
    EXPECT_TRUE(actual->text().contains("Mono12"));
    EXPECT_TRUE(actual->text().contains("12"));
    EXPECT_TRUE(actual->text().contains("320"));
    for (const auto& details : {actual->accessibleDescription(), actual->toolTip()}) {
        EXPECT_TRUE(details.contains("4095"));
        EXPECT_TRUE(details.contains("01100005"));
        EXPECT_TRUE(details.contains("unpacked", Qt::CaseInsensitive));
        EXPECT_TRUE(details.contains("least-significant", Qt::CaseInsensitive));
    }
    EXPECT_TRUE(message->text().contains("reopen", Qt::CaseInsensitive));
    EXPECT_TRUE(message->text().contains("Confirm"));
    EXPECT_TRUE(message->text().contains("Start"));

    auto changedAfterSuccess = std::make_shared<application::CameraStatusSnapshot>(*rebound);
    changedAfterSuccess->appliedConfiguration->actual.pixelFormat.alignment =
        core::BitAlignment::MostSignificant;
    changedAfterSuccess->currentConfiguration = changedAfterSuccess->appliedConfiguration->actual;
    presentation.cameraStatus = changedAfterSuccess;
    dialog.setPresentation(presentation);
    EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
    EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));

    changedAfterSuccess->appliedConfiguration->actual = *submitted;
    changedAfterSuccess->currentConfiguration = *submitted;
    dialog.setPresentation(presentation);
    EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
    EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));
}

TEST(CameraSettingsDialog, RebindWithMismatchedActualSourceModeFailsClosed) {
    using ActualChange = std::function<void(camera::CameraConfiguration&)>;
    int caseIndex = 0;
    for (const ActualChange& change : std::vector<ActualChange>{
        [](auto& actual) { actual.roi.width = 318U; },
        [](auto& actual) { actual.pixelFormat.canonicalEncoding = 0x01100006U; }}) {
        SCOPED_TRACE(caseIndex++);
        auto presentation = settingsPresentation();
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        status->capabilities->pixelFormats.push_back({"Mono12", 0x01100005U, 12U, 4095U,
            core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
            core::StorageType::UInt16});
        presentation.cameraStatus = status;
        CameraSettingsDialog dialog;
        dialog.setPresentation(presentation);

        auto* format = dialog.findChild<QComboBox*>("cameraPixelFormat");
        auto* roiWidth = dialog.findChild<QSpinBox*>("cameraRoiWidth");
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual");
        auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
        ASSERT_NE(format, nullptr);
        ASSERT_NE(roiWidth, nullptr);
        ASSERT_NE(apply, nullptr);
        ASSERT_NE(actual, nullptr);
        ASSERT_NE(message, nullptr);

        std::optional<camera::CameraConfiguration> submitted;
        QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
            [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
                submitted = std::move(request);
            });
        format->setCurrentIndex(1);
        roiWidth->setValue(320);
        ASSERT_TRUE(apply->isEnabled());
        apply->click();
        ASSERT_TRUE(submitted);

        presentation.requestedConfiguration = submitted;
        presentation.ordinaryOperationPending = true;
        dialog.setPresentation(presentation);

        auto rebound = std::make_shared<application::CameraStatusSnapshot>(*status);
        rebound->sessionGeneration = 18U;
        rebound->requestedRevision = 4U;
        rebound->appliedRevision = 4U;
        rebound->requestedConfiguration = submitted;
        rebound->appliedConfiguration = camera::AppliedCameraConfiguration{*submitted, *submitted};
        change(rebound->appliedConfiguration->actual);
        rebound->currentConfiguration = rebound->appliedConfiguration->actual;
        presentation.cameraStatus = rebound;
        presentation.ordinaryOperationPending = false;
        dialog.setPresentation(presentation);

        EXPECT_FALSE(apply->isEnabled());
        EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
        EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));

        // Once correlation fails, a later corrected snapshot cannot rehabilitate this dialog.
        rebound->appliedConfiguration->actual = *submitted;
        rebound->currentConfiguration = *submitted;
        dialog.setPresentation(presentation);
        EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
        EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));
    }
}

TEST(CameraSettingsDialog, RebindRequiresImmediateSingleGenerationTransition) {
    for (const bool completeFirst : {false, true}) {
        SCOPED_TRACE(completeFirst ? "second completion" : "skipped first generation");
        auto presentation = settingsPresentation();
        const auto status = presentation.cameraStatus;
        CameraSettingsDialog dialog;
        dialog.setPresentation(presentation);

        auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual");
        auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
        ASSERT_NE(frameRate, nullptr);
        ASSERT_NE(apply, nullptr);
        ASSERT_NE(actual, nullptr);
        ASSERT_NE(message, nullptr);

        std::optional<camera::CameraConfiguration> submitted;
        QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
            [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
                submitted = std::move(request);
            });
        frameRate->setValue(1.25);
        ASSERT_TRUE(apply->isEnabled());
        apply->click();
        ASSERT_TRUE(submitted);

        presentation.requestedConfiguration = submitted;
        presentation.ordinaryOperationPending = true;
        dialog.setPresentation(presentation);

        auto completion = std::make_shared<application::CameraStatusSnapshot>(*status);
        completion->sessionGeneration = completeFirst ? 18U : 19U;
        completion->requestedRevision = 4U;
        completion->appliedRevision = 4U;
        completion->requestedConfiguration = submitted;
        completion->appliedConfiguration = camera::AppliedCameraConfiguration{*submitted, *submitted};
        completion->currentConfiguration = *submitted;
        presentation.cameraStatus = completion;
        presentation.ordinaryOperationPending = false;
        if (completeFirst) {
            dialog.setPresentation(presentation);
            ASSERT_TRUE(message->text().contains("were applied", Qt::CaseInsensitive));

            completion = std::make_shared<application::CameraStatusSnapshot>(*completion);
            completion->sessionGeneration = 19U;
            completion->requestedRevision = 5U;
            completion->appliedRevision = 5U;
            presentation.cameraStatus = completion;
        }
        dialog.setPresentation(presentation);

        EXPECT_FALSE(apply->isEnabled());
        EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
        EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));

        // A later snapshot from the expected generation cannot rehabilitate the dialog.
        auto expectedGeneration = std::make_shared<application::CameraStatusSnapshot>(*completion);
        expectedGeneration->sessionGeneration = 18U;
        expectedGeneration->requestedRevision = 4U;
        expectedGeneration->appliedRevision = 4U;
        presentation.cameraStatus = expectedGeneration;
        dialog.setPresentation(presentation);
        EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
        EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));
    }
}

TEST(CameraSettingsDialog, RebindWithStaleSameCameraRevisionFailsClosed) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->pixelFormats.push_back({"Mono12", 0x01100005U, 12U, 4095U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt16});
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* format = dialog.findChild<QComboBox*>("cameraPixelFormat");
    auto* roiWidth = dialog.findChild<QSpinBox*>("cameraRoiWidth");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual");
    auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
    ASSERT_NE(format, nullptr);
    ASSERT_NE(roiWidth, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(message, nullptr);

    std::optional<camera::CameraConfiguration> submitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) {
            submitted = std::move(request);
        });
    format->setCurrentIndex(1);
    roiWidth->setValue(320);
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    ASSERT_TRUE(submitted);

    presentation.requestedConfiguration = submitted;
    presentation.ordinaryOperationPending = true;
    dialog.setPresentation(presentation);

    auto rebound = std::make_shared<application::CameraStatusSnapshot>(*status);
    rebound->sessionGeneration = 18U;
    rebound->requestedRevision = status->requestedRevision;
    rebound->appliedRevision = status->requestedRevision;
    rebound->requestedConfiguration = submitted;
    rebound->appliedConfiguration = camera::AppliedCameraConfiguration{*submitted, *submitted};
    rebound->currentConfiguration = *submitted;
    presentation.cameraStatus = rebound;
    presentation.ordinaryOperationPending = false;
    dialog.setPresentation(presentation);

    EXPECT_FALSE(apply->isEnabled());
    EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
    EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));

    // The stale transition permanently invalidates this dialog, even if the revision advances later.
    rebound->requestedRevision = status->requestedRevision + 1U;
    rebound->appliedRevision = rebound->requestedRevision;
    dialog.setPresentation(presentation);
    EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
    EXPECT_FALSE(message->text().contains("were applied", Qt::CaseInsensitive));
}

TEST(CameraSettingsDialog, UnrelatedGenerationNeverShowsReplacementReadback) {
    auto presentation = settingsPresentation();
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);
    auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_TRUE(actual->text().contains("30 fps"));

    auto replacement = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    replacement->sessionGeneration = 18U;
    replacement->actualIdentity = camera::CameraId{"replacement"};
    replacement->requestedConfiguration->roi.width = 320U;
    replacement->appliedConfiguration->actual.roi.width = 320U;
    replacement->currentConfiguration = replacement->appliedConfiguration->actual;
    presentation.cameraStatus = replacement;
    presentation.selectedCameraId = replacement->actualIdentity;
    dialog.setPresentation(presentation);

    EXPECT_FALSE(apply->isEnabled());
    EXPECT_TRUE(actual->text().contains("unavailable", Qt::CaseInsensitive));
    EXPECT_FALSE(actual->text().contains("320"));
}

TEST(CameraSettingsDialog, InvalidCapabilitiesAndRequestsCannotSilentlyClampIntoSubmission) {
    using Change = std::function<void(CameraStartupPanelPresentation&, application::CameraStatusSnapshot&)>;
    for (const Change& change : std::vector<Change>{
        [](auto&, auto& s) { s.capabilities->pixelFormats.front().validBits = 0U; },
        [](auto&, auto& s) { s.capabilities->roi.increment.width = 0U; },
        [](auto&, auto& s) { s.capabilities->gainModes.clear(); },
        [](auto&, auto& s) { s.capabilities->exposure.increment = 0; },
        [](auto&, auto& s) { s.capabilities->frameRate.minimum = 0; },
        [](auto&, auto& s) { s.capabilities->frameRate.maximum = std::numeric_limits<double>::infinity(); },
        [](auto&, auto& s) { s.capabilities->frameRate.increment = 0; },
        [](auto&, auto& s) { s.capabilities->frameRate.increment = std::numeric_limits<double>::quiet_NaN(); },
        [](auto& p, auto&) { p.requestedConfiguration->gain.requestedDb = 999; },
        [](auto& p, auto&) { p.requestedConfiguration->exposure.requestedMicroseconds = std::numeric_limits<double>::quiet_NaN(); },
        [](auto& p, auto&) { p.requestedConfiguration->gain.requestedDb.reset(); },
        [](auto& p, auto&) { p.requestedConfiguration->requestedFps = 0; },
        [](auto& p, auto&) { p.requestedConfiguration->requestedFps = -1; },
        [](auto& p, auto&) { p.requestedConfiguration->requestedFps = 0.999; },
        [](auto& p, auto&) { p.requestedConfiguration->requestedFps = 60.001; },
        [](auto& p, auto&) { p.requestedConfiguration->requestedFps = std::numeric_limits<double>::infinity(); },
        [](auto& p, auto&) { p.requestedConfiguration->requestedFps = std::numeric_limits<double>::quiet_NaN(); },
        [](auto& p, auto&) { p.requestedConfiguration->requestedFps.reset(); }}) {
        auto presentation = settingsPresentation();
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        change(presentation, *status); presentation.cameraStatus = status;
        CameraSettingsDialog dialog; dialog.setPresentation(presentation);
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
        ASSERT_NE(apply, nullptr); ASSERT_NE(message, nullptr);
        EXPECT_FALSE(apply->isEnabled()); EXPECT_FALSE(message->text().isEmpty());
        int count = 0; QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
            [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration) { ++count; });
        if (!presentation.requestedConfiguration->requestedFps) {
            auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue"); ASSERT_NE(exposure, nullptr);
            exposure->setValue(2000.5);
            EXPECT_FALSE(apply->isEnabled());
        }
        apply->click(); EXPECT_EQ(count, 0);
    }
}

TEST(CameraSettingsDialog, ReadOnlySourceFieldsAndActualArePlainAccessibleText) {
    CameraSettingsDialog dialog; dialog.setPresentation(settingsPresentation());
    for (const auto* name : {"cameraSettingsSource", "cameraSettingsFixedFields", "cameraSettingsActual", "cameraSettingsStatus"}) {
        auto* label = dialog.findChild<QLabel*>(name); ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->textFormat(), Qt::PlainText); EXPECT_FALSE(label->accessibleName().isEmpty());
    }
    const auto text = dialog.findChild<QLabel*>("cameraSettingsFixedFields")->text();
    EXPECT_FALSE(text.contains("fps", Qt::CaseInsensitive));
    EXPECT_TRUE(text.contains("640")); EXPECT_TRUE(text.contains("480"));
    EXPECT_TRUE(text.contains("8")); EXPECT_TRUE(text.contains("4"));
    EXPECT_TRUE(text.contains("Mono8")); EXPECT_TRUE(text.contains("Continuous"));
    EXPECT_TRUE(text.contains("read-only", Qt::CaseInsensitive));
    EXPECT_TRUE(dialog.findChild<QLabel*>("cameraSettingsSource")->text().contains("camera-settings-1"));
    EXPECT_TRUE(dialog.findChild<QLabel*>("cameraSettingsActual")->text().contains("30 fps"));
    for (const auto* name : {"cameraPixelFormat", "cameraRoiX", "cameraRoiY",
             "cameraRoiWidth", "cameraRoiHeight", "cameraExposureMode",
             "cameraGainMode", "cameraExposureValue", "cameraGainValue",
             "cameraFrameRateValue"}) {
        auto* widget = dialog.findChild<QWidget*>(name); ASSERT_NE(widget, nullptr);
        EXPECT_FALSE(widget->accessibleName().isEmpty());
        bool hasBuddy = false;
        for (auto* label : dialog.findChildren<QLabel*>()) hasBuddy = hasBuddy || label->buddy() == widget;
        EXPECT_TRUE(hasBuddy);
    }
}

TEST(CameraSettingsDialog, NativeDialogLayoutFitsSupportedSizesAndCanBeCaptured) {
    CameraSettingsDialog dialog; dialog.setPresentation(settingsPresentation());
    for (const QSize size : {QSize{560, 560}, QSize{720, 640}}) {
        dialog.resize(size); dialog.show(); QCoreApplication::processEvents();
        EXPECT_EQ(dialog.size(), size); EXPECT_FALSE(dialog.isModal());
        for (const auto* name : {"cameraPixelFormat", "cameraRoiX", "cameraRoiY",
                 "cameraRoiWidth", "cameraRoiHeight", "cameraExposureMode",
                 "cameraGainMode", "cameraExposureValue", "cameraGainValue", "cameraFrameRateValue",
                 "cameraSettingsSource", "cameraSettingsFixedFields", "cameraSettingsActual", "cameraSettingsStatus",
                 "applyCameraSettingsButton", "closeCameraSettingsButton"}) {
            auto* widget = dialog.findChild<QWidget*>(name); ASSERT_NE(widget, nullptr);
            EXPECT_TRUE(widget->isVisibleTo(&dialog));
            EXPECT_TRUE(dialog.rect().contains(QRect{widget->mapTo(&dialog, QPoint{0, 0}), widget->size()})) << name;
        }
        const auto prefix = qEnvironmentVariable("LUMORA_CAMERA_SETTINGS_SCREENSHOT");
        if (!prefix.isEmpty()) {
            const auto path = QStringLiteral("%1-%2x%3.bmp").arg(prefix).arg(size.width()).arg(size.height());
            EXPECT_TRUE(dialog.grab().save(path, "BMP"));
        }
    }
}
TEST(CameraSettingsDialog, RejectedIntentCannotAuthorizeALaterExternalRevision) {
    CameraSettingsDialog dialog; auto presentation = settingsPresentation(); dialog.setPresentation(presentation);
    auto* frameRate = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(frameRate, nullptr); ASSERT_NE(apply, nullptr);
    frameRate->setValue(1.25); apply->click();
    // Admission rejection preserves the old desired request and has no pending command.
    dialog.setPresentation(presentation);
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    ++status->requestedRevision;
    status->requestedConfiguration->requestedFps = 1.25;
    presentation.requestedConfiguration = status->requestedConfiguration;
    presentation.cameraStatus = status;
    dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled());
}

}  // namespace
}  // namespace lumora::ui

namespace lumora::ui {
namespace {
TEST(CameraSettingsDialog, EditingIntentIsSourceTaggedAndIncludesUncommittedNumericText) {
    for(const bool commitValue : {false,true}) {
        SCOPED_TRACE(commitValue);
        CameraSettingsDialog dialog;int edits=0;
        QObject::connect(&dialog,&CameraSettingsDialog::settingsEditingStarted,
            [&](std::uint64_t generation,camera::CameraId id) {
                ++edits;EXPECT_EQ(generation,17U);EXPECT_EQ(id.value,"camera-settings-1");
            });
        const auto presentation=settingsPresentation();dialog.setPresentation(presentation);dialog.setPresentation(presentation);
        EXPECT_EQ(edits,0);
        auto* exposure=dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");ASSERT_NE(exposure,nullptr);
        if(commitValue) exposure->setValue(2345);
        else {
            auto* input=exposure->findChild<QLineEdit*>();ASSERT_NE(input,nullptr);
            input->selectAll();QKeyEvent event(QEvent::KeyPress,Qt::Key_2,Qt::NoModifier,QStringLiteral("2345"));
            QCoreApplication::sendEvent(input,&event);
            ASSERT_EQ(input->text(),QStringLiteral("2345"));
            EXPECT_NE(exposure->value(),2345);
        }
        EXPECT_EQ(edits,1);
        exposure->setValue(3456);dialog.setPresentation(presentation);
        EXPECT_EQ(edits,1);
    }
}

TEST(CameraSettingsDialog, FixedReadbackDriftInvalidatesWithoutReplacingUncommittedText) {
    auto presentation = settingsPresentation();
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->frameRate.access = camera::ControlAccess::ReadOnly;
    status->currentConfiguration->requestedFps = 27.0;
    presentation.cameraStatus = status;
    CameraSettingsDialog dialog;
    dialog.setPresentation(presentation);

    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual");
    auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure, nullptr);
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(message, nullptr);
    ASSERT_NE(apply, nullptr);
    auto* input = exposure->findChild<QLineEdit*>();
    ASSERT_NE(input, nullptr);
    input->selectAll();
    QKeyEvent event(QEvent::KeyPress, Qt::Key_2, Qt::NoModifier, QStringLiteral("2345"));
    QCoreApplication::sendEvent(input, &event);
    ASSERT_EQ(input->text(), QStringLiteral("2345"));

    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->currentConfiguration->requestedFps = 28.0;
    presentation.cameraStatus = status;
    dialog.setPresentation(presentation);

    EXPECT_EQ(input->text(), QStringLiteral("2345"));
    EXPECT_TRUE(actual->text().contains("28 fps"));
    EXPECT_TRUE(message->text().contains("readback", Qt::CaseInsensitive));
    EXPECT_FALSE(apply->isEnabled());
}
}
}
