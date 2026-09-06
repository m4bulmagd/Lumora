#include <lumora/ui/ImageViewport.hpp>

#include <lumora/core/Error.hpp>
#include <lumora/core/PixelFormat.hpp>

#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace lumora::ui {
namespace {

constexpr double buttonZoomFactor = 1.2;

[[nodiscard]] core::Error invalidViewportFrame(
    std::string code,
    std::string detail) {
    return core::Error{
        core::ErrorCategory::InvalidFrame,
        std::move(code),
        "The frame cannot be displayed.",
        std::move(detail),
        false,
    };
}

[[nodiscard]] core::Error viewportAllocationError() {
    return core::Error{
        core::ErrorCategory::ResourceExhaustion,
        "viewport_image_allocation_failed",
        "The display image could not be prepared.",
        "Allocating immutable display ownership or a Qt image wrapper failed.",
        true,
    };
}

using FrameOwner = std::shared_ptr<const core::DisplayFrame>;

void releaseFrameOwner(void* context) {
    delete static_cast<FrameOwner*>(context);
}

struct ViewImage final {
    FrameOwner frame;
    QImage pixels;

    [[nodiscard]] Size size() const noexcept {
        return Size{
            static_cast<double>(frame->layout.width()),
            static_cast<double>(frame->layout.height()),
        };
    }
};

[[nodiscard]] core::Result<ViewImage> wrapFrame(FrameOwner frame) {
    if (!frame) {
        return core::Result<ViewImage>::failure(invalidViewportFrame(
            "viewport_frame_missing",
            "ImageViewport::present requires an immutable display frame."));
    }
    if (frame->storage != core::DisplayStorage::Gray8) {
        return core::Result<ViewImage>::failure(invalidViewportFrame(
            "viewport_storage_unsupported",
            "The evaluation viewport currently accepts Gray8 display pixels only."));
    }

    const auto width = frame->layout.width();
    const auto height = frame->layout.height();
    const auto stride = frame->layout.strideBytes();
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        stride > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max()) ||
        frame->layout.requiredBytes() >
            static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        return core::Result<ViewImage>::failure(invalidViewportFrame(
            "viewport_layout_unrepresentable",
            "The validated frame dimensions or row stride exceed Qt image limits."));
    }

    auto owner = std::unique_ptr<FrameOwner>{new (std::nothrow) FrameOwner(frame)};
    if (!owner) {
        return core::Result<ViewImage>::failure(viewportAllocationError());
    }

    QImage image(
        reinterpret_cast<const uchar*>(frame->pixels.bytes().data()),
        static_cast<int>(width),
        static_cast<int>(height),
        static_cast<qsizetype>(stride),
        QImage::Format_Grayscale8,
        releaseFrameOwner,
        owner.get());
    if (image.isNull()) {
        return core::Result<ViewImage>::failure(viewportAllocationError());
    }
    static_cast<void>(owner.release());

    return core::Result<ViewImage>::success(
        ViewImage{std::move(frame), std::move(image)});
}

[[nodiscard]] Size viewportSize(const QWidget& widget) noexcept {
    return Size{
        static_cast<double>(widget.width()),
        static_cast<double>(widget.height()),
    };
}

[[nodiscard]] bool sameSize(Size left, Size right) noexcept {
    return left.width == right.width && left.height == right.height;
}

}  // namespace

class ImageViewport::Impl final {
public:
    [[nodiscard]] const ViewImage* activeImage() const noexcept {
        if (pending) {
            return &*pending;
        }
        if (completed) {
            return &*completed;
        }
        return nullptr;
    }

    [[nodiscard]] Size activeSize() const noexcept {
        if (const auto* image = activeImage()) {
            return image->size();
        }
        return {0.0, 0.0};
    }

    void preserveCompletedTransformWhenVisible() {
        if (!pending && completed) {
            completedTransform = transform;
        }
    }

    std::optional<ViewImage> pending;
    std::optional<ViewImage> completed;
    std::optional<ViewportTransform> completedTransform;
    std::optional<std::uint64_t> completedFrameId;
    ViewportTransform transform = ViewportTransform::fit({0.0, 0.0}, {0.0, 0.0});
    PresentationObserver observer;
    QPointF lastMousePosition;
    bool dragging{false};
};

ImageViewport::ImageViewport(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>()) {
    setAutoFillBackground(false);
}

ImageViewport::~ImageViewport() = default;

core::Result<void> ImageViewport::present(FrameOwner frame) {
    auto wrapped = wrapFrame(std::move(frame));
    if (!wrapped.hasValue()) {
        return core::Result<void>::failure(std::move(wrapped).error());
    }

    const Size newSize = wrapped.value().size();
    if (const auto* active = impl_->activeImage();
        active == nullptr || !sameSize(active->size(), newSize)) {
        impl_->transform.resize(newSize, viewportSize(*this));
    }
    impl_->pending = std::move(wrapped).value();
    update();
    return core::Result<void>::success();
}

std::optional<std::uint64_t> ImageViewport::presentedFrameId() const noexcept {
    return impl_->completedFrameId;
}

void ImageViewport::setPresentationObserver(PresentationObserver observer) {
    impl_->observer = std::move(observer);
}

void ImageViewport::discardPendingPresentation() {
    if (!impl_->pending) {
        return;
    }

    impl_->pending.reset();
    if (impl_->completed && impl_->completedTransform) {
        impl_->transform = *impl_->completedTransform;
    } else {
        impl_->transform = ViewportTransform::fit(
            {0.0, 0.0}, viewportSize(*this));
    }
    update();
}

void ImageViewport::clear() {
    impl_->pending.reset();
    impl_->completed.reset();
    impl_->completedTransform.reset();
    impl_->completedFrameId.reset();
    impl_->dragging = false;
    impl_->transform = ViewportTransform::fit(
        {0.0, 0.0}, viewportSize(*this));
    update();
}

void ImageViewport::setFitMode() {
    impl_->transform = ViewportTransform::fit(
        impl_->activeSize(), viewportSize(*this));
    impl_->preserveCompletedTransformWhenVisible();
    update();
}

void ImageViewport::setActualPixels() {
    impl_->transform = ViewportTransform::actualPixels(
        impl_->activeSize(), viewportSize(*this));
    impl_->preserveCompletedTransformWhenVisible();
    update();
}

void ImageViewport::zoomIn() {
    impl_->transform.zoomAt(
        {static_cast<double>(width()) / 2.0,
         static_cast<double>(height()) / 2.0},
        buttonZoomFactor);
    impl_->preserveCompletedTransformWhenVisible();
    update();
}

void ImageViewport::zoomOut() {
    impl_->transform.zoomAt(
        {static_cast<double>(width()) / 2.0,
         static_cast<double>(height()) / 2.0},
        1.0 / buttonZoomFactor);
    impl_->preserveCompletedTransformWhenVisible();
    update();
}

const ViewportTransform& ImageViewport::transform() const noexcept {
    return impl_->transform;
}

void ImageViewport::paintEvent(QPaintEvent* event) {
    static_cast<void>(event);
    bool completedPendingPaint = false;
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor{22, 24, 28});

        const auto* image = impl_->activeImage();
        if (image != nullptr && impl_->transform.drawable()) {
            const auto origin = impl_->transform.imageToViewport({0.0, 0.0});
            const auto scale = impl_->transform.scale();
            const QRectF target{
                origin.x,
                origin.y,
                image->size().width * scale,
                image->size().height * scale,
            };
            painter.setRenderHint(
                QPainter::SmoothPixmapTransform, scale != 1.0);
            painter.drawImage(target, image->pixels, QRectF{image->pixels.rect()});
            completedPendingPaint = impl_->pending.has_value();
        }
    }

    if (!completedPendingPaint) {
        return;
    }

    impl_->completed = std::move(impl_->pending);
    impl_->pending.reset();
    impl_->completedFrameId = impl_->completed->frame->sourceFrameId;
    impl_->completedTransform = impl_->transform;
    if (impl_->observer) {
        impl_->observer(impl_->completed->frame);
    }
}

void ImageViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const auto newViewport = viewportSize(*this);
    if (impl_->pending) {
        impl_->transform.resize(impl_->pending->size(), newViewport);
        if (impl_->completed && impl_->completedTransform) {
            impl_->completedTransform->resize(
                impl_->completed->size(), newViewport);
        }
        return;
    }

    impl_->transform.resize(impl_->activeSize(), newViewport);
    impl_->preserveCompletedTransformWhenVisible();
}

void ImageViewport::wheelEvent(QWheelEvent* event) {
    const auto steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (steps == 0.0) {
        event->ignore();
        return;
    }

    impl_->transform.zoomAt(
        {event->position().x(), event->position().y()},
        std::pow(buttonZoomFactor, steps));
    impl_->preserveCompletedTransformWhenVisible();
    update();
    event->accept();
}

void ImageViewport::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    impl_->dragging = true;
    impl_->lastMousePosition = event->position();
    event->accept();
}

void ImageViewport::mouseMoveEvent(QMouseEvent* event) {
    if (!impl_->dragging || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const auto delta = event->position() - impl_->lastMousePosition;
    impl_->lastMousePosition = event->position();
    impl_->transform.panBy({delta.x(), delta.y()});
    impl_->preserveCompletedTransformWhenVisible();
    update();
    event->accept();
}

void ImageViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    impl_->dragging = false;
    event->accept();
}

void ImageViewport::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    setFitMode();
    event->accept();
}

}  // namespace lumora::ui
