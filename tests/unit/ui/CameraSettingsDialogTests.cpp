#include <lumora/ui/CameraSettingsDialog.hpp>
#include <lumora/application/StartupPreferences.hpp>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QPixmap>
#include <gtest/gtest.h>
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
    status->appliedConfiguration = camera::AppliedCameraConfiguration{request, actual};
    status->capabilities = camera::CameraCapabilities{{format},
        {{0U, 0U, 16U, 16U}, {100U, 100U, 1920U, 1080U}, {2U, 2U, 2U, 2U}},
        {1.0, 60.0, 1.0, false}, {10.0, 10000.0, 0.125, false},
        {camera::ExposureMode::Manual, camera::ExposureMode::Auto},
        {-6.0, 24.0, 0.25, false}, {camera::GainMode::Manual, camera::GainMode::Auto}};
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
             camera::NumericCapability{1.0, 60.0, 1.0, false},
             camera::NumericCapability{2.5, 45.5, 0.5, false}}) {
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
    status->capabilities->frameRate = {0.0001, 60.0, 0.0001, false};
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
    status->capabilities->frameRate = {1e-20, 60.0, 1e-20, false};
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
        status->capabilities->frameRate = {minimum, 60.0, minimum, false};
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
    auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
    ASSERT_NE(apply, nullptr); ASSERT_NE(exposure, nullptr); ASSERT_NE(frameRate, nullptr); ASSERT_NE(message, nullptr);
    frameRate->setValue(1.25);
    int count = 0;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration) { ++count; });
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->state = application::CameraSessionState::Streaming; presentation.cameraStatus = status;
    dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled()); EXPECT_FALSE(exposure->isEnabled());
    EXPECT_FALSE(frameRate->isEnabled()); apply->click(); EXPECT_EQ(count, 0);
    EXPECT_TRUE(message->text().contains("Stop"));
    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->state = application::CameraSessionState::ConnectedIdle; presentation.cameraStatus = status;
    presentation.ordinaryOperationPending = true; dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled()); EXPECT_FALSE(frameRate->isEnabled());
    apply->click(); EXPECT_EQ(count, 0);
    presentation.ordinaryOperationPending = false; dialog.setPresentation(presentation);
    EXPECT_TRUE(apply->isEnabled()); EXPECT_TRUE(frameRate->isEnabled());
    EXPECT_DOUBLE_EQ(frameRate->value(), 1.25);
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
        [](auto&, auto& s) { s.capabilities->frameRate.writableWhileStreaming = true; }};
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

TEST(CameraSettingsDialog, InvalidCapabilitiesAndRequestsCannotSilentlyClampIntoSubmission) {
    using Change = std::function<void(CameraStartupPanelPresentation&, application::CameraStatusSnapshot&)>;
    for (const Change& change : std::vector<Change>{
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
    for (const auto* name : {"cameraExposureMode", "cameraGainMode", "cameraExposureValue", "cameraGainValue", "cameraFrameRateValue"}) {
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
        for (const auto* name : {"cameraExposureMode", "cameraGainMode", "cameraExposureValue", "cameraGainValue", "cameraFrameRateValue",
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
