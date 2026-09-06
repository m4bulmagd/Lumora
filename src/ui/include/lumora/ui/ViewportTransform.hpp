#pragma once

namespace lumora::ui {

struct Size final {
    double width;
    double height;
};

struct Point final {
    double x;
    double y;
};

struct Vector final {
    double x;
    double y;
};

enum class ViewScaleMode {
    Fit,
    Manual,
};

class ViewportTransform final {
public:
    [[nodiscard]] static ViewportTransform fit(Size image, Size viewport);
    [[nodiscard]] static ViewportTransform actualPixels(Size image, Size viewport);

    void resize(Size image, Size viewport);
    void zoomAt(Point viewportPoint, double factor);
    void panBy(Vector delta);

    [[nodiscard]] double scale() const noexcept;
    [[nodiscard]] Point imageCenterInViewport() const noexcept;
    [[nodiscard]] Point imageToViewport(Point imagePoint) const noexcept;
    [[nodiscard]] ViewScaleMode mode() const noexcept;
    [[nodiscard]] bool drawable() const noexcept;

private:
    explicit ViewportTransform(ViewScaleMode mode) noexcept;

    Size image_{0.0, 0.0};
    Size viewport_{0.0, 0.0};
    double scale_{1.0};
    Point origin_{0.0, 0.0};
    ViewScaleMode mode_;
};

}  // namespace lumora::ui
