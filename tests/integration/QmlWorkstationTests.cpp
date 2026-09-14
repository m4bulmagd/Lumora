#include "QmlWorkstation.hpp"
#include "CameraAdapter.hpp"
#include "ProcessingAdapter.hpp"
#include "ViewerAdapter.hpp"
#include "QuickImageItem.hpp"
#include "SimulatorComposition.hpp"
#include "FrameEngineTestAccess.hpp"
#include <atomic>
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
    void completedViewingAndExactNumericEditing();
    void keyboardCanReturnToViewportWithoutStealingNumericInput();
    void presetsAndResetPreserveCameraPausedFrameAndViewport();
    void toneEditingPreservesExactValuesPausedPixelsAndResetAuthority();
    void localContrastEditingPreservesPausedFrameAndExactSettings();
    void denoiseEditingNormalizesModesAndPreservesPausedFrame();
    void renderedPixelsStayInsideViewport();
    void keepsLiveContentUsable_data();
    void keepsLiveContentUsable();
    void stopDisconnectAndExplicitSavedResume();
    void exposesFallbackAndExplicitProcessingRetry();
    void closesWithRenderingOwnersOutstanding();
    void cleanupTestCase();
private:
    QQuickItem* item(const char* name) const { return window_->findChild<QQuickItem*>(QString::fromLatin1(name)); }
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
    void revealProcessing(const char* name);
    void enterText(const char* name,const QString& text,bool commit=true);
    QImage viewportPixels() const;
    bool toneSaved(double brightness,double contrast,double gamma) const;
    bool localContrastSaved(double clip,int grid,bool enabled=true) const;
    bool persistedPresetIs(const std::string& id) const;
    void createRuntime(std::shared_ptr<EnhancementFailure> fault = {});
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
void QmlWorkstationTests::createRuntime(std::shared_ptr<EnhancementFailure> fault) {
    warnings_.clear();
    QVERIFY(directory_.isValid());
    installations_=std::make_unique<configuration::InstallationProfilesService>(
        std::make_unique<configuration::InstallationProfileStore>(directory_.filePath("machine.json").toStdString(),false),
        false,application::InstallationProfilePolicy::SimulatorIdentityFallback);
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
    QTRY_VERIFY(scroll->boundingRect().adjusted(-1,-1,1,1)
        .contains(control->mapRectToItem(scroll,control->boundingRect())));
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
    revealProcessing("denoiseSigmaField");
    capture(mode == "compare" ? "denoise-layout-compare" : "denoise-layout-original");
    revealProcessing("tileGridField");
    capture(mode == "compare" ? "local-contrast-layout-compare" : "local-contrast-layout-original");
    revealProcessing("gammaField");
    revealProcessing("brightnessField");
    capture(mode == "compare" ? "live-compare" : "live");
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
    const auto expectedId=QString::fromStdString(expectedProcessing.selectedId.value);
    QTRY_COMPARE(runtime_->processing()->selectedPresetId(),expectedId);
    QCOMPARE(item("presetSelector")->property("currentValue").toString(),expectedId);
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
    for(const auto* control:window_->findChildren<QQuickItem*>()) {
        if(control->objectName().isEmpty()) continue;
        const auto rect=control->mapRectToScene(control->boundingRect());
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
    const QJsonObject geometry{{"window",QJsonObject{{"width",window_->width()},{"height",window_->height()}}},{"items",items}};
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
