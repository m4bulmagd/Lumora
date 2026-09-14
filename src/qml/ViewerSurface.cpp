#include "ViewerSurface.hpp"
#include "QuickImageItem.hpp"

namespace lumora::qml {
ViewerSurface::ViewerSurface(QQuickItem* parent) : QQuickItem(parent) { setClip(true); }
ViewerSurface::~ViewerSurface() { detach(); }
ViewerAdapter* ViewerSurface::viewer() const noexcept { return viewer_; }
void ViewerSurface::detach() {
    QObject::disconnect(destroyedConnection_);
    if (viewer_ && viewer_->imageItem().parentItem() == this)
        viewer_->imageItem().setParentItem(nullptr);
    viewer_.clear();
}
void ViewerSurface::setViewer(ViewerAdapter* viewer) {
    if (viewer_ == viewer) return;
    detach();
    viewer_ = viewer;
    if (viewer_) {
        auto& item = viewer_->imageItem();
        item.setParentItem(this);
        item.setPosition({0, 0});
        item.setSize(size());
        // The sink's QObject parent remains null; setParentItem transfers only
        // the visual attachment, so destroying a QML root cannot destroy it.
        destroyedConnection_ = connect(viewer_, &QObject::destroyed, this, [this] {
            viewer_.clear();
            emit viewerChanged();
        });
    }
    emit viewerChanged();
}
void ViewerSurface::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickItem::geometryChange(next, previous);
    if (viewer_ && viewer_->imageItem().parentItem() == this)
        viewer_->imageItem().setSize(next.size());
}
}
