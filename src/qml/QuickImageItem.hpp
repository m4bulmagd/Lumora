#pragma once
#include <lumora/core/Clock.hpp>
#include <lumora/presentation/PresentationProtocol.hpp>
#include <lumora/presentation/ViewportTransform.hpp>
#include <QQuickItem>
#include <array>
#include <memory>

namespace lumora::qml {
struct RendererMetrics {
    std::size_t textures{}, maximumTextures{}, conversionImages{}, maximumConversionImages{};
    std::size_t requestedTextureBytes{}, requestedImageBytes{};
    std::chrono::nanoseconds preparation{}, admissionToConsume{}, consumeToSwap{}, guiDelivery{};
    std::uint64_t consumed{}, completed{};
};
struct RendererStorage {
    std::size_t currentImageBytes{}, replacementImageBytes{}, nominalTextureBytes{};
};
// C++ only experiment surface. The supplied clock must outlive render cleanup.
// The host keeps the item/window alive through explicit retirement completion.
class QuickImageItem final : public QQuickItem, public presentation::IPresentationSink {
public:
    explicit QuickImageItem(core::IClock&, QQuickItem* parent = nullptr);
    ~QuickImageItem() override;
    bool ready() const override;
    core::Result<void> submit(presentation::PresentationSubmission) override;
    bool cancelPending(presentation::PresentationTicket) override;
    void retire(std::uint64_t) override;
    std::optional<presentation::PresentationEvent> takeEvent() override;
    void fit();
    void actualPixels();
    // Item-local coordinates; the sink maps either Compare pane to its shared
    // image transform using the currently consumed display mode.
    void zoomAt(presentation::Point, double);
    void panBy(presentation::Vector);
    [[nodiscard]] std::array<QRectF,2> imageRects() const;
    [[nodiscard]] RendererMetrics metrics() const;
    // Frontend-thread diagnostic, including failures before a ticket exists.
    [[nodiscard]] const std::optional<core::Error>& initializationError() const noexcept;
    [[nodiscard]] static core::Result<RendererStorage> assessStorage(std::size_t width, std::size_t height, presentation::DisplayMode);
protected:
    QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF&, const QRectF&) override;
    void releaseResources() override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool drawable() const;
    bool drawable(presentation::DisplayMode, presentation::Size) const;
    void watchAncestors();
    void bindWindow(QQuickWindow*);
    void surfaceChanged();
};
}
