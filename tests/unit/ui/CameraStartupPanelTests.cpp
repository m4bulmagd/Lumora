#include <lumora/ui/CameraStartupPanel.hpp>

#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QMetaObject>
#include <QCoreApplication>
#include <QTranslator>

#include <gtest/gtest.h>

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
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->appliedConfiguration = camera::AppliedCameraConfiguration{
        configuration(30.0), configuration(29.5)};
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = std::move(status);
    presentation.fixedRequestedConfiguration = configuration(30.0);
    presentation.preferencesLoadCompleted = true;

    panel.setPresentation(std::move(presentation));

    auto* requested = panel.findChild<QLabel*>(
        QStringLiteral("requestedConfigurationLabel"));
    auto* actual = panel.findChild<QLabel*>(QStringLiteral("actualConfigurationLabel"));
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(actual, nullptr);
    EXPECT_TRUE(requested->text().contains(QStringLiteral("30")));
    EXPECT_TRUE(actual->text().contains(QStringLiteral("29.5")));
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
    presentation.fixedRequestedConfiguration = configuration(30.0);
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
    idle->requestedRevision = 4U;
    idle->appliedRevision = 4U;
    idle->appliedConfiguration = camera::AppliedCameraConfiguration{
        configuration(30.0), configuration(29.5)};
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
