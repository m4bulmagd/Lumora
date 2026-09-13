#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/CameraSettingsDialog.hpp>
#include <QDoubleSpinBox>
#include <QEvent>

#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QToolButton>
#include <QMetaObject>
#include <QCoreApplication>
#include <QTranslator>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <cstring>

namespace lumora::ui {
namespace {

core::SourcePixelFormat mono8() {
    return {"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
}

camera::CameraConfiguration configuration(double fps) {
    return {mono8(), {0U, 0U, 640U, 480U}, fps,
        {camera::ExposureMode::Manual, 1000.0},
        {camera::GainMode::Manual, 2.0}, camera::AcquisitionMode::Continuous};
}

TEST(CameraStartupPanel, WarningBodiesUseExtractableTranslationsAndTypedFallback) {
    class WarningTranslator final : public QTranslator {
    public:
        bool isEmpty() const override { return false; }
        QString translate(const char* context, const char* source, const char*, int) const override {
            if (std::strcmp(context, "lumora::ui::CameraStartupPanel") != 0) return {};
            if (std::strcmp(source, "Camera readback changed. Review and confirm settings before Start.") == 0)
                return QStringLiteral("Translated readback review");
            if (std::strcmp(source, "Camera retrieval timed out. Check the camera connection.") == 0)
                return QStringLiteral("Translated camera timeout");
            if (std::strcmp(source, "Startup preferences were not saved because the existing configuration could not be read or preserved safely.") == 0)
                return QStringLiteral("Translated unsafe preferences");
            if (std::strcmp(source, "Camera startup encountered an error (%1).") == 0)
                return QStringLiteral("Translated unknown (%1)");
            return {};
        }
    } translator;
    ASSERT_TRUE(QCoreApplication::installTranslator(&translator));
    struct RemoveTranslator final {
        QTranslator* translator;
        ~RemoveTranslator() { QCoreApplication::removeTranslator(translator); }
    } remove{&translator};
    CameraStartupPanel panel;
    struct Case { const char* code; const char* expected; };
    for (const auto& value : {Case{"startup_readback_changed", "Translated readback review"},
             Case{"acquisition_timeout", "Translated camera timeout"},
             Case{"startup_save_source_unsafe", "Translated unsafe preferences"},
             Case{"unrecognized_error", "Translated unknown (unrecognized_error)"}}) {
        CameraStartupPanelPresentation presentation;
        presentation.startupWarning = core::Error{core::ErrorCategory::Internal,
            value.code, "<b>Untranslated runtime warning</b>", "diagnostic detail", false};
        panel.setPresentation(presentation);
        const auto* label = panel.findChild<QLabel*>(QStringLiteral("startupWarningLabel"));
        EXPECT_EQ(label->text(), QStringLiteral("Warning: %1").arg(QString::fromUtf8(value.expected)));
        EXPECT_EQ(label->textFormat(), Qt::PlainText);
        EXPECT_EQ(panel.presentation().startupWarning->code, value.code);
        EXPECT_EQ(panel.presentation().startupWarning->diagnosticDetail, "diagnostic detail");
    }
}

TEST(CameraStartupPanel, StartIsDisabledBeforeExplicitConfirmation) {
    CameraStartupPanel panel;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->requestedRevision = 7U;
    status->appliedRevision = 7U;
    status->confirmedRevision.reset();
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = std::move(status);
    presentation.preferencesLoadCompleted = true;

    panel.setPresentation(std::move(presentation));

    auto* start = panel.findChild<QPushButton*>(QStringLiteral("startCameraButton"));
    ASSERT_NE(start, nullptr);
    EXPECT_FALSE(start->isEnabled());
}

TEST(CameraStartupPanel, RequestedAndActualFixedConfigurationAreBothVisible) {
    CameraStartupPanel panel;
    auto requestedConfiguration = configuration(30.0);
    requestedConfiguration.roi.x = 8U;
    requestedConfiguration.roi.y = 4U;
    auto actualConfiguration = requestedConfiguration;
    actualConfiguration.requestedFps = 29.5;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->appliedConfiguration = camera::AppliedCameraConfiguration{
        requestedConfiguration, actualConfiguration};
    status->currentConfiguration = actualConfiguration;
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = std::move(status);
    presentation.requestedConfiguration = requestedConfiguration;
    presentation.preferencesLoadCompleted = true;

    panel.setPresentation(std::move(presentation));

    auto* requested = panel.findChild<QLabel*>(
        QStringLiteral("requestedConfigurationLabel"));
    auto* actual = panel.findChild<QLabel*>(QStringLiteral("actualConfigurationLabel"));
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(actual, nullptr);
    EXPECT_TRUE(requested->text().contains(QStringLiteral("30")));
    EXPECT_TRUE(requested->text().contains(QStringLiteral("x 8")));
    EXPECT_TRUE(requested->text().contains(QStringLiteral("y 4")));
    EXPECT_TRUE(actual->text().contains(QStringLiteral("29.5")));
}

TEST(CameraStartupPanel, InstallationGuidanceReflectsCurrentBinding) {
    CameraStartupPanel panel;
    CameraStartupPanelPresentation presentation;
    presentation.activeOrientation = core::Orientation{};
    application::InstallationCameraProfile savedProfile;
    presentation.installationProfileOutcome = application::InstallationSaveOutcome{
        7U, savedProfile, std::nullopt};
    presentation.installationBindingCurrent = true;
    panel.setPresentation(presentation);
    auto* status = panel.findChild<QLabel*>("installationProfileStatusLabel");
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->isVisibleTo(&panel));
    EXPECT_FALSE(status->text().contains("Apply", Qt::CaseInsensitive));

    presentation.installationBindingCurrent = false;
    panel.setPresentation(presentation);
    EXPECT_TRUE(status->text().contains("Apply", Qt::CaseInsensitive));
}

TEST(CameraStartupPanel, PriorityActionsRemainEnabledWhileOrdinaryOperationIsPending) {
    CameraStartupPanel panel;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::Streaming;
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = std::move(status);
    presentation.ordinaryOperationPending = true;
    presentation.preferencesLoadCompleted = true;

    panel.setPresentation(std::move(presentation));

    auto* stop = panel.findChild<QPushButton*>(QStringLiteral("stopCameraButton"));
    auto* disconnect = panel.findChild<QPushButton*>(
        QStringLiteral("disconnectCameraButton"));
    auto* apply = panel.findChild<QPushButton*>(QStringLiteral("applyCameraButton"));
    ASSERT_NE(stop, nullptr);
    ASSERT_NE(disconnect, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(stop->isEnabled());
    EXPECT_TRUE(disconnect->isEnabled());
    EXPECT_FALSE(apply->isEnabled());
}

TEST(CameraStartupPanel, DisconnectAndRetryRemainDistinctInRecoverableStates) {
    CameraStartupPanel panel;
    panel.resize(280, 500);
    panel.show();
    for (const auto state : {application::CameraSessionState::Error,
             application::CameraSessionState::Reconnecting}) {
        auto status = std::make_shared<application::CameraStatusSnapshot>();
        status->state = state;
        status->actualIdentity = camera::CameraId{"camera-1"};
        status->desiredIdentity = camera::CameraId{"camera-1"};
        status->discoveredDescriptors.push_back(
            {{"camera-1"}, {"Lumora", "Simulator", "SIM-1", "virtual", {}}, true});
        CameraStartupPanelPresentation presentation;
        presentation.cameraStatus = std::move(status);
        presentation.selectedCameraId = camera::CameraId{"camera-1"};
        presentation.preferencesLoadCompleted = true;
        panel.setPresentation(std::move(presentation));
        QCoreApplication::processEvents();

        auto* disconnect = panel.findChild<QPushButton*>(
            QStringLiteral("disconnectCameraButton"));
        auto* retry = panel.findChild<QPushButton*>(QStringLiteral("retryCameraButton"));
        ASSERT_NE(disconnect, nullptr);
        ASSERT_NE(retry, nullptr);
        ASSERT_TRUE(disconnect->isVisibleTo(&panel));
        ASSERT_TRUE(retry->isVisibleTo(&panel));
        EXPECT_FALSE(disconnect->geometry().intersects(retry->geometry()));
    }
}

TEST(CameraStartupPanel, ControlsEmitIntentsWithoutOptimisticStateChanges) {
    CameraStartupPanel panel;
    int selectionCount = 0;
    int refreshCount = 0;
    int connectCount = 0;
    int applyCount = 0;
    int confirmCount = 0;
    int startCount = 0;
    int stopCount = 0;
    int disconnectCount = 0;
    int retryCount = 0;
    int resumeCount = 0;
    camera::CameraId selected;
    QObject::connect(&panel, &CameraStartupPanel::selectionRequested,
        [&](camera::CameraId id) { ++selectionCount; selected = std::move(id); });
    QObject::connect(&panel, &CameraStartupPanel::refreshRequested,
        [&] { ++refreshCount; });
    QObject::connect(&panel, &CameraStartupPanel::connectRequested,
        [&] { ++connectCount; });
    QObject::connect(&panel, &CameraStartupPanel::applyRequested,
        [&] { ++applyCount; });
    QObject::connect(&panel, &CameraStartupPanel::confirmRequested,
        [&] { ++confirmCount; });
    QObject::connect(&panel, &CameraStartupPanel::startRequested,
        [&] { ++startCount; });
    QObject::connect(&panel, &CameraStartupPanel::stopRequested,
        [&] { ++stopCount; });
    QObject::connect(&panel, &CameraStartupPanel::disconnectRequested,
        [&] { ++disconnectCount; });
    QObject::connect(&panel, &CameraStartupPanel::retryRequested,
        [&] { ++retryCount; });
    QObject::connect(&panel, &CameraStartupPanel::resumeLiveRequested,
        [&] { ++resumeCount; });

    auto disconnected = std::make_shared<application::CameraStatusSnapshot>();
    disconnected->state = application::CameraSessionState::Disconnected;
    disconnected->discoveredDescriptors.push_back(
        {{"camera-1"}, {"<b>Lumora</b>", "Simulator", "SIM-1", "virtual", {}}, true});
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = disconnected;
    presentation.selectedCameraId = camera::CameraId{"camera-1"};
    presentation.requestedConfiguration = configuration(30.0);
    presentation.preferencesLoadCompleted = true;
    panel.setPresentation(presentation);
    auto* combo = panel.findChild<QComboBox*>(QStringLiteral("cameraSelectionCombo"));
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->itemText(0), "<b>Lumora</b> Simulator — SIM-1");
    EXPECT_TRUE(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0)));
    panel.findChild<QPushButton*>(QStringLiteral("refreshCameraButton"))->click();
    panel.findChild<QPushButton*>(QStringLiteral("connectCameraButton"))->click();
    EXPECT_EQ(panel.presentation().cameraStatus->state,
        application::CameraSessionState::Disconnected);

    auto idle = std::make_shared<application::CameraStatusSnapshot>();
    idle->state = application::CameraSessionState::ConnectedIdle;
    idle->actualIdentity = camera::CameraId{"camera-1"};
    idle->requestedRevision = 4U;
    idle->appliedRevision = 4U;
    idle->requestedConfiguration = configuration(30.0);
    idle->appliedConfiguration = camera::AppliedCameraConfiguration{
        configuration(30.0), configuration(29.5)};
    idle->currentConfiguration = configuration(29.5);
    presentation.cameraStatus = idle;
    panel.setPresentation(presentation);
    panel.findChild<QPushButton*>(QStringLiteral("applyCameraButton"))->click();
    panel.findChild<QPushButton*>(QStringLiteral("confirmCameraButton"))->click();
    auto confirmedIdle = std::make_shared<application::CameraStatusSnapshot>(*idle);
    confirmedIdle->confirmedRevision = 4U;
    presentation.cameraStatus = confirmedIdle;
    panel.setPresentation(presentation);
    panel.findChild<QPushButton*>(QStringLiteral("startCameraButton"))->click();
    presentation.resumeLiveAvailable = true;
    panel.setPresentation(presentation);
    panel.findChild<QPushButton*>(QStringLiteral("resumeLiveButton"))->click();

    auto streaming = std::make_shared<application::CameraStatusSnapshot>(*confirmedIdle);
    streaming->state = application::CameraSessionState::Streaming;
    presentation.cameraStatus = streaming;
    presentation.resumeLiveAvailable = false;
    panel.setPresentation(presentation);
    panel.findChild<QPushButton*>(QStringLiteral("stopCameraButton"))->click();
    panel.findChild<QPushButton*>(QStringLiteral("disconnectCameraButton"))->click();

    auto failed = std::make_shared<application::CameraStatusSnapshot>();
    failed->state = application::CameraSessionState::Error;
    failed->desiredIdentity = camera::CameraId{"camera-1"};
    presentation.cameraStatus = failed;
    panel.setPresentation(presentation);
    panel.findChild<QPushButton*>(QStringLiteral("retryCameraButton"))->click();

    EXPECT_EQ(selectionCount, 1);
    EXPECT_EQ(selected.value, "camera-1");
    EXPECT_EQ(refreshCount, 1);
    EXPECT_EQ(connectCount, 1);
    EXPECT_EQ(applyCount, 1);
    EXPECT_EQ(confirmCount, 1);
    EXPECT_EQ(startCount, 1);
    EXPECT_EQ(stopCount, 1);
    EXPECT_EQ(disconnectCount, 1);
    EXPECT_EQ(retryCount, 1);
    EXPECT_EQ(resumeCount, 1);
    EXPECT_EQ(panel.findChild<QLabel*>(QStringLiteral("actualConfigurationLabel"))
                  ->textFormat(),
        Qt::PlainText);
}

}  // namespace
}  // namespace lumora::ui

namespace lumora::ui {
namespace {
TEST(CameraStartupPanel, OwnsOneModelessDialogAndForwardsOnlyExplicitSettingsApply) {
    CameraStartupPanel panel;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->actualIdentity = camera::CameraId{"camera-1"};
    status->sessionGeneration = 12;
    status->capabilities = camera::CameraCapabilities{{mono8()},
        {{0U, 0U, 16U, 16U}, {0U, 0U, 1920U, 1080U}, {1U, 1U, 1U, 1U}},
        {1, 60, 1, camera::ControlAccess::WritableStopped},
        {10, 10000, 1, camera::ControlAccess::WritableStopped},
        {camera::ExposureMode::Manual},
        {0, 24, 0.25, camera::ControlAccess::WritableStopped},
        {camera::GainMode::Manual}};
    status->currentConfiguration = configuration(30);
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = status;
    presentation.requestedConfiguration = configuration(30);
    presentation.selectedCameraId = status->actualIdentity;
    panel.setPresentation(presentation);
    int settingsCount = 0;
    int applyCount = 0;
    int confirmCount = 0;
    int startCount = 0;
    QObject::connect(&panel, &CameraStartupPanel::settingsApplyRequested,
        [&](std::uint64_t generation, camera::CameraId id, camera::CameraConfiguration request) {
            ++settingsCount; EXPECT_EQ(generation, 12U); EXPECT_EQ(id.value, "camera-1");
            EXPECT_EQ(request.exposure.requestedMicroseconds, 3456.0);
        });
    QObject::connect(&panel, &CameraStartupPanel::applyRequested, [&] { ++applyCount; });
    QObject::connect(&panel, &CameraStartupPanel::confirmRequested, [&] { ++confirmCount; });
    QObject::connect(&panel, &CameraStartupPanel::startRequested, [&] { ++startCount; });
    auto* button = panel.findChild<QPushButton*>("cameraSettingsButton"); ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isEnabled()); button->click(); button->click();
    const auto dialogs = panel.findChildren<CameraSettingsDialog*>(); ASSERT_EQ(dialogs.size(), 1);
    auto* dialog = dialogs.front(); EXPECT_FALSE(dialog->isModal());
    auto* exposure = dialog->findChild<QDoubleSpinBox*>("cameraExposureValue"); ASSERT_NE(exposure, nullptr);
    exposure->setValue(3456.0); panel.setPresentation(presentation);
    EXPECT_DOUBLE_EQ(exposure->value(), 3456.0); EXPECT_EQ(settingsCount, 0);
    auto* apply = dialog->findChild<QPushButton*>("applyCameraSettingsButton"); ASSERT_NE(apply, nullptr);
    apply->click(); EXPECT_EQ(settingsCount, 1);
    EXPECT_EQ(applyCount, 0); EXPECT_EQ(confirmCount, 0); EXPECT_EQ(startCount, 0);
    presentation.selectedCameraId = camera::CameraId{"different-camera"}; panel.setPresentation(presentation);
    EXPECT_FALSE(apply->isEnabled());
    dialog->reject(); EXPECT_EQ(settingsCount, 1);
    presentation.selectedCameraId = status->actualIdentity; panel.setPresentation(presentation);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    button->click(); ASSERT_EQ(panel.findChildren<CameraSettingsDialog*>().size(), 1);
    EXPECT_TRUE(panel.findChild<CameraSettingsDialog*>()->findChild<QPushButton*>("applyCameraSettingsButton")->isEnabled());
}
}
}

namespace lumora::ui {
namespace {

TEST(CameraStartupPanel, MissingOrUnavailableSelectionNeverLooksLikeAnotherCamera) {
    CameraStartupPanel panel;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::Disconnected;
    status->discoveredDescriptors = {
        {{"available"}, {"Lumora", "Live", "A-1", "USB", {}}, true},
        {{"unavailable"}, {"Lumora", "Live", "U-2", "USB", {}}, false}};
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = status;
    presentation.selectedCameraId = camera::CameraId{"available"};
    presentation.preferencesLoadCompleted = true;
    panel.setPresentation(presentation);

    auto* selection = panel.findChild<QComboBox*>("cameraSelectionCombo");
    auto* connectButton = panel.findChild<QPushButton*>("connectCameraButton");
    ASSERT_NE(selection, nullptr);
    ASSERT_NE(connectButton, nullptr);
    ASSERT_EQ(selection->currentData().toString(), QStringLiteral("available"));

    int selectionIntents = 0;
    QObject::connect(&panel, &CameraStartupPanel::selectionRequested,
        [&](camera::CameraId) { ++selectionIntents; });
    presentation.selectedCameraId = camera::CameraId{"missing"};
    panel.setPresentation(presentation);
    EXPECT_EQ(selection->currentIndex(), -1);
    EXPECT_FALSE(connectButton->isEnabled());
    EXPECT_EQ(selectionIntents, 0);

    presentation.selectedCameraId = camera::CameraId{"unavailable"};
    panel.setPresentation(presentation);
    ASSERT_EQ(selection->currentData().toString(), QStringLiteral("unavailable"));
    EXPECT_TRUE(selection->currentText().contains("Unavailable", Qt::CaseInsensitive));
    EXPECT_FALSE(connectButton->isEnabled());
    EXPECT_EQ(selectionIntents, 0);
}

TEST(CameraStartupPanel, ConnectionAndAcquisitionActionsAreContextual) {
    CameraStartupPanel panel;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::Disconnected;
    status->discoveredDescriptors.push_back(
        {{"camera-1"}, {"Lumora", "Live", "A-1", "USB", {}}, true});
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = status;
    presentation.selectedCameraId = camera::CameraId{"camera-1"};
    presentation.preferencesLoadCompleted = true;
    panel.setPresentation(presentation);

    auto* connectButton = panel.findChild<QPushButton*>("connectCameraButton");
    auto* disconnect = panel.findChild<QPushButton*>("disconnectCameraButton");
    auto* start = panel.findChild<QPushButton*>("startCameraButton");
    auto* stop = panel.findChild<QPushButton*>("stopCameraButton");
    auto* settings = panel.findChild<QPushButton*>("cameraSettingsButton");
    ASSERT_NE(connectButton, nullptr);
    ASSERT_NE(disconnect, nullptr);
    ASSERT_NE(start, nullptr);
    ASSERT_NE(stop, nullptr);
    ASSERT_NE(settings, nullptr);
    EXPECT_TRUE(connectButton->isVisibleTo(&panel));
    EXPECT_TRUE(connectButton->isEnabled());
    EXPECT_FALSE(disconnect->isVisibleTo(&panel));
    EXPECT_FALSE(start->isVisibleTo(&panel));
    EXPECT_FALSE(stop->isVisibleTo(&panel));

    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->state = application::CameraSessionState::ConnectedIdle;
    status->actualIdentity = camera::CameraId{"camera-1"};
    status->requestedRevision = 2U;
    status->appliedRevision = 2U;
    status->requestedConfiguration = configuration(30.0);
    status->appliedConfiguration = camera::AppliedCameraConfiguration{
        configuration(30.0), configuration(29.5)};
    presentation.cameraStatus = status;
    presentation.requestedConfiguration = configuration(30.0);
    panel.setPresentation(presentation);
    EXPECT_FALSE(connectButton->isVisibleTo(&panel));
    EXPECT_TRUE(disconnect->isVisibleTo(&panel));
    EXPECT_TRUE(start->isVisibleTo(&panel));
    EXPECT_FALSE(start->isEnabled());
    EXPECT_FALSE(stop->isVisibleTo(&panel));
    EXPECT_TRUE(settings->isVisibleTo(&panel));
    EXPECT_TRUE(settings->isEnabled());

    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->confirmedRevision = 2U;
    presentation.cameraStatus = status;
    panel.setPresentation(presentation);
    EXPECT_TRUE(start->isVisibleTo(&panel));
    EXPECT_TRUE(start->isEnabled());

    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->state = application::CameraSessionState::Streaming;
    presentation.cameraStatus = status;
    panel.setPresentation(presentation);
    EXPECT_FALSE(start->isVisibleTo(&panel));
    EXPECT_TRUE(stop->isVisibleTo(&panel));
    EXPECT_TRUE(stop->isEnabled());
    EXPECT_TRUE(settings->isVisibleTo(&panel));
    EXPECT_TRUE(settings->isEnabled());
}

TEST(CameraStartupPanel, CompactCameraPanelUsesReviewDisclosure) {
    CameraStartupPanel panel;
    auto* title = panel.findChild<QLabel*>("cameraPanelTitleLabel");
    auto* reviewToggle = panel.findChild<QToolButton*>("cameraReviewToggle");
    auto* reviewDetails = panel.findChild<QWidget*>("cameraReviewDetails");
    ASSERT_NE(title, nullptr);
    ASSERT_NE(reviewToggle, nullptr);
    ASSERT_NE(reviewDetails, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Camera"));

    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->actualIdentity = camera::CameraId{"camera-1"};
    status->requestedRevision = 2U;
    status->appliedRevision = 2U;
    status->requestedConfiguration = configuration(30.0);
    status->appliedConfiguration = camera::AppliedCameraConfiguration{
        configuration(30.0), configuration(29.5)};
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = status;
    presentation.selectedCameraId = status->actualIdentity;
    presentation.requestedConfiguration = configuration(30.0);
    presentation.preferencesLoadCompleted = true;
    panel.setPresentation(presentation);

    EXPECT_TRUE(reviewToggle->isVisibleTo(&panel));
    EXPECT_TRUE(reviewToggle->isChecked());
    EXPECT_TRUE(reviewDetails->isVisibleTo(&panel));
}

TEST(CameraStartupPanel, CameraAndViewerStatesRemainIndependent) {
    CameraStartupPanel panel;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::Streaming;
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = status;
    presentation.preferencesLoadCompleted = true;
    presentation.workstationStatus = {
        ViewerState::Paused, FrameFreshness::Current, std::nullopt,
        std::chrono::milliseconds{0}, std::nullopt};
    panel.setPresentation(presentation);

    auto* cameraState = panel.findChild<QLabel*>("cameraStartupStateLabel");
    auto* viewerState = panel.findChild<QLabel*>("cameraViewerStateLabel");
    ASSERT_NE(cameraState, nullptr);
    ASSERT_NE(viewerState, nullptr);
    EXPECT_TRUE(cameraState->text().contains("Streaming", Qt::CaseInsensitive));
    EXPECT_TRUE(viewerState->text().contains("Paused", Qt::CaseInsensitive));
    EXPECT_FALSE(viewerState->text().contains("Live", Qt::CaseInsensitive));

    presentation.workstationStatus.viewerState = ViewerState::Live;
    presentation.workstationStatus.freshness = FrameFreshness::Stale;
    panel.setPresentation(presentation);
    EXPECT_TRUE(viewerState->text().contains("Stale", Qt::CaseInsensitive));
    EXPECT_FALSE(viewerState->text().contains("Live", Qt::CaseInsensitive));

    presentation.workstationStatus.freshness = FrameFreshness::Current;
    panel.setPresentation(presentation);
    EXPECT_TRUE(viewerState->text().contains("Live", Qt::CaseInsensitive));
}

}  // namespace
}  // namespace lumora::ui

namespace lumora::ui {
namespace {
TEST(CameraStartupPanel, DifferentSelectedSourceDisablesApplyConfirmAndStart) {
    CameraStartupPanel panel;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->actualIdentity = camera::CameraId{"connected-camera"};
    status->requestedRevision = 2;
    status->appliedRevision = 2;
    status->requestedConfiguration = configuration(30);
    status->appliedConfiguration = camera::AppliedCameraConfiguration{configuration(30), configuration(30)};
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = status;
    presentation.requestedConfiguration = configuration(30);
    presentation.selectedCameraId = camera::CameraId{"different-camera"};
    panel.setPresentation(presentation);
    EXPECT_FALSE(panel.findChild<QPushButton*>("applyCameraButton")->isEnabled());
    EXPECT_FALSE(panel.findChild<QPushButton*>("confirmCameraButton")->isEnabled());
    status = std::make_shared<application::CameraStatusSnapshot>(*status);
    status->confirmedRevision = 2; presentation.cameraStatus = status; panel.setPresentation(presentation);
    EXPECT_FALSE(panel.findChild<QPushButton*>("startCameraButton")->isEnabled());
    EXPECT_TRUE(panel.findChild<QPushButton*>("disconnectCameraButton")->isEnabled());
    presentation.selectedCameraId.reset(); panel.setPresentation(presentation);
    EXPECT_FALSE(panel.findChild<QPushButton*>("applyCameraButton")->isEnabled());
    EXPECT_FALSE(panel.findChild<QPushButton*>("confirmCameraButton")->isEnabled());
    EXPECT_FALSE(panel.findChild<QPushButton*>("startCameraButton")->isEnabled());
}
}
}
