#include "QuickImageItem.hpp"

#include <QEvent>
#include <QImage>
#include <QPointer>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGClipNode>
#include <QSGImageNode>
#include <QSGGeometry>
#include <QSGTexture>
#include <QSGTransformNode>
#include <algorithm>
#include <limits>
#include <mutex>
#include <vector>

namespace lumora::qml {
namespace {
using namespace presentation;
using Time = std::chrono::steady_clock::time_point;
core::Error rendererError(const char* code) {
    return {core::ErrorCategory::ResourceExhaustion,code,"Image surface is unavailable.",
        "The Qt Quick renderer could not retain a drawable image.",true};
}
struct Prepared {
    PresentationTicket ticket;
    DisplayMode mode;
    std::array<QImage,2> images;
    std::size_t count{};
    Time admittedAt{};
};
struct Binding;
struct State {
    explicit State(core::IClock& supplied):clock(supplied) {}
    std::mutex mutex;
    core::IClock& clock;
    std::optional<Prepared> pending;
    std::optional<PresentationTicket> armed, displayed;
    Time consumedAt{};
    std::uint64_t cycle{}, armedCycle{}, epoch{};
    bool rendered{}, dead{};
    std::optional<std::uint64_t> retirement;
    std::array<std::optional<PresentationEvent>,2> events;
    RendererMetrics metrics;
    template<typename Event>
    void push(Event event) {
        for(auto& slot:events) if(!slot) {slot.emplace(std::in_place_type<Event>,std::move(event));return;}
        // Serialized admission and deduplicated image loss prove the two-slot bound.
        Q_ASSERT_X(false,"QuickImageItem","terminal mailbox capacity exceeded");
    }
    void discardPending() {
        if(!pending) return;
        for(const auto& image:pending->images) if(!image.isNull()) {
            --metrics.conversionImages;
            metrics.requestedImageBytes-=static_cast<std::size_t>(image.sizeInBytes());
        }
        pending.reset();
    }
    void acknowledge() {
        if(retirement && metrics.textures==0 && metrics.conversionImages==0) {
            events={}; push(PresentationRetired{*retirement}); retirement.reset();
        }
    }
    void lose(const char* code) {
        const auto ticket=pending ? std::optional{pending->ticket} : armed ? armed : displayed;
        discardPending(); armed.reset(); displayed.reset(); rendered=false;
        if(ticket && !retirement && !dead) push(PresentationFailure{*ticket,rendererError(code),false});
    }
};
// The software renderer caches inherited state for transform nodes, but not
// generic QSGNode containers. An identity transform keeps dynamically replaced
// pane children under the item's inherited transform, opacity and clipping.
struct ImageRoot final:QSGTransformNode {
    std::shared_ptr<State> state;
    std::shared_ptr<Binding> binding;
    std::array<QImage,2> images;
    std::array<QSGImageNode*,2> imageNodes{};
    std::array<QSGClipNode*,2> clips{};
    std::size_t count{};
    ImageRoot(std::shared_ptr<State> s,std::shared_ptr<Binding> b):state(std::move(s)),binding(std::move(b)){}
    ~ImageRoot() override;
    // All callers hold State::mutex on the render thread.
    void clear() {
        while(auto* child=firstChild()) {removeChildNode(child);delete child;}
        for(const auto& image:images) if(!image.isNull()) {
            --state->metrics.conversionImages;
            state->metrics.requestedImageBytes-=static_cast<std::size_t>(image.sizeInBytes());
            --state->metrics.textures;
            state->metrics.requestedTextureBytes-=static_cast<std::size_t>(image.sizeInBytes());
        }
        images={}; imageNodes={}; clips={}; count=0;
        state->acknowledge();
    }
};
struct Binding {
    std::uint64_t epoch{};
    ImageRoot* root{};
    bool cleanupScheduled{};
    std::uint64_t cleanupToken{};
    bool active{true};
    std::array<QMetaObject::Connection,2> lifecycleConnections;
    void disconnectIfInactive() {
        if(active) return;
        for(const auto& connection:lifecycleConnections) QObject::disconnect(connection);
        lifecycleConnections={};
    }
};
ImageRoot::~ImageRoot() {
    std::lock_guard lock(state->mutex);
    if(binding->root==this) binding->root=nullptr;
    if(binding->epoch==state->epoch && count) state->lose("quick_node_destroyed");
    clear();
    binding->disconnectIfInactive();
}
void cleanup(const std::shared_ptr<State>& state,const std::shared_ptr<Binding>& binding,
    std::optional<std::uint64_t> token = {}) {
    std::lock_guard lock(state->mutex);
    if(token && *token!=binding->cleanupToken) return;
    ++binding->cleanupToken;
    if(binding->root) binding->root->clear();
    binding->cleanupScheduled=false;
    binding->disconnectIfInactive();
    state->acknowledge();
}
class CleanupJob final:public QRunnable {
public:
    CleanupJob(std::shared_ptr<State> state,std::shared_ptr<Binding> binding,std::uint64_t token):state_(std::move(state)),binding_(std::move(binding)),token_(token){}
    void run() override {cleanup(state_,binding_,token_);}
private:
    std::shared_ptr<State> state_;
    std::shared_ptr<Binding> binding_;
    std::uint64_t token_;
};
QImage rgbImage(const core::DisplayFrame& plane) {
    const auto width=plane.layout.width(),height=plane.layout.height();
    if(width>static_cast<unsigned>(std::numeric_limits<int>::max()) || height>static_cast<unsigned>(std::numeric_limits<int>::max())) return {};
    QImage image(static_cast<int>(width),static_cast<int>(height),QImage::Format_RGB32);
    if(image.isNull()) return {};
    const auto pixels=plane.pixels.bytes();
    for(unsigned y=0;y<height;++y) {
        auto* row=reinterpret_cast<QRgb*>(image.scanLine(static_cast<int>(y)));
        for(unsigned x=0;x<width;++x) {
            const auto gray=std::to_integer<unsigned char>(pixels[y*plane.layout.strideBytes()+x]);
            row[x]=qRgb(gray,gray,gray);
        }
    }
    return image;
}
}
struct QuickImageItem::Impl {
    explicit Impl(core::IClock& clock):state(std::make_shared<State>(clock)){}
    std::shared_ptr<State> state;
    std::shared_ptr<Binding> binding;
    QPointer<QQuickWindow> window;
    std::vector<QMetaObject::Connection> connections;
    std::vector<QMetaObject::Connection> ancestorConnections;
    ViewportTransform transform=ViewportTransform::fit({0,0},{0,0});
    Size imageSize{0,0};
    DisplayMode mode=DisplayMode::Original;
    std::optional<core::Error> initializationError;
    void scheduleCleanup() {
        if(!binding || !window) return;
        std::uint64_t token;
        {
            std::lock_guard lock(state->mutex);
            if(!binding->root || binding->root->count==0) {binding->disconnectIfInactive();state->acknowledge();return;}
            if(binding->cleanupScheduled) return;
            binding->cleanupScheduled=true;
            token=++binding->cleanupToken;
        }
        // Stop/invalidation directly performs the same cleanup if a hidden window
        // never runs this job. A discarded job's destructor acknowledges nothing.
        window->scheduleRenderJob(new CleanupJob(state,binding,token),QQuickWindow::BeforeSynchronizingStage);
        window->update();
    }
};
QuickImageItem::QuickImageItem(core::IClock& clock,QQuickItem* parent):QQuickItem(parent),impl_(std::make_unique<Impl>(clock)) {
    setFlag(ItemHasContents); setClip(true);
    connect(this,&QQuickItem::windowChanged,this,&QuickImageItem::bindWindow);
    connect(this,&QQuickItem::visibleChanged,this,&QuickImageItem::surfaceChanged);
    connect(this,&QQuickItem::opacityChanged,this,&QuickImageItem::surfaceChanged);
    connect(this,&QQuickItem::xChanged,this,&QuickImageItem::surfaceChanged);
    connect(this,&QQuickItem::yChanged,this,&QuickImageItem::surfaceChanged);
    connect(this,&QQuickItem::parentChanged,this,[this] {watchAncestors();surfaceChanged();});
    watchAncestors();
    bindWindow(window());
}
QuickImageItem::~QuickImageItem() {
    disconnect(this,nullptr,this,nullptr);
    for(const auto& connection:impl_->connections) QObject::disconnect(connection);
    for(const auto& connection:impl_->ancestorConnections) QObject::disconnect(connection);
    {
        std::lock_guard lock(impl_->state->mutex);
        if(impl_->binding) impl_->binding->active=false;
        impl_->state->dead=true; impl_->state->discardPending();
        impl_->state->armed.reset(); impl_->state->displayed.reset(); impl_->state->events={};
    }
    impl_->scheduleCleanup();
    // Window callbacks own transport, never this QObject, and survive until their
    // QObject context (the window) dies. The graph owns the resource root.
    if(impl_->window) impl_->window->removeEventFilter(this);
}
bool QuickImageItem::drawable() const {return drawable(impl_->mode,impl_->imageSize);}
bool QuickImageItem::drawable(DisplayMode mode,Size imageSize) const {
    auto* w=window();
    if(!w || !w->isVisible() || !w->isExposed() || w->visibility()==QWindow::Minimized ||
       !isVisible() || width()<=0 || height()<=0 || impl_->initializationError) return false;
    QRectF visible=mapRectToScene(boundingRect()).intersected(QRectF(0,0,w->width(),w->height()));
    for(const QQuickItem* item=this;item;item=item->parentItem()) {
        if(!item->isVisible() || item->opacity()<=0) return false;
        if(item->clip()) visible=visible.intersected(item->mapRectToScene(item->boundingRect()));
    }
    if(visible.isEmpty()) return false;
    if(imageSize.width<=0 || imageSize.height<=0) return true;
    const unsigned planes=mode==DisplayMode::Compare?2U:1U;
    const double paneWidth=width()/planes;
    auto transform=impl_->transform;
    transform.resize(imageSize,{paneWidth,height()});
    const auto origin=transform.imageToViewport({0,0});
    for(unsigned index=0;index<planes;++index) {
        const QRectF pane(index*paneWidth,0,paneWidth,height());
        const QRectF image(origin.x+index*paneWidth,origin.y,
            imageSize.width*transform.scale(),imageSize.height*transform.scale());
        if(mapRectToScene(image.intersected(pane)).intersected(visible).isEmpty()) return false;
    }
    return true;
}
void QuickImageItem::watchAncestors() {
    for(const auto& connection:impl_->ancestorConnections) QObject::disconnect(connection);
    impl_->ancestorConnections.clear();
    for(QQuickItem* ancestor=parentItem();ancestor;ancestor=ancestor->parentItem()) {
        const auto observe=[&](auto signal) {
            impl_->ancestorConnections.push_back(connect(ancestor,signal,this,&QuickImageItem::surfaceChanged));
        };
        observe(&QQuickItem::visibleChanged);observe(&QQuickItem::opacityChanged);
        observe(&QQuickItem::xChanged);observe(&QQuickItem::yChanged);
        observe(&QQuickItem::widthChanged);observe(&QQuickItem::heightChanged);
        observe(&QQuickItem::clipChanged);observe(&QQuickItem::scaleChanged);observe(&QQuickItem::rotationChanged);
        impl_->ancestorConnections.push_back(connect(ancestor,&QQuickItem::parentChanged,this,[this] {
            watchAncestors();surfaceChanged();
        }));
    }
}
bool QuickImageItem::ready() const {
    if(!drawable()) return false;
    std::lock_guard lock(impl_->state->mutex);
    const auto& s=*impl_->state;
    return !s.dead && !s.pending && !s.armed && !s.retirement && !s.events[0] &&
        impl_->binding && !impl_->binding->cleanupScheduled &&
        (s.metrics.textures==0 || (impl_->binding->root &&
            impl_->binding->root->count==s.metrics.textures));
}
core::Result<void> QuickImageItem::submit(PresentationSubmission submission) {
    auto valid=validatePresentation(submission); if(!valid.hasValue()) return valid;
    if(!ready()) return core::Result<void>::failure(rendererError("quick_not_ready"));
    const Size imageSize{static_cast<double>(submission.bundle->originalDisplay->layout.width()),
        static_cast<double>(submission.bundle->originalDisplay->layout.height())};
    if(!drawable(submission.mode,imageSize))
        return core::Result<void>::failure(rendererError("quick_planes_not_drawable"));
    auto& s=*impl_->state;
    const auto begin=s.clock.steadyNow();
    Prepared prepared{submission.ticket,submission.mode,{},submission.mode==DisplayMode::Compare?2U:1U,begin};
    const auto storage=assessStorage(submission.bundle->originalDisplay->layout.width(),submission.bundle->originalDisplay->layout.height(),submission.mode);
    if(!storage.hasValue()) return core::Result<void>::failure(storage.error());
    try {
        prepared.images[0]=rgbImage(submission.mode==DisplayMode::Enhanced ? *submission.bundle->enhancedDisplay : *submission.bundle->originalDisplay);
        if(prepared.count==2) prepared.images[1]=rgbImage(*submission.bundle->enhancedDisplay);
    } catch(const std::bad_alloc&) {return core::Result<void>::failure(rendererError("quick_conversion_allocation"));}
    for(std::size_t i=0;i<prepared.count;++i) if(prepared.images[i].isNull()) return core::Result<void>::failure(rendererError("quick_conversion_allocation"));
    {
        std::lock_guard lock(s.mutex);
        if(s.pending || s.armed || s.retirement || s.events[0] || s.dead || !impl_->binding || impl_->binding->cleanupScheduled)
            return core::Result<void>::failure(rendererError("quick_not_ready"));
        prepared.admittedAt=s.clock.steadyNow();
        s.metrics.preparation=prepared.admittedAt-begin;
        for(const auto& image:prepared.images) if(!image.isNull()) {
            ++s.metrics.conversionImages; s.metrics.requestedImageBytes+=static_cast<std::size_t>(image.sizeInBytes());
        }
        s.metrics.maximumConversionImages=std::max(s.metrics.maximumConversionImages,s.metrics.conversionImages);
        s.pending=std::move(prepared);
    }
    update(); return core::Result<void>::success();
}
bool QuickImageItem::cancelPending(PresentationTicket ticket) {
    std::lock_guard lock(impl_->state->mutex);
    if(!impl_->state->pending || impl_->state->pending->ticket!=ticket) return false;
    impl_->state->discardPending(); return true;
}
void QuickImageItem::retire(std::uint64_t id) {
    {
        std::lock_guard lock(impl_->state->mutex);
        auto& s=*impl_->state; s.retirement=id; s.events={}; s.discardPending(); s.armed.reset(); s.displayed.reset();
        s.acknowledge();
    }
    impl_->scheduleCleanup();
}
std::optional<PresentationEvent> QuickImageItem::takeEvent() {
    auto& s=*impl_->state; std::lock_guard lock(s.mutex);
    auto event=std::move(s.events[0]); s.events[0]=std::move(s.events[1]); s.events[1].reset();
    if(event && std::holds_alternative<PresentationReceipt>(*event))
        s.metrics.guiDelivery=s.clock.steadyNow()-std::get<PresentationReceipt>(*event).completedAt;
    return event;
}
void QuickImageItem::fit() {impl_->transform=ViewportTransform::fit(impl_->imageSize,{width()/(impl_->mode==DisplayMode::Compare?2:1),height()});update();}
void QuickImageItem::actualPixels() {impl_->transform=ViewportTransform::actualPixels(impl_->imageSize,{width()/(impl_->mode==DisplayMode::Compare?2:1),height()});update();}
void QuickImageItem::zoomAt(Point point,double factor) {
    // Geometry belongs to the consumed image, which can differ from a queued
    // submission or a presenter receipt still awaiting GUI delivery.
    if(impl_->mode==DisplayMode::Compare && point.x>=width()/2)
        point.x-=width()/2;
    impl_->transform.zoomAt(point,factor);update();
}
void QuickImageItem::panBy(Vector vector) {impl_->transform.panBy(vector);update();}
std::array<QRectF,2> QuickImageItem::imageRects() const {
    const auto point=impl_->transform.imageToViewport({0,0});
    QRectF first(point.x,point.y,impl_->imageSize.width*impl_->transform.scale(),impl_->imageSize.height*impl_->transform.scale());
    return {first,first.translated(width()/2,0)};
}
const std::optional<core::Error>& QuickImageItem::initializationError() const noexcept {
    return impl_->initializationError;
}
RendererMetrics QuickImageItem::metrics() const {std::lock_guard lock(impl_->state->mutex);return impl_->state->metrics;}
core::Result<RendererStorage> QuickImageItem::assessStorage(std::size_t width,std::size_t height,DisplayMode mode) {
    const std::size_t planes=mode==DisplayMode::Compare?2U:1U;
    const auto maximum=std::numeric_limits<std::size_t>::max();
    if(!width || !height || width>maximum/height || width*height>maximum/(8U*planes))
        return core::Result<RendererStorage>::failure(rendererError("quick_storage_overflow"));
    const auto pair=width*height*4U*planes;
    return core::Result<RendererStorage>::success({pair,pair,2U*pair});
}
QSGNode* QuickImageItem::updatePaintNode(QSGNode* old,UpdatePaintNodeData*) {
    auto state=impl_->state; auto binding=impl_->binding;
    if(!binding) return old;
    const bool visible=drawable();
    std::lock_guard lock(state->mutex);
    auto* root=static_cast<ImageRoot*>(old);
    if(!visible || state->retirement || state->dead) {
        state->lose("quick_not_drawable"); if(root) root->clear(); return root;
    }
    if(!root) {
        try {root=new ImageRoot(state,binding);binding->root=root;}
        catch(const std::bad_alloc&) {
            if(state->pending) {
                const auto ticket=state->pending->ticket;state->discardPending();
                state->push(PresentationFailure{ticket,rendererError("quick_node_allocation"),false});
            }
            return nullptr;
        }
    }
    if(state->pending) {
        const auto consumedAt=state->clock.steadyNow();
        auto& prepared=*state->pending;
        const Size nextSize{static_cast<double>(prepared.images[0].width()),static_cast<double>(prepared.images[0].height())};
        if(!drawable(prepared.mode,nextSize)) {
            state->lose("quick_planes_not_drawable");root->clear();return root;
        }
        std::array<QSGImageNode*,2> nodes{};
        std::array<QSGClipNode*,2> clips{};
        bool failed=false;
        try {
        for(std::size_t i=0;i<prepared.count;++i) {
            clips[i]=new QSGClipNode;clips[i]->setIsRectangular(true);
            clips[i]->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),4));
            clips[i]->setFlag(QSGNode::OwnsGeometry);
            std::unique_ptr<QSGTexture> texture(window()->createTextureFromImage(prepared.images[i],QQuickWindow::TextureIsOpaque));
            if(!texture) {failed=true;break;}
            nodes[i]=window()->createImageNode();
            if(!nodes[i]) {failed=true;break;}
            nodes[i]->setOwnsTexture(true); nodes[i]->setTexture(texture.release()); nodes[i]->setFiltering(QSGTexture::Nearest);
            ++state->metrics.textures;
            state->metrics.requestedTextureBytes+=static_cast<std::size_t>(prepared.images[i].sizeInBytes());
            state->metrics.maximumTextures=std::max(state->metrics.maximumTextures,state->metrics.textures);
        }
        } catch(const std::bad_alloc&) {failed=true;}
        if(failed) {
            for(std::size_t i=0;i<nodes.size();++i) if(nodes[i]) {
                delete nodes[i]; --state->metrics.textures;
                state->metrics.requestedTextureBytes-=static_cast<std::size_t>(prepared.images[i].sizeInBytes());
            }
            for(auto* clip:clips) delete clip;
            const auto ticket=prepared.ticket; state->discardPending();
            state->push(PresentationFailure{ticket,rendererError("quick_texture_allocation"),root->count!=0});
        } else {
            impl_->imageSize=nextSize;impl_->mode=prepared.mode;
            impl_->transform.resize(impl_->imageSize,{width()/(impl_->mode==DisplayMode::Compare?2:1),height()});
            root->clear();
            root->images=std::move(prepared.images);root->count=prepared.count;root->imageNodes=nodes;root->clips=clips;
            for(std::size_t i=0;i<root->count;++i) {
                root->clips[i]->appendChildNode(nodes[i]); root->appendChildNode(root->clips[i]);
            }
            state->armed=prepared.ticket;state->armedCycle=state->cycle;state->rendered=false;
            state->consumedAt=consumedAt;state->metrics.admissionToConsume=consumedAt-prepared.admittedAt;
            ++state->metrics.consumed; state->pending.reset();
        }
    }
    const auto rects=imageRects();
    const double paneWidth=width()/(root->count==2?2:1);
    for(std::size_t i=0;i<root->count;++i) {
        const QRectF clipRect(static_cast<double>(i)*paneWidth,0,paneWidth,height());
        root->clips[i]->setClipRect(clipRect);
        QSGGeometry::updateRectGeometry(root->clips[i]->geometry(),clipRect);
        root->clips[i]->markDirty(QSGNode::DirtyGeometry);
        root->imageNodes[i]->setRect(rects[i]);
        root->imageNodes[i]->setSourceRect(QRectF(QPointF(0,0),root->images[i].size()));
    }
    return root;
}
void QuickImageItem::geometryChange(const QRectF& next,const QRectF& previous) {
    QQuickItem::geometryChange(next,previous);
    if(!impl_) return;
    impl_->transform.resize(impl_->imageSize,{width()/(impl_->mode==DisplayMode::Compare?2:1),height()});
    surfaceChanged();update();
}
void QuickImageItem::releaseResources() {
    {
        std::lock_guard lock(impl_->state->mutex);
        impl_->state->lose("quick_resources_released");
    }
    impl_->scheduleCleanup();
}
bool QuickImageItem::eventFilter(QObject* object,QEvent* event) {
    if(event->type()==QEvent::Hide || event->type()==QEvent::Close || event->type()==QEvent::WindowStateChange || event->type()==QEvent::Expose)
        surfaceChanged();
    return QQuickItem::eventFilter(object,event);
}
void QuickImageItem::surfaceChanged() {
    if(drawable()) return;
    {
        std::lock_guard lock(impl_->state->mutex);
        impl_->state->lose("quick_surface_lost");
    }
    impl_->scheduleCleanup();
}
void QuickImageItem::bindWindow(QQuickWindow* window) {
    if(impl_->window==window && impl_->binding) return;
    if(impl_->window) {
        impl_->window->removeEventFilter(this);
        {std::lock_guard lock(impl_->state->mutex);impl_->state->lose("quick_window_changed");
         if(impl_->binding) impl_->binding->active=false;}
        impl_->scheduleCleanup();
    }
    for(const auto& connection:impl_->connections) QObject::disconnect(connection);
    impl_->connections.clear();
    impl_->window=window; impl_->initializationError.reset();
    if(!window) {impl_->binding.reset();return;}
    auto state=impl_->state;
    auto binding=std::make_shared<Binding>();
    {std::lock_guard lock(state->mutex);binding->epoch=++state->epoch;}
    impl_->binding=binding;window->installEventFilter(this);
    // Lifecycle callbacks deliberately have window lifetime, including after the
    // item moves/dies. Epoch checks fence tickets, cleanup targets only its root.
    auto stop=[state,binding] {
        {std::lock_guard lock(state->mutex);
         if(binding->epoch==state->epoch) {state->lose("quick_scene_graph_stopped");}}
        cleanup(state,binding);
    };
    binding->lifecycleConnections[0]=connect(window,&QQuickWindow::sceneGraphAboutToStop,window,stop,Qt::DirectConnection);
    binding->lifecycleConnections[1]=connect(window,&QQuickWindow::sceneGraphInvalidated,window,stop,Qt::DirectConnection);
    impl_->connections.push_back(connect(window,&QQuickWindow::beforeSynchronizing,window,[state,binding] {
        bool abandoned=false;
        {
            std::lock_guard lock(state->mutex);
            if(binding->epoch!=state->epoch || state->dead) return;
            ++state->cycle;
            if(state->armed) {state->lose("quick_unswapped_frame");abandoned=true;}
        }
        if(abandoned) cleanup(state,binding);
    },Qt::DirectConnection));
    impl_->connections.push_back(connect(window,&QQuickWindow::afterRendering,window,[state,binding] {
        std::lock_guard lock(state->mutex);
        if(binding->epoch==state->epoch && state->armed && state->armedCycle==state->cycle) state->rendered=true;
    },Qt::DirectConnection));
    impl_->connections.push_back(connect(window,&QQuickWindow::frameSwapped,window,[state,binding] {
        std::lock_guard lock(state->mutex);
        if(binding->epoch!=state->epoch || !state->armed || !state->rendered || state->armedCycle!=state->cycle || state->dead) return;
        const auto completedAt=state->clock.steadyNow();
        state->push(PresentationReceipt{*state->armed,completedAt});state->displayed=state->armed;
        state->armed.reset();state->rendered=false;
        state->metrics.consumeToSwap=completedAt-state->consumedAt;++state->metrics.completed;
    },Qt::DirectConnection));
    impl_->connections.push_back(connect(window,&QQuickWindow::afterFrameEnd,window,[state,binding] {
        bool abandoned=false;
        {
            std::lock_guard lock(state->mutex);
            if(binding->epoch==state->epoch && state->armed) {
                state->lose("quick_frame_ended_without_swap");abandoned=true;
            }
        }
        if(abandoned) cleanup(state,binding);
    },Qt::DirectConnection));
    impl_->connections.push_back(connect(window,&QQuickWindow::sceneGraphError,this,[this,binding](QQuickWindow::SceneGraphError error,const QString& message) {
        {
            std::lock_guard lock(impl_->state->mutex);
            if(binding->epoch!=impl_->state->epoch) return;
        }
        impl_->initializationError=core::Error{
            core::ErrorCategory::ResourceExhaustion,
            "quick_scene_graph_initialization_failed",
            "Image renderer initialization failed. Reopen the image window and check the graphics backend.",
            message.toStdString(),true,static_cast<std::int64_t>(error)};
        surfaceChanged();
    }));
    impl_->connections.push_back(connect(window,&QWindow::visibleChanged,this,[this](bool){surfaceChanged();}));
    impl_->connections.push_back(connect(window,&QWindow::visibilityChanged,this,[this](QWindow::Visibility){surfaceChanged();}));
}
}
