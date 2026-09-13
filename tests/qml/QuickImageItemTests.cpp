#include "QuickImageItem.hpp"
#include "RendererFixture.hpp"
#include <lumora/presentation/FramePresenter.hpp>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest/QTest>
#include <gtest/gtest.h>
#include <functional>
namespace {
using namespace lumora;
using namespace presentation;
bool waitFor(const std::function<bool()>& predicate) {
    for(int i=0;i<200;++i) { if(predicate()) return true; QTest::qWait(5); }
    return predicate();
}
class QuickRenderer:public ::testing::Test {
protected:
    core::SystemClock clock;
    qml::test::Frames frames;
    QQuickWindow window;
    qml::QuickImageItem item{clock,window.contentItem()};
    void SetUp() override {
        window.setColor(Qt::magenta); window.resize(512,128);
        item.setSize({256,3}); window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        ASSERT_TRUE(waitFor([&]{return item.ready();}));
        qInfo() << "Actual graphics API" << window.rendererInterface()->graphicsApi()
            << "actual DPR" << window.effectiveDevicePixelRatio();
        if(qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
            EXPECT_DOUBLE_EQ(window.effectiveDevicePixelRatio(),qEnvironmentVariable("QT_SCALE_FACTOR").toDouble());
        }
    }
    std::optional<PresentationEvent> terminal() {
        std::optional<PresentationEvent> result;
        waitFor([&]{ result=item.takeEvent(); return result.has_value(); });
        return result;
    }
    void expectPixels(const QImage& image,int offset,bool inverse) {
        const auto dpr=window.effectiveDevicePixelRatio();
        for(int y=0;y<3;++y) for(int x=0;x<256;++x) {
            const int value=(x+53*y)%256;
            EXPECT_EQ(image.pixel(static_cast<int>((offset+x+.5)*dpr),static_cast<int>((y+.5)*dpr)),
                qRgb(inverse?255-value:value,inverse?255-value:value,inverse?255-value:value)) << x << ',' << y;
        }
    }
};
TEST_F(QuickRenderer, PaddedAlreadyOrientedRampCompletesOnlyOnActualSwap) {
    auto frame=frames.make(1,clock);
    ASSERT_TRUE(item.submit({{1,1,1},frame,DisplayMode::Original}).hasValue());
    EXPECT_FALSE(item.takeEvent());
    auto event=terminal();
    ASSERT_TRUE(event); ASSERT_TRUE(std::holds_alternative<PresentationReceipt>(*event));
    EXPECT_EQ(std::get<PresentationReceipt>(*event).ticket,(PresentationTicket{1,1,1}));
    expectPixels(window.grabWindow(),0,false);
    window.update(); QTest::qWait(40); EXPECT_FALSE(item.takeEvent());
}
TEST_F(QuickRenderer, EnhancedAndCompareUseOneBundleAndOneReceipt) {
    auto frame=frames.make(1,clock);
    ASSERT_TRUE(item.submit({{1,1,1},frame,DisplayMode::Enhanced}).hasValue());
    ASSERT_TRUE(terminal()); expectPixels(window.grabWindow(),0,true);
    item.setWidth(512);
    ASSERT_TRUE(item.submit({{1,1,2},frame,DisplayMode::Compare}).hasValue());
    auto event=terminal(); ASSERT_TRUE(event); ASSERT_TRUE(std::holds_alternative<PresentationReceipt>(*event));
    auto capture=window.grabWindow(); expectPixels(capture,0,false); expectPixels(capture,256,true);
    EXPECT_FALSE(item.takeEvent());
}
TEST_F(QuickRenderer, InvalidPlaneRetainsPriorImageAndRejectsAtomically) {
    ASSERT_TRUE(item.submit({{1,1,1},frames.make(1,clock),DisplayMode::Original}).hasValue());
    ASSERT_TRUE(terminal());
    EXPECT_FALSE(item.submit({{1,2,2},frames.make(2,clock,256,3,false),DisplayMode::Compare}).hasValue());
    EXPECT_FALSE(item.takeEvent()); expectPixels(window.grabWindow(),0,false);
}
}
namespace {
TEST_F(QuickRenderer, CancellationBeforeSyncReleasesSourceAndNeverCompletes) {
    auto frame=frames.make(1,clock); std::weak_ptr weak=frame;
    ASSERT_TRUE(item.submit({{1,1,1},frame,DisplayMode::Compare}).hasValue());
    frame.reset(); EXPECT_TRUE(weak.expired()); EXPECT_EQ(frames.leases(),0U);
    EXPECT_TRUE(item.cancelPending({1,1,1}));
    QTest::qWait(40); EXPECT_FALSE(item.takeEvent());
    EXPECT_EQ(item.metrics().conversionImages,0U);
}
TEST_F(QuickRenderer, HiddenAndZeroSizeRejectAndRetireWithoutSwap) {
    ASSERT_TRUE(item.submit({{1,1,1},frames.make(1,clock),DisplayMode::Original}).hasValue());
    ASSERT_TRUE(terminal());
    window.hide(); QTest::qWait(40);
    EXPECT_FALSE(item.ready());
    auto lost=item.takeEvent(); ASSERT_TRUE(lost);
    EXPECT_TRUE(std::holds_alternative<PresentationFailure>(*lost));
    item.retire(7);
    auto retired=terminal(); ASSERT_TRUE(retired);
    ASSERT_TRUE(std::holds_alternative<PresentationRetired>(*retired));
    EXPECT_EQ(std::get<PresentationRetired>(*retired).retirementId,7U);
    EXPECT_EQ(item.metrics().textures,0U); EXPECT_EQ(item.metrics().conversionImages,0U);
    window.show(); ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    item.setWidth(0); EXPECT_FALSE(item.ready());
    EXPECT_FALSE(item.submit({{2,2,1},frames.make(2,clock),DisplayMode::Original}).hasValue());
}
TEST_F(QuickRenderer, ReceiptBeforeHideSurvivesDelayedGuiDelivery) {
    ASSERT_TRUE(item.submit({{1,1,1},frames.make(1,clock),DisplayMode::Original}).hasValue());
    ASSERT_TRUE(waitFor([&]{return item.metrics().completed==1;}));
    window.hide(); QTest::qWait(30);
    auto first=item.takeEvent(); auto second=item.takeEvent();
    ASSERT_TRUE(first); ASSERT_TRUE(second);
    EXPECT_TRUE(std::holds_alternative<PresentationReceipt>(*first));
    EXPECT_TRUE(std::holds_alternative<PresentationFailure>(*second));
    EXPECT_FALSE(item.takeEvent());
}
TEST_F(QuickRenderer, SharedPresenterPauseCancellationAndFrozenRecovery) {
    core::LatestValueSlot<core::FrameBundle> slot;
    FramePresenter presenter(slot,item,clock,1);
    (void)slot.publish(frames.make(1,clock)); presenter.refresh(); presenter.pause();
    EXPECT_EQ(presenter.status().viewerState,ViewerState::Paused);
    EXPECT_EQ(presenter.displayedFrameCount(),0U);
    presenter.resume(); presenter.refresh();
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.displayedFrameCount()==1;}));
    presenter.pause();
    const auto frozen=presenter.presentedBundle();
    window.hide(); QTest::qWait(30); presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle(),nullptr);
    (void)slot.publish(frames.make(2,clock));
    window.show(); ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.presentedBundle()!=nullptr;}));
    EXPECT_EQ(presenter.presentedBundle(),frozen); EXPECT_EQ(presenter.displayedFrameCount(),1U);
    presenter.setDisplayMode(DisplayMode::Enhanced);
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return item.ready();}));
    EXPECT_EQ(presenter.displayedFrameCount(),1U);
    presenter.retire(); ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.retirementComplete();}));
    EXPECT_EQ(item.metrics().textures,0U);
}
TEST_F(QuickRenderer, SharedGeometryClipsBothPanesAndUsesLogicalPixels) {
    item.setSize({400,100});
    ASSERT_TRUE(item.submit({{1,1,1},frames.make(1,clock,100,50),DisplayMode::Compare}).hasValue());
    ASSERT_TRUE(terminal());
    auto rects=item.imageRects(); EXPECT_EQ(rects[0],QRectF(0,0,200,100)); EXPECT_EQ(rects[1],QRectF(200,0,200,100));
    item.actualPixels(); rects=item.imageRects();
    EXPECT_EQ(rects[0],QRectF(50,25,100,50)); EXPECT_EQ(rects[1],QRectF(250,25,100,50));
    item.zoomAt({100,50},4); item.panBy({20,10}); rects=item.imageRects();
    EXPECT_EQ(rects[1].topLeft()-rects[0].topLeft(),QPointF(200,0));
    EXPECT_EQ(rects[0].size(),rects[1].size());
    // Verify the clip seam after shared pan/zoom, including pixels where the
    // other pane's enlarged texture would bleed through without its clip node.
    const auto capture=window.grabWindow();
    const auto dpr=window.effectiveDevicePixelRatio();
    for(const int x:{0,99,199,200,299,399}) for(const int y:{0,49,99}) {
        const auto& rect=rects[x<200?0:1];
        const int sourceX=static_cast<int>((x+.5-rect.x())/4);
        const int sourceY=static_cast<int>((y+.5-rect.y())/4);
        const int original=(sourceX+53*sourceY)%256;
        const int gray=x<200?original:255-original;
        EXPECT_EQ(capture.pixel(static_cast<int>((x+.5)*dpr),static_cast<int>((y+.5)*dpr)),qRgb(gray,gray,gray));
    }
}
TEST_F(QuickRenderer, RepeatedRetirementReturnsKnownOwnersToBaseline) {
    for(unsigned id=1;id<=8;++id) {
        ASSERT_TRUE(item.submit({{id,id,1},frames.make(id,clock),DisplayMode::Compare}).hasValue());
        ASSERT_TRUE(terminal());
        EXPECT_EQ(frames.leases(),0U);
        item.retire(id); auto event=terminal(); ASSERT_TRUE(event);
        ASSERT_TRUE(std::holds_alternative<PresentationRetired>(*event));
        EXPECT_EQ(item.metrics().textures,0U); EXPECT_EQ(item.metrics().conversionImages,0U);
    }
    EXPECT_LE(item.metrics().maximumTextures,4U); EXPECT_LE(item.metrics().maximumConversionImages,4U);
}
TEST(RendererStorage, CheckedCurrentAndReplacementPairs) {
    auto storage=qml::QuickImageItem::assessStorage(2048,2048,DisplayMode::Compare);
    ASSERT_TRUE(storage.hasValue());
    EXPECT_EQ(storage.value().currentImageBytes,32U*1024U*1024U);
    EXPECT_EQ(storage.value().replacementImageBytes,32U*1024U*1024U);
    EXPECT_EQ(storage.value().nominalTextureBytes,64U*1024U*1024U);
    EXPECT_FALSE(qml::QuickImageItem::assessStorage(SIZE_MAX,2,DisplayMode::Compare).hasValue());
}
}

#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
namespace {
TEST_F(QuickRenderer, SharedPresenterPauseAfterConsumeBeforeActualSwap) {
    core::LatestValueSlot<core::FrameBundle> slot;
    FramePresenter presenter(slot,item,clock,1);
    std::mutex mutex; std::condition_variable condition;
    std::atomic<bool> reached=false, timedOut=false; bool released=false;
    auto pauseAtBoundary=[&] {
        EXPECT_EQ(item.metrics().completed,0U);
        presenter.pause();
        EXPECT_EQ(presenter.status().viewerState,ViewerState::Pausing);
        EXPECT_EQ(presenter.displayedFrameCount(),0U);
    };
    const auto connection=QObject::connect(&window,&QQuickWindow::afterRendering,&window,[&] {
        if(item.metrics().consumed!=1 || reached.load()) return;
        if(QThread::currentThread()==window.thread()) {pauseAtBoundary();reached=true;return;}
        std::unique_lock lock(mutex); reached=true;
        timedOut=!condition.wait_for(lock,std::chrono::seconds(3),[&]{return released;});
    },Qt::DirectConnection);
    (void)slot.publish(frames.make(1,clock)); presenter.refresh();
    const bool boundaryReached=waitFor([&]{return reached.load();});
    // On threaded loops this runs while the real afterRendering callback holds
    // the swap. Basic/software loops execute the same GUI action inline there.
    if(presenter.status().viewerState==ViewerState::Live) pauseAtBoundary();
    {std::lock_guard lock(mutex);released=true;} condition.notify_all();
    QObject::disconnect(connection);
    ASSERT_TRUE(boundaryReached);
    EXPECT_FALSE(timedOut.load());
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.displayedFrameCount()==1;}));
    EXPECT_EQ(presenter.status().viewerState,ViewerState::Paused);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(),1U);
}
TEST_F(QuickRenderer, SharedPresenterPauseAfterSwapBeforeDelivery) {
    core::LatestValueSlot<core::FrameBundle> slot;
    FramePresenter presenter(slot,item,clock,1);
    (void)slot.publish(frames.make(1,clock));presenter.refresh();
    ASSERT_TRUE(waitFor([&]{return item.metrics().completed==1;}));
    EXPECT_EQ(presenter.displayedFrameCount(),0U);
    presenter.pause(); EXPECT_EQ(presenter.status().viewerState,ViewerState::Pausing);
    presenter.refresh(); EXPECT_EQ(presenter.status().viewerState,ViewerState::Paused);
    EXPECT_EQ(presenter.displayedFrameCount(),1U);
}
TEST_F(QuickRenderer, AncestorOpacityAndClippingInvalidateTheDisplayedImage) {
    QQuickItem parent(window.contentItem()); parent.setSize({256,3});
    item.setParentItem(&parent);
    ASSERT_TRUE(item.submit({{1,1,1},frames.make(1,clock),DisplayMode::Original}).hasValue());
    ASSERT_TRUE(terminal()); parent.setOpacity(0);
    EXPECT_FALSE(item.ready());
    auto lost=terminal(); EXPECT_TRUE(lost && std::holds_alternative<PresentationFailure>(*lost));
    EXPECT_TRUE(waitFor([&]{return item.metrics().textures==0;}));
    parent.setOpacity(1); parent.setClip(true); parent.setSize({0,0});
    EXPECT_FALSE(item.ready());
    item.setParentItem(window.contentItem());
}
TEST_F(QuickRenderer, CancelledModeNeverChangesCompletedGeometry) {
    item.setSize({512,6});
    auto frame=frames.make(1,clock);
    ASSERT_TRUE(item.submit({{1,1,1},frame,DisplayMode::Original}).hasValue()); ASSERT_TRUE(terminal());
    const auto original=item.imageRects();
    ASSERT_TRUE(item.submit({{1,1,2},frame,DisplayMode::Compare}).hasValue());
    ASSERT_TRUE(item.cancelPending({1,1,2}));
    EXPECT_EQ(item.imageRects(),original);
    item.update();QTest::qWait(30);EXPECT_FALSE(item.takeEvent());
}
TEST_F(QuickRenderer, CloseAndResetSettleWithoutAnotherSwapAndFenceOldWindow) {
    core::LatestValueSlot<core::FrameBundle> first,second;
    FramePresenter presenter(first,item,clock,1);
    (void)first.publish(frames.make(1,clock));presenter.refresh();
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.displayedFrameCount()==1;}));
    window.close(); QTest::qWait(20);
    presenter.resetSource(second,2); presenter.refresh();
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.retirementComplete();}));
    EXPECT_EQ(item.metrics().textures,0U);
    QQuickWindow replacement; replacement.resize(512,128); replacement.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&replacement));
    item.setParentItem(replacement.contentItem());
    (void)second.publish(frames.make(0,clock));
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.presentedBundle()!=nullptr;}));
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(),0U);
    window.releaseResources();QTest::qWait(20);presenter.refresh();
    EXPECT_NE(presenter.presentedBundle(),nullptr);
    presenter.retire();ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.retirementComplete();}));
    item.setParentItem(window.contentItem());
}
}
namespace {
TEST_F(QuickRenderer, FullyClippedComparePaneRejectsInsteadOfReportingBothPlanes) {
    item.setSize({1024,3});
    EXPECT_FALSE(item.submit({{1,1,1},frames.make(1,clock),DisplayMode::Compare}).hasValue());
    EXPECT_FALSE(item.takeEvent());
}
TEST_F(QuickRenderer, SceneGraphInvalidationRecreatesFromFrozenOwner) {
    core::LatestValueSlot<core::FrameBundle> slot;
    FramePresenter presenter(slot,item,clock,1);
    (void)slot.publish(frames.make(1,clock));presenter.refresh();
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.displayedFrameCount()==1;}));
    presenter.pause();
    window.setPersistentSceneGraph(false);window.setPersistentGraphics(false);
    window.hide();window.releaseResources();
    ASSERT_TRUE(waitFor([&]{return item.metrics().textures==0;}));
    presenter.refresh();EXPECT_EQ(presenter.presentedBundle(),nullptr);
    window.show();ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.presentedBundle()!=nullptr;}));
    EXPECT_EQ(presenter.displayedFrameCount(),1U);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(),1U);
}
}
namespace {
TEST_F(QuickRenderer, NeverShownSurfaceRetiresWithoutInitialization) {
    QQuickWindow hidden;
    qml::QuickImageItem uninitialized(clock,hidden.contentItem());uninitialized.setSize({256,3});
    EXPECT_FALSE(uninitialized.ready());
    uninitialized.retire(42);auto event=uninitialized.takeEvent();ASSERT_TRUE(event);
    ASSERT_TRUE(std::holds_alternative<PresentationRetired>(*event));
    EXPECT_EQ(uninitialized.metrics().textures,0U);
}
TEST_F(QuickRenderer, DelayedResetAcknowledgesCleanupOnCloseWithoutNewSwap) {
    core::LatestValueSlot<core::FrameBundle> first,second;
    FramePresenter presenter(first,item,clock,1);
    (void)first.publish(frames.make(1,clock));presenter.refresh();
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.displayedFrameCount()==1;}));
    const auto swaps=item.metrics().completed;
    presenter.resetSource(second,2);
    EXPECT_FALSE(presenter.retirementComplete());EXPECT_GT(item.metrics().textures,0U);
    window.close();
    ASSERT_TRUE(waitFor([&]{presenter.refresh();return presenter.retirementComplete();}));
    EXPECT_EQ(item.metrics().completed,swaps);EXPECT_EQ(item.metrics().textures,0U);
}
TEST_F(QuickRenderer, RepeatedWindowMovesAndHiddenRetirementFenceOldCallbacks) {
    QQuickWindow other;other.resize(512,128);other.show();ASSERT_TRUE(QTest::qWaitForWindowExposed(&other));
    for(unsigned id=1;id<=5;++id) {
        auto* target=id%2?&other:&window;
        item.setParentItem(target->contentItem());
        ASSERT_TRUE(waitFor([&]{return item.ready();}));
        ASSERT_TRUE(item.submit({{id,id,1},frames.make(id,clock),DisplayMode::Compare}).hasValue());
        ASSERT_TRUE(terminal());
        target->hide();item.retire(id);
        auto event=terminal();ASSERT_TRUE(event);ASSERT_TRUE(std::holds_alternative<PresentationRetired>(*event));
        EXPECT_EQ(item.metrics().textures,0U);EXPECT_EQ(item.metrics().conversionImages,0U);
        target->show();ASSERT_TRUE(QTest::qWaitForWindowExposed(target));
    }
    item.setParentItem(window.contentItem());
}
TEST(QuickRendererClock, DelayedDeliveryDoesNotRenewActualCallbackTime) {
    core::ManualClock clock; qml::test::Frames frames;
    QQuickWindow window;window.resize(512,128);
    qml::QuickImageItem item(clock,window.contentItem());item.setSize({256,3});
    core::LatestValueSlot<core::FrameBundle> slot;
    FramePresenter presenter(slot,item,clock,1);
    window.show();ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    (void)slot.publish(frames.make(1,clock));presenter.refresh();
    ASSERT_TRUE(waitFor([&]{return item.metrics().completed==1;}));
    clock.advance(std::chrono::milliseconds(600));presenter.refresh();
    EXPECT_EQ(presenter.status().freshness,FrameFreshness::Stale);
    EXPECT_EQ(item.metrics().guiDelivery,std::chrono::milliseconds(600));
}
}
