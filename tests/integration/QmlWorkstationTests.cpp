#include "QmlWorkstation.hpp"
#include "CameraAdapter.hpp"
#include "CameraSettingsAdapter.hpp"
#include "ProcessingAdapter.hpp"
#include "ViewerAdapter.hpp"
#include "QuickImageItem.hpp"
#include "SimulatorComposition.hpp"
#include "FrameEngineTestAccess.hpp"
#include <atomic>
#include <array>
#include <QLineF>
#include <algorithm>
#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/ConfigurationStore.hpp>
#include <lumora/configuration/InstallationProfilesService.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>
#include <QtTest/QTest>
#include <memory>

namespace {
using namespace lumora;
class EnhancementFailure final : public processing::detail::EngineHooks {
public:
    std::atomic<bool> enabled{true};
    core::Result<void> before(processing::ProcessingOperation operation,std::span<std::byte>) override {
        if(enabled && operation==processing::ProcessingOperation::EnhancedDisplayMap)
            return core::Result<void>::failure({core::ErrorCategory::Processing,
                "qml_test_enhancement_failure","Enhancement failed.","Test-injected display-map failure.",true});
        return core::Result<void>::success();
    }
};
class QmlWorkstationTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void requiresGuardedStartupThroughRealControls();
    void stoppedCameraSettingsRequireExplicitApplyReviewAndConfirmation();
    void frameRateAndRegionRequireExplicitRebindReview();
    void installationCanBeInspectedWithoutAdministratorAuthority();
    void administratorOrientationPreviewAndSaveRequireSeparateActivation();
    void installationRepairRequiresSeparateKeyboardConsentAtMinimumSize();
    void completedViewingAndExactNumericEditing();
    void keyboardCanReturnToViewportWithoutStealingNumericInput();
    void presetsAndResetPreserveCameraPausedFrameAndViewport();
    void toneEditingPreservesExactValuesPausedPixelsAndResetAuthority();
    void localContrastEditingPreservesPausedFrameAndExactSettings();
    void denoiseEditingNormalizesModesAndPreservesPausedFrame();
    void sharpenEditingPreservesAdvancedValuesAndPausedFrame();
    void invertEditingPreservesRecipeAndPausedFrame();
    void renderedPixelsStayInsideViewport();
    void keepsLiveContentUsable_data();
    void keepsLiveContentUsable();
    void stopDisconnectAndExplicitSavedResume();
    void exposesFallbackAndExplicitProcessingRetry();
    void closesWithRenderingOwnersOutstanding();
    void cleanupTestCase();
private:
    QQuickItem* item(const char* name) const {
        const auto wanted=QString::fromLatin1(name);
        if(auto* result=window_->findChild<QQuickItem*>(wanted)) return result;
        // Popup content belongs to the overlay visual tree; its QObject owner
        // need not be a descendant of the application window.
        const auto find=[&](const auto& self,QQuickItem* parent)->QQuickItem* {
            if(parent->objectName()==wanted) return parent;
            for(auto* child:parent->childItems()) if(auto* result=self(self,child)) return result;
            return nullptr;
        };
        return find(find,window_->contentItem());
    }
    void click(const char* name) {
        auto* control=item(name);
        QVERIFY2(control, name);
        QVERIFY2(control->isVisible(),name);
        // Threaded rendering may withdraw the displayed bundle during a mode
        // change. Wait for the control's published readiness before input.
        QTRY_VERIFY2_WITH_TIMEOUT(control->isEnabled(),name,5000);
        // Policy publication can rearrange the camera grid before it is drawn.
        // Wait for real layout completion before sampling the click position.
        QVERIFY(QQuickTest::qWaitForPolish(window_));
        QSignalSpy activated(control,SIGNAL(clicked()));
        QVERIFY(activated.isValid());
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
            control->mapToScene(control->boundingRect().center()).toPoint());
        QCOMPARE(activated.count(),1);
    }
    QString diagnostics() const { return warnings_.join('\n'); }
    void capture(const QString& state);
    void choosePreset(const QString& id);
    void chooseDenoiseOption(const char* name,int index);
    bool denoiseSaved(processing::DenoiseMode mode,int kernel,double sigma,bool enabled=true) const;
    bool sharpenSaved(double amount,double radius,double threshold,bool enabled=true) const;
    void revealProcessing(const char* name);
    void enterText(const char* name,const QString& text,bool commit=true);
    QImage viewportPixels() const;
    bool toneSaved(double brightness,double contrast,double gamma) const;
    bool localContrastSaved(double clip,int grid,bool enabled=true) const;
    bool persistedPresetIs(const std::string& id) const;
    void createRuntime(std::shared_ptr<EnhancementFailure> fault = {}, bool administrator = false);
    void destroyRuntime();
    QTemporaryDir directory_;
    core::SystemClock clock_;
    camera::sim::SimulatedCameraProvider provider_{app::simulatorOptions(),clock_};
    std::unique_ptr<configuration::InstallationProfilesService> installations_;
    std::unique_ptr<application::LivePipeline> pipeline_;
    std::unique_ptr<configuration::StartupPreferencesService> preferences_;
    std::unique_ptr<qml::QmlWorkstation> runtime_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_{};
    QStringList warnings_;
};
void QmlWorkstationTests::initTestCase() { createRuntime(); }
void QmlWorkstationTests::createRuntime(std::shared_ptr<EnhancementFailure> fault, bool administrator) {
    warnings_.clear();
    QVERIFY(directory_.isValid());
    installations_=std::make_unique<configuration::InstallationProfilesService>(
        std::make_unique<configuration::InstallationProfileStore>(directory_.filePath("machine.json").toStdString(),administrator),
        administrator,application::InstallationProfilePolicy::SimulatorIdentityFallback);
    processing::ProcessingPreparationOptions options;
    QVERIFY(qml::reserveSimulatorRendererStorage(options).hasValue());
    // Fault coverage uses a real engine with its existing private test seam;
    // default-factory admission is exercised by all other UI/runtime cases.
    application::LivePipeline::ProcessorFactory factory;
    if(fault) factory=[fault](core::BufferPool& working,core::BufferPool& display,const core::ImageLayout& layout) {
        auto engine=processing::detail::FrameEngineTestAccess::create(working,display,layout,processing::defaultPipeline(),{},fault);
        using Result=core::Result<std::unique_ptr<processing::IFrameProcessor>>;
        if(!engine.hasValue()) return Result::failure(engine.error());
        return Result::success(std::move(engine).value());
    };
    pipeline_=std::make_unique<application::LivePipeline>(provider_,clock_,app::simulatorConfiguration(),
        std::move(factory),options,installations_.get());
    const auto preferencePath=directory_.filePath("pilot.json");
    if(!QFile::exists(preferencePath)) {
        configuration::ApplicationConfiguration settings;
        application::Preset saved;
        saved.id={"saved-fractional"};
        saved.name="Saved fractional";
        saved.description="Exact saved window/level fixture";
        saved.pipeline.stages[1].parameters=processing::WindowLevelParameters{12000.125,23000.875};
        saved.pipeline.stages[2].enabled=true;
        saved.pipeline.stages[2].parameters=processing::BrightnessContrastParameters{0.0123456789123456,1.012345678912345};
        saved.pipeline.stages[3].enabled=true;
        saved.pipeline.stages[3].parameters=processing::GammaParameters{1.234567891234567};
        settings.presets.customPresets.push_back(saved);
        saved.id={"saved-local-contrast"};
        saved.name="Saved local contrast";
        saved.description="Exact saved local contrast fixture";
        saved.pipeline.stages[4].enabled=true;
        saved.pipeline.stages[4].parameters=processing::ClaheParameters{2.3456789123456,4};
        settings.presets.customPresets.push_back(saved);
        saved.id={"saved-denoise"};
        saved.name="Saved denoise";
        saved.description="Exact saved denoise fixture";
        saved.pipeline.stages[5].enabled=true;
        saved.pipeline.stages[5].parameters=processing::DenoiseParameters{processing::DenoiseMode::Gaussian,7,1.234567891234567};
        settings.presets.customPresets.push_back(saved);
        saved.id={"saved-sharpen"};
        saved.name="Saved sharpen";
        saved.description="Exact saved sharpen fixture";
        saved.pipeline.stages[6].enabled=true;
        saved.pipeline.stages[6].parameters=processing::SharpenParameters{1.234567891234567,2.345678912345678,13.45678912345678};
        settings.presets.customPresets.push_back(saved);
        saved.id={"saved-invert"};
        saved.name="Saved invert";
        saved.description="Enabled invert with exact preceding stages";
        saved.pipeline.stages[7].enabled=true;
        settings.presets.customPresets.push_back(saved);
        QVERIFY(configuration::ConfigurationStore{preferencePath.toStdString()}.save(settings).hasValue());
    }
    preferences_=std::make_unique<configuration::StartupPreferencesService>(
        configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()});
    runtime_=std::make_unique<qml::QmlWorkstation>(*pipeline_,*preferences_,clock_,app::simulatorConfiguration());
    QVERIFY(installations_->start().hasValue());
    QVERIFY(preferences_->start().hasValue());
    QVERIFY(pipeline_->start().hasValue());
    QVERIFY(runtime_->start().hasValue());
    engine_=std::make_unique<QQmlApplicationEngine>();
    connect(engine_.get(),&QQmlEngine::warnings,this,[this](const QList<QQmlError>& errors){
        for(const auto& error:errors) warnings_.append(error.toString());
    });
    engine_->setInitialProperties({{"workstation",QVariant::fromValue(runtime_.get())},
        {"camera",QVariant::fromValue(runtime_->camera())},
        {"processing",QVariant::fromValue(runtime_->processing())},
        {"viewer",QVariant::fromValue(runtime_->viewer())}});
    engine_->loadFromModule("Lumora.Workstation","Main");
    QCOMPARE(engine_->rootObjects().size(),1);
    window_=qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
    QVERIFY(window_);
    QTRY_VERIFY_WITH_TIMEOUT(window_->isExposed(),5000);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->selectionEnabled(),5000);
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
    capture("waiting");
}
void QmlWorkstationTests::requiresGuardedStartupThroughRealControls() {
    QVERIFY(!runtime_->camera()->startLive());
    QVERIFY(!runtime_->camera()->confirmConfiguration());
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Disconnected);
    auto* selector=item("sourceSelector");
    QVERIFY(selector);
    selector->forceActiveFocus();
    QTest::keyClick(window_,Qt::Key_Space);
    QTest::keyClick(window_,Qt::Key_Home);
    QTest::keyClick(window_,Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->connectEnabled(),3000);
    capture("selected");
    click("connectButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->applyEnabled(),5000);
    capture("connected");
    QVERIFY(!runtime_->camera()->startLive());
    click("applyButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->confirmEnabled(),10000);
    QVERIFY(!runtime_->camera()->startLive());
    QVERIFY(item("currentReadback")->property("text").toString().contains("640"));
    capture("review");
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->startEnabled(),5000);
    capture("confirmed");
    click("startButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame(),15000);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    capture("live");
}
void QmlWorkstationTests::stoppedCameraSettingsRequireExplicitApplyReviewAndConfirmation() {
    QVERIFY(item("cameraSettingsButton"));
    auto* camera=runtime_->camera();
    auto* settings=camera->settings();
    const auto readPreferences=[&] {
        QFile file(directory_.filePath("pilot.json"));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto savedCameraMatches=[&](const camera::CameraConfiguration& requested,
                                     const camera::CameraConfiguration& actual) {
        const auto loaded=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
        if(!loaded.hasValue()) return false;
        const auto& profiles=loaded.value().cameraProfiles.profiles;
        const auto found=std::find_if(profiles.begin(),profiles.end(),[](const auto& profile) {
            return profile.cameraId.value=="SIM-LIVE";
        });
        return found!=profiles.end() && found->confirmed &&
            application::cameraConfigurationsEqual(found->requested,requested) &&
            application::cameraConfigurationsEqual(found->lastApplied,actual);
    };
    const auto typeSetting=[&](const char* name,const QString& text) {
        auto* field=item(name);
        QVERIFY2(field,name);
        QVERIFY(field->isVisible() && field->isEnabled());
        QVERIFY(QQuickTest::qWaitForPolish(window_));
        const QRectF rectangle=field->mapRectToScene(QRectF(0,0,field->width(),field->height()));
        QVERIFY(window_->contentItem()->boundingRect().contains(rectangle));
        QTest::mouseClick(window_,Qt::LeftButton,Qt::NoModifier,rectangle.center().toPoint());
        QVERIFY(field->hasActiveFocus());
        QTest::keyClick(window_,Qt::Key_A,Qt::ControlModifier);
        for(const QChar character:text)
            QTest::keyClick(window_,static_cast<Qt::Key>(character.unicode()));
        QCOMPARE(field->property("text").toString(),text);
    };
    const auto defaults=app::simulatorConfiguration();
    QTRY_VERIFY_WITH_TIMEOUT(savedCameraMatches(defaults,defaults),5000);
    click("stopButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(),5000);
    click("cameraSettingsButton");
    QTRY_VERIFY(settings->isOpen() && settings->editable());
    auto* dialog=window_->findChild<QObject*>(QStringLiteral("cameraSettingsDialog"));
    QVERIFY(dialog);
    QVERIFY(!dialog->property("modal").toBool());
    QCOMPARE(item("cameraExposureValue")->property("text").toString(),QStringLiteral("1000"));
    QCOMPARE(item("cameraGainValue")->property("text").toString(),QStringLiteral("0"));
    capture("camera-settings-stopped");
    const auto before=*pipeline_->snapshot().camera;
    const auto requestBefore=camera->requestedConfiguration();
    const auto currentBefore=camera->currentConfiguration();
    const auto preferencesBefore=readPreferences();
    QVERIFY(!preferencesBefore.isEmpty());
    const auto savesBefore=*preferences_->latestStatus();
    const auto processingBefore=QJsonDocument::fromJson(preferencesBefore).object().value("presets");
    const QString exposure=QStringLiteral("2345.678912345678");
    const QString gain=QStringLiteral("3.456789123456789");
    typeSetting("cameraExposureValue",exposure);
    QTest::keyClick(window_,Qt::Key_Tab);
    typeSetting("cameraGainValue",gain);
    QTest::keyClick(window_,Qt::Key_Return);
    QCOMPARE(settings->exposureText(),exposure);
    QCOMPARE(settings->gainText(),gain);
    QVERIFY(settings->applyEnabled());
    // Local draft edits, focus loss and Enter must not post camera commands or
    // save a profile, including an accidental same-value Apply.
    for(int poll=0;poll<10;++poll) {
        QCoreApplication::processEvents();
        QTest::qWait(10);
        const auto now=pipeline_->snapshot().camera;
        QCOMPARE(now->sessionGeneration,before.sessionGeneration);
        QCOMPARE(now->appliedRevision,before.appliedRevision);
        QCOMPARE(now->confirmedRevision,before.confirmedRevision);
        QCOMPARE(now->latestOutcome.has_value(),before.latestOutcome.has_value());
        if(before.latestOutcome) QCOMPARE(now->latestOutcome->requestId,before.latestOutcome->requestId);
        QCOMPARE(camera->requestedConfiguration(),requestBefore);
        QCOMPARE(camera->currentConfiguration(),currentBefore);
        QCOMPARE(preferences_->latestStatus()->latestAttemptedSaveRevision,savesBefore.latestAttemptedSaveRevision);
        QCOMPARE(preferences_->latestStatus()->latestSavedRevision,savesBefore.latestSavedRevision);
        QCOMPARE(readPreferences(),preferencesBefore);
    }
    capture("camera-settings-draft");
    click("closeCameraSettingsButton");
    QTRY_VERIFY(!settings->isOpen());
    QCOMPARE(readPreferences(),preferencesBefore);
    click("cameraSettingsButton");
    QTRY_VERIFY(settings->editable());
    QCOMPARE(settings->exposureText(),QStringLiteral("1000"));
    QCOMPARE(settings->gainText(),QStringLiteral("0"));
    typeSetting("cameraExposureValue",QStringLiteral("10000.5"));
    QVERIFY(!settings->applyEnabled());
    QVERIFY(!settings->apply());
    QCOMPARE(camera->requestedConfiguration(),requestBefore);
    QCOMPARE(readPreferences(),preferencesBefore);
    capture("camera-settings-invalid");
    typeSetting("cameraExposureValue",exposure);
    typeSetting("cameraGainValue",gain);
    QVERIFY(settings->applyEnabled());
    click("applyCameraSettingsButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->confirmEnabled(),10000);
    QVERIFY(!camera->startLive());
    auto expectedRequest=defaults;
    expectedRequest.exposure.requestedMicroseconds=exposure.toDouble();
    expectedRequest.gain.requestedDb=gain.toDouble();
    auto expectedActual=defaults;
    expectedActual.exposure.requestedMicroseconds=2346.0;
    expectedActual.gain.requestedDb=3.0;
    const auto applied=pipeline_->snapshot().camera;
    QVERIFY(applied->appliedConfiguration);
    QVERIFY(application::cameraConfigurationsEqual(applied->appliedConfiguration->requested,expectedRequest));
    QVERIFY(application::cameraConfigurationsEqual(applied->appliedConfiguration->actual,expectedActual));
    QVERIFY(!applied->confirmedRevision);
    QCOMPARE(readPreferences(),preferencesBefore);
    capture("camera-settings-review");
    click("closeCameraSettingsButton");
    click("cameraSettingsButton");
    QTRY_VERIFY(settings->isOpen());
    QVERIFY(settings->currentSummary().contains(QStringLiteral("2346")));
    click("closeCameraSettingsButton");
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(),5000);
    QTRY_VERIFY_WITH_TIMEOUT(savedCameraMatches(expectedRequest,expectedActual),5000);
    QVERIFY(preferences_->latestStatus()->latestSavedRevision!=savesBefore.latestSavedRevision);
    QCOMPARE(QJsonDocument::fromJson(readPreferences()).object().value("presets"),processingBefore);
    const auto frameBeforeStart=runtime_->viewer()->sourceFrameId();
    click("startButton");
    QTRY_COMPARE_WITH_TIMEOUT(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming,5000);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame()
        && runtime_->viewer()->sourceFrameId()!=frameBeforeStart,15000);
    click("cameraSettingsButton");
    QTRY_VERIFY(settings->isOpen());
    QVERIFY(!settings->editable() && !settings->applyEnabled());
    QVERIFY(!item("cameraExposureValue")->isEnabled());
    QVERIFY(!item("cameraGainValue")->isEnabled());
    QVERIFY(!item("applyCameraSettingsButton")->isEnabled());
    const auto streamingRequest=camera->requestedConfiguration();
    const auto streamingRevision=pipeline_->snapshot().camera->appliedRevision;
    QVERIFY(!settings->editExposureText(QStringLiteral("4444")));
    QVERIFY(!settings->editGainText(QStringLiteral("7")));
    QVERIFY(!settings->apply());
    QCOMPARE(camera->requestedConfiguration(),streamingRequest);
    QCOMPARE(pipeline_->snapshot().camera->appliedRevision,streamingRevision);
    capture("camera-settings-streaming");
    // A nonmodal dialog must leave the actual viewer/priority controls usable.
    click("pauseButton");
    // A native screenshot can temporarily withdraw the displayed bundle while
    // the renderer restores it. Freeze only a completed, visible Paused frame.
    QTRY_VERIFY(runtime_->viewer()->playbackState()==QStringLiteral("Paused")
        && runtime_->viewer()->hasFrame());
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    QVERIFY(!settings->editable() && !settings->applyEnabled());
    const auto pausedFrame=runtime_->viewer()->sourceFrameId();
    QVERIFY(!pausedFrame.isEmpty());
    capture("camera-settings-paused");
    QTRY_VERIFY(runtime_->viewer()->hasFrame());
    QCOMPARE(runtime_->viewer()->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(runtime_->viewer()->sourceFrameId(),pausedFrame);
    click("stopButton");
    QTRY_COMPARE_WITH_TIMEOUT(pipeline_->snapshot().camera->state,application::CameraSessionState::ConnectedIdle,5000);
    QVERIFY(!pipeline_->snapshot().camera->desiredStreaming);
    QVERIFY(item("disconnectButton")->isEnabled());
    capture("camera-settings-after-stop");
    click("disconnectButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->connectEnabled(),5000);
    QVERIFY(!settings->editable());
    QVERIFY(!settings->apply());
    QCOMPARE(QJsonDocument::fromJson(readPreferences()).object().value("presets"),processingBefore);
    // A new runtime must load the exact requested profile and independently
    // rounded actual facts; merely inspecting already-written bytes is weak.
    destroyRuntime();
    createRuntime();
    camera=runtime_->camera();
    settings=camera->settings();
    QTRY_VERIFY_WITH_TIMEOUT(camera->resumeLiveEnabled(),10000);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    QVERIFY(!runtime_->viewer()->hasFrame());
    QCOMPARE(camera->requestedConfiguration().value("exposureMicroseconds").toDouble(),exposure.toDouble());
    QCOMPARE(camera->requestedConfiguration().value("gainDb").toDouble(),gain.toDouble());
    click("cameraSettingsButton");
    QTRY_VERIFY(settings->isOpen() && settings->editable());
    QCOMPARE(settings->exposureText(),exposure);
    QCOMPARE(settings->gainText(),gain);
    capture("camera-settings-reopened");
    click("closeCameraSettingsButton");
    click("resumeLiveButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame(),15000);
    QVERIFY(application::cameraConfigurationsEqual(*pipeline_->snapshot().camera->currentConfiguration,expectedActual));
    // Restore the established fixture contract for every subsequent scene.
    click("stopButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(),5000);
    click("cameraSettingsButton");
    QTRY_VERIFY(settings->editable());
    typeSetting("cameraExposureValue",QStringLiteral("1000"));
    typeSetting("cameraGainValue",QStringLiteral("0"));
    click("applyCameraSettingsButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->confirmEnabled(),10000);
    click("closeCameraSettingsButton");
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(savedCameraMatches(defaults,defaults),5000);
    QCOMPARE(QJsonDocument::fromJson(readPreferences()).object().value("presets"),processingBefore);
    const auto stoppedFrame=runtime_->viewer()->sourceFrameId();
    click("startButton");
    QTRY_COMPARE_WITH_TIMEOUT(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming,5000);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame()
        && runtime_->viewer()->sourceFrameId()!=stoppedFrame,15000);
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::frameRateAndRegionRequireExplicitRebindReview() {
    auto* camera = runtime_->camera();
    auto* settings = camera->settings();
    click("stopButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    click("cameraSettingsButton");
    QTRY_VERIFY(settings->isOpen() && settings->editable());
    QVERIFY2(item("cameraAcquisitionTab"), "FPS and ROI controls must be available in QML");
    click("cameraAcquisitionTab");
    capture("camera-acquisition-open");
    const auto input = [&](const char* name, const QString& value) {
        auto* field = item(name);
        QVERIFY2(field, name);
        QVERIFY(field->isVisible() && field->isEnabled());
        field->forceActiveFocus();
        QVERIFY(QQuickTest::qWaitForPolish(window_));
        const auto rect = field->mapRectToScene(field->boundingRect());
        QVERIFY(window_->contentItem()->boundingRect().contains(rect));
        QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
        for (const auto character : value)
            QTest::keyClick(window_, character.toLatin1());
        QCOMPARE(field->property("text").toString(), value);
    };
    const auto original = *pipeline_->snapshot().camera->currentConfiguration;
    input("cameraFrameRateValue", QStringLiteral("12e"));
    QVERIFY(!item("applyCameraSettingsButton")->isEnabled());
    input("cameraFrameRateValue", QStringLiteral("12.3456789123456"));
    input("cameraRoiWidth", QStringLiteral("320"));
    input("cameraRoiHeight", QStringLiteral("240"));
    QVERIFY(item("cameraPixelFormat"));
    QVERIFY(application::cameraConfigurationsEqual(*pipeline_->snapshot().camera->currentConfiguration, original));
    capture("camera-acquisition-draft");
    click("applyCameraSettingsButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->confirmEnabled(), 10000);
    QVERIFY(!camera->startEnabled());
    QVERIFY(!settings->editable());
    QCOMPARE(pipeline_->snapshot().camera->state, application::CameraSessionState::ConnectedIdle);
    const auto actual = *pipeline_->snapshot().camera->currentConfiguration;
    QCOMPARE(actual.roi.width, 320U);
    QCOMPARE(actual.roi.height, 240U);
    QCOMPARE(*pipeline_->snapshot().camera->requestedConfiguration->requestedFps, 12.3456789123456);
    QCOMPARE(*actual.requestedFps, 12.0);
    capture("camera-acquisition-readback");
    click("closeCameraSettingsButton");
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    click("startButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame() &&
        pipeline_->snapshot().camera->state == application::CameraSessionState::Streaming, 10000);
    click("cameraSettingsButton");
    click("cameraAcquisitionTab");
    QVERIFY(!item("cameraFrameRateValue")->isEnabled());
    QVERIFY(!item("cameraRoiWidth")->isEnabled());
    QVERIFY(!item("applyCameraSettingsButton")->isEnabled());
    click("closeCameraSettingsButton");
    click("stopButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    destroyRuntime();
    createRuntime();
    camera = runtime_->camera();
    settings = camera->settings();
    QTRY_VERIFY_WITH_TIMEOUT(camera->resumeLiveEnabled(), 10000);
    click("cameraSettingsButton");
    click("cameraAcquisitionTab");
    QCOMPARE(settings->frameRateText(), QStringLiteral("12.3456789123456"));
    QCOMPARE(settings->roiWidthText(), QStringLiteral("320"));
    QCOMPARE(settings->roiHeightText(), QStringLiteral("240"));
    capture("camera-acquisition-reopened");
    input("cameraFrameRateValue", QStringLiteral("30"));
    input("cameraRoiWidth", QStringLiteral("640"));
    input("cameraRoiHeight", QStringLiteral("480"));
    click("applyCameraSettingsButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->confirmEnabled(), 10000);
    click("closeCameraSettingsButton");
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    const auto stoppedFrame = runtime_->viewer()->sourceFrameId();
    click("startButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame() && runtime_->viewer()->sourceFrameId() != stoppedFrame &&
        pipeline_->snapshot().camera->state == application::CameraSessionState::Streaming, 15000);
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}
void QmlWorkstationTests::installationCanBeInspectedWithoutAdministratorAuthority() {
    QVERIFY2(item("installationSettingsButton"), "Installation inspection must be available in QML");
    click("installationSettingsButton");
    auto* dialog = window_->findChild<QObject*>(QStringLiteral("installationSettingsDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(!dialog->property("modal").toBool());
    for (const auto* name : {"installationFlipHorizontal", "installationFlipVertical", "installationRotation",
                             "installationConfirmation", "saveInstallationButton"}) {
        auto* control = item(name);
        QVERIFY2(control, name);
        QVERIFY(!control->isEnabled());
    }
    QVERIFY(item("installationActive"));
    QVERIFY(item("installationSaved"));
    QVERIFY(item("installationOriginalPreview"));
    QVERIFY(item("installationEnhancedPreview"));
    QVERIFY(item("stopButton")->isEnabled());
    capture("installation-operator-inspection");
    click("closeInstallationButton");
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}
void QmlWorkstationTests::administratorOrientationPreviewAndSaveRequireSeparateActivation() {
    destroyRuntime();
    createRuntime({}, true);
    auto* camera = runtime_->camera();
    auto* installation = runtime_->installation();
    QTRY_VERIFY_WITH_TIMEOUT(camera->resumeLiveEnabled(), 10000);
    window_->resize(900, 600);
    QTRY_COMPARE(window_->size(), QSize(900, 600));
    const auto focusedInside = [&](const char* name, const char* scroller) {
        auto* control = item(name);
        auto* viewport = item(scroller);
        QVERIFY2(control && viewport, name);
        QTRY_VERIFY_WITH_TIMEOUT(control->hasActiveFocus(), 3000);
        QTRY_VERIFY2_WITH_TIMEOUT(viewport->mapRectToScene(viewport->boundingRect()).adjusted(-1,-1,1,1)
            .contains(control->mapRectToScene(QRectF(0,0,control->width(),control->height()))), name, 3000);
    };
    click("cameraSettingsButton");
    click("cameraAcquisitionTab");
    item("cameraFrameRateValue")->forceActiveFocus();
    focusedInside("cameraFrameRateValue", "cameraSettingsFlickable");
    for (const auto* name : {"cameraPixelFormat", "cameraRoiX", "cameraRoiY", "cameraRoiWidth", "cameraRoiHeight"}) {
        QTest::keyClick(window_, Qt::Key_Tab);
        focusedInside(name, "cameraSettingsFlickable");
    }
    capture("camera-acquisition-minimum-keyboard");
    for (const auto* name : {"cameraRoiWidth", "cameraRoiY", "cameraRoiX", "cameraPixelFormat", "cameraFrameRateValue"}) {
        QTest::keyClick(window_, Qt::Key_Tab, Qt::ShiftModifier);
        focusedInside(name, "cameraSettingsFlickable");
    }
    click("cameraExposureTab");
    item("cameraExposureMode")->forceActiveFocus();
    focusedInside("cameraExposureMode", "cameraSettingsFlickable");
    for (const auto* name : {"cameraExposureValue", "cameraGainMode", "cameraGainValue"}) {
        QTest::keyClick(window_, Qt::Key_Tab);
        focusedInside(name, "cameraSettingsFlickable");
    }
    for (const auto* name : {"cameraGainMode", "cameraExposureValue", "cameraExposureMode"}) {
        QTest::keyClick(window_, Qt::Key_Tab, Qt::ShiftModifier);
        focusedInside(name, "cameraSettingsFlickable");
    }
    click("closeCameraSettingsButton");
    click("installationSettingsButton");
    QTRY_VERIFY(installation->isOpen() && installation->editable());
    item("installationFlipHorizontal")->forceActiveFocus();
    focusedInside("installationFlipHorizontal", "installationSettingsFlickable");
    for (const auto* name : {"installationFlipVertical", "installationRotation", "installationConfirmation"}) {
        QTest::keyClick(window_, Qt::Key_Tab);
        focusedInside(name, "installationSettingsFlickable");
    }
    QTest::keyClick(window_, Qt::Key_Space);
    QVERIFY(installation->confirmationChecked());
    QTest::keyClick(window_, Qt::Key_Tab);
    QTRY_VERIFY(item("saveInstallationButton")->hasActiveFocus());
    // Focus never obscures the fixed save action or the priority camera column.
    QVERIFY(window_->contentItem()->boundingRect().contains(item("saveInstallationButton")->mapRectToScene(item("saveInstallationButton")->boundingRect())));
    auto* popup = window_->findChild<QObject*>(QStringLiteral("installationSettingsDialog"));
    QVERIFY(popup);
    const QRectF popupRect(popup->property("x").toDouble(), popup->property("y").toDouble(),
                           popup->property("width").toDouble(), popup->property("height").toDouble());
    QVERIFY(!popupRect.intersects(item("disconnectButton")->mapRectToScene(item("disconnectButton")->boundingRect())));
    capture("installation-minimum-keyboard");
    QTest::keyClick(window_, Qt::Key_Tab, Qt::ShiftModifier);
    focusedInside("installationConfirmation", "installationSettingsFlickable");
    QTest::keyClick(window_, Qt::Key_Space);
    QVERIFY(!installation->confirmationChecked());
    for (const auto* name : {"installationRotation", "installationFlipVertical", "installationFlipHorizontal"}) {
        QTest::keyClick(window_, Qt::Key_Tab, Qt::ShiftModifier);
        focusedInside(name, "installationSettingsFlickable");
    }
    window_->resize(1280, 800);
    QTRY_COMPARE(window_->size(), QSize(1280, 800));
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    const auto activeBefore = runtime_->coordinator().state().activeOrientation;
    QVERIFY(!item("saveInstallationButton")->isEnabled());
    const auto choose = [&](bool horizontal, bool vertical, int rotation) {
        if (item("installationFlipHorizontal")->property("checked").toBool() != horizontal)
            click("installationFlipHorizontal");
        if (item("installationFlipVertical")->property("checked").toBool() != vertical)
            click("installationFlipVertical");
        auto* selector = item("installationRotation");
        selector->forceActiveFocus();
        QVERIFY(QQuickTest::qWaitForPolish(window_));
        QTest::keyClick(window_, Qt::Key_Space);
        QTest::keyClick(window_, Qt::Key_Home);
        for (int row = 0; row < rotation; ++row) QTest::keyClick(window_, Qt::Key_Down);
        QTest::keyClick(window_, Qt::Key_Return);
        QTRY_COMPARE(installation->flipHorizontal(), horizontal);
        QTRY_COMPARE(installation->flipVertical(), vertical);
        QTRY_COMPARE(installation->rotationIndex(), rotation);
        QVERIFY(QQuickTest::qWaitForPolish(window_));
    };
    // Independent geometry/color oracle: flip native offsets first, then rotate
    // clockwise. Check both real QML previews for all sixteen orientations.
    const std::array<const char*, 4> corners{"referenceTopLeft", "referenceTopRight", "referenceBottomLeft", "referenceBottomRight"};
    const std::array<QPointF, 4> offsets{QPointF(-80,-48), QPointF(80,-48), QPointF(-80,48), QPointF(80,48)};
    const std::array<QColor, 4> colors{QColor("#ef6262"), QColor("#64d18a"), QColor("#68a7ee"), QColor("#f0cd60")};
    for (const bool horizontal : {false, true}) for (const bool vertical : {false, true}) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            choose(horizontal, vertical, rotation);
            const auto screenshot = window_->grabWindow();
            QVERIFY(!screenshot.isNull());
            for (const auto* name : {"installationOriginalPreview", "installationEnhancedPreview"}) {
                auto* preview = item(name);
                QVERIFY(preview);
                const auto center = preview->mapToScene(preview->boundingRect().center());
                const double scale = std::min(preview->width() / (rotation % 2 ? 128.0 : 192.0),
                                              preview->height() / (rotation % 2 ? 192.0 : 128.0));
                for (std::size_t index = 0; index < corners.size(); ++index) {
                    auto offset = offsets[index];
                    if (horizontal) offset.setX(-offset.x());
                    if (vertical) offset.setY(-offset.y());
                    for (int turn = 0; turn < rotation; ++turn) offset = QPointF(-offset.y(), offset.x());
                    const auto expected = center + offset * scale;
                    auto* corner = preview->findChild<QQuickItem*>(QString::fromLatin1(corners[index]));
                    QVERIFY(corner);
                    const auto actual = corner->mapToScene(corner->boundingRect().center());
                    // Qt Quick centered anchors round to logical pixels; two
                    // axes can each differ by half a pixel from ideal center.
                    QVERIFY2(QLineF(expected, actual).length() <= 1.0,
                        qPrintable(QString("%1 H=%2 V=%3 R=%4 corner=%5 expected=%6,%7 actual=%8,%9 size=%10x%11")
                            .arg(name).arg(horizontal).arg(vertical).arg(rotation).arg(index)
                            .arg(expected.x()).arg(expected.y()).arg(actual.x()).arg(actual.y())
                            .arg(preview->width()).arg(preview->height())));
                    const auto pixel = (expected * window_->devicePixelRatio()).toPoint();
                    QCOMPARE(screenshot.pixelColor(pixel).rgb(), colors[index].rgb());
                }
            }
        }
    }
    choose(true, false, 1);
    click("installationConfirmation");
    QVERIFY(item("saveInstallationButton")->isEnabled());
    choose(true, true, 1);
    QVERIFY(!item("installationConfirmation")->property("checked").toBool());
    QVERIFY(!item("saveInstallationButton")->isEnabled());
    choose(true, false, 1);
    click("installationConfirmation");
    click("saveInstallationButton");
    QTRY_VERIFY_WITH_TIMEOUT(!installation->pending() && installation->savedSummary().contains("90"), 5000);
    QVERIFY(!installation->confirmationChecked());
    QCOMPARE(runtime_->coordinator().state().activeOrientation, activeBefore);
    QVERIFY(!camera->startEnabled());
    capture("installation-saved-awaiting-apply");
    click("closeInstallationButton");
    click("applyButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->confirmEnabled(), 10000);
    QCOMPARE(runtime_->coordinator().state().activeOrientation,
             std::optional(core::Orientation{true, false, core::Rotation::Degrees90}));
    QVERIFY(!camera->startEnabled());
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    click("startButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame() &&
        pipeline_->snapshot().camera->state == application::CameraSessionState::Streaming, 10000);
    click("stopButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    // Restore identity through the same explicit production workflow.
    click("installationSettingsButton");
    choose(false, false, 0);
    click("installationConfirmation");
    click("saveInstallationButton");
    QTRY_VERIFY_WITH_TIMEOUT(!installation->pending() && installation->editable(), 5000);
    click("closeInstallationButton");
    click("applyButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->confirmEnabled(), 10000);
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
    destroyRuntime();
    createRuntime();
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->resumeLiveEnabled(), 10000);
    click("resumeLiveButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame() &&
        pipeline_->snapshot().camera->state == application::CameraSessionState::Streaming, 15000);
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}
void QmlWorkstationTests::installationRepairRequiresSeparateKeyboardConsentAtMinimumSize() {
    destroyRuntime();
    QFile invalid(directory_.filePath("machine.json"));
    QVERIFY(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(invalid.write("invalid installation fixture") > 0);
    invalid.close();
    createRuntime({}, true);
    auto* camera = runtime_->camera();
    auto* installation = runtime_->installation();
    QTRY_VERIFY_WITH_TIMEOUT(camera->installationEnabled(), 10000);
    window_->resize(900, 600);
    QTRY_COMPARE(window_->size(), QSize(900, 600));
    click("installationSettingsButton");
    QTRY_VERIFY(installation->editable() && installation->repairVisible());
    auto* confirmation = item("installationConfirmation");
    auto* repair = item("installationRepair");
    auto* scroll = item("installationSettingsFlickable");
    QVERIFY(confirmation && repair && scroll);
    confirmation->forceActiveFocus();
    QTest::keyClick(window_, Qt::Key_Space);
    QVERIFY(installation->confirmationChecked());
    QVERIFY(!installation->saveEnabled());
    QTest::keyClick(window_, Qt::Key_Tab);
    QTRY_VERIFY(repair->hasActiveFocus());
    capture("installation-repair-minimum-focus");
    QTRY_VERIFY(scroll->mapRectToScene(scroll->boundingRect()).adjusted(-1,-1,1,1)
        .contains(repair->mapRectToScene(repair->boundingRect())));
    QTest::keyClick(window_, Qt::Key_Space);
    QVERIFY(installation->repairChecked());
    QVERIFY(!installation->confirmationChecked());
    QVERIFY(!installation->saveEnabled());
    capture("installation-repair-minimum-keyboard");
    QTest::keyClick(window_, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(confirmation->hasActiveFocus());
    QTest::keyClick(window_, Qt::Key_Space);
    QVERIFY(installation->saveEnabled());
    click("saveInstallationButton");
    QTRY_VERIFY_WITH_TIMEOUT(!installation->pending() && !installation->repairVisible(), 5000);
    QVERIFY(!installation->confirmationChecked());
    click("closeInstallationButton");
    click("applyButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->confirmEnabled(), 10000);
    click("confirmButton");
    QTRY_VERIFY_WITH_TIMEOUT(camera->startEnabled(), 5000);
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
    destroyRuntime();
    createRuntime();
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->resumeLiveEnabled(), 10000);
    click("resumeLiveButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame() &&
        pipeline_->snapshot().camera->state == application::CameraSessionState::Streaming, 15000);
}
void QmlWorkstationTests::completedViewingAndExactNumericEditing() {
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->compareAvailable(),10000);
    click("compareButton");
    QTRY_COMPARE_WITH_TIMEOUT(runtime_->viewer()->displayMode(),QStringLiteral("compare"),5000);
    click("pauseButton");
    QTRY_COMPARE_WITH_TIMEOUT(runtime_->viewer()->playbackState(),QStringLiteral("Paused"),5000);
    const QString pausedFrame=runtime_->viewer()->sourceFrameId();
    auto* field=item("levelField");
    QVERIFY(field);
    field->forceActiveFocus();
    QTest::keyClick(window_,Qt::Key_A,Qt::ControlModifier);
    for(const QChar digit:QStringLiteral("32767.125"))
        QTest::keyClick(window_,static_cast<Qt::Key>(digit.unicode()));
    QTest::keyClick(window_,Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(runtime_->processing()->level(),32767.125,5000);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),10000);
    QCOMPARE(runtime_->viewer()->sourceFrameId(),pausedFrame);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->frameAgeMs() >= 500,3000);
    QCOMPARE(runtime_->viewer()->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(runtime_->viewer()->sourceFrameId(),pausedFrame);
    capture("paused-compare");
    click("resumeButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->sourceFrameId()!=pausedFrame,5000);
    click("originalButton");
    QTRY_COMPARE_WITH_TIMEOUT(runtime_->viewer()->displayMode(),QStringLiteral("original"),5000);
    click("fitButton");
    click("actualPixelsButton");
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::choosePreset(const QString& id) {
    auto* selector=item("presetSelector");
    QVERIFY(selector);
    QVERIFY(selector->isEnabled());
    const auto rows=runtime_->processing()->property("presets").toList();
    int index=-1;
    for(qsizetype row=0;row<rows.size();++row)
        if(rows[row].toMap().value("id").toString()==id) index=static_cast<int>(row);
    QVERIFY2(index>=0,qPrintable(id));
    QCOMPARE(selector->property("count").toInt(),rows.size());
    selector->forceActiveFocus();
    QTest::keyClick(window_,Qt::Key_Space);
    QTest::keyClick(window_,Qt::Key_Home);
    for(int row=0;row<index;++row) QTest::keyClick(window_,Qt::Key_Down);
    QTest::keyClick(window_,Qt::Key_Return);
    QTRY_COMPARE(selector->property("currentValue").toString(),id);
    QTRY_COMPARE(runtime_->processing()->property("selectedPresetId").toString(),id);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),10000);
    const auto& acknowledged=runtime_->coordinator().processingControls()->acknowledged();
    QVERIFY(acknowledged);
    QCOMPARE(QString::fromStdString(acknowledged->selectedId.value),id);
}
bool QmlWorkstationTests::persistedPresetIs(const std::string& id) const {
    const auto loaded=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
    return loaded.hasValue() && loaded.value().presets.selectedId.value==id;
}
void QmlWorkstationTests::revealProcessing(const char* name) {
    auto* control=item(name);
    QVERIFY2(control,name);
    control->forceActiveFocus();
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    auto* scroll=item("processingFlickable");
    QVERIFY(scroll);
    // Keyboard focus must reveal the real editor inside the scroll viewport.
    QTRY_VERIFY2(scroll->boundingRect().adjusted(-1,-1,1,1).contains(
        control->mapRectToItem(scroll,QRectF(0,0,control->width(),control->height()))),name);
}
void QmlWorkstationTests::enterText(const char* name,const QString& text,bool commit) {
    revealProcessing(name);
    auto* control=item(name);
    QVERIFY(control && control->isEnabled());
    QTest::keyClick(window_,Qt::Key_A,Qt::ControlModifier);
    for(const QChar character:text)
        QTest::keyClick(window_,static_cast<Qt::Key>(character.unicode()));
    QCOMPARE(control->property("text").toString(),text);
    if(commit) QTest::keyClick(window_,Qt::Key_Return);
}
QImage QmlWorkstationTests::viewportPixels() const {
    const auto rect=item("imageArea")->mapRectToScene(item("imageArea")->boundingRect());
    const auto image=window_->grabWindow();
    const auto dpr=image.devicePixelRatio();
    return image.copy(QRect(qRound(rect.x()*dpr),qRound(rect.y()*dpr),
        qRound(rect.width()*dpr),qRound(rect.height()*dpr)));
}
bool QmlWorkstationTests::toneSaved(double brightness,double contrast,double gamma) const {
    const auto loaded=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
    if(!loaded.hasValue()) return false;
    const auto& state=loaded.value().presets;
    const auto& stages=state.activePipeline.stages;
    const auto& tone=std::get<processing::BrightnessContrastParameters>(stages[2].parameters);
    return state.selectedId.value=="custom" && stages[2].enabled && stages[3].enabled
        && tone.brightness==brightness && tone.contrast==contrast
        && std::get<processing::GammaParameters>(stages[3].parameters).gamma==gamma;
}
bool QmlWorkstationTests::localContrastSaved(double clip,int grid,bool enabled) const {
    const auto loaded=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
    if(!loaded.hasValue()) return false;
    const auto& state=loaded.value().presets;
    const auto& stage=state.activePipeline.stages[4];
    const auto& parameters=std::get<processing::ClaheParameters>(stage.parameters);
    return state.selectedId.value=="custom" && stage.enabled==enabled
        && parameters.clipLimit==clip && parameters.tileGridSize==static_cast<std::uint32_t>(grid);
}
bool QmlWorkstationTests::sharpenSaved(double amount,double radius,double threshold,bool enabled) const {
    const auto loaded=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
    if(!loaded.hasValue()) return false;
    const auto& state=loaded.value().presets;
    const auto& stage=state.activePipeline.stages[6];
    const auto& parameters=std::get<processing::SharpenParameters>(stage.parameters);
    return state.selectedId.value=="custom" && stage.enabled==enabled
        && parameters.amount==amount && parameters.radius==radius && parameters.threshold==threshold;
}
void QmlWorkstationTests::sharpenEditingPreservesAdvancedValuesAndPausedFrame() {
    // Missing controls, integer-only threshold, UI disclosure submitting edits,
    // stale text/gestures after replacement, or changed frozen pixels must fail.
    for(const auto* name:{"sharpenEnabled","sharpenAmountField","sharpenAmountSlider",
            "sharpenAdvanced","sharpenRadiusField","sharpenThresholdField"})
        QVERIFY2(item(name),name);
    choosePreset(QStringLiteral("saved-sharpen"));
    QTRY_VERIFY_WITH_TIMEOUT(persistedPresetIs("saved-sharpen"),5000);
    QTRY_COMPARE(preferences_->latestStatus()->latestSavedPresetRevision,
        preferences_->latestStatus()->latestAttemptedPresetSaveRevision);
    const auto activeRevision=runtime_->processing()->activeRevision();
    const auto saveRevision=preferences_->latestStatus()->latestSavedPresetRevision;
    QVERIFY(!item("sharpenAdvanced")->property("checked").toBool());
    QVERIFY(!item("sharpenRadiusField")->isVisible());
    QVERIFY(!item("sharpenThresholdField")->isVisible());
    auto expected=runtime_->coordinator().processingControls()->draft().activePipeline;
    revealProcessing("sharpenAmountField");
    QCOMPARE(item("sharpenAmountField")->property("text").toString(),QStringLiteral("1.234567891234567"));
    revealProcessing("sharpenAdvanced"); click("sharpenAdvanced");
    revealProcessing("sharpenRadiusField");
    QCOMPARE(item("sharpenRadiusField")->property("text").toString(),QStringLiteral("2.345678912345678"));
    revealProcessing("sharpenThresholdField");
    QCOMPARE(item("sharpenThresholdField")->property("text").toString(),QStringLiteral("13.45678912345678"));
    for(const auto* name:{"sharpenRadiusSlider","sharpenThresholdSlider"}) {
        QVERIFY(item(name));
        QVERIFY(!item(name)->isVisible());
    }
    revealProcessing("sharpenAdvanced"); click("sharpenAdvanced");
    QVERIFY(!item("sharpenRadiusField")->isVisible());
    click("sharpenAdvanced");
    item("presetSelector")->forceActiveFocus();
    QTest::qWait(100);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("saved-sharpen"));
    QVERIFY(!runtime_->processing()->pending());
    QCOMPARE(runtime_->processing()->activeRevision(),activeRevision);
    QCOMPARE(preferences_->latestStatus()->latestAttemptedPresetSaveRevision,saveRevision);
    QCOMPARE(preferences_->latestStatus()->latestSavedPresetRevision,saveRevision);
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expected));

    constexpr double liveAmount=2.123456789012345;
    constexpr double liveRadius=3.234567891234567;
    constexpr double liveThreshold=23.45678912345678;
    enterText("sharpenAmountField",QStringLiteral("2.123456789012345"));
    enterText("sharpenRadiusField",QStringLiteral("3.234567891234567"));
    enterText("sharpenThresholdField",QStringLiteral("23.45678912345678"),false);
    QTest::keyClick(window_,Qt::Key_Tab);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && sharpenSaved(liveAmount,liveRadius,liveThreshold),10000);
    expected.stages[6].parameters=processing::SharpenParameters{liveAmount,liveRadius,liveThreshold};
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expected));
    capture("sharpen-live");
    item("presetSelector")->forceActiveFocus();
    for(int steps=0;steps<60 && window_->activeFocusItem()!=item("sharpenAmountSlider");++steps) {
        QTest::keyClick(window_,Qt::Key_Tab,Qt::NoModifier,30);
        QCoreApplication::processEvents();
        QVERIFY(QQuickTest::qWaitForPolish(window_));
    }
    QCOMPARE(window_->activeFocusItem(),item("sharpenAmountSlider"));
    revealProcessing("sharpenAmountSlider");
    capture("sharpen-amount-focused");

    click("compareButton");
    QTRY_COMPARE(runtime_->viewer()->displayMode(),QStringLiteral("compare"));
    click("pauseButton");
    QTRY_COMPARE(runtime_->viewer()->playbackState(),QStringLiteral("Paused"));
    auto* viewer=runtime_->viewer();
    QVERIFY(viewer->fit());
    QVERIFY(viewer->zoomAt(viewer->imageItem().width()/4,viewer->imageItem().height()/2,1.1));
    QVERIFY(viewer->panBy(13,17));
    const auto frozenId=viewer->sourceFrameId();
    const auto rectangles=viewer->imageItem().imageRects();
    const auto camera=*pipeline_->snapshot().camera;
    const auto readback=runtime_->camera()->currentSummary();
    const auto frozen=viewportPixels();
    QVERIFY(!frozen.isNull());
    for(const auto* invalid:{"-1","5.1","."}) {
        enterText("sharpenAmountField",QString::fromLatin1(invalid));
        QVERIFY(!runtime_->processing()->validationError().isEmpty());
        QVERIFY(sharpenSaved(liveAmount,liveRadius,liveThreshold));
    }
    enterText("sharpenAmountField",QStringLiteral("2.123456789012345"));
    for(const auto* invalid:{"0.4","5.1","."}) {
        enterText("sharpenRadiusField",QString::fromLatin1(invalid));
        QVERIFY(!runtime_->processing()->validationError().isEmpty());
        QVERIFY(sharpenSaved(liveAmount,liveRadius,liveThreshold));
    }
    enterText("sharpenRadiusField",QStringLiteral("3.234567891234567"));
    for(const auto* invalid:{"-1","65535.1","."}) {
        enterText("sharpenThresholdField",QString::fromLatin1(invalid));
        QVERIFY(!runtime_->processing()->validationError().isEmpty());
        QVERIFY(sharpenSaved(liveAmount,liveRadius,liveThreshold));
    }
    capture("sharpen-invalid-threshold");
    enterText("sharpenThresholdField",QStringLiteral("23.45678912345678"));
    QCOMPARE(viewportPixels(),frozen);
    revealProcessing("sharpenEnabled"); click("sharpenEnabled");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && sharpenSaved(liveAmount,liveRadius,liveThreshold,false),10000);
    for(const auto* name:{"sharpenAmountField","sharpenAmountSlider","sharpenAdvanced",
            "sharpenRadiusField","sharpenThresholdField"}) QVERIFY(!item(name)->isEnabled());
    QVERIFY(item("sharpenAdvanced")->property("checked").toBool());
    click("sharpenEnabled");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && sharpenSaved(liveAmount,liveRadius,liveThreshold),10000);

    revealProcessing("sharpenAmountSlider");
    QTest::keyClick(window_,Qt::Key_Right);
    QTRY_VERIFY(runtime_->processing()->property("sharpenAmount").toDouble()>liveAmount);
    auto* slider=item("sharpenAmountSlider");
    const auto start=slider->mapToScene(QPointF(slider->width()*0.4,slider->height()/2)).toPoint();
    const auto finish=slider->mapToScene(QPointF(slider->width()*0.65,slider->height()/2)).toPoint();
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,start);
    QTest::mouseMove(window_,finish);
    QVERIFY(slider->property("pressed").toBool());
    QVERIFY(runtime_->processing()->selectPreset(QStringLiteral("soft-detail")));
    QTest::mouseMove(window_,start);
    QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,start);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),10000);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("soft-detail"));
    QCOMPARE(slider->property("value").toDouble(),0.5);
    QTest::keyClick(window_,Qt::Key_Right);
    QTRY_VERIFY(runtime_->processing()->property("sharpenAmount").toDouble()>0.5);
    enterText("sharpenRadiusField",QStringLiteral("4.5"),false);
    QVERIFY(runtime_->processing()->selectPreset(QStringLiteral("standard")));
    QTest::keyClick(window_,Qt::Key_Return);
    item("presetSelector")->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),10000);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("standard"));
    QCOMPARE(runtime_->processing()->property("sharpenRadius").toDouble(),1.0);
    enterText("sharpenThresholdField",QStringLiteral("71.5"),false);
    QVERIFY(runtime_->processing()->resetProcessing());
    item("presetSelector")->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persistedPresetIs("original"),10000);
    QCOMPARE(runtime_->processing()->property("sharpenAmount").toDouble(),1.0);
    QCOMPARE(runtime_->processing()->property("sharpenRadius").toDouble(),1.0);
    QCOMPARE(runtime_->processing()->property("sharpenThreshold").toDouble(),0.0);
    QVERIFY(!item("sharpenRadiusField")->isEnabled());
    QVERIFY(!item("sharpenThresholdField")->isEnabled());
    QCOMPARE(viewportPixels(),frozen);
    capture("sharpen-reset-paused");

    choosePreset(QStringLiteral("saved-sharpen"));
    constexpr double finalAmount=1.876543210987654;
    constexpr double finalRadius=2.876543210987654;
    constexpr double finalThreshold=43.87654321098765;
    enterText("sharpenAmountField",QStringLiteral("1.876543210987654"));
    enterText("sharpenRadiusField",QStringLiteral("2.876543210987654"));
    enterText("sharpenThresholdField",QStringLiteral("43.87654321098765"));
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && sharpenSaved(finalAmount,finalRadius,finalThreshold),10000);
    expected.stages[6].parameters=processing::SharpenParameters{finalAmount,finalRadius,finalThreshold};
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expected));
    QCOMPARE(viewer->sourceFrameId(),frozenId);
    QCOMPARE(viewer->displayMode(),QStringLiteral("compare"));
    QCOMPARE(viewer->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(viewer->imageItem().imageRects(),rectangles);
    QCOMPARE(viewportPixels(),frozen);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    QCOMPARE(pipeline_->snapshot().camera->sessionGeneration,camera.sessionGeneration);
    QCOMPARE(pipeline_->snapshot().camera->confirmedRevision,camera.confirmedRevision);
    QCOMPARE(runtime_->camera()->currentSummary(),readback);
    capture("sharpen-paused");
    click("resumeButton");
    QTRY_VERIFY_WITH_TIMEOUT(viewer->sourceFrameId()!=frozenId,5000);
    click("originalButton");
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::invertEditingPreservesRecipeAndPausedFrame() {
    // Missing wiring, editing a stale/partial recipe, or applying processing
    // changes to an already paused frame must fail this real-control route.
    QVERIFY(item("invertEnabled"));
    choosePreset(QStringLiteral("saved-invert"));
    QTRY_VERIFY_WITH_TIMEOUT(persistedPresetIs("saved-invert"),5000);
    const auto loaded=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
    QVERIFY(loaded.hasValue());
    const auto& recipe=loaded.value().presets.customPresets.back();
    QCOMPARE(recipe.id.value,std::string("saved-invert"));
    const auto savedPipeline=recipe.pipeline;
    auto expected=savedPipeline;
    const auto persisted=[&](const std::string& id,const processing::PipelineDefinition& pipeline) {
        const auto current=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
        return current.hasValue() && current.value().presets.selectedId.value==id
            && processing::semanticallyEqualPipelineDefinitions(current.value().presets.activePipeline,pipeline);
    };
    QVERIFY(persisted("saved-invert",expected));
    QTRY_COMPARE(item("invertEnabled")->property("checked").toBool(),true);
    QCOMPARE(runtime_->processing()->property("invertEnabled").toBool(),true);
    QTRY_COMPARE(preferences_->latestStatus()->latestSavedPresetRevision,
        preferences_->latestStatus()->latestAttemptedPresetSaveRevision);
    const auto saveRevision=preferences_->latestStatus()->latestSavedPresetRevision;
    const auto activeRevision=runtime_->processing()->activeRevision();
    if(item("sharpenAdvanced")->property("checked").toBool()) {
        revealProcessing("sharpenAdvanced"); click("sharpenAdvanced");
    }
    item("presetSelector")->forceActiveFocus();
    for(int steps=0;steps<60 && window_->activeFocusItem()!=item("invertEnabled");++steps) {
        QTest::keyClick(window_,Qt::Key_Tab,Qt::NoModifier,30);
        QCoreApplication::processEvents();
        QVERIFY(QQuickTest::qWaitForPolish(window_));
    }
    QCOMPARE(window_->activeFocusItem(),item("invertEnabled"));
    revealProcessing("invertEnabled");
    QTest::qWait(100);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("saved-invert"));
    QCOMPARE(runtime_->processing()->activeRevision(),activeRevision);
    QCOMPARE(preferences_->latestStatus()->latestAttemptedPresetSaveRevision,saveRevision);
    capture("invert-focused");

    click("invertEnabled");
    expected.stages[7].enabled=false;
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persisted("custom",expected),10000);
    QVERIFY(!item("invertEnabled")->property("checked").toBool());
    QTest::keyClick(window_,Qt::Key_Space);
    expected.stages[7].enabled=true;
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persisted("custom",expected),10000);
    QVERIFY(item("invertEnabled")->property("checked").toBool());
    capture("invert-live");

    click("compareButton");
    QTRY_COMPARE(runtime_->viewer()->displayMode(),QStringLiteral("compare"));
    click("pauseButton");
    auto* viewer=runtime_->viewer();
    QTRY_COMPARE(viewer->playbackState(),QStringLiteral("Paused"));
    QVERIFY(viewer->fit());
    QVERIFY(viewer->zoomAt(viewer->imageItem().width()/4,viewer->imageItem().height()/2,1.1));
    QVERIFY(viewer->panBy(13,17));
    const auto frozenId=viewer->sourceFrameId();
    const auto rectangles=viewer->imageItem().imageRects();
    const auto camera=*pipeline_->snapshot().camera;
    const auto readback=runtime_->camera()->currentSummary();
    const auto frozen=viewportPixels();
    QVERIFY(!frozen.isNull());
    for(bool enabled:{false,true}) {
        revealProcessing("invertEnabled"); click("invertEnabled");
        expected.stages[7].enabled=enabled;
        QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persisted("custom",expected),10000);
        QCOMPARE(item("invertEnabled")->property("checked").toBool(),enabled);
        QCOMPARE(viewportPixels(),frozen);
    }
    choosePreset(QStringLiteral("standard"));
    QTRY_VERIFY_WITH_TIMEOUT(persisted("standard",processing::standardPipeline()),5000);
    QVERIFY(!item("invertEnabled")->property("checked").toBool());
    QCOMPARE(viewportPixels(),frozen);
    choosePreset(QStringLiteral("saved-invert"));
    QVERIFY(item("invertEnabled")->property("checked").toBool());
    QCOMPARE(viewportPixels(),frozen);
    click("resetProcessingButton");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && persisted("original",processing::defaultPipeline()),10000);
    QVERIFY(!item("invertEnabled")->property("checked").toBool());
    QCOMPARE(viewportPixels(),frozen);
    capture("invert-reset-paused");

    choosePreset(QStringLiteral("saved-invert"));
    QTRY_VERIFY_WITH_TIMEOUT(persisted("saved-invert",savedPipeline),5000);
    QCOMPARE(viewer->sourceFrameId(),frozenId);
    QCOMPARE(viewer->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(viewer->displayMode(),QStringLiteral("compare"));
    QCOMPARE(viewer->imageItem().imageRects(),rectangles);
    QCOMPARE(viewportPixels(),frozen);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    QCOMPARE(pipeline_->snapshot().camera->sessionGeneration,camera.sessionGeneration);
    QCOMPARE(pipeline_->snapshot().camera->confirmedRevision,camera.confirmedRevision);
    QCOMPARE(runtime_->camera()->currentSummary(),readback);
    capture("invert-paused");
    click("resumeButton");
    QTRY_VERIFY_WITH_TIMEOUT(viewer->sourceFrameId()!=frozenId,5000);
    click("originalButton");
    // Leave the existing advanced-field layout checks able to reach their fields.
    revealProcessing("sharpenAdvanced"); click("sharpenAdvanced");
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::chooseDenoiseOption(const char* name,int index) {
    revealProcessing(name);
    auto* control=item(name);
    QVERIFY(control->isEnabled());
    QTest::keyClick(window_,Qt::Key_Space);
    QTest::keyClick(window_,Qt::Key_Home);
    for(int row=0;row<index;++row) QTest::keyClick(window_,Qt::Key_Down);
    QTest::keyClick(window_,Qt::Key_Return);
    QTRY_COMPARE(control->property("currentIndex").toInt(),index);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),10000);
}
bool QmlWorkstationTests::denoiseSaved(processing::DenoiseMode mode,int kernel,double sigma,bool enabled) const {
    const auto loaded=configuration::ConfigurationStore{directory_.filePath("pilot.json").toStdString()}.load();
    if(!loaded.hasValue()) return false;
    const auto& state=loaded.value().presets;
    const auto& stage=state.activePipeline.stages[5];
    const auto& parameters=std::get<processing::DenoiseParameters>(stage.parameters);
    return state.selectedId.value=="custom" && stage.enabled==enabled && parameters.mode==mode
        && parameters.kernelSize==static_cast<std::uint32_t>(kernel) && parameters.sigma==sigma;
}
void QmlWorkstationTests::denoiseEditingNormalizesModesAndPreservesPausedFrame() {
    // Missing controls, non-atomic mode normalization, stale edited sigma, or
    // changed paused pixels must fail this real scene and persistence route.
    for(const auto* name:{"denoiseEnabled","denoiseMode","denoiseKernel","denoiseSigmaField"})
        QVERIFY2(item(name),name);
    choosePreset(QStringLiteral("saved-denoise"));
    revealProcessing("denoiseSigmaField");
    QCOMPARE(item("denoiseSigmaField")->property("text").toString(),QStringLiteral("1.234567891234567"));
    QCOMPARE(item("denoiseKernel")->property("count").toInt(),3);
    QCOMPARE(item("denoiseMode")->property("currentText").toString(),QStringLiteral("Gaussian"));
    QCOMPARE(item("denoiseMode")->property("currentValue").toString(),QStringLiteral("gaussian"));
    QCOMPARE(item("denoiseKernel")->property("currentText").toString(),QStringLiteral("7"));
    QCOMPARE(item("denoiseKernel")->property("currentValue").toInt(),7);
    QVERIFY(item("denoiseSigmaSlider"));
    QVERIFY(!item("denoiseSigmaSlider")->isVisible());
    chooseDenoiseOption("denoiseMode",0);
    chooseDenoiseOption("denoiseKernel",2);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("saved-denoise"));
    auto expected=runtime_->coordinator().processingControls()->draft().activePipeline;
    constexpr double liveSigma=3.123456789012345;
    enterText("denoiseSigmaField",QStringLiteral("3.123456789012345"),false);
    QTest::keyClick(window_,Qt::Key_Tab);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && denoiseSaved(processing::DenoiseMode::Gaussian,7,liveSigma),10000);
    expected.stages[5].parameters=processing::DenoiseParameters{processing::DenoiseMode::Gaussian,7,liveSigma};
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expected));
    capture("denoise-live");

    click("compareButton");
    QTRY_COMPARE(runtime_->viewer()->displayMode(),QStringLiteral("compare"));
    click("pauseButton");
    QTRY_COMPARE(runtime_->viewer()->playbackState(),QStringLiteral("Paused"));
    auto* viewer=runtime_->viewer();
    QVERIFY(viewer->fit());
    QVERIFY(viewer->zoomAt(viewer->imageItem().width()/4,viewer->imageItem().height()/2,1.1));
    QVERIFY(viewer->panBy(13,17));
    const auto frozenId=viewer->sourceFrameId();
    const auto rectangles=viewer->imageItem().imageRects();
    const auto camera=*pipeline_->snapshot().camera;
    const auto readback=runtime_->camera()->currentSummary();
    const auto frozen=viewportPixels();
    QVERIFY(!frozen.isNull());
    for(const auto* invalid:{"-1","5.1","."}) {
        enterText("denoiseSigmaField",QString::fromLatin1(invalid));
        QVERIFY(!runtime_->processing()->validationError().isEmpty());
        QCOMPARE(runtime_->processing()->property("denoiseSigma").toDouble(),liveSigma);
        QVERIFY(denoiseSaved(processing::DenoiseMode::Gaussian,7,liveSigma));
    }
    capture("denoise-invalid-sigma");
    enterText("denoiseSigmaField",QStringLiteral("3.123456789012345"));
    chooseDenoiseOption("denoiseMode",1);
    QTRY_VERIFY_WITH_TIMEOUT(denoiseSaved(processing::DenoiseMode::Median,5,0),10000);
    QCOMPARE(item("denoiseKernel")->property("count").toInt(),2);
    QCOMPARE(item("denoiseMode")->property("currentText").toString(),QStringLiteral("Median"));
    QCOMPARE(item("denoiseMode")->property("currentValue").toString(),QStringLiteral("median"));
    QCOMPARE(item("denoiseKernel")->property("currentText").toString(),QStringLiteral("5"));
    QCOMPARE(item("denoiseKernel")->property("currentValue").toInt(),5);
    QVERIFY(!item("denoiseSigmaField")->isEnabled());
    QCOMPARE(item("denoiseSigmaField")->property("text").toString(),QStringLiteral("0"));
    chooseDenoiseOption("denoiseKernel",0);
    QTRY_VERIFY_WITH_TIMEOUT(denoiseSaved(processing::DenoiseMode::Median,3,0),10000);
    capture("denoise-median-paused");
    chooseDenoiseOption("denoiseMode",0);
    QTRY_VERIFY_WITH_TIMEOUT(denoiseSaved(processing::DenoiseMode::Gaussian,3,0),10000);
    QVERIFY(item("denoiseSigmaField")->isEnabled());

    // An external mode replacement arrives before focus loss, just like a
    // preset/reset replacement. It must cancel the old Gaussian field buffer.
    enterText("denoiseSigmaField",QStringLiteral("4.5"),false);
    bool changed=false;
    QVERIFY(QMetaObject::invokeMethod(runtime_->processing(),"setDenoiseMode",
        Q_RETURN_ARG(bool,changed),Q_ARG(QString,QStringLiteral("median"))));
    QVERIFY(changed);
    item("presetSelector")->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && denoiseSaved(processing::DenoiseMode::Median,3,0),10000);
    QVERIFY(runtime_->processing()->validationError().isEmpty());
    chooseDenoiseOption("denoiseMode",0);
    enterText("denoiseSigmaField",QStringLiteral("4.5"),false);
    QVERIFY(runtime_->processing()->selectPreset(QStringLiteral("standard")));
    QTest::keyClick(window_,Qt::Key_Return);
    item("presetSelector")->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),10000);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("standard"));
    QCOMPARE(runtime_->processing()->property("denoiseSigma").toDouble(),0.0);

    // A replaced draft also dismisses a dropdown so Enter cannot choose a
    // highlighted option from the previous draft after Reset.
    revealProcessing("denoiseKernel");
    QTest::keyClick(window_,Qt::Key_Space);
    QTest::keyClick(window_,Qt::Key_End);
    auto* popup=item("denoiseKernel")->property("popup").value<QObject*>();
    QVERIFY(popup);
    QVERIFY(popup->property("visible").toBool());
    QVERIFY(runtime_->processing()->resetProcessing());
    QTRY_VERIFY(!popup->property("visible").toBool());
    QTest::keyClick(window_,Qt::Key_Return);
    item("presetSelector")->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persistedPresetIs("original"),10000);
    QVERIFY(!item("denoiseMode")->isEnabled());
    QVERIFY(!item("denoiseKernel")->isEnabled());
    QVERIFY(!item("denoiseSigmaField")->isEnabled());
    QCOMPARE(viewportPixels(),frozen);
    capture("denoise-reset-paused");

    choosePreset(QStringLiteral("saved-denoise"));
    constexpr double finalSigma=4.23456789123456;
    enterText("denoiseSigmaField",QStringLiteral("4.23456789123456"));
    revealProcessing("denoiseEnabled"); click("denoiseEnabled");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && denoiseSaved(processing::DenoiseMode::Gaussian,7,finalSigma,false),10000);
    QVERIFY(!item("denoiseMode")->isEnabled());
    QVERIFY(!item("denoiseKernel")->isEnabled());
    QVERIFY(!item("denoiseSigmaField")->isEnabled());
    click("denoiseEnabled");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending()
        && denoiseSaved(processing::DenoiseMode::Gaussian,7,finalSigma),10000);
    expected.stages[5].parameters=processing::DenoiseParameters{processing::DenoiseMode::Gaussian,7,finalSigma};
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expected));
    QCOMPARE(viewer->sourceFrameId(),frozenId);
    QCOMPARE(viewer->displayMode(),QStringLiteral("compare"));
    QCOMPARE(viewer->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(viewer->imageItem().imageRects(),rectangles);
    QCOMPARE(viewportPixels(),frozen);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    QCOMPARE(pipeline_->snapshot().camera->sessionGeneration,camera.sessionGeneration);
    QCOMPARE(pipeline_->snapshot().camera->confirmedRevision,camera.confirmedRevision);
    QCOMPARE(runtime_->camera()->currentSummary(),readback);
    revealProcessing("denoiseSigmaField");
    capture("denoise-paused");
    click("resumeButton");
    QTRY_VERIFY_WITH_TIMEOUT(viewer->sourceFrameId()!=frozenId,5000);
    click("originalButton");
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::localContrastEditingPreservesPausedFrameAndExactSettings() {
    // Missing bindings, fractional grid truncation, or replaced input resubmission
    // must fail this route through the actual scene and production persistence.
    QVERIFY(item("localContrastEnabled"));
    QVERIFY(item("clipLimitField"));
    QVERIFY(item("tileGridField"));
    choosePreset(QStringLiteral("saved-local-contrast"));
    revealProcessing("clipLimitField");
    QCOMPARE(item("clipLimitField")->property("text").toString(),QStringLiteral("2.3456789123456"));
    revealProcessing("tileGridField");
    QCOMPARE(item("tileGridField")->property("text").toString(),QStringLiteral("4"));
    item("presetSelector")->forceActiveFocus();
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("saved-local-contrast"));
    auto expected=runtime_->coordinator().processingControls()->draft().activePipeline;
    constexpr double liveClip=3.123456789012345;
    enterText("clipLimitField",QStringLiteral("3.123456789012345"));
    enterText("tileGridField",QStringLiteral("12"),false);
    QTest::keyClick(window_,Qt::Key_Tab);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && localContrastSaved(liveClip,12),10000);
    expected.stages[4].parameters=processing::ClaheParameters{liveClip,12};
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expected));
    capture("local-contrast-live");

    click("compareButton");
    QTRY_COMPARE(runtime_->viewer()->displayMode(),QStringLiteral("compare"));
    click("pauseButton");
    QTRY_COMPARE(runtime_->viewer()->playbackState(),QStringLiteral("Paused"));
    auto* viewer=runtime_->viewer();
    QVERIFY(viewer->fit());
    QVERIFY(viewer->zoomAt(viewer->imageItem().width()/4,viewer->imageItem().height()/2,1.1));
    QVERIFY(viewer->panBy(13,17));
    const auto frozenId=viewer->sourceFrameId();
    const auto rectangles=viewer->imageItem().imageRects();
    const auto camera=*pipeline_->snapshot().camera;
    const auto readback=runtime_->camera()->currentSummary();
    const auto frozen=viewportPixels();
    QVERIFY(!frozen.isNull());
    for(int pane=0;pane<2;++pane) {
        bool bright=false,dark=false;
        for(int x=pane*frozen.width()/2;x<(pane+1)*frozen.width()/2;++x) {
            const auto gray=qGray(frozen.pixel(x,frozen.height()/2));
            bright=bright || gray>192; dark=dark || gray<64;
        }
        QVERIFY2(bright && dark,"Local contrast Pause baseline must contain visible detail in each pane");
    }

    for(const auto* invalid:{"8.5","33"}) {
        enterText("tileGridField",QString::fromLatin1(invalid));
        QVERIFY(!runtime_->processing()->validationError().isEmpty());
        QCOMPARE(runtime_->processing()->property("tileGridSize").toInt(),12);
        QVERIFY(localContrastSaved(liveClip,12));
    }
    QVERIFY2(viewportPixels()==frozen,"After grid rejection");
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    QVERIFY(item("processingMessages")->isVisible());
    QVERIFY(window_->contentItem()->boundingRect().contains(
        item("processingMessages")->mapRectToScene(item("processingMessages")->boundingRect())));
    capture("local-contrast-invalid-grid");
    enterText("tileGridField",QStringLiteral("12"));
    QVERIFY(runtime_->processing()->validationError().isEmpty());
    for(const auto* invalid:{"0","41","."}) {
        enterText("clipLimitField",QString::fromLatin1(invalid));
        QVERIFY(!runtime_->processing()->validationError().isEmpty());
        QCOMPARE(runtime_->processing()->property("clipLimit").toDouble(),liveClip);
        QVERIFY(localContrastSaved(liveClip,12));
    }
    enterText("clipLimitField",QStringLiteral("3.123456789012345"));
    QVERIFY2(viewportPixels()==frozen,"After clip rejection");
    revealProcessing("localContrastEnabled"); click("localContrastEnabled");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && localContrastSaved(liveClip,12,false),5000);
    QVERIFY(!item("clipLimitField")->isEnabled());
    QVERIFY(!item("tileGridField")->isEnabled());
    click("localContrastEnabled");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && localContrastSaved(liveClip,12),5000);
    QVERIFY2(viewportPixels()==frozen,"After local contrast toggles");

    revealProcessing("clipLimitSlider");
    QTest::keyClick(window_,Qt::Key_Right);
    QTRY_VERIFY(runtime_->processing()->property("clipLimit").toDouble()>liveClip);
    auto* slider=item("clipLimitSlider");
    const auto start=slider->mapToScene(QPointF(slider->width()*0.4,slider->height()/2)).toPoint();
    const auto finish=slider->mapToScene(QPointF(slider->width()*0.65,slider->height()/2)).toPoint();
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,start);
    QTest::mouseMove(window_,finish);
    QVERIFY(slider->property("pressed").toBool());
    QVERIFY(runtime_->processing()->selectPreset(QStringLiteral("high-contrast")));
    QTest::mouseMove(window_,start);
    QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,start);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),5000);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("high-contrast"));
    QCOMPARE(slider->property("value").toDouble(),3.0);
    QTest::keyClick(window_,Qt::Key_Right);
    QTRY_VERIFY(runtime_->processing()->property("clipLimit").toDouble()>3.0);
    QVERIFY2(viewportPixels()==frozen,"After replaced clip gesture");
    enterText("tileGridField",QStringLiteral("21"),false);
    QVERIFY(runtime_->processing()->selectPreset(QStringLiteral("standard")));
    QTest::keyClick(window_,Qt::Key_Return);
    item("presetSelector")->forceActiveFocus();
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("standard"));
    QCOMPARE(runtime_->processing()->property("tileGridSize").toInt(),8);
    enterText("tileGridField",QStringLiteral("18"),false);
    QVERIFY(runtime_->processing()->resetProcessing());
    item("presetSelector")->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persistedPresetIs("original"),5000);
    QCOMPARE(runtime_->processing()->property("clipLimit").toDouble(),2.0);
    QCOMPARE(runtime_->processing()->property("tileGridSize").toInt(),8);
    QVERIFY(!item("clipLimitField")->isEnabled());
    QVERIFY(!item("tileGridField")->isEnabled());
    QCOMPARE(viewportPixels(),frozen);
    capture("local-contrast-reset-paused");

    choosePreset(QStringLiteral("saved-local-contrast"));
    constexpr double finalClip=4.23456789123456;
    enterText("clipLimitField",QStringLiteral("4.23456789123456"));
    enterText("tileGridField",QStringLiteral("16"));
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && localContrastSaved(finalClip,16),10000);
    expected.stages[4].parameters=processing::ClaheParameters{finalClip,16};
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expected));
    QCOMPARE(viewer->sourceFrameId(),frozenId);
    QCOMPARE(viewer->displayMode(),QStringLiteral("compare"));
    QCOMPARE(viewer->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(viewer->imageItem().imageRects(),rectangles);
    QCOMPARE(viewportPixels(),frozen);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    QCOMPARE(pipeline_->snapshot().camera->sessionGeneration,camera.sessionGeneration);
    QCOMPARE(pipeline_->snapshot().camera->confirmedRevision,camera.confirmedRevision);
    QCOMPARE(runtime_->camera()->currentSummary(),readback);
    capture("local-contrast-paused");
    click("resumeButton");
    QTRY_VERIFY_WITH_TIMEOUT(viewer->sourceFrameId()!=frozenId,5000);
    click("originalButton");
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::toneEditingPreservesExactValuesPausedPixelsAndResetAuthority() {
    // Missing controls, focus-driven commits, stale text after Reset or edits
    // touching presentation/camera state break this actual operator route.
    QVERIFY(item("brightnessField"));
    QVERIFY(item("contrastField"));
    QVERIFY(item("gammaField"));
    choosePreset(QStringLiteral("saved-fractional"));
    revealProcessing("gammaField");
    QCOMPARE(item("gammaField")->property("text").toString(),QStringLiteral("1.234567891234567"));
    revealProcessing("brightnessField");
    QCOMPARE(item("brightnessField")->property("text").toString(),QStringLiteral("0.0123456789123456"));
    revealProcessing("contrastField");
    QCOMPARE(item("contrastField")->property("text").toString(),QStringLiteral("1.012345678912345"));
    item("presetSelector")->forceActiveFocus();
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("saved-fractional"));
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),5000);

    click("compareButton");
    QTRY_COMPARE(runtime_->viewer()->displayMode(),QStringLiteral("compare"));
    click("pauseButton");
    QTRY_COMPARE(runtime_->viewer()->playbackState(),QStringLiteral("Paused"));
    auto* viewer=runtime_->viewer();
    QVERIFY(viewer->fit());
    QVERIFY(viewer->zoomAt(viewer->imageItem().width()/4,viewer->imageItem().height()/2,1.1));
    QVERIFY(viewer->panBy(13,17));
    const auto frozenId=viewer->sourceFrameId();
    const auto rectangles=viewer->imageItem().imageRects();
    const auto camera=*pipeline_->snapshot().camera;
    const auto readback=runtime_->camera()->currentSummary();
    const auto frozen=viewportPixels();
    QVERIFY(!frozen.isNull());
    for(int pane=0;pane<2;++pane) {
        bool bright=false,dark=false;
        for(int x=pane*frozen.width()/2;x<(pane+1)*frozen.width()/2;++x) {
            const auto gray=qGray(frozen.pixel(x,frozen.height()/2));
            bright=bright || gray>192; dark=dark || gray<64;
        }
        QVERIFY2(bright && dark,"Frozen tone-edit baseline must contain visible image detail");
    }

    constexpr double brightness=-0.125123456789012;
    constexpr double contrast=0.8751234567890123;
    constexpr double gamma=1.34567891234567;
    enterText("brightnessField",QStringLiteral("-0.125123456789012"),false);
    QTest::keyClick(window_,Qt::Key_Tab);
    QTRY_COMPARE(runtime_->processing()->property("brightness").toDouble(),brightness);
    enterText("contrastField",QStringLiteral("0.8751234567890123"));
    enterText("gammaField",QStringLiteral("1.34567891234567"));
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && toneSaved(brightness,contrast,gamma),10000);
    QCOMPARE(runtime_->processing()->property("brightness").toDouble(),brightness);
    QCOMPARE(runtime_->processing()->property("contrast").toDouble(),contrast);
    QCOMPARE(runtime_->processing()->property("gamma").toDouble(),gamma);
    QCOMPARE(item("presetSelector")->property("currentValue").toString(),QStringLiteral("custom"));

    for(const auto* invalid:{".","0"}) {
        enterText("gammaField",QString::fromLatin1(invalid));
        QVERIFY(!runtime_->processing()->validationError().isEmpty());
        QCOMPARE(runtime_->processing()->property("gamma").toDouble(),gamma);
        QVERIFY(toneSaved(brightness,contrast,gamma));
        QVERIFY(item("processingMessages")->isVisible());
        QVERIFY(QQuickTest::qWaitForPolish(window_));
        QVERIFY(window_->contentItem()->boundingRect().contains(
            item("processingMessages")->mapRectToScene(item("processingMessages")->boundingRect())));
    }
    enterText("gammaField",QStringLiteral("1.34567891234567"));
    QVERIFY(runtime_->processing()->validationError().isEmpty());
    for(const auto* toggle:{"brightnessContrastEnabled","gammaEnabled"}) {
        revealProcessing(toggle); click(toggle);
        QCOMPARE(runtime_->processing()->property("brightness").toDouble(),brightness);
        QCOMPARE(runtime_->processing()->property("contrast").toDouble(),contrast);
        QCOMPARE(runtime_->processing()->property("gamma").toDouble(),gamma);
        QVERIFY(!item(toggle==QStringLiteral("gammaEnabled") ? "gammaField" : "brightnessField")->isEnabled());
        click(toggle);
    }
    revealProcessing("gammaSlider");
    QTest::keyClick(window_,Qt::Key_Right);
    QTRY_VERIFY(runtime_->processing()->property("gamma").toDouble()>gamma);
    revealProcessing("brightnessSlider");
    auto* slider=item("brightnessSlider");
    const auto start=slider->mapToScene(QPointF(slider->width()*0.4,slider->height()/2)).toPoint();
    const auto finish=slider->mapToScene(QPointF(slider->width()*0.65,slider->height()/2)).toPoint();
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,start);
    QTest::mouseMove(window_,finish);
    QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,finish);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),5000);
    QVERIFY(runtime_->processing()->property("brightness").toDouble()!=brightness);
    QCOMPARE(viewportPixels(),frozen);
    capture("tone-paused");

    // Replacing a draft while a slider is held must survive its later release.
    revealProcessing("brightnessSlider");
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,start);
    QTest::mouseMove(window_,finish);
    QVERIFY(slider->property("pressed").toBool());
    QVERIFY(runtime_->processing()->selectPreset(QStringLiteral("high-contrast")));
    QTest::mouseMove(window_,start);
    QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,finish);
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),5000);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("high-contrast"));
    QCOMPARE(runtime_->processing()->property("brightness").toDouble(),0.0);

    QCOMPARE(slider->property("value").toDouble(),0.0);
    QTest::keyClick(window_,Qt::Key_Right);
    QTRY_VERIFY(runtime_->processing()->property("brightness").toDouble()>0.0);

    // A preset can replace dirty input while the same editor remains enabled.
    enterText("gammaField",QStringLiteral("4.567"),false);
    QVERIFY(runtime_->processing()->selectPreset(QStringLiteral("standard")));
    QTest::keyClick(window_,Qt::Key_Return);
    item("presetSelector")->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending(),5000);
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("standard"));
    QCOMPARE(runtime_->processing()->property("gamma").toDouble(),1.0);

    // An external preset/reset can replace a focused draft before focus loss.
    // Reset also disables tone fields; that transition must not submit old text.
    enterText("gammaField",QStringLiteral("4.567"),false);
    QVERIFY(runtime_->processing()->resetProcessing());
    item("presetSelector")->forceActiveFocus();
    QTRY_COMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("original"));
    QCOMPARE(runtime_->processing()->property("gamma").toDouble(),1.0);
    QVERIFY(!item("gammaField")->isEnabled());
    QVERIFY(!item("brightnessField")->isEnabled());
    enterText("windowField",QStringLiteral("12000"),false);
    QVERIFY(runtime_->processing()->resetProcessing()); // Same Original ID.
    QTRY_COMPARE(item("windowField")->property("text").toString(),QStringLiteral("65535"));
    QTest::keyClick(window_,Qt::Key_Return);
    item("presetSelector")->forceActiveFocus();
    QCOMPARE(runtime_->processing()->selectedPresetId(),QStringLiteral("original"));
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persistedPresetIs("original"),5000);
    QCOMPARE(viewportPixels(),frozen);
    capture("tone-reset-paused");

    revealProcessing("brightnessContrastEnabled"); click("brightnessContrastEnabled");
    revealProcessing("gammaEnabled"); click("gammaEnabled");
    enterText("brightnessField",QStringLiteral("-0.125123456789012"));
    enterText("contrastField",QStringLiteral("0.8751234567890123"));
    enterText("gammaField",QStringLiteral("1.34567891234567"));
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && toneSaved(brightness,contrast,gamma),10000);
    QCOMPARE(viewer->sourceFrameId(),frozenId);
    QCOMPARE(viewer->displayMode(),QStringLiteral("compare"));
    QCOMPARE(viewer->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(viewer->imageItem().imageRects(),rectangles);
    QCOMPARE(viewportPixels(),frozen);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    QCOMPARE(pipeline_->snapshot().camera->sessionGeneration,camera.sessionGeneration);
    QCOMPARE(pipeline_->snapshot().camera->confirmedRevision,camera.confirmedRevision);
    QCOMPARE(runtime_->camera()->currentSummary(),readback);
    click("resumeButton");
    QTRY_VERIFY_WITH_TIMEOUT(viewer->sourceFrameId()!=frozenId,5000);
    click("originalButton");
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::presetsAndResetPreserveCameraPausedFrameAndViewport() {
    // Missing bindings, accidental index-change activation, Reset touching the
    // camera/presenter, or saving a draft before acknowledgment break this route.
    QVERIFY(item("presetSelector"));
    QVERIFY(item("resetProcessingButton"));
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->compareAvailable(),5000);
    click("compareButton");
    QTRY_COMPARE(runtime_->viewer()->displayMode(),QStringLiteral("compare"));
    click("pauseButton");
    QTRY_COMPARE(runtime_->viewer()->playbackState(),QStringLiteral("Paused"));
    auto* viewer=runtime_->viewer();
    // A modest zoom crops less than the moving bar's width, so the frozen
    // image remains visibly structured at every phase of the simulator.
    QVERIFY(viewer->fit());
    QVERIFY(viewer->zoomAt(viewer->imageItem().width()/4,viewer->imageItem().height()/2,1.1));
    const auto beforePan=viewer->imageItem().imageRects();
    QVERIFY(viewer->panBy(13,17));
    const auto rectangles=viewer->imageItem().imageRects();
    QVERIFY(rectangles!=beforePan);
    const auto frozenId=viewer->sourceFrameId();
    const auto cameraBefore=*pipeline_->snapshot().camera;
    const auto readback=runtime_->camera()->currentSummary();
    const auto viewportRect=item("imageArea")->mapRectToScene(item("imageArea")->boundingRect());
    const auto capturePixels=[&] {
        const auto windowImage=window_->grabWindow();
        const auto dpr=windowImage.devicePixelRatio();
        return windowImage.copy(QRect(qRound(viewportRect.x()*dpr),qRound(viewportRect.y()*dpr),
            qRound(viewportRect.width()*dpr),qRound(viewportRect.height()*dpr)));
    };
    const auto frozenPixels=capturePixels();
    QVERIFY(!frozenPixels.isNull());
    for(int pane=0;pane<2;++pane) {
        bool bright=false;
        bool dark=false;
        for(int x=pane*frozenPixels.width()/2;x<(pane+1)*frozenPixels.width()/2;++x) {
            const int gray=qGray(frozenPixels.pixel(x,frozenPixels.height()/2));
            bright=bright || gray>192;
            dark=dark || gray<64;
        }
        QVERIFY2(bright && dark,"Each frozen pane must contain visible simulator detail");
    }
    for(const auto* id:{"original","standard","high-contrast","soft-detail","saved-fractional"}) {
        choosePreset(QString::fromLatin1(id));
        QTRY_VERIFY_WITH_TIMEOUT(persistedPresetIs(id),5000);
        QCOMPARE(viewer->sourceFrameId(),frozenId);
        QCOMPARE(viewer->playbackState(),QStringLiteral("Paused"));
        QCOMPARE(viewer->displayMode(),QStringLiteral("compare"));
        QCOMPARE(viewer->imageItem().imageRects(),rectangles);
        QCOMPARE(capturePixels(),frozenPixels);
        QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
        QCOMPARE(pipeline_->snapshot().camera->sessionGeneration,cameraBefore.sessionGeneration);
        QCOMPARE(pipeline_->snapshot().camera->confirmedRevision,cameraBefore.confirmedRevision);
        QCOMPARE(runtime_->camera()->currentSummary(),readback);
    }
    QCOMPARE(runtime_->processing()->window(),12000.125);
    QCOMPARE(runtime_->processing()->level(),23000.875);
    capture("preset-saved-paused");
    click("resetProcessingButton");
    QTRY_COMPARE(runtime_->processing()->property("selectedPresetId").toString(),QStringLiteral("original"));
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->pending() && persistedPresetIs("original"),5000);
    QCOMPARE(runtime_->processing()->window(),65535.0);
    QCOMPARE(runtime_->processing()->level(),32767.5);
    QCOMPARE(item("presetSelector")->property("currentValue").toString(),QStringLiteral("original"));
    QCOMPARE(viewer->sourceFrameId(),frozenId);
    QCOMPARE(viewer->playbackState(),QStringLiteral("Paused"));
    QCOMPARE(viewer->displayMode(),QStringLiteral("compare"));
    QCOMPARE(viewer->imageItem().imageRects(),rectangles);
    QCOMPARE(capturePixels(),frozenPixels);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
    QCOMPARE(pipeline_->snapshot().camera->sessionGeneration,cameraBefore.sessionGeneration);
    QCOMPARE(pipeline_->snapshot().camera->confirmedRevision,cameraBefore.confirmedRevision);
    QCOMPARE(runtime_->camera()->currentSummary(),readback);
    capture("preset-reset-paused");
    click("resumeButton");
    QTRY_VERIFY_WITH_TIMEOUT(viewer->sourceFrameId()!=frozenId,5000);
    choosePreset(QStringLiteral("high-contrast"));
    QTRY_VERIFY_WITH_TIMEOUT(persistedPresetIs("high-contrast"),5000);
    click("originalButton");
    QTRY_COMPARE(viewer->displayMode(),QStringLiteral("original"));
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::keyboardCanReturnToViewportWithoutStealingNumericInput() {
    auto& image=runtime_->viewer()->imageItem();
    auto* surface=image.parentItem();
    auto* field=item("levelField");
    QVERIFY(surface);
    QVERIFY(field);
    auto* windowField=item("windowField");
    QVERIFY(windowField);
    const auto windowText=windowField->property("text").toString();
    windowField->forceActiveFocus();
    QCOMPARE(windowField->property("text").toString(),windowText);
    const auto numericText=field->property("text").toString();
    field->forceActiveFocus();
    QCOMPARE(field->property("text").toString(),numericText);
    const auto before=image.imageRects();
    QTest::keyClick(window_,Qt::Key_End);
    QTest::keyClick(window_,Qt::Key_Minus);
    QCOMPARE(field->property("text").toString(),numericText+"-");
    QCOMPARE(image.imageRects(),before);
    QTest::keyClick(window_,Qt::Key_Backspace);
    QCOMPARE(field->property("text").toString(),numericText);
    for(const auto modifiers:{Qt::NoModifier,Qt::ShiftModifier}) {
        field->forceActiveFocus();
        for(int step=0;step<64 && !surface->hasActiveFocus();++step)
            QTest::keyClick(window_,Qt::Key_Tab,modifiers);
        QVERIFY2(surface->hasActiveFocus(),"Tab navigation must reach the image viewport in either direction");
        const auto width=image.imageRects()[0].width();
        QTest::keyClick(window_,Qt::Key_Equal);
        QVERIFY(image.imageRects()[0].width()>width);
        QTest::keyClick(window_,Qt::Key_Minus);
        QVERIFY(qAbs(image.imageRects()[0].width()-width)<0.001);
    }
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::renderedPixelsStayInsideViewport() {
    QVERIFY(runtime_->viewer()->hasFrame());
    auto* viewport=item("imageArea");
    QVERIFY(viewport);
    const auto rect=viewport->mapRectToScene(viewport->boundingRect());
    const auto capture=window_->grabWindow();
    QVERIFY(!capture.isNull());
    const auto dpr=capture.devicePixelRatio();
    const QColor canvas("#111412");
    // These points are in the fixed gaps around the dedicated image surface,
    // away from changing labels/buttons. Camera pixels must never reach them.
    for(const QPointF point:{QPointF(5,5),QPointF(rect.left()-5,rect.center().y()),
            QPointF(rect.right()+5,rect.center().y()),QPointF(rect.center().x(),rect.top()-5)}) {
        QCOMPARE(capture.pixelColor(qRound(point.x()*dpr),qRound(point.y()*dpr)),canvas);
    }
}
void QmlWorkstationTests::keepsLiveContentUsable_data() {
    QTest::addColumn<QSize>("size");
    QTest::addColumn<QString>("mode");
    QTest::newRow("minimum-original")<<QSize(900,600)<<QStringLiteral("original");
    QTest::newRow("default-original")<<QSize(1280,800)<<QStringLiteral("original");
    QTest::newRow("minimum-compare")<<QSize(900,600)<<QStringLiteral("compare");
    QTest::newRow("default-compare")<<QSize(1280,800)<<QStringLiteral("compare");
}
void QmlWorkstationTests::keepsLiveContentUsable() {
    QFETCH(QSize,size);
    QFETCH(QString,mode);
    window_->resize(size);
    QVERIFY(runtime_->viewer()->setDisplayMode(mode));
    QTRY_COMPARE_WITH_TIMEOUT(runtime_->viewer()->displayMode(),mode,5000);
    QTRY_COMPARE(window_->size(),size);
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    for(const auto* name:{"evaluationBanner","imageArea","statusStrip","viewingToolbar"}) {
        auto* content=item(name);
        QVERIFY2(content,name);
        QTRY_VERIFY(content->isVisible());
        QVERIFY(content->width()>0 && content->height()>0);
        QVERIFY2(window_->contentItem()->boundingRect().adjusted(-1,-1,1,1)
            .contains(content->mapRectToScene(content->boundingRect())),name);
    }
    QVERIFY(item("evaluationBanner")->property("text").toString().contains("NOT FOR CLINICAL USE"));
    QVERIFY(item("imageArea")->width()>300);
    for(const auto* name:{"presetSelector","resetProcessingButton","processingStatus","activePresetSummary"}) {
        auto* control=item(name);
        QVERIFY2(control,name);
        QVERIFY(control->isEnabled());
        QVERIFY(window_->contentItem()->boundingRect().contains(control->mapRectToScene(control->boundingRect())));
    }
    revealProcessing("invertEnabled");
    capture(mode == "compare" ? "invert-layout-compare" : "invert-layout-original");
    revealProcessing("sharpenThresholdField");
    capture(mode == "compare" ? "sharpen-layout-compare" : "sharpen-layout-original");
    revealProcessing("denoiseSigmaField");
    capture(mode == "compare" ? "denoise-layout-compare" : "denoise-layout-original");
    revealProcessing("tileGridField");
    capture(mode == "compare" ? "local-contrast-layout-compare" : "local-contrast-layout-original");
    revealProcessing("gammaField");
    revealProcessing("brightnessField");
    capture(mode == "compare" ? "live-compare" : "live");
    click("cameraSettingsButton");
    QTRY_VERIFY(runtime_->camera()->settings()->isOpen());
    QVERIFY(!runtime_->camera()->settings()->editable());
    QVERIFY(QQuickTest::qWaitForPolish(window_));
    auto* dialog=window_->findChild<QObject*>(QStringLiteral("cameraSettingsDialog"));
    QVERIFY(dialog);
    QVERIFY(!dialog->property("modal").toBool());
    const QRectF dialogRect(dialog->property("x").toDouble(),dialog->property("y").toDouble(),
        dialog->property("width").toDouble(),dialog->property("height").toDouble());
    QVERIFY(window_->contentItem()->boundingRect().contains(dialogRect));
    for(const auto* name:{"cameraExposureMode","cameraGainMode","cameraExposureValue","cameraGainValue",
                         "cameraSettingsActual","cameraSettingsStatus","applyCameraSettingsButton","closeCameraSettingsButton"}) {
        auto* control=item(name);
        QVERIFY2(control,name);
        QVERIFY2(control->isVisible(),name);
        const auto rect=control->mapRectToScene(QRectF(0,0,control->width(),control->height()));
        QVERIFY2(rect.width()>0 && rect.height()>0 && dialogRect.contains(rect),name);
    }
    for(const auto* name:{"stopButton","disconnectButton"}) {
        auto* control=item(name);
        QVERIFY(control && control->isVisible() && control->isEnabled());
        const auto rect=control->mapRectToScene(QRectF(0,0,control->width(),control->height()));
        QVERIFY(window_->contentItem()->boundingRect().contains(rect));
        QVERIFY(!dialogRect.intersects(rect));
    }
    capture(mode == "compare" ? "camera-settings-layout-compare" : "camera-settings-layout-original");
    click("closeCameraSettingsButton");
    QTRY_VERIFY(!runtime_->camera()->settings()->isOpen());
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::stopDisconnectAndExplicitSavedResume() {
    const auto expectedProcessing=runtime_->coordinator().processingControls()->draft();
    QVERIFY(!runtime_->camera()->applyConfiguration());
    click("stopButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->startEnabled(),5000);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    QTRY_COMPARE_WITH_TIMEOUT(runtime_->viewer()->freshness(),QStringLiteral("Stale"),3000);
    capture("stopped-stale");
    click("disconnectButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->connectEnabled(),5000);
    destroyRuntime();
    createRuntime();
    QVERIFY(!item("sharpenAdvanced")->property("checked").toBool());
    const auto expectedId=QString::fromStdString(expectedProcessing.selectedId.value);
    QTRY_COMPARE(runtime_->processing()->selectedPresetId(),expectedId);
    QCOMPARE(item("presetSelector")->property("currentValue").toString(),expectedId);
    QCOMPARE(item("invertEnabled")->property("checked").toBool(),expectedProcessing.activePipeline.stages[7].enabled);
    QVERIFY(processing::semanticallyEqualPipelineDefinitions(
        runtime_->coordinator().processingControls()->draft().activePipeline,expectedProcessing.activePipeline));
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->resumeLiveEnabled(),10000);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    QVERIFY(!runtime_->viewer()->hasFrame());
    click("resumeLiveButton");
    if(runtime_->camera()->pending()) {
        QTRY_VERIFY(item("stopButton")->isVisible());
        QVERIFY(item("stopButton")->isEnabled());
    }
    QVERIFY(runtime_->camera()->stopLive());
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->camera()->pending(),10000);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    QVERIFY(!pipeline_->snapshot().camera->desiredStreaming);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->resumeLiveEnabled(),10000);
    click("resumeLiveButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame(),15000);
    QCOMPARE(pipeline_->snapshot().camera->state,application::CameraSessionState::Streaming);
}
void QmlWorkstationTests::exposesFallbackAndExplicitProcessingRetry() {
    destroyRuntime();
    auto fault=std::make_shared<EnhancementFailure>();
    createRuntime(fault);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->camera()->resumeLiveEnabled(),10000);
    click("resumeLiveButton");
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->processing()->fallback(),15000);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->hasFrame(),10000);
    QVERIFY(!runtime_->processing()->processingError().isEmpty());
    QTRY_VERIFY(item("retryProcessingButton")->isVisible());
    QVERIFY(!item("enhancedButton")->isEnabled());
    capture("fallback");
    fault->enabled=false;
    click("retryProcessingButton");
    QTRY_VERIFY_WITH_TIMEOUT(!runtime_->processing()->fallback() && !runtime_->processing()->retryPending(),10000);
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->viewer()->enhancedAvailable(),10000);
    QVERIFY(runtime_->processing()->processingError().isEmpty());
}
void QmlWorkstationTests::closesWithRenderingOwnersOutstanding() {
    QVERIFY(runtime_->viewer()->hasFrame());
    window_->close();
    QTRY_VERIFY_WITH_TIMEOUT(runtime_->closed(),10000);
    QVERIFY(!pipeline_->snapshot().context);
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::cleanupTestCase() { destroyRuntime(); }
void QmlWorkstationTests::destroyRuntime() {
    if(runtime_ && !runtime_->closed()) {
        runtime_->requestShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(runtime_->closed(),10000);
    }
    engine_.reset();window_=nullptr;runtime_.reset();
    if(preferences_) {preferences_->requestStop();preferences_->join();}
    if(installations_) {installations_->requestStop();installations_->join();}
}
void QmlWorkstationTests::capture(const QString& state) {
    const auto path=qEnvironmentVariable("LUMORA_QML_CAPTURE_DIR");
    if(path.isEmpty()) return;
    QVERIFY(QDir().mkpath(path));
    QTest::qWait(80);
    const auto image=window_->grabWindow();
    QVERIFY(!image.isNull());
    const auto basename=QStringLiteral("%1-%2x%3").arg(state).arg(window_->width()).arg(window_->height());
    QVERIFY(image.save(QDir(path).filePath(basename + ".png")));
    QJsonObject items;
    auto controls=window_->findChildren<QQuickItem*>();
    const auto collect=[&](const auto& self,QQuickItem* parent)->void {
        if(!controls.contains(parent)) controls.append(parent);
        for(auto* child:parent->childItems()) self(self,child);
    };
    collect(collect,window_->contentItem());
    for(const auto* control:controls) {
        if(control->objectName().isEmpty()) continue;
        const bool cameraInput=control->objectName()=="cameraExposureValue" || control->objectName()=="cameraGainValue"
            || control->objectName()=="cameraFrameRateValue" || control->objectName().startsWith("cameraRoi");
        const auto rect=control->mapRectToScene(cameraInput
            ? QRectF(0,0,control->width(),control->height()) : control->boundingRect());
        QJsonObject details{{"x",rect.x()},{"y",rect.y()},
            {"width",rect.width()},{"height",rect.height()},
            {"visible",control->isVisible()},{"enabled",control->isEnabled()},
            {"text",control->property("text").toString()}};
        if(control->objectName()=="presetSelector") {
            details.insert("entries",QJsonArray::fromVariantList(runtime_->processing()->property("presets").toList()));
            details.insert("currentValue",control->property("currentValue").toString());
            details.insert("currentIndex",control->property("currentIndex").toInt());
        }
        items.insert(control->objectName(),details);
    }
    for (const auto* dialogName : {"cameraSettingsDialog", "installationSettingsDialog"}) {
      if(auto* dialog=window_->findChild<QObject*>(QString::fromLatin1(dialogName))) {
        items.insert(QString::fromLatin1(dialogName),QJsonObject{{"x",dialog->property("x").toDouble()},
            {"y",dialog->property("y").toDouble()},{"width",dialog->property("width").toDouble()},
            {"height",dialog->property("height").toDouble()},{"visible",dialog->property("visible").toBool()},
            {"enabled",dialog->property("enabled").toBool()},{"modal",dialog->property("modal").toBool()}});
    }
    }
    const auto* camera=runtime_->camera();
    const auto* settings=camera->settings();
    const QJsonObject geometry{{"window",QJsonObject{{"width",window_->width()},{"height",window_->height()}}},{"items",items},
        {"camera",QJsonObject{{"requested",QJsonObject::fromVariantMap(camera->requestedConfiguration())},
            {"current",QJsonObject::fromVariantMap(camera->currentConfiguration())},
            {"applied",QJsonObject::fromVariantMap(camera->appliedConfiguration())}}},
        {"cameraSettings",QJsonObject{{"open",settings->isOpen()},{"editable",settings->editable()},
            {"applyEnabled",settings->applyEnabled()},{"exposureText",settings->exposureText()},
            {"gainText",settings->gainText()},{"frameRateText",settings->frameRateText()},
            {"roiWidthText",settings->roiWidthText()},{"roiHeightText",settings->roiHeightText()},
            {"pixelFormat",settings->pixelFormat()},{"status",settings->status()},{"actual",settings->currentSummary()}}}};
    QFile output(QDir(path).filePath(basename + ".json"));
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(output.write(QJsonDocument(geometry).toJson())>0);
}
}
int main(int argc,char** argv) {
    QQuickStyle::setStyle("Basic");
    QGuiApplication application(argc,argv);
    application.setOrganizationName("Lumora");application.setApplicationName("LumoraQmlPilotTest");
    application.setQuitOnLastWindowClosed(false);
    QmlWorkstationTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "QmlWorkstationTests.moc"
