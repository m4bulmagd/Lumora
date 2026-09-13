#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/CameraSettingsDialog.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/MainWindow.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QKeyEvent>
#include <QLayout>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>

#include <gtest/gtest.h>

#include <set>
#include <sstream>

namespace lumora::ui {
namespace {

CameraStartupPanelPresentation layoutPresentation(bool confirmed) {
    const core::SourcePixelFormat format{"Mono8", 0x01080001U, 8U, 255U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt8};
    const camera::CameraConfiguration requested{format, {8, 16, 640, 480}, 30.0,
        {camera::ExposureMode::Manual, 1000.0}, {camera::GainMode::Manual, 2.0},
        camera::AcquisitionMode::Continuous};
    auto actual = requested;
    actual.requestedFps = 29.5;
    auto status = std::make_shared<application::CameraStatusSnapshot>();
    status->state = application::CameraSessionState::ConnectedIdle;
    status->actualIdentity = camera::CameraId{"camera-a"};
    status->sessionGeneration = 5;
    status->currentConfiguration = actual;
    status->capabilities = camera::CameraCapabilities{{format},
        {{0, 0, 1, 1}, {1000, 1000, 2048, 2048}, {1, 1, 1, 1}},
        {1, 60, 0.1, camera::ControlAccess::WritableStopped},
        {1, 10000, 1, camera::ControlAccess::WritableStopped}, {camera::ExposureMode::Manual},
        {0, 24, 0.1, camera::ControlAccess::WritableStopped}, {camera::GainMode::Manual}};
    status->requestedConfiguration = requested;
    status->appliedConfiguration = camera::AppliedCameraConfiguration{requested, actual};
    status->requestedRevision = 3;
    status->appliedRevision = 3;
    if (confirmed) status->confirmedRevision = 3;
    status->discoveredDescriptors = {{{"camera-a"},
        {"Lumora Laboratory", "Long physical camera identity for narrow panel verification",
            "SERIAL-12345678901234567890", "simulator", {}}, true}};
    CameraStartupPanelPresentation presentation;
    presentation.cameraStatus = status;
    presentation.selectedCameraId = status->actualIdentity;
    presentation.requestedConfiguration = requested;
    presentation.preferencesLoadCompleted = true;
    presentation.activeOrientation = core::Orientation{true, false, core::Rotation::Degrees90};
    auto installation = std::make_shared<application::InstallationProfilesSnapshot>();
    installation->loadCompleted = true;
    installation->policy = application::InstallationProfilePolicy::SimulatorIdentityFallback;
    application::InstallationCameraProfile installed;
    installed.revision = 1;
    installed.identity = status->discoveredDescriptors.front().identity;
    installed.capabilities = *status->capabilities;
    installed.orientation = *presentation.activeOrientation;
    installed.confirmed = true;
    installation->profiles.push_back(installed);
    presentation.activeInstallationProfile = application::installationProfileReference(installed);
    presentation.installationProfiles = installation;
    return presentation;
}

void inspectLayout(MainWindow& window) {
    auto& view = window.workstationView();
    auto& panel = window.cameraStartupPanel();
    const auto scrolls = view.sidebar()->findChildren<QScrollArea*>();
    ASSERT_EQ(scrolls.size(), 1);
    std::ostringstream minimumWidths;
    for (auto* button : panel.findChildren<QAbstractButton*>()) {
        if (button->isVisibleTo(&panel)) minimumWidths << button->objectName().toStdString()
            << '=' << button->minimumSizeHint().width() << ' ';
    }
    EXPECT_EQ(scrolls.front()->horizontalScrollBar()->maximum(), 0) << minimumWidths.str();
    EXPECT_GT(view.imageViewport()->width(), view.sidebar()->width());
    const auto checkWidth = [&](QWidget* child) {
        if (!child->isVisibleTo(&panel)) return;
        const auto position = child->mapTo(&panel, QPoint{});
        EXPECT_GE(position.x(), 0) << child->objectName().toStdString();
        EXPECT_LE(position.x() + child->width(), panel.width())
            << child->objectName().toStdString();
        EXPECT_GE(child->width(), child->minimumSizeHint().width())
            << child->objectName().toStdString();
    };
    for (auto* child : panel.findChildren<QAbstractButton*>()) checkWidth(child);
    for (auto* child : panel.findChildren<QComboBox*>()) checkWidth(child);
    auto* pause = view.findChild<QAbstractButton*>("pauseLiveButton");
    ASSERT_NE(pause, nullptr);
    EXPECT_FALSE(scrolls.front()->widget()->isAncestorOf(pause));
    EXPECT_TRUE(pause->isVisible());
}

void capture(MainWindow& window, const QString& name) {
    const auto directory = QString::fromLocal8Bit(qgetenv("LUMORA_CAMERA_PANEL_CAPTURE_DIR"));
    if (directory.isEmpty()) return;
    ASSERT_TRUE(QDir{}.mkpath(directory));
    ASSERT_TRUE(window.grab().save(QDir{directory}.filePath(name + ".png")));
}

TEST(CameraPanelLayout, ConfirmedPanelFitsCompactlyAtSupportedWindowSizes) {
    for (const auto size : {QSize{900, 600}, QSize{1280, 800}}) {
        MainWindow window;
        window.resize(size);
        window.cameraStartupPanel().setPresentation(layoutPresentation(true));
        window.show();
        QCoreApplication::processEvents();
        inspectLayout(window);
        EXPECT_LE(window.cameraStartupPanel().sizeHint().height(), 380);
        capture(window, QStringLiteral("confirmed-%1x%2").arg(size.width()).arg(size.height()));
    }
}

TEST(CameraPanelLayout, ExpandedReviewAndWarningsPreserveImageAndPriorityControls) {
    for (const auto size : {QSize{900, 600}, QSize{1280, 800}}) {
        MainWindow window;
        window.resize(size);
        auto presentation = layoutPresentation(false);
        presentation.startupWarning = core::Error{core::ErrorCategory::CameraConfiguration,
            "startup_readback_changed", "Readback changed", "", true};
        window.cameraStartupPanel().setPresentation(presentation);
        processing::ProcessorStatus processingStatus;
        processingStatus.mode = processing::ProcessorMode::OriginalOnlyLatched;
        processingStatus.retrySupported = true;
        window.workstationView().setProcessingStatus(processingStatus);
        window.show();
        QCoreApplication::processEvents();
        inspectLayout(window);
        auto* warning = window.workstationView().findChild<QLabel*>("processingWarning");
        ASSERT_NE(warning, nullptr);
        EXPECT_TRUE(warning->isVisible());
        EXPECT_GE(window.workstationView().sidebar()->layout()->indexOf(warning), 0);
        capture(window, QStringLiteral("review-warning-%1x%2").arg(size.width()).arg(size.height()));
    }
}

TEST(CameraPanelLayout, PausedStaleViewRetainsItsOwnStateBesideAcquisition) {
    MainWindow window;
    window.resize(900, 600);
    auto presentation = layoutPresentation(true);
    auto camera = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    camera->state = application::CameraSessionState::Streaming;
    presentation.cameraStatus = camera;
    presentation.workstationStatus.viewerState = ViewerState::Paused;
    presentation.workstationStatus.freshness = FrameFreshness::Stale;
    window.workstationView().setStatus(presentation.workstationStatus);
    window.cameraStartupPanel().setPresentation(presentation);
    window.show();
    QCoreApplication::processEvents();
    inspectLayout(window);
    capture(window, QStringLiteral("paused-stale-900x600"));
}

TEST(CameraPanelLayout, KeyboardTraversalReachesReviewAndConfirmationActions) {
    MainWindow window;
    window.cameraStartupPanel().setPresentation(layoutPresentation(false));
    window.show();
    window.activateWindow();
    QCoreApplication::processEvents();
    auto& panel = window.cameraStartupPanel();
    auto* selection = panel.findChild<QComboBox*>("cameraSelectionCombo");
    ASSERT_NE(selection, nullptr);
    selection->setFocus();
    std::set<QString> visited;
    for (int i = 0; i < 40; ++i) {
        auto* focused = QApplication::focusWidget();
        ASSERT_NE(focused, nullptr);
        if (panel.isAncestorOf(focused)) {
            EXPECT_TRUE(focused->isVisible());
            EXPECT_TRUE(focused->isEnabled());
            visited.insert(focused->objectName());
        }
        QKeyEvent tab{QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier};
        QApplication::sendEvent(focused, &tab);
    }
    EXPECT_TRUE(visited.contains(QStringLiteral("cameraReviewToggle")));
    EXPECT_TRUE(visited.contains(QStringLiteral("confirmCameraButton")));
}

TEST(CameraPanelLayout, CapabilityDialogShowsActualFixedFactsAtSupportedSizes) {
    auto presentation = layoutPresentation(true);
    auto status = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    status->capabilities->frameRate.access = camera::ControlAccess::ReadOnly;
    status->capabilities->exposureModeAccess = camera::ControlAccess::ReadOnly;
    status->capabilities->gain = {0, 0, 0, camera::ControlAccess::Unavailable};
    status->capabilities->gainModes.clear();
    status->capabilities->gainModeAccess = camera::ControlAccess::Unavailable;
    status->currentConfiguration->gain = {};
    status->requestedConfiguration->gain = {};
    status->appliedConfiguration->requested.gain = {};
    status->appliedConfiguration->actual.gain = {};
    presentation.requestedConfiguration = status->requestedConfiguration;
    presentation.cameraStatus = status;
    auto installation = std::make_shared<application::InstallationProfilesSnapshot>(*presentation.installationProfiles);
    installation->profiles.front().capabilities = *status->capabilities;
    presentation.installationProfiles = installation;
    for (const auto size : {QSize{560, 560}, QSize{720, 640}}) {
        CameraSettingsDialog dialog;
        dialog.setPresentation(presentation);
        dialog.resize(size);
        dialog.show();
        QCoreApplication::processEvents();
        EXPECT_EQ(dialog.size(), size);
        auto* fps = dialog.findChild<QDoubleSpinBox*>("cameraFrameRateValue");
        auto* gain = dialog.findChild<QDoubleSpinBox*>("cameraGainValue");
        ASSERT_NE(fps, nullptr);
        ASSERT_NE(gain, nullptr);
        EXPECT_DOUBLE_EQ(fps->value(), 29.5);
        EXPECT_FALSE(fps->isEnabled());
        EXPECT_FALSE(gain->isEnabled());
        for (const auto* name : {"cameraRoiX", "cameraRoiY", "cameraRoiWidth", "cameraRoiHeight"}) {
            auto* widget = dialog.findChild<QWidget*>(name);
            ASSERT_NE(widget, nullptr);
            EXPECT_GE(widget->height(), widget->minimumSizeHint().height()) << name;
        }
        for (const auto* name : {"cameraSettingsSource", "cameraSettingsStatus",
                 "applyCameraSettingsButton", "closeCameraSettingsButton"}) {
            auto* widget = dialog.findChild<QWidget*>(name);
            ASSERT_NE(widget, nullptr);
            EXPECT_TRUE(dialog.rect().contains(QRect{widget->mapTo(&dialog, QPoint{}), widget->size()})) << name;
        }
        const auto directory = QString::fromLocal8Bit(qgetenv("LUMORA_CAMERA_PANEL_CAPTURE_DIR"));
        if (!directory.isEmpty()) {
            ASSERT_TRUE(QDir{}.mkpath(directory));
            ASSERT_TRUE(dialog.grab().save(QDir{directory}.filePath(
                QStringLiteral("fixed-absent-dialog-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
    }
}

}  // namespace
}  // namespace lumora::ui
