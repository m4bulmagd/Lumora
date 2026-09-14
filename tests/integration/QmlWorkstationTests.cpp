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
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
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
        QVERIFY2(control->isEnabled(),name);
        QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier,
            control->mapToScene(control->boundingRect().center()).toPoint());
    }
    QString diagnostics() const { return warnings_.join('\n'); }
    void capture(const QString& state);
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
void QmlWorkstationTests::keyboardCanReturnToViewportWithoutStealingNumericInput() {
    auto& image=runtime_->viewer()->imageItem();
    auto* surface=image.parentItem();
    auto* field=item("levelField");
    QVERIFY(surface);
    QVERIFY(field);
    field->forceActiveFocus();
    const auto before=image.imageRects();
    const auto numericText=field->property("text").toString();
    QTest::keyClick(window_,Qt::Key_End);
    QTest::keyClick(window_,Qt::Key_Minus);
    QCOMPARE(field->property("text").toString(),numericText+"-");
    QCOMPARE(image.imageRects(),before);
    QTest::keyClick(window_,Qt::Key_Backspace);
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
    capture(mode == "compare" ? "live-compare" : "live");
    QVERIFY2(warnings_.isEmpty(),qPrintable(diagnostics()));
}
void QmlWorkstationTests::stopDisconnectAndExplicitSavedResume() {
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
        items.insert(control->objectName(),QJsonObject{{"x",rect.x()},{"y",rect.y()},
            {"width",rect.width()},{"height",rect.height()},
            {"visible",control->isVisible()},{"enabled",control->isEnabled()},
            {"text",control->property("text").toString()}});
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
