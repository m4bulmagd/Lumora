#include <lumora/ui/ImageViewport.hpp>

#include "ViewportTestSupport.hpp"

#include <lumora/core/BufferPool.hpp>

#include <QApplication>
#include <QColor>
#include <QMouseEvent>
#include <QThread>
#include <QWheelEvent>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace {

using lumora::core::DisplayFrame;
using lumora::ui::ImageViewport;
using lumora::ui::ViewScaleMode;

constexpr QRgb background = qRgb(22, 24, 28);

std::shared_ptr<const DisplayFrame> makeRowsFrame(
    std::uint64_t id,
    const std::array<std::byte, 8>& bytes,
    lumora::core::Orientation orientation = {
        false, false, lumora::core::Rotation::Degrees0}) {
    const auto layout = lumora::core::ImageLayout::create(
        3U, 2U, 4U, lumora::core::StorageType::UInt8, bytes.size()).value();
    auto pool = lumora::core::BufferPool::create(1U, bytes.size()).value();
    auto lease = pool->tryAcquire();
    std::ranges::copy(bytes, lease->bytes().begin());
    return DisplayFrame::create(
        id,
        layout,
        std::move(*lease).seal(),
        lumora::core::DisplayStorage::Gray8,
        lumora::core::DisplayMapping{0U, 255U, 255U, 1U},
        orientation)
        .value();
}

std::shared_ptr<const DisplayFrame> makeGray16Frame(std::uint64_t id) {
    const auto layout = lumora::core::ImageLayout::create(
        4U, 2U, 8U, lumora::core::StorageType::UInt16, 16U).value();
    auto pool = lumora::core::BufferPool::create(1U, 16U).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), std::byte{0});
    return DisplayFrame::create(
        id,
        layout,
        std::move(*lease).seal(),
        lumora::core::DisplayStorage::Gray16,
        lumora::core::DisplayMapping{0U, 65'535U, 65'535U, 1U},
        lumora::core::Orientation{
            false, false, lumora::core::Rotation::Degrees0})
        .value();
}

void expectGray(const QImage& image, int x, int y, int value) {
    const QColor actual = image.pixelColor(x, y);
    EXPECT_EQ(actual.red(), value);
    EXPECT_EQ(actual.green(), value);
    EXPECT_EQ(actual.blue(), value);
    EXPECT_EQ(actual.alpha(), 255);
}

TEST(ImageViewport, RetainsPixelsUntilReplacement) {
    std::weak_ptr<const DisplayFrame> weak;
    ImageViewport viewport;
    viewport.resize(128, 128);
    {
        auto frame = lumora::test::makeDisplayFrame(64, 32, 7);
        weak = frame;
        ASSERT_TRUE(viewport.present(frame).hasValue());
    }
    lumora::test::paintWidget(viewport);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{7});
    EXPECT_FALSE(weak.expired());
    ASSERT_TRUE(viewport.present(lumora::test::makeDisplayFrame(64, 32, 8)).hasValue());
    EXPECT_FALSE(weak.expired());
    lumora::test::paintWidget(viewport);
    EXPECT_TRUE(weak.expired());
}

TEST(ImageViewport, FitPreservesAspectRatioAcrossViewportShapes) {
    const auto frame = lumora::test::makeDisplayFrame(64, 32, 1);

    ImageViewport square;
    square.resize(128, 128);
    ASSERT_TRUE(square.present(frame).hasValue());
    const auto squarePaint = lumora::test::paintWidget(square);
    EXPECT_EQ(squarePaint.pixel(64, 16), background);
    expectGray(squarePaint, 64, 32, 128);
    expectGray(squarePaint, 64, 95, 128);
    EXPECT_EQ(squarePaint.pixel(64, 112), background);

    ImageViewport portrait;
    portrait.resize(64, 128);
    ASSERT_TRUE(portrait.present(frame).hasValue());
    const auto portraitPaint = lumora::test::paintWidget(portrait);
    EXPECT_EQ(portraitPaint.pixel(32, 32), background);
    expectGray(portraitPaint, 32, 48, 128);
    expectGray(portraitPaint, 32, 79, 128);
    EXPECT_EQ(portraitPaint.pixel(32, 96), background);

    ImageViewport wide;
    wide.resize(256, 64);
    ASSERT_TRUE(wide.present(frame).hasValue());
    const auto widePaint = lumora::test::paintWidget(wide);
    EXPECT_EQ(widePaint.pixel(32, 32), background);
    expectGray(widePaint, 64, 32, 128);
    expectGray(widePaint, 191, 32, 128);
    EXPECT_EQ(widePaint.pixel(224, 32), background);
}

TEST(ImageViewport, UsesValidatedStrideForPaddedRows) {
    const auto frame = makeRowsFrame(
        2,
        {std::byte{40}, std::byte{40}, std::byte{40}, std::byte{250},
         std::byte{210}, std::byte{210}, std::byte{210}, std::byte{5}});
    ImageViewport viewport;
    viewport.resize(3, 2);
    viewport.setActualPixels();
    ASSERT_TRUE(viewport.present(frame).hasValue());

    const auto painted = lumora::test::paintWidget(viewport);

    expectGray(painted, 1, 0, 40);
    expectGray(painted, 1, 1, 210);
}

TEST(ImageViewport, DoesNotApplyPresentationOrientationToDisplayPixelsAgain) {
    const auto frame = makeRowsFrame(
        2,
        {std::byte{40}, std::byte{40}, std::byte{40}, std::byte{250},
         std::byte{210}, std::byte{210}, std::byte{210}, std::byte{5}},
        {false, true, lumora::core::Rotation::Degrees0});
    ImageViewport viewport;
    viewport.resize(3, 2);
    viewport.setActualPixels();
    ASSERT_TRUE(viewport.present(frame).hasValue());

    const auto painted = lumora::test::paintWidget(viewport);

    expectGray(painted, 1, 0, 40);
    expectGray(painted, 1, 1, 210);
}

TEST(ImageViewport, ActualPixelsStaysLogicalAtEveryDevicePixelRatio) {
    for (const double dpr : {1.0, 1.25, 1.5, 2.0}) {
        ImageViewport viewport;
        viewport.resize(40, 24);
        ASSERT_TRUE(viewport.present(
            lumora::test::makeDisplayFrame(16, 8, 3)).hasValue());
        viewport.setActualPixels();

        const auto painted = lumora::test::paintWidget(viewport, dpr);

        EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.0);
        EXPECT_EQ(painted.width(), static_cast<int>(std::ceil(40.0 * dpr)));
        EXPECT_EQ(painted.height(), static_cast<int>(std::ceil(24.0 * dpr)));
        const auto imageX = static_cast<int>(12.0 * dpr);
        const auto imageY = static_cast<int>(8.0 * dpr);
        const auto imageWidth = static_cast<int>(16.0 * dpr);
        const auto imageHeight = static_cast<int>(8.0 * dpr);
        const auto imageCenterY = imageY + imageHeight / 2;
        const auto imageCenterX = imageX + imageWidth / 2;

        EXPECT_EQ(painted.pixel(imageX - 1, imageCenterY), background);
        expectGray(painted, imageX, imageCenterY, 128);
        expectGray(painted, imageX + imageWidth - 1, imageCenterY, 128);
        EXPECT_EQ(painted.pixel(imageX + imageWidth, imageCenterY), background);
        EXPECT_EQ(painted.pixel(imageCenterX, imageY - 1), background);
        expectGray(painted, imageCenterX, imageY, 128);
        expectGray(painted, imageCenterX, imageY + imageHeight - 1, 128);
        EXPECT_EQ(painted.pixel(imageCenterX, imageY + imageHeight), background);
    }
}

TEST(ImageViewport, RejectsNullAndGray16WithoutReplacingStagedState) {
    ImageViewport viewport;
    viewport.resize(32, 32);
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(8, 8, 4)).hasValue());
    lumora::test::paintWidget(viewport);

    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(4, 4, 6)).hasValue());
    const auto nullResult = viewport.present(nullptr);
    EXPECT_FALSE(nullResult.hasValue());
    EXPECT_EQ(nullResult.error().category, lumora::core::ErrorCategory::InvalidFrame);
    const auto gray16Result = viewport.present(makeGray16Frame(5));
    EXPECT_FALSE(gray16Result.hasValue());
    EXPECT_EQ(gray16Result.error().category, lumora::core::ErrorCategory::InvalidFrame);
    lumora::test::paintWidget(viewport);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{6});
}

TEST(ImageViewport, ZeroSizedViewportDoesNotCompletePendingFrame) {
    ImageViewport viewport;
    viewport.resize(0, 0);
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(8, 8, 7)).hasValue());

    QImage paintTarget{1, 1, QImage::Format_ARGB32_Premultiplied};
    paintTarget.fill(Qt::transparent);
    viewport.render(&paintTarget);
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);

    viewport.resize(16, 16);
    static_cast<void>(lumora::test::paintWidget(viewport));
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{7});
}

TEST(ImageViewport, CompletionAndObserverOccurOnlyForNewlyPaintedFrame) {
    ImageViewport viewport;
    viewport.resize(64, 64);
    int observations = 0;
    std::uint64_t observedId = 99;
    QThread* observedThread = nullptr;
    viewport.setPresentationObserver(
        [&](std::shared_ptr<const DisplayFrame> frame) {
            ++observations;
            observedId = frame->sourceFrameId;
            observedThread = QThread::currentThread();
        });

    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(16, 8, 0)).hasValue());
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);
    EXPECT_EQ(observations, 0);

    lumora::test::paintWidget(viewport);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{0});
    EXPECT_EQ(observations, 1);
    EXPECT_EQ(observedId, 0U);
    EXPECT_EQ(observedThread, viewport.thread());

    viewport.zoomIn();
    lumora::test::paintWidget(viewport);
    viewport.resize(80, 60);
    lumora::test::paintWidget(viewport);
    EXPECT_EQ(observations, 1);
}

TEST(ImageViewport, CoalescesUnpaintedReplacements) {
    ImageViewport viewport;
    viewport.resize(64, 64);
    int observations = 0;
    std::uint64_t observedId = 0;
    viewport.setPresentationObserver(
        [&](std::shared_ptr<const DisplayFrame> frame) {
            ++observations;
            observedId = frame->sourceFrameId;
        });
    std::weak_ptr<const DisplayFrame> first;
    std::weak_ptr<const DisplayFrame> second;
    {
        auto frame = lumora::test::makeDisplayFrame(8, 8, 10);
        first = frame;
        ASSERT_TRUE(viewport.present(frame).hasValue());
    }
    {
        auto frame = lumora::test::makeDisplayFrame(8, 8, 11);
        second = frame;
        ASSERT_TRUE(viewport.present(frame).hasValue());
    }
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(8, 8, 12)).hasValue());

    EXPECT_TRUE(first.expired());
    EXPECT_TRUE(second.expired());
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);
    lumora::test::paintWidget(viewport);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{12});
    EXPECT_EQ(observations, 1);
    EXPECT_EQ(observedId, 12U);
}

TEST(ImageViewport, SameSizeReplacementPreservesManualTransform) {
    ImageViewport viewport;
    viewport.resize(100, 100);
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(200, 200, 13)).hasValue());
    viewport.setActualPixels();
    static_cast<void>(lumora::test::paintWidget(viewport));
    viewport.zoomIn();
    const auto expectedScale = viewport.transform().scale();
    const auto expectedOrigin = viewport.transform().imageToViewport({0.0, 0.0});

    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(200, 200, 14)).hasValue());

    EXPECT_DOUBLE_EQ(viewport.transform().scale(), expectedScale);
    EXPECT_DOUBLE_EQ(
        viewport.transform().imageToViewport({0.0, 0.0}).x, expectedOrigin.x);
    EXPECT_DOUBLE_EQ(
        viewport.transform().imageToViewport({0.0, 0.0}).y, expectedOrigin.y);
}

TEST(ImageViewport, DiscardPendingRestoresCompletedPixelsAndGeometry) {
    ImageViewport viewport;
    viewport.resize(12, 8);
    const auto completed = makeRowsFrame(
        20,
        {std::byte{40}, std::byte{40}, std::byte{40}, std::byte{250},
         std::byte{210}, std::byte{210}, std::byte{210}, std::byte{5}});
    ASSERT_TRUE(viewport.present(completed).hasValue());
    viewport.setActualPixels();
    lumora::test::paintWidget(viewport);
    viewport.zoomIn();
    const auto expectedScale = viewport.transform().scale();
    const auto expectedOrigin = viewport.transform().imageToViewport({0.0, 0.0});
    const auto expectedPaint = lumora::test::paintWidget(viewport);

    std::weak_ptr<const DisplayFrame> pending;
    {
        auto replacement = lumora::test::makeDisplayFrame(6, 6, 21);
        pending = replacement;
        ASSERT_TRUE(viewport.present(replacement).hasValue());
    }
    viewport.discardPendingPresentation();

    EXPECT_TRUE(pending.expired());
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{20});
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), expectedScale);
    EXPECT_DOUBLE_EQ(
        viewport.transform().imageToViewport({0.0, 0.0}).x, expectedOrigin.x);
    EXPECT_DOUBLE_EQ(
        viewport.transform().imageToViewport({0.0, 0.0}).y, expectedOrigin.y);
    const auto painted = lumora::test::paintWidget(viewport);
    EXPECT_EQ(painted, expectedPaint);
}

TEST(ImageViewport, ClearAndDestructionReleaseAllOwners) {
    std::weak_ptr<const DisplayFrame> completed;
    std::weak_ptr<const DisplayFrame> pending;
    ImageViewport viewport;
    viewport.resize(32, 32);
    {
        auto frame = lumora::test::makeDisplayFrame(8, 8, 30);
        completed = frame;
        ASSERT_TRUE(viewport.present(frame).hasValue());
    }
    lumora::test::paintWidget(viewport);
    {
        auto frame = lumora::test::makeDisplayFrame(4, 4, 31);
        pending = frame;
        ASSERT_TRUE(viewport.present(frame).hasValue());
    }
    viewport.clear();

    EXPECT_TRUE(completed.expired());
    EXPECT_TRUE(pending.expired());
    EXPECT_EQ(viewport.presentedFrameId(), std::nullopt);
    EXPECT_EQ(viewport.transform().mode(), ViewScaleMode::Fit);
    EXPECT_FALSE(viewport.transform().drawable());
    const auto emptyPaint = lumora::test::paintWidget(viewport);
    EXPECT_EQ(emptyPaint.pixel(16, 16), background);

    std::weak_ptr<const DisplayFrame> destruction;
    {
        auto ownedViewport = std::make_unique<ImageViewport>();
        auto frame = lumora::test::makeDisplayFrame(8, 8, 32);
        destruction = frame;
        ASSERT_TRUE(ownedViewport->present(frame).hasValue());
        frame.reset();
    }
    EXPECT_TRUE(destruction.expired());

    std::weak_ptr<const DisplayFrame> paintedDestruction;
    {
        auto ownedViewport = std::make_unique<ImageViewport>();
        ownedViewport->resize(16, 16);
        auto frame = lumora::test::makeDisplayFrame(8, 8, 33);
        paintedDestruction = frame;
        ASSERT_TRUE(ownedViewport->present(frame).hasValue());
        frame.reset();
        static_cast<void>(lumora::test::paintWidget(*ownedViewport));
        EXPECT_FALSE(paintedDestruction.expired());
    }
    EXPECT_TRUE(paintedDestruction.expired());
}

TEST(ImageViewport, ButtonZoomUsesViewportCenterAndFixedFactors) {
    ImageViewport viewport;
    viewport.resize(100, 100);
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(200, 200, 40)).hasValue());
    viewport.setActualPixels();
    lumora::test::paintWidget(viewport);

    viewport.zoomIn();
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.2);
    viewport.zoomOut();
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.0);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{40});
}

TEST(ImageViewport, WheelZoomKeepsSourcePointUnderLogicalPointer) {
    ImageViewport viewport;
    viewport.resize(100, 100);
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(200, 200, 41)).hasValue());
    viewport.setActualPixels();
    static_cast<void>(lumora::test::paintWidget(viewport));

    const QPointF wheelPosition{25.0, 40.0};
    QWheelEvent wheel(
        wheelPosition,
        wheelPosition,
        QPoint{},
        QPoint{0, 120},
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false);
    QApplication::sendEvent(&viewport, &wheel);
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 1.2);
    EXPECT_NEAR(viewport.transform().imageToViewport({75.0, 90.0}).x, 25.0, 1e-9);
    EXPECT_NEAR(viewport.transform().imageToViewport({75.0, 90.0}).y, 40.0, 1e-9);
}

TEST(ImageViewport, LeftDragPansOversizedImageAxes) {
    ImageViewport viewport;
    viewport.resize(100, 100);
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(200, 200, 42)).hasValue());
    viewport.setActualPixels();
    static_cast<void>(lumora::test::paintWidget(viewport));

    QMouseEvent press(
        QEvent::MouseButtonPress,
        QPointF{50.0, 50.0},
        QPointF{50.0, 50.0},
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &press);
    QMouseEvent move(
        QEvent::MouseMove,
        QPointF{60.0, 55.0},
        QPointF{60.0, 55.0},
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier);
    const auto beforePan = viewport.transform().imageToViewport({0.0, 0.0});
    QApplication::sendEvent(&viewport, &move);
    const auto afterPan = viewport.transform().imageToViewport({0.0, 0.0});
    EXPECT_DOUBLE_EQ(afterPan.x, beforePan.x + 10.0);
    EXPECT_DOUBLE_EQ(afterPan.y, beforePan.y + 5.0);

    QMouseEvent release(
        QEvent::MouseButtonRelease,
        QPointF{60.0, 55.0},
        QPointF{60.0, 55.0},
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &release);
}

TEST(ImageViewport, LeftDoubleClickReturnsToFitMode) {
    ImageViewport viewport;
    viewport.resize(100, 100);
    ASSERT_TRUE(viewport.present(
        lumora::test::makeDisplayFrame(200, 200, 43)).hasValue());
    viewport.setActualPixels();
    static_cast<void>(lumora::test::paintWidget(viewport));
    viewport.zoomIn();

    QMouseEvent doubleClick(
        QEvent::MouseButtonDblClick,
        QPointF{50.0, 50.0},
        QPointF{50.0, 50.0},
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QApplication::sendEvent(&viewport, &doubleClick);
    EXPECT_EQ(viewport.transform().mode(), ViewScaleMode::Fit);
    EXPECT_DOUBLE_EQ(viewport.transform().scale(), 0.5);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{43});
}

}  // namespace
