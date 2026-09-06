#pragma once

#include <lumora/core/Frame.hpp>
#include <lumora/core/Result.hpp>
#include <lumora/ui/ViewportTransform.hpp>

#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace lumora::ui {

class ImageViewport final : public QWidget {
public:
    using PresentationObserver =
        std::function<void(std::shared_ptr<const core::DisplayFrame>)>;

    explicit ImageViewport(QWidget* parent = nullptr);
    ~ImageViewport() override;

    core::Result<void> present(std::shared_ptr<const core::DisplayFrame> frame);
    [[nodiscard]] std::optional<std::uint64_t> presentedFrameId() const noexcept;
    void setPresentationObserver(PresentationObserver observer);
    void discardPendingPresentation();
    void clear();
    void setFitMode();
    void setActualPixels();
    void zoomIn();
    void zoomOut();
    [[nodiscard]] const ViewportTransform& transform() const noexcept;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::ui
