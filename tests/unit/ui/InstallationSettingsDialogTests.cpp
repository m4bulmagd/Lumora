#include <lumora/ui/InstallationSettingsDialog.hpp>
#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <gtest/gtest.h>
namespace lumora::ui {
namespace {
CameraStartupPanelPresentation presentation(bool administrator = true) {
    CameraStartupPanelPresentation p;
    auto camera = std::make_shared<application::CameraStatusSnapshot>();
    camera->state = application::CameraSessionState::ConnectedIdle;
    camera->sessionGeneration = 42;
    camera->actualIdentity = camera::CameraId{"camera-a"};
    camera->capabilities = camera::CameraCapabilities{};
    camera->discoveredDescriptors = {{{"camera-a"}, {"Lumora", "Camera", "A", "test", {}}, true}};
    p.cameraStatus = camera;
    p.selectedCameraId = camera->actualIdentity;
    auto repository = std::make_shared<application::InstallationProfilesSnapshot>();
    repository->administratorMode = administrator;
    repository->loadCompleted = true;
    repository->policy = application::InstallationProfilePolicy::SimulatorIdentityFallback;
    p.installationProfiles = repository;
    p.activeOrientation = core::Orientation{false, false, core::Rotation::Degrees0};
    return p;
}
TEST(InstallationSettingsDialog, OperatorInspectsButCannotEditOrSave) {
    InstallationSettingsDialog dialog;
    dialog.setPresentation(presentation(false));
    const auto* horizontal = dialog.findChild<QCheckBox*>("installationFlipHorizontal");
    const auto* save = dialog.findChild<QPushButton*>("saveInstallationButton");
    const auto* status = dialog.findChild<QLabel*>("installationStatusLabel");
    ASSERT_NE(horizontal, nullptr); ASSERT_NE(save, nullptr); ASSERT_NE(status, nullptr);
    EXPECT_FALSE(horizontal->isEnabled()); EXPECT_FALSE(save->isEnabled());
    EXPECT_TRUE(status->text().contains("Read-only"));
}
TEST(InstallationSettingsDialog, ExplicitConfirmationResetsOnEditAndSaveIsSynchronousSingleSubmission) {
    InstallationSettingsDialog dialog;
    auto p = presentation(); dialog.setPresentation(p);
    auto* horizontal = dialog.findChild<QCheckBox*>("installationFlipHorizontal");
    auto* confirmation = dialog.findChild<QCheckBox*>("installationConfirmation");
    auto* rotation = dialog.findChild<QComboBox*>("installationRotation");
    auto* save = dialog.findChild<QPushButton*>("saveInstallationButton");
    ASSERT_NE(horizontal, nullptr); ASSERT_NE(confirmation, nullptr); ASSERT_NE(rotation, nullptr); ASSERT_NE(save, nullptr);
    EXPECT_FALSE(save->isEnabled()); confirmation->setChecked(true); EXPECT_TRUE(save->isEnabled());
    horizontal->setChecked(true); EXPECT_FALSE(confirmation->isChecked()); EXPECT_FALSE(save->isEnabled());
    rotation->setCurrentIndex(1); confirmation->setChecked(true);
    int saves = 0;
    QObject::connect(&dialog, &InstallationSettingsDialog::installationSaveRequested,
        [&](std::uint64_t generation, camera::CameraId id, core::Orientation orientation, bool confirmed, bool repair) {
            ++saves; EXPECT_EQ(generation, 42U); EXPECT_EQ(id.value, "camera-a");
            EXPECT_TRUE(orientation.flipHorizontal); EXPECT_EQ(orientation.rotation, core::Rotation::Degrees90);
            EXPECT_TRUE(confirmed); EXPECT_FALSE(repair); EXPECT_FALSE(save->isEnabled());
        });
    save->click(); save->click(); EXPECT_EQ(saves, 1);
    dialog.setPresentation(p); EXPECT_FALSE(save->isEnabled());
}
TEST(InstallationSettingsDialog, SourceChangesPermanentlyInvalidateOpenEditorAndPendingOrStreamingBlocksSaving) {
    for (const auto state : {0, 1, 2, 3, 4, 5}) {
        InstallationSettingsDialog dialog; auto p = presentation(); dialog.setPresentation(p);
        auto* confirmation = dialog.findChild<QCheckBox*>("installationConfirmation");
        auto* save = dialog.findChild<QPushButton*>("saveInstallationButton");
        ASSERT_NE(confirmation, nullptr); ASSERT_NE(save, nullptr); confirmation->setChecked(true);
        auto camera = std::make_shared<application::CameraStatusSnapshot>(*p.cameraStatus);
        if (state == 0) camera->sessionGeneration++;
        if (state == 1) p.selectedCameraId = camera::CameraId{"camera-b"};
        if (state == 2) camera->state = application::CameraSessionState::Streaming;
        if (state == 3) p.ordinaryOperationPending = true;
        if (state == 4) camera->discoveredDescriptors.front().identity.serial = "replaced-identity";
        if (state == 5) camera->capabilities->roi.maximum.width++;
        p.cameraStatus = camera; dialog.setPresentation(p); EXPECT_FALSE(save->isEnabled());
        if (state < 2 || state > 3) { dialog.setPresentation(presentation()); EXPECT_FALSE(save->isEnabled()); }
    }
}
TEST(InstallationSettingsDialog, InvalidRepairNeedsSeparateConsentAndFailureNeedsRenewedConfirmation) {
    InstallationSettingsDialog dialog; auto p = presentation();
    auto repository = std::make_shared<application::InstallationProfilesSnapshot>(*p.installationProfiles);
    repository->loadError = core::Error{core::ErrorCategory::Configuration, "invalid", "Invalid installation file", "", false};
    p.installationProfiles = repository; dialog.setPresentation(p);
    auto* confirmation = dialog.findChild<QCheckBox*>("installationConfirmation");
    auto* repair = dialog.findChild<QCheckBox*>("installationRepairConsent");
    auto* save = dialog.findChild<QPushButton*>("saveInstallationButton");
    ASSERT_NE(confirmation, nullptr); ASSERT_NE(repair, nullptr); ASSERT_NE(save, nullptr);
    confirmation->setChecked(true); EXPECT_FALSE(save->isEnabled());
    repair->setChecked(true); confirmation->setChecked(true); EXPECT_TRUE(save->isEnabled()); save->click();
    p.installationProfilePending = true; dialog.setPresentation(p);
    p.installationProfilePending = false;
    p.installationProfileOutcome = application::InstallationSaveOutcome{1, {}, core::Error{
        core::ErrorCategory::Configuration, "write_failed", "Save failed", "", true}};
    dialog.setPresentation(p); EXPECT_FALSE(confirmation->isChecked()); EXPECT_FALSE(save->isEnabled());
    confirmation->setChecked(true); EXPECT_TRUE(save->isEnabled());
}
TEST(InstallationSettingsDialog, BothAsymmetricPreviewsFlipThenRotateClockwiseAndKeepSavedSeparateFromActive) {
    InstallationSettingsDialog dialog; auto p = presentation(); dialog.setPresentation(p);
    auto* original = dialog.findChild<QLabel*>("installationOriginalPreview");
    auto* enhanced = dialog.findChild<QLabel*>("installationEnhancedPreview");
    auto* horizontal = dialog.findChild<QCheckBox*>("installationFlipHorizontal");
    auto* rotation = dialog.findChild<QComboBox*>("installationRotation");
    ASSERT_NE(original, nullptr); ASSERT_NE(enhanced, nullptr); ASSERT_NE(horizontal, nullptr); ASSERT_NE(rotation, nullptr);
    const auto initial = original->pixmap().toImage(); ASSERT_FALSE(initial.isNull());
    EXPECT_NE(initial.width(), initial.height());
    EXPECT_NE(initial.pixel(0, 0), initial.pixel(initial.width()-1, 0));
    horizontal->setChecked(true); rotation->setCurrentIndex(1);
    const auto oriented = original->pixmap().toImage();
    EXPECT_EQ(oriented, enhanced->pixmap().toImage());
    ASSERT_EQ(oriented.width(), initial.height()); ASSERT_EQ(oriented.height(), initial.width());
    for (int y=0; y<initial.height(); ++y) for (int x=0; x<initial.width(); ++x)
        EXPECT_EQ(oriented.pixel(initial.height()-1-y, initial.width()-1-x), initial.pixel(x,y));
    auto* active = dialog.findChild<QLabel*>("installationActiveLabel");
    auto* saved = dialog.findChild<QLabel*>("installationSavedLabel");
    ASSERT_NE(active, nullptr); ASSERT_NE(saved, nullptr);
    const auto activeBefore = active->text();
    auto repository = std::make_shared<application::InstallationProfilesSnapshot>(*p.installationProfiles);
    application::InstallationCameraProfile record;
    record.identity = p.cameraStatus->discoveredDescriptors.front().identity;
    record.revision = 3; record.orientation = {true,false,core::Rotation::Degrees90}; record.confirmed = true;
    repository->profiles.push_back(record); p.installationProfiles = repository;
    p.installationProfileOutcome = application::InstallationSaveOutcome{1,record,{}};
    dialog.setPresentation(p); EXPECT_EQ(active->text(), activeBefore); EXPECT_TRUE(saved->text().contains("3"));
    if (const auto path = qEnvironmentVariable("LUMORA_INSTALLATION_SCREENSHOT"); !path.isEmpty()) {
        dialog.show(); ASSERT_TRUE(dialog.grab().save(path));
    }
}
}
}

namespace lumora::ui {
TEST(CameraStartupPanel, InstallationInspectionIsReadOnlyForOperatorsAndFencesOrdinaryActionsDuringSave) {
    CameraStartupPanel panel; auto p = presentation(false); panel.setPresentation(p);
    auto* open = panel.findChild<QPushButton*>("installationSettingsButton");
    ASSERT_NE(open, nullptr); ASSERT_TRUE(open->isEnabled()); open->click(); open->click();
    const auto dialogs=panel.findChildren<InstallationSettingsDialog*>(); ASSERT_EQ(dialogs.size(),1);
    EXPECT_FALSE(dialogs.front()->findChild<QPushButton*>("saveInstallationButton")->isEnabled());
    p.installationProfilePending=true;panel.setPresentation(p);
    EXPECT_FALSE(panel.findChild<QPushButton*>("applyCameraButton")->isEnabled());
    EXPECT_TRUE(panel.findChild<QPushButton*>("disconnectCameraButton")->isEnabled());
}
}

namespace lumora::ui {
TEST(InstallationSettingsDialog, FirstCompletedRepositorySeedsSavedDraftForOperatorsAndAdministratorsWithoutReplacingLaterEdits) {
    for(const bool administrator : {false,true}) {
        SCOPED_TRACE(administrator);
        InstallationSettingsDialog dialog;auto p=presentation(administrator);
        auto repository=std::make_shared<application::InstallationProfilesSnapshot>(*p.installationProfiles);
        repository->loadCompleted=false;p.installationProfiles=repository;dialog.setPresentation(p);
        auto* horizontal=dialog.findChild<QCheckBox*>("installationFlipHorizontal");
        auto* vertical=dialog.findChild<QCheckBox*>("installationFlipVertical");
        auto* rotation=dialog.findChild<QComboBox*>("installationRotation");
        auto* original=dialog.findChild<QLabel*>("installationOriginalPreview");
        auto* enhanced=dialog.findChild<QLabel*>("installationEnhancedPreview");
        auto* confirmation=dialog.findChild<QCheckBox*>("installationConfirmation");
        ASSERT_NE(horizontal,nullptr);ASSERT_NE(vertical,nullptr);ASSERT_NE(rotation,nullptr);
        ASSERT_NE(original,nullptr);ASSERT_NE(enhanced,nullptr);ASSERT_NE(confirmation,nullptr);
        EXPECT_FALSE(horizontal->isEnabled());EXPECT_FALSE(confirmation->isEnabled());
        const auto initial=original->pixmap().toImage();ASSERT_FALSE(initial.isNull());
        auto ready=std::make_shared<application::InstallationProfilesSnapshot>(*repository);
        ready->loadCompleted=true;
        application::InstallationCameraProfile record;
        record.identity=p.cameraStatus->discoveredDescriptors.front().identity;record.revision=7;
        record.orientation={true,false,core::Rotation::Degrees90};record.confirmed=true;
        ready->profiles={record};p.installationProfiles=ready;dialog.setPresentation(p);
        EXPECT_TRUE(horizontal->isChecked());EXPECT_FALSE(vertical->isChecked());EXPECT_EQ(rotation->currentIndex(),1);
        EXPECT_EQ(horizontal->isEnabled(),administrator);EXPECT_FALSE(confirmation->isChecked());
        const auto oriented=original->pixmap().toImage();EXPECT_EQ(oriented,enhanced->pixmap().toImage());
        EXPECT_EQ(oriented.width(),initial.height());EXPECT_EQ(oriented.height(),initial.width());
        if(oriented.width()==initial.height() && oriented.height()==initial.width()) {
            for(int y=0;y<initial.height();++y) for(int x=0;x<initial.width();++x)
                EXPECT_EQ(oriented.pixel(initial.height()-1-y,initial.width()-1-x),initial.pixel(x,y));
        }
        if(administrator) {
            vertical->setChecked(true);confirmation->setChecked(true);dialog.setPresentation(p);
            EXPECT_TRUE(vertical->isChecked());EXPECT_TRUE(confirmation->isChecked());
        }
    }
}
TEST(InstallationSettingsDialog, AdministratorAndReadonlyLayoutsKeepVisibleControlsInsideDialogAtSupportedWidths) {
    for(const bool administrator : {false,true}) {
        InstallationSettingsDialog dialog;dialog.setPresentation(presentation(administrator));
        dialog.show();QCoreApplication::processEvents();
        const int minimumWidth=dialog.minimumSizeHint().width();
        for(const int width : {580,minimumWidth}) {
            SCOPED_TRACE(administrator);
            SCOPED_TRACE(width);
            dialog.resize(width,640);QCoreApplication::processEvents();
            EXPECT_EQ(dialog.width(),width)
                << "Confirmation minimum width: " << dialog.findChild<QCheckBox*>("installationConfirmation")->minimumSizeHint().width()
                << "; repair minimum width: " << dialog.findChild<QCheckBox*>("installationRepairConsent")->minimumSizeHint().width();
            EXPECT_EQ(dialog.height(),640);
            for(auto* widget:dialog.findChildren<QWidget*>()) {
                if(!widget->isVisibleTo(&dialog) || widget->isWindow())continue;
                const QRect bounds(widget->mapTo(&dialog,QPoint{}),widget->size());
                EXPECT_TRUE(dialog.rect().contains(bounds)) << widget->objectName().toStdString();
            }
            const auto directory=qEnvironmentVariable("LUMORA_INSTALLATION_LAYOUT_DIR");
            if(!directory.isEmpty()) {
                const auto path=directory+QStringLiteral("/installation-%1-%2.png")
                    .arg(administrator?QStringLiteral("admin"):QStringLiteral("readonly")).arg(width);
                ASSERT_TRUE(dialog.grab().save(path));
            }
        }
    }
}
}
