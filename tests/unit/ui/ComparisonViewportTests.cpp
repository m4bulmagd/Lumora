#include <lumora/ui/ImageViewport.hpp>

#include "ViewportTestSupport.hpp"

#include <lumora/core/BufferPool.hpp>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QTranslator>
#include <QWheelEvent>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

using lumora::core::DisplayFrame;
using lumora::ui::DisplayMode;
using lumora::ui::ImageViewport;
using lumora::ui::ViewportPresentation;
using lumora::ui::ViewScaleMode;
using FrameOwner = std::shared_ptr<const DisplayFrame>;

constexpr QRgb background = qRgb(22, 24, 28);

FrameOwner makePlane(
    std::uint64_t id, std::byte value, std::uint32_t width = 100U,
    std::uint32_t height = 100U,
    lumora::core::Orientation orientation = {
        false, false, lumora::core::Rotation::Degrees0},
    lumora::core::DisplayMapping mapping = {0U, 255U, 255U, 1U},
    lumora::core::DisplayStorage storage = lumora::core::DisplayStorage::Gray8) {
    const bool gray16 = storage == lumora::core::DisplayStorage::Gray16;
    const auto rowBytes = static_cast<std::size_t>(width) * (gray16 ? 2U : 1U);
    const auto stride = (rowBytes + 3U) & ~std::size_t{3U};
    const auto layout = lumora::core::ImageLayout::create(
        width, height, stride,
        gray16 ? lumora::core::StorageType::UInt16 : lumora::core::StorageType::UInt8,
        stride * static_cast<std::size_t>(height)).value();
    auto pool = lumora::core::BufferPool::create(1U, layout.payloadBytes()).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), value);
    return DisplayFrame::create(
        id, layout, std::move(*lease).seal(), storage, mapping, orientation).value();
}

ViewportPresentation comparison(std::uint64_t id, std::uint64_t token = 1U) {
    return {token, DisplayMode::Compare,
            makePlane(id, std::byte{40}), makePlane(id, std::byte{210})};
}

void expectGray(const QImage& image, int x, int y, int value) {
    EXPECT_EQ(image.pixelColor(x, y), QColor(value, value, value));
}

void wheelAt(ImageViewport& viewport, QPointF position) {
    QWheelEvent wheel(position, position, QPoint{}, QPoint{0, 120},
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&viewport, &wheel);
}

TEST(ComparisonViewport, DrawsDifferentPlanesBeforeIssuingOneExactReceipt) {
    ImageViewport viewport;
    viewport.resize(302, 200);
    const auto submitted = comparison(10U, 73U);
    QImage painted(302, 200, QImage::Format_ARGB32_Premultiplied);
    painted.fill(Qt::transparent);
    std::vector<ViewportPresentation> receipts;
    viewport.setCompletionObserver([&](const ViewportPresentation& receipt) {
        expectGray(painted, 75, 100, 40);
        expectGray(painted, 227, 100, 210);
        receipts.push_back(receipt);
    });

    ASSERT_TRUE(viewport.present(submitted).hasValue());
    EXPECT_TRUE(receipts.empty());
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);
    viewport.render(&painted);

    expectGray(painted, 75, 100, 40);
    expectGray(painted, 227, 100, 210);
    ASSERT_EQ(receipts.size(), 1U);
    EXPECT_EQ(receipts.front().token, 73U);
    EXPECT_EQ(receipts.front().mode, DisplayMode::Compare);
    EXPECT_EQ(receipts.front().originalDisplay, submitted.originalDisplay);
    EXPECT_EQ(receipts.front().enhancedDisplay, submitted.enhancedDisplay);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{10U});
    viewport.zoomIn();
    static_cast<void>(lumora::test::paintWidget(viewport));
    EXPECT_EQ(receipts.size(), 1U);
}

TEST(ComparisonViewport, SingleModesSelectTheRequestedPlaneAcrossFullCanvas) {
    ImageViewport viewport;
    viewport.resize(302, 200);
    auto presentation = comparison(11U);
    for (const auto mode : {DisplayMode::Original, DisplayMode::Enhanced}) {
        presentation.mode = mode;
        ASSERT_TRUE(viewport.present(presentation).hasValue());
        const auto painted = lumora::test::paintWidget(viewport);
        const int expected = mode == DisplayMode::Original ? 40 : 210;
        expectGray(painted, 75, 100, expected);
        expectGray(painted, 227, 100, expected);
        EXPECT_DOUBLE_EQ(viewport.transform().scale(), 2.0);
    }
}

TEST(ComparisonViewport, PaintedLabelsUseTheImageViewportTranslationContext) {
    ImageViewport viewport;
    viewport.resize(302, 200);
    ASSERT_TRUE(viewport.present(comparison(11U)).hasValue());
    const auto untranslated = lumora::test::paintWidget(viewport);

    class LabelTranslator final : public QTranslator {
    public:
        bool isEmpty() const override { return false; }
        QString translate(const char* context, const char* source, const char*, int) const override {
            const auto text = QString::fromUtf8(source);
            if (text != QStringLiteral("Original") && text != QStringLiteral("Enhanced")) {
                return {};
            }
            contexts.push_back(QString::fromUtf8(context));
            if (QString::fromUtf8(context) != QStringLiteral("lumora::ui::ImageViewport")) {
                return {};
            }
            if (text == QStringLiteral("Original")) {
                translatedOriginal = true;
                return QStringLiteral("Brut");
            }
            translatedEnhanced = true;
            // Different lengths remain visually distinct with the minimal plugin's fallback glyphs.
            return QStringLiteral("Traite");
        }

        mutable std::vector<QString> contexts;
        mutable bool translatedOriginal{false};
        mutable bool translatedEnhanced{false};
    } translator;
    ASSERT_TRUE(QCoreApplication::installTranslator(&translator));
    struct RemoveTranslator final {
        QTranslator* translator;
        ~RemoveTranslator() { QCoreApplication::removeTranslator(translator); }
    } remove{&translator};

    const auto translated = lumora::test::paintWidget(viewport);

    ASSERT_FALSE(translator.contexts.empty());
    for (const auto& context : translator.contexts) {
        EXPECT_EQ(context, QStringLiteral("lumora::ui::ImageViewport"));
    }
    EXPECT_TRUE(translator.translatedOriginal);
    EXPECT_TRUE(translator.translatedEnhanced);
    EXPECT_NE(translated.copy(4, 4, 142, 22), untranslated.copy(4, 4, 142, 22));
    EXPECT_NE(translated.copy(156, 4, 142, 22), untranslated.copy(156, 4, 142, 22));
    expectGray(translated, 75, 100, 40);
    expectGray(translated, 227, 100, 210);
}

TEST(ComparisonViewport, RejectsMissingRequiredPlanes) {
    ImageViewport viewport;
    const auto frame = makePlane(12U, std::byte{40});
    for (const auto mode : {DisplayMode::Original, DisplayMode::Enhanced, DisplayMode::Compare}) {
        const auto missingOriginal = viewport.present(ViewportPresentation{1U, mode, nullptr, frame});
        ASSERT_FALSE(missingOriginal.hasValue());
        EXPECT_EQ(missingOriginal.error().category, lumora::core::ErrorCategory::InvalidFrame);
    }
    for (const auto mode : {DisplayMode::Enhanced, DisplayMode::Compare}) {
        const auto missingEnhanced = viewport.present(ViewportPresentation{2U, mode, frame, nullptr});
        ASSERT_FALSE(missingEnhanced.hasValue());
        EXPECT_EQ(missingEnhanced.error().category, lumora::core::ErrorCategory::InvalidFrame);
    }
    EXPECT_TRUE(viewport.present(ViewportPresentation{3U, DisplayMode::Original, frame, nullptr}).hasValue());
}

TEST(ComparisonViewport, RejectsMismatchedPairEvenWhenOriginalIsSelected) {
    ImageViewport viewport;
    viewport.resize(302, 200);
    const auto original = makePlane(20U, std::byte{40});
    const std::array<FrameOwner, 6> mismatches{
        makePlane(21U, std::byte{210}),
        makePlane(20U, std::byte{210}, 101U),
        makePlane(20U, std::byte{210}, 100U, 101U),
        makePlane(20U, std::byte{210}, 100U, 100U,
            {true, false, lumora::core::Rotation::Degrees0}),
        makePlane(20U, std::byte{210}, 100U, 100U,
            {false, false, lumora::core::Rotation::Degrees0}, {1U, 255U, 255U, 1U}),
        makePlane(20U, std::byte{210}, 100U, 100U,
            {false, false, lumora::core::Rotation::Degrees0}, {0U, 255U, 255U, 2U}),
    };
    for (const auto mode : {DisplayMode::Original, DisplayMode::Enhanced, DisplayMode::Compare}) {
        for (const auto& enhanced : mismatches) {
            const auto rejected = viewport.present(ViewportPresentation{1U, mode, original, enhanced});
            ASSERT_FALSE(rejected.hasValue());
            EXPECT_EQ(rejected.error().category, lumora::core::ErrorCategory::InvalidFrame);
            if (enhanced->sourceFrameId != original->sourceFrameId) {
                EXPECT_EQ(rejected.error().code, "comparison_frame_mismatch");
            }
        }
    }
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);
}

TEST(ComparisonViewport, SecondPlaneRejectionPreservesPendingPixelsTokenAndTransform) {
    ImageViewport viewport;
    viewport.resize(302, 200);
    ASSERT_TRUE(viewport.present(comparison(30U, 101U)).hasValue());
    static_cast<void>(lumora::test::paintWidget(viewport));
    ASSERT_TRUE(viewport.present(comparison(31U, 102U)).hasValue());
    viewport.setActualPixels();
    const auto before = viewport.transform().imageToViewport({0.0, 0.0});
    std::uint64_t completedToken = 0U;
    viewport.setCompletionObserver([&](const ViewportPresentation& receipt) {
        completedToken = receipt.token;
    });
    const auto rejected = viewport.present(ViewportPresentation{
        103U, DisplayMode::Compare, makePlane(32U, std::byte{60}, 200U, 200U),
        makePlane(32U, std::byte{230}, 200U, 200U,
            {false, false, lumora::core::Rotation::Degrees0}, {0U, 255U, 255U, 1U},
            lumora::core::DisplayStorage::Gray16)});

    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().category, lumora::core::ErrorCategory::InvalidFrame);
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.0);
    EXPECT_DOUBLE_EQ(viewport.transform().imageToViewport({0.0, 0.0}).x, before.x);
    EXPECT_DOUBLE_EQ(viewport.transform().imageToViewport({0.0, 0.0}).y, before.y);
    const auto painted = lumora::test::paintWidget(viewport);
    expectGray(painted, 75, 100, 40);
    expectGray(painted, 227, 100, 210);
    EXPECT_EQ(completedToken, 102U);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{31U});
}

TEST(ComparisonViewport, UnpaintablePairDoesNotAcknowledgeUntilBothPanesHaveArea) {
    ImageViewport viewport;
    viewport.resize(1, 100);
    int receipts = 0;
    viewport.setCompletionObserver([&](const ViewportPresentation&) { ++receipts; });
    ASSERT_TRUE(viewport.present(comparison(40U)).hasValue());
    static_cast<void>(lumora::test::paintWidget(viewport));
    EXPECT_EQ(receipts, 0);
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);
    viewport.resize(302, 200);
    const auto painted = lumora::test::paintWidget(viewport);
    expectGray(painted, 75, 100, 40);
    expectGray(painted, 227, 100, 210);
    EXPECT_EQ(receipts, 1);
}

TEST(ComparisonViewport, FitUsesEqualPaneWidthForEvenAndOddCanvasSizes) {
    for (const int width : {302, 301}) {
        ImageViewport viewport;
        viewport.resize(width, 200);
        ASSERT_TRUE(viewport.present(comparison(50U)).hasValue());
        const auto painted = lumora::test::paintWidget(viewport);
        EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.5);
        EXPECT_DOUBLE_EQ(viewport.transform().imageToViewport({0.0, 0.0}).x, 0.0);
        EXPECT_DOUBLE_EQ(viewport.transform().imageToViewport({0.0, 0.0}).y, 25.0);
        expectGray(painted, 75, 30, 40);
        expectGray(painted, 75, 174, 40);
        EXPECT_EQ(painted.pixel(75, 180), background);
        expectGray(painted, width - 75, 30, 210);
        expectGray(painted, width - 75, 174, 210);
        EXPECT_EQ(painted.pixel(width - 75, 180), background);
        EXPECT_NE(painted.pixel(150, 100), qRgb(40, 40, 40));
        EXPECT_NE(painted.pixel(150, 100), qRgb(210, 210, 210));
    }
}

TEST(ComparisonViewport, WheelZoomIsSymmetricInLeftAndRightPaneCoordinates) {
    for (const double x : {25.0, 177.0}) {
        ImageViewport viewport;
        viewport.resize(302, 100);
        ASSERT_TRUE(viewport.present(ViewportPresentation{1U, DisplayMode::Compare,
            makePlane(60U, std::byte{40}, 300U, 200U),
            makePlane(60U, std::byte{210}, 300U, 200U)}).hasValue());
        viewport.setActualPixels();
        static_cast<void>(lumora::test::paintWidget(viewport));
        wheelAt(viewport, {x, 40.0});
        EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.2);
        EXPECT_NEAR(viewport.transform().imageToViewport({100.0, 90.0}).x, 25.0, 1e-9);
        EXPECT_NEAR(viewport.transform().imageToViewport({100.0, 90.0}).y, 40.0, 1e-9);
    }
}

TEST(ComparisonViewport, ActualPixelsKeepsBothPlanesAtLogicalScaleAcrossDevicePixelRatios) {
    for (const double dpr : {1.0, 1.25, 1.5, 2.0}) {
        ImageViewport viewport;
        viewport.resize(302, 200);
        ASSERT_TRUE(viewport.present(ViewportPresentation{1U, DisplayMode::Compare,
            makePlane(62U, std::byte{40}, 86U, 80U),
            makePlane(62U, std::byte{210}, 86U, 80U)}).hasValue());
        viewport.setActualPixels();
        const auto painted = lumora::test::paintWidget(viewport, dpr);

        EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.0);
        EXPECT_EQ(painted.width(), static_cast<int>(std::ceil(302.0 * dpr)));
        EXPECT_EQ(painted.height(), static_cast<int>(std::ceil(200.0 * dpr)));
        const auto pixel = [dpr](int logical) { return static_cast<int>(logical * dpr); };
        // At 100%, image bounds are x=[32,118), [184,270), y=[60,140).
        for (const int x : {34, 116}) {
            expectGray(painted, pixel(x), pixel(100), 40);
        }
        for (const int x : {186, 268}) {
            expectGray(painted, pixel(x), pixel(100), 210);
        }
        for (const int x : {30, 120, 182, 272}) {
            EXPECT_EQ(painted.pixel(pixel(x), pixel(100)), background);
        }
        for (const int x : {75, 227}) {
            EXPECT_EQ(painted.pixel(pixel(x), pixel(58)), background);
            EXPECT_EQ(painted.pixel(pixel(x), pixel(142)), background);
        }
        expectGray(painted, pixel(75), pixel(62), 40);
        expectGray(painted, pixel(227), pixel(138), 210);
    }
}

TEST(ComparisonViewport, DragAndZoomControlsShareOnePaneTransform) {
    ImageViewport viewport;
    viewport.resize(302, 100);
    ASSERT_TRUE(viewport.present(ViewportPresentation{1U, DisplayMode::Compare,
        makePlane(61U, std::byte{40}, 300U, 200U),
        makePlane(61U, std::byte{210}, 300U, 200U)}).hasValue());
    viewport.setActualPixels();
    static_cast<void>(lumora::test::paintWidget(viewport));
    QMouseEvent press(QEvent::MouseButtonPress, QPointF{200.0, 50.0},
        QPointF{200.0, 50.0}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF{210.0, 55.0},
        QPointF{210.0, 55.0}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &move);
    EXPECT_DOUBLE_EQ(viewport.transform().imageToViewport({0.0, 0.0}).x, -65.0);
    EXPECT_DOUBLE_EQ(viewport.transform().imageToViewport({0.0, 0.0}).y, -45.0);
    viewport.zoomIn();
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.2);
    EXPECT_NEAR(viewport.transform().imageToViewport({140.0, 95.0}).x, 75.0, 1e-9);
    EXPECT_NEAR(viewport.transform().imageToViewport({140.0, 95.0}).y, 50.0, 1e-9);
    viewport.zoomOut();
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.0);
    viewport.setFitMode();
    EXPECT_EQ(viewport.transform().mode(), ViewScaleMode::Fit);
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 0.5);
    const auto painted = lumora::test::paintWidget(viewport);
    expectGray(painted, 75, 50, 40);
    expectGray(painted, 227, 50, 210);
}

TEST(ComparisonViewport, DiscardRestoresCompletedModePixelsAndGeometry) {
    ImageViewport viewport;
    viewport.resize(302, 200);
    ASSERT_TRUE(viewport.present(comparison(70U)).hasValue());
    const auto completedPaint = lumora::test::paintWidget(viewport);
    std::weak_ptr<const DisplayFrame> pendingOriginal;
    std::weak_ptr<const DisplayFrame> pendingEnhanced;
    {
        auto pending = comparison(71U);
        pending.mode = DisplayMode::Enhanced;
        pendingOriginal = pending.originalDisplay;
        pendingEnhanced = pending.enhancedDisplay;
        ASSERT_TRUE(viewport.present(pending).hasValue());
    }
    viewport.discardPendingPresentation();
    EXPECT_TRUE(pendingOriginal.expired());
    EXPECT_TRUE(pendingEnhanced.expired());
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.5);
    EXPECT_EQ(lumora::test::paintWidget(viewport), completedPaint);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{70U});
}

TEST(ComparisonViewport, ReplacementAndClearReleaseBothImmutableOwners) {
    ImageViewport viewport;
    viewport.resize(302, 200);
    std::weak_ptr<const DisplayFrame> completedOriginal;
    std::weak_ptr<const DisplayFrame> completedEnhanced;
    {
        auto completed = comparison(80U);
        completedOriginal = completed.originalDisplay;
        completedEnhanced = completed.enhancedDisplay;
        ASSERT_TRUE(viewport.present(completed).hasValue());
    }
    static_cast<void>(lumora::test::paintWidget(viewport));
    EXPECT_FALSE(completedOriginal.expired());
    EXPECT_FALSE(completedEnhanced.expired());
    ASSERT_TRUE(viewport.present(comparison(81U)).hasValue());
    EXPECT_FALSE(completedOriginal.expired());
    EXPECT_FALSE(completedEnhanced.expired());
    static_cast<void>(lumora::test::paintWidget(viewport));
    EXPECT_TRUE(completedOriginal.expired());
    EXPECT_TRUE(completedEnhanced.expired());
    std::weak_ptr<const DisplayFrame> pendingOriginal;
    std::weak_ptr<const DisplayFrame> pendingEnhanced;
    {
        auto pending = comparison(82U);
        pendingOriginal = pending.originalDisplay;
        pendingEnhanced = pending.enhancedDisplay;
        ASSERT_TRUE(viewport.present(pending).hasValue());
    }
    viewport.clear();
    EXPECT_TRUE(pendingOriginal.expired());
    EXPECT_TRUE(pendingEnhanced.expired());
    EXPECT_FALSE(viewport.transform().drawable());
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);
    EXPECT_EQ(lumora::test::paintWidget(viewport).pixel(75, 100), background);
}

}  // namespace
