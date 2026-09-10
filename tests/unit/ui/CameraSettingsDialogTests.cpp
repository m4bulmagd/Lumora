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
        {1.0, 60.0, 0.1, false}, {10.0, 10000.0, 0.125, false},
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
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure, nullptr); ASSERT_NE(gain, nullptr); ASSERT_NE(apply, nullptr);
    EXPECT_DOUBLE_EQ(exposure->minimum(), 10.0); EXPECT_DOUBLE_EQ(exposure->maximum(), 10000.0);
    EXPECT_DOUBLE_EQ(exposure->singleStep(), 0.125); EXPECT_DOUBLE_EQ(gain->singleStep(), 0.25);
    exposure->setValue(2468.875); gain->setValue(-1.375);
    EXPECT_EQ(count, 0);
    dialog.setPresentation(presentation);
    EXPECT_DOUBLE_EQ(exposure->value(), 2468.875);
    ASSERT_TRUE(apply->isEnabled()); apply->click();
    ASSERT_EQ(count, 1); ASSERT_TRUE(emitted);
    EXPECT_EQ(generation, 17U); EXPECT_EQ(id.value, "camera-settings-1");
    auto expected = *presentation.requestedConfiguration;
    expected.exposure.requestedMicroseconds = 2468.875; expected.gain.requestedDb = -1.375;
    EXPECT_TRUE(application::cameraConfigurationsEqual(*emitted, expected));
}

TEST(CameraSettingsDialog, UntouchedFractionalValuesRoundTripWithoutDisplayRounding) {
    CameraSettingsDialog dialog;
    const auto presentation = settingsPresentation(); dialog.setPresentation(presentation);
    std::optional<camera::CameraConfiguration> emitted;
    QObject::connect(&dialog, &CameraSettingsDialog::settingsApplyRequested,
        [&](std::uint64_t, camera::CameraId, camera::CameraConfiguration request) { emitted = request; });
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton"); ASSERT_NE(apply, nullptr);
    apply->click(); ASSERT_TRUE(emitted);
    EXPECT_TRUE(application::cameraConfigurationsEqual(*emitted, *presentation.requestedConfiguration));
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
    auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
    ASSERT_NE(apply, nullptr); ASSERT_NE(exposure, nullptr); ASSERT_NE(message, nullptr);
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->state = application::CameraSessionState::Streaming; presentation.cameraStatus = status;
    dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled()); EXPECT_FALSE(exposure->isEnabled());
    EXPECT_TRUE(message->text().contains("Stop"));
    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->state = application::CameraSessionState::ConnectedIdle; presentation.cameraStatus = status;
    presentation.ordinaryOperationPending = true; dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled());
    presentation.ordinaryOperationPending = false; dialog.setPresentation(presentation);
    EXPECT_TRUE(apply->isEnabled());
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
        [](auto&, auto& s) { s.capabilities->frameRate.writableWhileStreaming = true; }};
    for (const auto& change : changes) {
        CameraSettingsDialog dialog; auto original = settingsPresentation(); dialog.setPresentation(original);
        auto presentation = original;
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        change(presentation, *status); presentation.cameraStatus = status; dialog.setPresentation(presentation);
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        auto* message = dialog.findChild<QLabel*>("cameraSettingsStatus");
        ASSERT_NE(apply, nullptr); ASSERT_NE(message, nullptr);
        EXPECT_FALSE(apply->isEnabled()); EXPECT_TRUE(message->text().contains("reopen"));
        dialog.setPresentation(original); EXPECT_FALSE(apply->isEnabled());
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
        auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
        ASSERT_NE(exposure, nullptr); ASSERT_NE(apply, nullptr);
        exposure->setValue(2000.5);
        if (ownSubmission) {
            apply->click();
            presentation.requestedConfiguration->exposure.requestedMicroseconds = 2000.5;
            presentation.ordinaryOperationPending = true;
            dialog.setPresentation(presentation);
        }
        auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        ++status->requestedRevision;
        status->requestedConfiguration->exposure.requestedMicroseconds = 2000.5;
        presentation.requestedConfiguration = status->requestedConfiguration;
        presentation.cameraStatus = status; presentation.ordinaryOperationPending = false;
        dialog.setPresentation(presentation);
        EXPECT_EQ(apply->isEnabled(), ownSubmission);
        auto* actual = dialog.findChild<QLabel*>("cameraSettingsActual"); ASSERT_NE(actual, nullptr);
        EXPECT_TRUE(actual->text().contains("999.5"));
        EXPECT_FALSE(status->confirmedRevision);
    }
}

TEST(CameraSettingsDialog, InvalidCapabilitiesAndRequestsCannotSilentlyClampIntoSubmission) {
    using Change = std::function<void(CameraStartupPanelPresentation&, application::CameraStatusSnapshot&)>;
    for (const Change& change : std::vector<Change>{
        [](auto&, auto& s) { s.capabilities->gainModes.clear(); },
        [](auto&, auto& s) { s.capabilities->exposure.increment = 0; },
        [](auto& p, auto&) { p.requestedConfiguration->gain.requestedDb = 999; },
        [](auto& p, auto&) { p.requestedConfiguration->exposure.requestedMicroseconds = std::numeric_limits<double>::quiet_NaN(); },
        [](auto& p, auto&) { p.requestedConfiguration->gain.requestedDb.reset(); }}) {
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
    EXPECT_TRUE(text.contains("30")); EXPECT_TRUE(text.contains("640")); EXPECT_TRUE(text.contains("480"));
    EXPECT_TRUE(text.contains("8")); EXPECT_TRUE(text.contains("4"));
    EXPECT_TRUE(text.contains("Mono8")); EXPECT_TRUE(text.contains("Continuous"));
    EXPECT_TRUE(text.contains("read-only", Qt::CaseInsensitive));
    EXPECT_TRUE(dialog.findChild<QLabel*>("cameraSettingsSource")->text().contains("camera-settings-1"));
    for (const auto* name : {"cameraExposureMode", "cameraGainMode", "cameraExposureValue", "cameraGainValue"}) {
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
        for (const auto* name : {"cameraExposureMode", "cameraGainMode", "cameraExposureValue", "cameraGainValue",
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
    auto* exposure = dialog.findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* apply = dialog.findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure, nullptr); ASSERT_NE(apply, nullptr);
    exposure->setValue(2000.5); apply->click();
    // Admission rejection preserves the old desired request and has no pending command.
    dialog.setPresentation(presentation);
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    ++status->requestedRevision;
    status->requestedConfiguration->exposure.requestedMicroseconds = 2000.5;
    presentation.requestedConfiguration = status->requestedConfiguration;
    presentation.cameraStatus = status;
    dialog.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled());
}

}  // namespace
}  // namespace lumora::ui
