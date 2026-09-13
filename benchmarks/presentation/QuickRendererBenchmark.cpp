#include "QuickImageItem.hpp"
#include "RendererFixture.hpp"
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QtTest/QTest>
#include <algorithm>
#include <iostream>
#include <functional>
#include <mutex>
#include <vector>

namespace {
using namespace lumora;
using namespace presentation;
bool waitFor(const std::function<bool()>& predicate) {
    for(unsigned i=0;i<5000;++i) {if(predicate())return true;QCoreApplication::processEvents();QThread::msleep(1);}
    return predicate();
}
QJsonObject distribution(std::vector<double> values) {
    std::ranges::sort(values);
    return {{"p50_us",values[29]},{"p95_us",values[56]},{"max_us",values.back()}};
}
double micros(std::chrono::nanoseconds time) {return static_cast<double>(time.count())/1000.;}
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    core::SystemClock clock;
    QQuickWindow window;window.resize(1280,800);window.setColor(Qt::black);
    window.setTitle("Lumora renderer experiment — synthetic fixtures");
    std::mutex diagnosticsMutex;
    QString renderer="software",version;
    bool renderThread=false;
    QObject::connect(&window,&QQuickWindow::beforeRendering,&window,[&] {
        std::lock_guard lock(diagnosticsMutex);
        renderThread=QThread::currentThread()!=app.thread();
        if(auto* context=QOpenGLContext::currentContext()) {
            renderer=QString::fromLatin1(reinterpret_cast<const char*>(context->functions()->glGetString(GL_RENDERER)));
            version=QString::fromLatin1(reinterpret_cast<const char*>(context->functions()->glGetString(GL_VERSION)));
        }
    },Qt::DirectConnection);
    window.show();if(!QTest::qWaitForWindowExposed(&window))return 2;
    QJsonArray cases;
    for(const auto dimensions:{QSize(640,480),QSize(2048,2048)}) for(const auto mode:{DisplayMode::Original,DisplayMode::Compare}) {
        qml::QuickImageItem item(clock,window.contentItem());item.setSize({1280,800});
        if(!waitFor([&]{return item.ready();}))return 3;
        qml::test::Frames frames;
        auto frame=frames.make(1,clock,static_cast<unsigned>(dimensions.width()),static_cast<unsigned>(dimensions.height()));
        auto storage=qml::QuickImageItem::assessStorage(static_cast<std::size_t>(dimensions.width()),static_cast<std::size_t>(dimensions.height()),mode);
        if(!storage.hasValue())return 4;
        processing::ProcessingPreparationOptions options;
        options.externalSessionStorageBytes=storage.value().currentImageBytes+storage.value().replacementImageBytes;
        const auto pixels=static_cast<std::size_t>(dimensions.width())*static_cast<std::size_t>(dimensions.height());
        auto assessment=processing::FrameProcessingEngine::plan({9,2*pixels},{16,pixels},frame->raw->layout,processing::defaultPipeline(),options);
        if(!assessment.plan || assessment.resources.externalSessionBytes!=options.externalSessionStorageBytes)return 5;
        std::vector<double> conversion,admission,swap,delivery;
        const auto before=item.metrics();
        for(unsigned index=0;index<72;++index) {
            if(!waitFor([&]{return item.ready();}))return 6;
            if(!item.submit({{1,1,index+1},frame,mode}).hasValue())return 7;
            std::optional<PresentationEvent> event;
            if(!waitFor([&]{event=item.takeEvent();return event.has_value();}) || !std::holds_alternative<PresentationReceipt>(*event))return 8;
            const auto metrics=item.metrics();
            if(index>=12) {
                conversion.push_back(micros(metrics.preparation));admission.push_back(micros(metrics.admissionToConsume));
                swap.push_back(micros(metrics.consumeToSwap));delivery.push_back(micros(metrics.guiDelivery));
            }
        }
        const auto metrics=item.metrics();
        cases.append(QJsonObject{{"width",dimensions.width()},{"height",dimensions.height()},
            {"mode",mode==DisplayMode::Compare?"Compare":"Original"},{"warmup",12},{"samples",60},
            {"observed_consumes",static_cast<qint64>(metrics.consumed-before.consumed)},
            {"observed_swaps",static_cast<qint64>(metrics.completed-before.completed)},
            {"preparation",distribution(conversion)},{"admission_to_consume",distribution(admission)},
            {"consume_to_frameSwapped",distribution(swap)},{"GUI_delivery_delay",distribution(delivery)},
            {"maximum_texture_owners",static_cast<qint64>(metrics.maximumTextures)},
            {"maximum_RGB32_image_owners",static_cast<qint64>(metrics.maximumConversionImages)},
            {"current_RGB32_bytes",static_cast<qint64>(storage.value().currentImageBytes)},
            {"replacement_RGB32_bytes",static_cast<qint64>(storage.value().replacementImageBytes)},
            {"old_and_new_nominal_texture_bytes",static_cast<qint64>(storage.value().nominalTextureBytes)},
            {"preparation_external_session_bytes",static_cast<qint64>(assessment.resources.externalSessionBytes)},
            {"preparation_required_storage_bytes",static_cast<qint64>(assessment.resources.requiredStorageBytes)},
            {"processing_pool_capacity",9},{"display_pool_capacity",16},
            {"processing_pool_requested_bytes",static_cast<qint64>(assessment.resources.processingPoolBytes)},
            {"display_pool_requested_bytes",static_cast<qint64>(assessment.resources.displayPoolBytes)},
            {"source_pool_leases",static_cast<qint64>(frames.leases())},
            {"source_bundle_owners_after_submit",static_cast<qint64>(frame.use_count())},
            {"sink_source_bundle_owners",static_cast<qint64>(frame.use_count()-1)}});
        item.retire(1);std::optional<PresentationEvent> retired;
        if(!waitFor([&]{retired=item.takeEvent();return retired.has_value();}) || !std::holds_alternative<PresentationRetired>(*retired))return 9;
        if(item.metrics().textures || item.metrics().conversionImages)return 10;
        auto recorded=cases.last().toObject();
        recorded.insert("texture_owners_after_retirement",static_cast<qint64>(item.metrics().textures));
        recorded.insert("RGB32_image_owners_after_retirement",static_cast<qint64>(item.metrics().conversionImages));
        frame.reset();
        recorded.insert("source_pool_leases_after_release",static_cast<qint64>(frames.leases()));
        if(frames.leases()!=0)return 15;
        cases.replace(cases.size()-1,recorded);
    }
    // Separate untimed captures at both workstation sizes; never part of samples.
    const QString captureDirectory=qEnvironmentVariable("LUMORA_RENDERER_CAPTURE_DIR");
    if(!captureDirectory.isEmpty()) {
        QDir().mkpath(captureDirectory);
        qml::QuickImageItem item(clock,window.contentItem());
        qml::test::Frames frames;
        auto frame=frames.make(1,clock,640,480);
        for(const auto size:{QSize(900,600),QSize(1280,800)}) {
            window.resize(size);item.setSize(size);item.fit();
            if(!waitFor([&]{return item.ready();}))return 11;
            if(!item.submit({{2,1,static_cast<std::uint64_t>(size.width())},frame,DisplayMode::Compare}).hasValue())return 12;
            std::optional<PresentationEvent> event;
            if(!waitFor([&]{event=item.takeEvent();return event.has_value();}))return 13;
            if(!window.grabWindow().save(captureDirectory+QString("/compare-%1x%2.png").arg(size.width()).arg(size.height())))return 14;
        }
    }
    QJsonObject result;
    {
        std::lock_guard lock(diagnosticsMutex);
        result={{"Qt",qVersion()},{"platform",QGuiApplication::platformName()},
            {"graphics_API",static_cast<int>(window.rendererInterface()->graphicsApi())},
            {"renderer",renderer},{"graphics_version",version},{"observed_render_thread",renderThread},
            {"DPR",window.effectiveDevicePixelRatio()},{"cases",cases},
            {"scope","Renderer experiment only; owned RGB32 copy plus public Qt texture. No processing FPS, physical scan-out, zero-copy, driver-memory or zero-allocation claim."}};
    }
    std::cout<<QJsonDocument(result).toJson(QJsonDocument::Indented).constData();
    return 0;
}
