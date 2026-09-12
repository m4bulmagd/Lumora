#include <lumora/ui/InstallationSettingsDialog.hpp>
#include <lumora/ui/OrientationPresentation.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace lumora::ui {
namespace {
QImage referenceImage() {
    QImage image(192,128,QImage::Format_RGB32);
    image.fill(QColor("#243746"));
    QPainter painter(&image);
    painter.fillRect(0,0,48,40,QColor("#efb366"));
    painter.fillRect(144,0,48,40,QColor("#74c8c3"));
    painter.fillRect(0,88,48,40,QColor("#9c91d8"));
    painter.fillRect(144,88,48,40,QColor("#dd7690"));
    painter.setPen(Qt::white);
    painter.drawText(QRect(55,12,90,26),Qt::AlignCenter,QStringLiteral("TOP ↑"));
    painter.drawText(QRect(20,48,150,30),Qt::AlignCenter,QStringLiteral("L   →   RIGHT"));
    painter.drawText(QRect(62,96,76,24),Qt::AlignCenter,QStringLiteral("BOTTOM"));
    return image;
}
QImage orient(const QImage& source,core::Orientation orientation) {
    const bool quarter=orientation.rotation==core::Rotation::Degrees90 || orientation.rotation==core::Rotation::Degrees270;
    QImage result(quarter?source.height():source.width(),quarter?source.width():source.height(),source.format());
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) {
        const int flippedX=orientation.flipHorizontal?source.width()-1-x:x;
        const int flippedY=orientation.flipVertical?source.height()-1-y:y;
        int targetX=flippedX,targetY=flippedY;
        switch(orientation.rotation) {
        case core::Rotation::Degrees0:break;
        case core::Rotation::Degrees90:targetX=source.height()-1-flippedY;targetY=flippedX;break;
        case core::Rotation::Degrees180:targetX=source.width()-1-flippedX;targetY=source.height()-1-flippedY;break;
        case core::Rotation::Degrees270:targetX=flippedY;targetY=source.width()-1-flippedX;break;
        }
        result.setPixel(targetX,targetY,source.pixel(x,y));
    }
    return result;
}
QLabel* label(const char* name,QWidget* parent) {
    auto* value=new QLabel(parent);value->setObjectName(QString::fromLatin1(name));
    value->setTextFormat(Qt::PlainText);value->setWordWrap(true);return value;
}
}
struct InstallationSettingsDialog::Impl {
    CameraStartupPanelPresentation presentation;
    std::optional<camera::CameraId> cameraId;
    std::optional<core::CameraIdentity> identity;
    std::optional<camera::CameraCapabilities> capabilities;
    std::uint64_t generation{};
    bool invalidated{false};
    bool draftInitialized{false};
    bool localPending{false};
    std::optional<std::uint64_t> outcomeAtSubmission;
    QCheckBox* horizontal{};
    QCheckBox* vertical{};
    QComboBox* rotation{};
    QCheckBox* confirmation{};
    QCheckBox* repair{};
    QPushButton* save{};
    QLabel* status{};
    QLabel* active{};
    QLabel* saved{};
    QLabel* source{};
    QLabel* original{};
    QLabel* enhanced{};
    QImage reference=referenceImage();
    core::Orientation orientation() const {
        return {horizontal->isChecked(),vertical->isChecked(),static_cast<core::Rotation>(rotation->currentIndex())};
    }
    void update() {
        const auto& repository=presentation.installationProfiles;
        const auto& camera=presentation.cameraStatus;
        const bool administrator=repository && repository->administratorMode;
        const bool pending=localPending || presentation.installationProfilePending || (repository && repository->savePending);
        const bool idle=camera && camera->state==application::CameraSessionState::ConnectedIdle;
        const bool editing=administrator && repository->loadCompleted && presentation.controlsEnabled &&
            cameraId && draftInitialized && !invalidated && idle && !presentation.ordinaryOperationPending && !pending;
        horizontal->setEnabled(editing);vertical->setEnabled(editing);rotation->setEnabled(editing);
        confirmation->setEnabled(editing);repair->setEnabled(editing);
        repair->setVisible(repository && repository->loadError.has_value());
        save->setEnabled(editing && confirmation->isChecked() && (!repository->loadError || repair->isChecked()));
        if(invalidated) status->setText(InstallationSettingsDialog::tr("Camera or session changed. Close and reopen this editor for the current camera."));
        else if(!administrator) status->setText(InstallationSettingsDialog::tr("Read-only. Editing requires an administrator launch with --installation."));
        else if(pending) status->setText(InstallationSettingsDialog::tr("Saving installation settings… The active image orientation stays unchanged until Apply."));
        else if(!idle || presentation.ordinaryOperationPending) status->setText(InstallationSettingsDialog::tr("Stop acquisition and wait for camera operations to finish before editing installation settings."));
        else if(repository->loadError) status->setText(InstallationSettingsDialog::tr("The installation file is invalid or unavailable: %1\nRepair requires separate consent and preservation of the original file.")
            .arg(QString::fromStdString(repository->loadError->operatorSummary)));
        else if(presentation.installationProfileOutcome && presentation.installationProfileOutcome->error)
            status->setText(InstallationSettingsDialog::tr("Save failed: %1\nReview settings and confirm again to retry.")
                .arg(QString::fromStdString(presentation.installationProfileOutcome->error->operatorSummary)));
        else if(presentation.installationProfileOutcome && presentation.installationProfileOutcome->savedProfile)
            status->setText(InstallationSettingsDialog::tr("Installation saved. Apply → review Original and Enhanced → Confirm → Start. Saving does not activate these settings."));
        else status->setText(InstallationSettingsDialog::tr("Save installation → Apply → review Original and Enhanced → Confirm → Start."));
        if(!repository || !repository->loadCompleted)
            status->setText(InstallationSettingsDialog::tr("Loading installation settings…"));
        active->setText(presentation.activeOrientation
            ? InstallationSettingsDialog::tr("Active: %1").arg(orientationDescription(*presentation.activeOrientation))
            : InstallationSettingsDialog::tr("Active: no camera binding"));
        const auto* profile=repository && identity?application::findInstallationProfile(repository->profiles,*identity):nullptr;
        saved->setText(profile?InstallationSettingsDialog::tr("Saved revision %1: %2").arg(profile->revision).arg(orientationDescription(profile->orientation))
            : InstallationSettingsDialog::tr("Saved: no installation profile"));
        const auto image=QPixmap::fromImage(orient(reference,orientation()));
        original->setPixmap(image);enhanced->setPixmap(image);
    }
};
InstallationSettingsDialog::InstallationSettingsDialog(QWidget* parent)
    :QDialog(parent),impl_(std::make_unique<Impl>()) {
    auto& d=*impl_;
    setWindowTitle(tr("Camera installation"));setModal(false);resize(580,640);
    auto* layout=new QVBoxLayout(this);
    d.source=label("installationCameraLabel",this);layout->addWidget(d.source);
    d.active=label("installationActiveLabel",this);d.saved=label("installationSavedLabel",this);
    layout->addWidget(d.active);layout->addWidget(d.saved);
    auto* explanation=label("installationExplanationLabel",this);
    explanation->setText(tr("Both display views use the same orientation. Native Original and intermediate pixels stay unchanged. Flips are applied before clockwise rotation."));
    layout->addWidget(explanation);
    d.horizontal=new QCheckBox(tr("Flip horizontally"),this);d.horizontal->setObjectName("installationFlipHorizontal");
    d.vertical=new QCheckBox(tr("Flip vertically"),this);d.vertical->setObjectName("installationFlipVertical");
    d.rotation=new QComboBox(this);d.rotation->setObjectName("installationRotation");d.rotation->setAccessibleName(tr("Clockwise rotation"));
    d.rotation->addItems({tr("0° clockwise"),tr("90° clockwise"),tr("180° clockwise"),tr("270° clockwise")});
    layout->addWidget(d.horizontal);layout->addWidget(d.vertical);layout->addWidget(d.rotation);
    auto* previews=new QGridLayout;
    previews->addWidget(new QLabel(tr("Original preview"),this),0,0,Qt::AlignCenter);
    previews->addWidget(new QLabel(tr("Enhanced preview"),this),0,1,Qt::AlignCenter);
    d.original=label("installationOriginalPreview",this);d.enhanced=label("installationEnhancedPreview",this);
    for(auto* preview:{d.original,d.enhanced}) {preview->setAlignment(Qt::AlignCenter);preview->setMinimumSize(200,200);}
    previews->addWidget(d.original,1,0);previews->addWidget(d.enhanced,1,1);layout->addLayout(previews);
    d.confirmation=new QCheckBox(tr("Confirm orientation"),this);
    d.confirmation->setAccessibleDescription(tr("I confirm this orientation for this camera installation."));
    d.confirmation->setToolTip(d.confirmation->accessibleDescription());
    d.confirmation->setObjectName("installationConfirmation");layout->addWidget(d.confirmation);
    d.repair=new QCheckBox(tr("Preserve and replace invalid file"),this);
    d.repair->setAccessibleDescription(tr("Preserve the invalid file and replace it with this installation."));
    d.repair->setToolTip(d.repair->accessibleDescription());
    d.repair->setObjectName("installationRepairConsent");layout->addWidget(d.repair);
    d.status=label("installationStatusLabel",this);layout->addWidget(d.status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);
    d.save=buttons->addButton(tr("Save installation"),QDialogButtonBox::ActionRole);
    d.save->setObjectName("saveInstallationButton");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    const auto edited=[this] {impl_->confirmation->setChecked(false);impl_->update();};
    connect(d.horizontal,&QCheckBox::toggled,this,edited);connect(d.vertical,&QCheckBox::toggled,this,edited);
    connect(d.rotation,&QComboBox::currentIndexChanged,this,edited);
    connect(d.repair,&QCheckBox::toggled,this,edited);
    connect(d.confirmation,&QCheckBox::toggled,this,[this]{impl_->update();});
    connect(d.save,&QPushButton::clicked,this,[this] {
        auto& state=*impl_;if(!state.save->isEnabled() || !state.cameraId)return;
        state.localPending=true;
        state.outcomeAtSubmission=state.presentation.installationProfileOutcome
            ?std::optional{state.presentation.installationProfileOutcome->requestId}:std::nullopt;
        const auto orientation=state.orientation();const bool repair=state.repair->isChecked();
        state.confirmation->setChecked(false);state.update();
        emit installationSaveRequested(state.generation,*state.cameraId,orientation,true,repair);
    });
    d.update();
}
InstallationSettingsDialog::~InstallationSettingsDialog()=default;
void InstallationSettingsDialog::setPresentation(CameraStartupPanelPresentation presentation) {
    auto& d=*impl_;const auto& camera=presentation.cameraStatus;
    const camera::CameraDescriptor* descriptor=nullptr;
    if(camera && camera->actualIdentity) {
        const auto found=std::find_if(camera->discoveredDescriptors.begin(),camera->discoveredDescriptors.end(),
            [&](const auto& value){return value.id==*camera->actualIdentity;});
        if(found!=camera->discoveredDescriptors.end())descriptor=&*found;
    }
    if(!d.cameraId && camera && camera->actualIdentity && camera->capabilities && descriptor) {
        d.cameraId=camera->actualIdentity;d.generation=camera->sessionGeneration;
        d.identity=descriptor->identity;d.capabilities=camera->capabilities;
        d.source->setText(tr("%1 %2 · %3").arg(QString::fromStdString(d.identity->manufacturer),
            QString::fromStdString(d.identity->model),QString::fromStdString(d.identity->serial)));
    }
    if(d.cameraId && (!camera || camera->actualIdentity!=d.cameraId || presentation.selectedCameraId!=d.cameraId ||
        camera->sessionGeneration!=d.generation || !descriptor || !camera->capabilities ||
        !application::cameraIdentityKeysEqual(*d.identity,descriptor->identity) ||
        !application::cameraCapabilitiesEqual(*d.capabilities,*camera->capabilities))) {
        d.invalidated=true;d.confirmation->setChecked(false);
    }
    if(!d.draftInitialized && d.cameraId && !d.invalidated &&
        presentation.installationProfiles && presentation.installationProfiles->loadCompleted) {
        auto orientation=presentation.activeOrientation.value_or(core::Orientation{false,false,core::Rotation::Degrees0});
        if(const auto* profile=application::findInstallationProfile(presentation.installationProfiles->profiles,*d.identity))
            orientation=profile->orientation;
        const QSignalBlocker horizontal(d.horizontal),vertical(d.vertical),rotation(d.rotation);
        d.horizontal->setChecked(orientation.flipHorizontal);d.vertical->setChecked(orientation.flipVertical);
        d.rotation->setCurrentIndex(static_cast<int>(orientation.rotation));
        d.draftInitialized=true;
    }
    if(d.localPending && presentation.installationProfileOutcome &&
        presentation.installationProfileOutcome->requestId!=d.outcomeAtSubmission) {
        d.localPending=false;d.confirmation->setChecked(false);
    }
    d.presentation=std::move(presentation);d.update();
}
}
