#include <lumora/ui/ViewportTransform.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace lumora::ui {
namespace {

constexpr double minimumManualScale = 0.05;
constexpr double maximumManualScale = 32.0;

struct Geometry final {
    double scale;
    Point origin;
};

[[nodiscard]] bool valid(Size size) noexcept {
    return std::isfinite(size.width) && std::isfinite(size.height) &&
           size.width >= 0.0 && size.height >= 0.0;
}

[[nodiscard]] bool sizesDrawable(Size image, Size viewport) noexcept {
    return image.width > 0.0 && image.height > 0.0 && viewport.width > 0.0 &&
           viewport.height > 0.0;
}

[[nodiscard]] bool finite(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool finite(Vector vector) noexcept {
    return std::isfinite(vector.x) && std::isfinite(vector.y);
}

[[nodiscard]] std::optional<Point> clampedOrigin(
    Size image,
    Size viewport,
    double scale,
    Point candidate) {
    const Size scaled{image.width * scale, image.height * scale};
    if (!std::isfinite(scaled.width) || !std::isfinite(scaled.height) ||
        !finite(candidate)) {
        return std::nullopt;
    }

    const auto clampAxis = [](double scaledExtent, double viewportExtent, double origin) {
        if (scaledExtent < viewportExtent) {
            return (viewportExtent - scaledExtent) / 2.0;
        }
        return std::clamp(origin, viewportExtent - scaledExtent, 0.0);
    };

    const Point result{
        clampAxis(scaled.width, viewport.width, candidate.x),
        clampAxis(scaled.height, viewport.height, candidate.y),
    };
    if (!finite(result)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<Geometry> fitGeometry(Size image, Size viewport) {
    const double scale = std::min(
        viewport.width / image.width,
        viewport.height / image.height);
    const Size scaled{image.width * scale, image.height * scale};
    if (!std::isfinite(scale) || scale <= 0.0 || !std::isfinite(scaled.width) ||
        !std::isfinite(scaled.height)) {
        return std::nullopt;
    }

    const Point origin{
        (viewport.width - scaled.width) / 2.0,
        (viewport.height - scaled.height) / 2.0,
    };
    if (!finite(origin)) {
        return std::nullopt;
    }
    return Geometry{scale, origin};
}

}  // namespace

ViewportTransform::ViewportTransform(ViewScaleMode mode) noexcept : mode_(mode) {}

ViewportTransform ViewportTransform::fit(Size image, Size viewport) {
    auto transform = ViewportTransform{ViewScaleMode::Fit};
    if (!valid(image) || !valid(viewport)) {
        return transform;
    }

    if (!sizesDrawable(image, viewport)) {
        transform.image_ = image;
        transform.viewport_ = viewport;
        return transform;
    }

    const auto geometry = fitGeometry(image, viewport);
    if (!geometry) {
        return transform;
    }
    transform.image_ = image;
    transform.viewport_ = viewport;
    transform.scale_ = geometry->scale;
    transform.origin_ = geometry->origin;
    return transform;
}

ViewportTransform ViewportTransform::actualPixels(Size image, Size viewport) {
    auto transform = ViewportTransform{ViewScaleMode::Manual};
    if (!valid(image) || !valid(viewport)) {
        return transform;
    }

    const Point centered{
        viewport.width / 2.0 - image.width / 2.0,
        viewport.height / 2.0 - image.height / 2.0,
    };
    const auto origin = clampedOrigin(image, viewport, transform.scale_, centered);
    if (!origin) {
        return transform;
    }
    transform.image_ = image;
    transform.viewport_ = viewport;
    transform.origin_ = *origin;
    return transform;
}

void ViewportTransform::resize(Size image, Size viewport) {
    if (!valid(image) || !valid(viewport)) {
        return;
    }

    if (mode_ == ViewScaleMode::Fit) {
        if (!sizesDrawable(image, viewport)) {
            const auto newOrigin = clampedOrigin(image, viewport, scale_, {0.0, 0.0});
            if (!newOrigin) {
                return;
            }
            image_ = image;
            viewport_ = viewport;
            origin_ = *newOrigin;
            return;
        }

        const auto geometry = fitGeometry(image, viewport);
        if (!geometry) {
            return;
        }
        image_ = image;
        viewport_ = viewport;
        scale_ = geometry->scale;
        origin_ = geometry->origin;
        return;
    }

    Point source{
        (viewport_.width / 2.0 - origin_.x) / scale_,
        (viewport_.height / 2.0 - origin_.y) / scale_,
    };
    if (!finite(source)) {
        return;
    }
    source.x = std::clamp(source.x, 0.0, image.width);
    source.y = std::clamp(source.y, 0.0, image.height);

    const Point candidate{
        viewport.width / 2.0 - source.x * scale_,
        viewport.height / 2.0 - source.y * scale_,
    };
    const auto newOrigin = clampedOrigin(image, viewport, scale_, candidate);
    if (!newOrigin) {
        return;
    }

    image_ = image;
    viewport_ = viewport;
    origin_ = *newOrigin;
}

void ViewportTransform::zoomAt(Point viewportPoint, double factor) {
    if (!finite(viewportPoint) || !std::isfinite(factor) || factor <= 0.0 ||
        factor == 1.0 || !std::isfinite(scale_) || scale_ <= 0.0) {
        return;
    }

    const double requestedScale = scale_ * factor;
    if (!std::isfinite(requestedScale) || requestedScale <= 0.0) {
        return;
    }
    const double newScale =
        std::clamp(requestedScale, minimumManualScale, maximumManualScale);
    if ((factor > 1.0 && newScale <= scale_) ||
        (factor < 1.0 && newScale >= scale_)) {
        return;
    }

    const Point source{
        (viewportPoint.x - origin_.x) / scale_,
        (viewportPoint.y - origin_.y) / scale_,
    };
    const Point candidate{
        viewportPoint.x - source.x * newScale,
        viewportPoint.y - source.y * newScale,
    };
    const auto newOrigin = clampedOrigin(image_, viewport_, newScale, candidate);
    if (!finite(source) || !newOrigin) {
        return;
    }

    scale_ = newScale;
    origin_ = *newOrigin;
    mode_ = ViewScaleMode::Manual;
}

void ViewportTransform::panBy(Vector delta) {
    if (!finite(delta)) {
        return;
    }

    const Point candidate{origin_.x + delta.x, origin_.y + delta.y};
    if (const auto newOrigin = clampedOrigin(image_, viewport_, scale_, candidate)) {
        origin_ = *newOrigin;
    }
}

double ViewportTransform::scale() const noexcept {
    return scale_;
}

Point ViewportTransform::imageCenterInViewport() const noexcept {
    return imageToViewport({image_.width / 2.0, image_.height / 2.0});
}

Point ViewportTransform::imageToViewport(Point imagePoint) const noexcept {
    return {origin_.x + imagePoint.x * scale_, origin_.y + imagePoint.y * scale_};
}

ViewScaleMode ViewportTransform::mode() const noexcept {
    return mode_;
}

bool ViewportTransform::drawable() const noexcept {
    return sizesDrawable(image_, viewport_);
}

}  // namespace lumora::ui
