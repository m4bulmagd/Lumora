#include <lumora/ui/MainWindow.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <gtest/gtest.h>
#include <QPushButton>
#include <QComboBox>
#include <QPersistentModelIndex>
TEST(WorkstationController, MainWindowHostsStartupControls) {
    lumora::ui::MainWindow window;
    EXPECT_NE(window.findChild<lumora::ui::CameraStartupPanel*>(),nullptr);
}
TEST(WorkstationController, ErrorRequiresDisconnectBeforeRefresh) {
    lumora::ui::CameraStartupPanel panel;
    auto status=std::make_shared<lumora::application::CameraStatusSnapshot>();
    status->state=lumora::application::CameraSessionState::Error;
    lumora::ui::CameraStartupPanelPresentation presentation;presentation.cameraStatus=status;
    panel.setPresentation(presentation);
    EXPECT_FALSE(panel.findChild<QPushButton*>("refreshCameraButton")->isEnabled());
}
TEST(WorkstationController, RetryRequiresARetainedIdentity) {
    lumora::ui::CameraStartupPanel panel;
    auto status=std::make_shared<lumora::application::CameraStatusSnapshot>();
    status->state=lumora::application::CameraSessionState::Error;
    lumora::ui::CameraStartupPanelPresentation presentation;presentation.cameraStatus=status;
    panel.setPresentation(presentation);
    EXPECT_FALSE(panel.findChild<QPushButton*>("retryCameraButton")->isEnabled());
    status->desiredIdentity=lumora::camera::CameraId{"SIM-LIVE"};panel.setPresentation(presentation);
    EXPECT_TRUE(panel.findChild<QPushButton*>("retryCameraButton")->isEnabled());
}
TEST(WorkstationController, OrdinaryPollingKeepsDiscoverySelectionModelStable) {
    lumora::ui::CameraStartupPanel panel;
    auto status=std::make_shared<lumora::application::CameraStatusSnapshot>();
    status->discoveredDescriptors={{{"SIM-LIVE"},{"Lumora","Generated Camera","SIM-LIVE","Simulator",std::nullopt},true}};
    lumora::ui::CameraStartupPanelPresentation presentation;presentation.cameraStatus=status;
    panel.setPresentation(presentation);
    auto* combo=panel.findChild<QComboBox*>("cameraSelectionCombo");
    QPersistentModelIndex item(combo->model()->index(0,0));
    for(int i=0;i<10;++i) panel.setPresentation(presentation);
    EXPECT_TRUE(item.isValid());EXPECT_FALSE(panel.presentation().selectedCameraId.has_value());
    auto changed=std::make_shared<lumora::application::CameraStatusSnapshot>(*status);
    changed->discoveredDescriptors.clear();presentation.cameraStatus=changed;panel.setPresentation(presentation);
    EXPECT_EQ(combo->count(),0);
}
TEST(WorkstationController, DiscoveryShowsNoSelectionUntilOperatorActivation) {
    lumora::ui::CameraStartupPanel panel;
    auto status=std::make_shared<lumora::application::CameraStatusSnapshot>();
    status->discoveredDescriptors={{{"SIM-LIVE"},{"Lumora","Generated Camera","SIM-LIVE","Simulator",std::nullopt},true}};
    lumora::ui::CameraStartupPanelPresentation presentation;presentation.cameraStatus=status;
    panel.setPresentation(presentation);
    auto* combo=panel.findChild<QComboBox*>("cameraSelectionCombo");
    EXPECT_EQ(combo->currentIndex(),-1);
    EXPECT_FALSE(combo->placeholderText().isEmpty());
    QPersistentModelIndex item(combo->model()->index(0,0));
    for(int i=0;i<10;++i) panel.setPresentation(presentation);
    EXPECT_EQ(combo->currentIndex(),-1);EXPECT_TRUE(item.isValid());
    QObject::connect(&panel,&lumora::ui::CameraStartupPanel::selectionRequested,
        &panel,[&](const lumora::camera::CameraId& id) {
            presentation.selectedCameraId=id;panel.setPresentation(presentation);
        });
    combo->activated(0);
    ASSERT_TRUE(panel.presentation().selectedCameraId.has_value());
    EXPECT_EQ(panel.presentation().selectedCameraId->value,"SIM-LIVE");
    EXPECT_EQ(combo->currentIndex(),0);
    EXPECT_TRUE(panel.findChild<QPushButton*>("connectCameraButton")->isEnabled());
}
