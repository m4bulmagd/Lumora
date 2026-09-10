#include <lumora/ui/ImageViewport.hpp>

#include <lumora/core/Error.hpp>
#include <lumora/core/PixelFormat.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
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

[[nodiscard]] Size paneSize(const QWidget& widget, DisplayMode mode) noexcept {
    auto size = viewportSize(widget);
    if (mode == DisplayMode::Compare) {
        size.width = static_cast<double>(std::max(0, (widget.width() - 1) / 2));
    }
    return size;
}

struct PreparedPresentation final {
    ViewportPresentation receipt;
    std::optional<ViewImage> original;
    std::optional<ViewImage> enhanced;

    [[nodiscard]] Size size() const noexcept {
        return {static_cast<double>(receipt.originalDisplay->layout.width()),
                static_cast<double>(receipt.originalDisplay->layout.height())};
    }
};

[[nodiscard]] core::Result<PreparedPresentation> preparePresentation(
    ViewportPresentation presentation) {
    const auto& original = presentation.originalDisplay;
    const auto& enhanced = presentation.enhancedDisplay;
    if (!original || (presentation.mode != DisplayMode::Original && !enhanced)) {
        return core::Result<PreparedPresentation>::failure(invalidViewportFrame(
            "viewport_frame_missing", "The selected mode requires its display planes."));
    }
    if (enhanced) {
        if (original->sourceFrameId != enhanced->sourceFrameId) {
            return core::Result<PreparedPresentation>::failure(invalidViewportFrame(
                "comparison_frame_mismatch", "Comparison planes must have the same source frame ID."));
        }
        if (original->layout.width() != enhanced->layout.width() ||
            original->layout.height() != enhanced->layout.height() ||
            original->presentationOrientation != enhanced->presentationOrientation ||
            original->mapping != enhanced->mapping) {
            return core::Result<PreparedPresentation>::failure(invalidViewportFrame(
                "comparison_display_mismatch",
                "Comparison planes must have identical dimensions, orientation and display mapping."));
        }
    }
    if (presentation.mode != DisplayMode::Original &&
        presentation.mode != DisplayMode::Enhanced &&
        presentation.mode != DisplayMode::Compare) {
        return core::Result<PreparedPresentation>::failure(invalidViewportFrame(
            "viewport_mode_invalid", "The requested display mode is unknown."));
    }

    PreparedPresentation prepared{std::move(presentation), std::nullopt, std::nullopt};
    if (prepared.receipt.mode != DisplayMode::Enhanced) {
        auto wrapped = wrapFrame(prepared.receipt.originalDisplay);
        if (!wrapped.hasValue()) {
            return core::Result<PreparedPresentation>::failure(std::move(wrapped).error());
        }
        prepared.original = std::move(wrapped).value();
    }
    if (prepared.receipt.mode != DisplayMode::Original) {
        auto wrapped = wrapFrame(prepared.receipt.enhancedDisplay);
        if (!wrapped.hasValue()) {
            return core::Result<PreparedPresentation>::failure(std::move(wrapped).error());
        }
        prepared.enhanced = std::move(wrapped).value();
    }
    return core::Result<PreparedPresentation>::success(std::move(prepared));
}

}  // namespace

class ImageViewport::Impl final {
public:
    [[nodiscard]] const PreparedPresentation* activePresentation() const noexcept {
        if (pending) {
            return &*pending;
        }
        if (completed) {
            return &*completed;
        }
        return nullptr;
    }

    [[nodiscard]] Size activeSize() const noexcept {
        if (const auto* presentation = activePresentation()) {
            return presentation->size();
        }
        return {0.0, 0.0};
    }

    [[nodiscard]] DisplayMode activeMode() const noexcept {
        const auto* presentation = activePresentation();
        return presentation ? presentation->receipt.mode : DisplayMode::Original;
    }

    [[nodiscard]] std::optional<Point> panePoint(
        const QWidget& widget, QPointF position) const noexcept {
        if (activeMode() == DisplayMode::Compare) {
            const auto paneWidth = paneSize(widget, activeMode()).width;
            const auto rightOrigin = static_cast<double>(widget.width()) - paneWidth;
            if (position.x() >= rightOrigin) {
                position.setX(position.x() - rightOrigin);
            } else if (position.x() >= paneWidth) {
                return std::nullopt;
            }
        }
        return Point{position.x(), position.y()};
    }

    void preserveCompletedTransformWhenVisible() {
        if (!pending && completed) {
            completedTransform = transform;
        }
    }

    std::optional<PreparedPresentation> pending;
    std::optional<PreparedPresentation> completed;
    std::optional<ViewportTransform> completedTransform;
    std::optional<std::uint64_t> completedFrameId;
    ViewportTransform transform = ViewportTransform::fit({0.0, 0.0}, {0.0, 0.0});
    CompletionObserver observer;
    QPointF lastMousePosition;
    bool dragging{false};
};

ImageViewport::ImageViewport(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>()) {
    setAutoFillBackground(false);
}

ImageViewport::~ImageViewport() = default;

core::Result<void> ImageViewport::present(ViewportPresentation presentation) {
    auto prepared = preparePresentation(std::move(presentation));
    if (!prepared.hasValue()) {
        return core::Result<void>::failure(std::move(prepared).error());
    }

    const Size newSize = prepared.value().size();
    const auto newMode = prepared.value().receipt.mode;
    if (const auto* active = impl_->activePresentation();
        active == nullptr || !sameSize(active->size(), newSize) ||
        !sameSize(paneSize(*this, active->receipt.mode), paneSize(*this, newMode))) {
        impl_->transform.resize(newSize, paneSize(*this, newMode));
    }
    impl_->pending = std::move(prepared).value();
    update();
    return core::Result<void>::success();
}

core::Result<void> ImageViewport::present(FrameOwner frame) {
    return present(ViewportPresentation{0U, DisplayMode::Original, std::move(frame), nullptr});
}

void ImageViewport::setCompletionObserver(CompletionObserver observer) {
    impl_->observer = std::move(observer);
}

std::optional<std::uint64_t> ImageViewport::presentedFrameId() const noexcept {
    return impl_->completedFrameId;
}

void ImageViewport::setPresentationObserver(PresentationObserver observer) {
    if (!observer) {
        setCompletionObserver({});
        return;
    }
    setCompletionObserver([observer = std::move(observer)](const ViewportPresentation& receipt) {
        observer(receipt.mode == DisplayMode::Enhanced
            ? receipt.enhancedDisplay : receipt.originalDisplay);
    });
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
        impl_->activeSize(), paneSize(*this, impl_->activeMode()));
    impl_->preserveCompletedTransformWhenVisible();
    update();
}

void ImageViewport::setActualPixels() {
    impl_->transform = ViewportTransform::actualPixels(
        impl_->activeSize(), paneSize(*this, impl_->activeMode()));
    impl_->preserveCompletedTransformWhenVisible();
    update();
}

void ImageViewport::zoomIn() {
    impl_->transform.zoomAt(
        {paneSize(*this, impl_->activeMode()).width / 2.0,
         static_cast<double>(height()) / 2.0},
        buttonZoomFactor);
    impl_->preserveCompletedTransformWhenVisible();
    update();
}

void ImageViewport::zoomOut() {
    impl_->transform.zoomAt(
        {paneSize(*this, impl_->activeMode()).width / 2.0,
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

        const auto* presentation = impl_->activePresentation();
        if (presentation != nullptr && impl_->transform.drawable()) {
            const auto origin = impl_->transform.imageToViewport({0.0, 0.0});
            const auto scale = impl_->transform.scale();
            painter.setRenderHint(
                QPainter::SmoothPixmapTransform, scale != 1.0);
            const auto bounds = paneSize(*this, presentation->receipt.mode);
            const auto drawPlane = [&](const ViewImage& image, double offset) {
                painter.save();
                painter.setClipRect(QRectF{offset, 0.0, bounds.width, bounds.height});
                const QRectF target{
                    offset + origin.x, origin.y,
                    image.size().width * scale, image.size().height * scale};
                painter.drawImage(target, image.pixels, QRectF{image.pixels.rect()});
                painter.restore();
            };
            if (presentation->receipt.mode == DisplayMode::Compare) {
                const auto rightOrigin = static_cast<double>(width()) - bounds.width;
                drawPlane(*presentation->original, 0.0);
                drawPlane(*presentation->enhanced, rightOrigin);
                painter.fillRect(
                    QRectF{bounds.width, 0.0, rightOrigin - bounds.width, bounds.height},
                    QColor{75, 80, 88});
                const auto drawLabel = [&](double offset, const QString& label) {
                    painter.save();
                    painter.setClipRect(QRectF{offset, 0.0, bounds.width, bounds.height});
                    const QRectF labelBounds{offset + 4.0, 4.0, bounds.width - 8.0, 22.0};
                    painter.fillRect(labelBounds, QColor{22, 24, 28, 210});
                    painter.setPen(QColor{238, 240, 244});
                    painter.drawText(labelBounds, Qt::AlignCenter, label);
                    painter.restore();
                };
                drawLabel(0.0, QCoreApplication::translate("lumora::ui::ImageViewport", "Original"));
                drawLabel(rightOrigin, QCoreApplication::translate("lumora::ui::ImageViewport", "Enhanced"));
            } else {
                drawPlane(presentation->receipt.mode == DisplayMode::Original
                    ? *presentation->original : *presentation->enhanced, 0.0);
            }
            completedPendingPaint = impl_->pending.has_value();
        }
    }

    if (!completedPendingPaint) {
        return;
    }

    impl_->completed = std::move(impl_->pending);
    impl_->pending.reset();
    impl_->completedFrameId = impl_->completed->receipt.originalDisplay->sourceFrameId;
    impl_->completedTransform = impl_->transform;
    if (impl_->observer) {
        const auto receipt = impl_->completed->receipt;
        impl_->observer(receipt);
    }
}

void ImageViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const auto newViewport = paneSize(*this, impl_->activeMode());
    if (impl_->pending) {
        impl_->transform.resize(impl_->pending->size(), newViewport);
        if (impl_->completed && impl_->completedTransform) {
            impl_->completedTransform->resize(
                impl_->completed->size(), paneSize(*this, impl_->completed->receipt.mode));
        }
        return;
    }

    impl_->transform.resize(impl_->activeSize(), newViewport);
    impl_->preserveCompletedTransformWhenVisible();
}

void ImageViewport::wheelEvent(QWheelEvent* event) {
    const auto steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    const auto point = impl_->panePoint(*this, event->position());
    if (steps == 0.0 || !point) {
        event->ignore();
        return;
    }

    impl_->transform.zoomAt(
        *point,
        std::pow(buttonZoomFactor, steps));
    impl_->preserveCompletedTransformWhenVisible();
    update();
    event->accept();
}

void ImageViewport::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton ||
        !impl_->panePoint(*this, event->position())) {
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
