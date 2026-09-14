#pragma once

#include "ViewerAdapter.hpp"
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace lumora::qml {
// A visual attachment only: deletion detaches the application-owned sink.
class ViewerSurface : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(lumora::qml::ViewerAdapter* viewer READ viewer WRITE setViewer NOTIFY viewerChanged REQUIRED FINAL)
public:
    explicit ViewerSurface(QQuickItem* parent = nullptr);
    ~ViewerSurface() override;
    [[nodiscard]] ViewerAdapter* viewer() const noexcept;
    void setViewer(ViewerAdapter* viewer);
signals:
    void viewerChanged();
protected:
    void geometryChange(const QRectF&, const QRectF&) override;
private:
    void detach();
    QPointer<ViewerAdapter> viewer_;
    QMetaObject::Connection destroyedConnection_;
};
}
