#include <lumora/ui/ViewportTransform.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

using lumora::ui::ViewScaleMode;
using lumora::ui::ViewportTransform;

TEST(ViewportTransform, FitPreservesAspectRatio) {
    auto view = ViewportTransform::fit({2048, 1024}, {1000, 800});

    EXPECT_DOUBLE_EQ(view.scale(), 1000.0 / 2048.0);
    EXPECT_NEAR(view.imageCenterInViewport().y, 400.0, 1e-9);
}

TEST(ViewportTransform, FitCanBeSmallerThanManualMinimum) {
    auto view = ViewportTransform::fit({4096, 2048}, {100, 100});

    EXPECT_DOUBLE_EQ(view.scale(), 100.0 / 4096.0);
    EXPECT_NEAR(view.imageToViewport({4096, 2048}).x, 100.0, 1e-9);
}

TEST(ViewportTransform, ActualPixelsUsesLogicalPixelScaleAndCentersImage) {
    auto view = ViewportTransform::actualPixels({64, 32}, {128, 128});

    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_EQ(view.mode(), ViewScaleMode::Manual);
    EXPECT_TRUE(view.drawable());
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 64.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 64.0);
}

TEST(ViewportTransform, FitBelowManualMinimumOnlyZoomsInward) {
    auto view = ViewportTransform::fit({4096, 2048}, {100, 100});

    view.zoomAt({50, 50}, 1.0);
    EXPECT_DOUBLE_EQ(view.scale(), 100.0 / 4096.0);
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);

    view.zoomAt({50, 50}, 0.5);
    EXPECT_DOUBLE_EQ(view.scale(), 100.0 / 4096.0);
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);

    view.zoomAt({50, 50}, 2.0);
    EXPECT_DOUBLE_EQ(view.scale(), 0.05);
    EXPECT_EQ(view.mode(), ViewScaleMode::Manual);
    EXPECT_NEAR(view.imageToViewport({2048, 1024}).x, 50.0, 1e-9);
    EXPECT_NEAR(view.imageToViewport({2048, 1024}).y, 50.0, 1e-9);
}

TEST(ViewportTransform, FitAboveManualMaximumOnlyZoomsOutward) {
    auto view = ViewportTransform::fit({1, 1}, {100, 100});

    view.zoomAt({50, 50}, 2.0);
    EXPECT_DOUBLE_EQ(view.scale(), 100.0);
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);

    view.zoomAt({50, 50}, 0.5);
    EXPECT_DOUBLE_EQ(view.scale(), 32.0);
    EXPECT_EQ(view.mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 50.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 50.0);
}

TEST(ViewportTransform, RepeatedZoomKeepsTheSourcePointUnderTheCursor) {
    auto view = ViewportTransform::fit({1000, 1000}, {500, 500});

    view.zoomAt({125, 200}, 2.0);
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_NEAR(view.imageToViewport({250, 400}).x, 125.0, 1e-9);
    EXPECT_NEAR(view.imageToViewport({250, 400}).y, 200.0, 1e-9);

    view.zoomAt({125, 200}, 2.0);
    EXPECT_DOUBLE_EQ(view.scale(), 2.0);
    EXPECT_NEAR(view.imageToViewport({250, 400}).x, 125.0, 1e-9);
    EXPECT_NEAR(view.imageToViewport({250, 400}).y, 200.0, 1e-9);

    view.zoomAt({125, 200}, 0.5);
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_NEAR(view.imageToViewport({250, 400}).x, 125.0, 1e-9);
    EXPECT_NEAR(view.imageToViewport({250, 400}).y, 200.0, 1e-9);
}

TEST(ViewportTransform, RepeatedManualZoomStopsAtBothBounds) {
    auto view = ViewportTransform::actualPixels({1000, 1000}, {100, 100});

    view.zoomAt({50, 50}, 8.0);
    view.zoomAt({50, 50}, 8.0);
    EXPECT_DOUBLE_EQ(view.scale(), 32.0);

    view.zoomAt({50, 50}, 2.0);
    EXPECT_DOUBLE_EQ(view.scale(), 32.0);

    view.zoomAt({50, 50}, 0.001);
    EXPECT_DOUBLE_EQ(view.scale(), 0.05);

    view.zoomAt({50, 50}, 0.5);
    EXPECT_DOUBLE_EQ(view.scale(), 0.05);
}

TEST(ViewportTransform, InvalidZoomInputsLeaveStateUnchanged) {
    auto view = ViewportTransform::actualPixels({200, 200}, {100, 100});
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();

    view.zoomAt({50, 50}, 0.0);
    view.zoomAt({50, 50}, -1.0);
    view.zoomAt({50, 50}, infinity);
    view.zoomAt({50, 50}, nan);
    view.zoomAt({nan, 50}, 2.0);
    view.zoomAt({50, infinity}, 2.0);

    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, -50.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, -50.0);
}

TEST(ViewportTransform, PanClampsToEveryImageEdge) {
    auto view = ViewportTransform::actualPixels({300, 200}, {100, 100});

    view.panBy({1000, 1000});
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, 0.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, 0.0);

    view.panBy({-1000, -1000});
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, -200.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, -100.0);
}

TEST(ViewportTransform, PanKeepsSmallerAxesCentered) {
    auto view = ViewportTransform::actualPixels({200, 50}, {100, 100});

    view.panBy({25, 1000});

    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, -25.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, 25.0);
}

TEST(ViewportTransform, EdgeClampingTakesPrecedenceOverCursorAnchoring) {
    auto view = ViewportTransform::actualPixels({200, 100}, {100, 100});

    view.zoomAt({0, 50}, 0.5);

    EXPECT_DOUBLE_EQ(view.scale(), 0.5);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, 0.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, 25.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({100, 50}).x, 50.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({100, 50}).y, 50.0);
}

TEST(ViewportTransform, InvalidPanDeltasLeaveStateUnchanged) {
    auto view = ViewportTransform::actualPixels({200, 200}, {100, 100});
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();

    view.panBy({nan, 10});
    view.panBy({10, infinity});

    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, -50.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, -50.0);
}

TEST(ViewportTransform, FitResizeRecomputesScaleAndCenter) {
    auto view = ViewportTransform::fit({200, 100}, {100, 100});

    view.resize({200, 100}, {200, 100});

    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 100.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 50.0);
}

TEST(ViewportTransform, ManualResizeRetainsSourceAtViewportCenter) {
    auto view = ViewportTransform::actualPixels({1000, 1000}, {200, 200});
    view.panBy({-100, 0});

    view.resize({1000, 1000}, {400, 200});

    EXPECT_EQ(view.mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({600, 500}).x, 200.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({600, 500}).y, 100.0);
}

TEST(ViewportTransform, ManualResizeClampsSourceToChangedImageBounds) {
    auto view = ViewportTransform::actualPixels({1000, 1000}, {200, 200});
    view.panBy({-400, 0});

    view.resize({500, 1000}, {200, 200});

    EXPECT_DOUBLE_EQ(view.imageToViewport({500, 500}).x, 200.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({500, 500}).y, 100.0);
}

TEST(ViewportTransform, FitRecoversAfterZeroImageOrViewportExtents) {
    auto view = ViewportTransform::fit({200, 100}, {100, 100});

    view.resize({0, 100}, {100, 100});
    EXPECT_FALSE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);
    EXPECT_TRUE(std::isfinite(view.scale()));
    EXPECT_TRUE(std::isfinite(view.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(view.imageCenterInViewport().y));

    view.resize({200, 100}, {0, 100});
    EXPECT_FALSE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);
    EXPECT_TRUE(std::isfinite(view.scale()));

    view.resize({400, 200}, {100, 100});
    EXPECT_TRUE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);
    EXPECT_DOUBLE_EQ(view.scale(), 0.25);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 50.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 50.0);
}

TEST(ViewportTransform, ManualRecoversAfterZeroImageAndViewportExtents) {
    auto view = ViewportTransform::actualPixels({64, 32}, {128, 128});

    view.resize({0, 0}, {0, 0});
    EXPECT_FALSE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_TRUE(std::isfinite(view.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(view.imageCenterInViewport().y));

    view.resize({64, 32}, {128, 128});
    EXPECT_TRUE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 64.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 64.0);
}

TEST(ViewportTransform, InvalidFactorySizesProduceFiniteNonDrawableStates) {
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();

    const auto negativeImage = ViewportTransform::fit({-1, 10}, {100, 100});
    EXPECT_FALSE(negativeImage.drawable());
    EXPECT_EQ(negativeImage.mode(), ViewScaleMode::Fit);
    EXPECT_TRUE(std::isfinite(negativeImage.scale()));
    EXPECT_GT(negativeImage.scale(), 0.0);
    EXPECT_TRUE(std::isfinite(negativeImage.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(negativeImage.imageCenterInViewport().y));

    const auto infiniteViewport = ViewportTransform::fit({10, 10}, {infinity, 100});
    EXPECT_FALSE(infiniteViewport.drawable());
    EXPECT_EQ(infiniteViewport.mode(), ViewScaleMode::Fit);
    EXPECT_TRUE(std::isfinite(infiniteViewport.scale()));
    EXPECT_TRUE(std::isfinite(infiniteViewport.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(infiniteViewport.imageCenterInViewport().y));

    const auto nanImage = ViewportTransform::actualPixels({10, nan}, {100, 100});
    EXPECT_FALSE(nanImage.drawable());
    EXPECT_EQ(nanImage.mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(nanImage.scale(), 1.0);
    EXPECT_TRUE(std::isfinite(nanImage.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(nanImage.imageCenterInViewport().y));
}

TEST(ViewportTransform, InvalidResizeSizesLeaveFitStateUnchanged) {
    auto view = ViewportTransform::fit({200, 100}, {100, 100});
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();

    view.resize({-1, 100}, {100, 100});
    view.resize({200, 100}, {infinity, 100});
    view.resize({200, nan}, {100, 100});

    EXPECT_TRUE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);
    EXPECT_DOUBLE_EQ(view.scale(), 0.5);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 50.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 50.0);
}

TEST(ViewportTransform, InvalidResizeSizesLeaveManualStateUnchanged) {
    auto view = ViewportTransform::actualPixels({200, 200}, {100, 100});
    view.panBy({-25, 0});
    const auto infinity = std::numeric_limits<double>::infinity();

    view.resize({200, 200}, {-1, 100});
    view.resize({infinity, 200}, {100, 100});

    EXPECT_TRUE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Manual);
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, -75.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, -50.0);
}

TEST(ViewportTransform, ExtremeFitArithmeticProducesFiniteNonDrawableState) {
    const auto maximum = std::numeric_limits<double>::max();
    const auto minimum = std::numeric_limits<double>::denorm_min();

    const auto underflow = ViewportTransform::fit({maximum, 1}, {minimum, 1});
    EXPECT_FALSE(underflow.drawable());
    EXPECT_TRUE(std::isfinite(underflow.scale()));
    EXPECT_GT(underflow.scale(), 0.0);
    EXPECT_TRUE(std::isfinite(underflow.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(underflow.imageCenterInViewport().y));

    const auto overflow = ViewportTransform::fit(
        {minimum, minimum},
        {maximum, maximum});
    EXPECT_FALSE(overflow.drawable());
    EXPECT_TRUE(std::isfinite(overflow.scale()));
    EXPECT_GT(overflow.scale(), 0.0);
    EXPECT_TRUE(std::isfinite(overflow.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(overflow.imageCenterInViewport().y));
}

TEST(ViewportTransform, MaximumFiniteActualPixelsStateRemainsDrawable) {
    const auto maximum = std::numeric_limits<double>::max();

    const auto view = ViewportTransform::actualPixels(
        {maximum, maximum},
        {maximum, maximum});

    EXPECT_TRUE(view.drawable());
    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, maximum / 2.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, maximum / 2.0);
}

TEST(ViewportTransform, ExtremeFiniteZoomThatOverflowsScaleIsRejected) {
    auto view = ViewportTransform::actualPixels({1000, 1000}, {100, 100});
    view.zoomAt({50, 50}, 2.0);

    view.zoomAt({50, 50}, std::numeric_limits<double>::max());

    EXPECT_DOUBLE_EQ(view.scale(), 2.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, -950.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, -950.0);
}

TEST(ViewportTransform, ExtremeFiniteCursorArithmeticIsRejected) {
    const auto maximum = std::numeric_limits<double>::max();
    auto view = ViewportTransform::actualPixels({1, 1}, {1, 1});

    view.zoomAt({maximum, maximum}, 2.0);

    EXPECT_DOUBLE_EQ(view.scale(), 1.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, 0.0);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, 0.0);
}

TEST(ViewportTransform, ExtremeFinitePanThatOverflowsOriginIsRejected) {
    const auto maximum = std::numeric_limits<double>::max();
    auto view = ViewportTransform::actualPixels({maximum, maximum}, {1, 1});
    const auto before = view.imageToViewport({0, 0});

    view.panBy({-maximum, -maximum});

    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).x, before.x);
    EXPECT_DOUBLE_EQ(view.imageToViewport({0, 0}).y, before.y);
    EXPECT_TRUE(std::isfinite(view.imageCenterInViewport().x));
    EXPECT_TRUE(std::isfinite(view.imageCenterInViewport().y));
}

TEST(ViewportTransform, ManualResizeRejectsScaledExtentOverflow) {
    auto view = ViewportTransform::actualPixels({100, 100}, {100, 100});
    view.zoomAt({50, 50}, 32.0);

    view.resize(
        {std::numeric_limits<double>::max(), 1},
        {100, 100});

    EXPECT_TRUE(view.drawable());
    EXPECT_DOUBLE_EQ(view.scale(), 32.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 50.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 50.0);
}

TEST(ViewportTransform, FitResizeRejectsScaleUnderflow) {
    auto view = ViewportTransform::fit({200, 100}, {100, 100});

    view.resize(
        {std::numeric_limits<double>::max(), 1},
        {std::numeric_limits<double>::denorm_min(), 1});

    EXPECT_TRUE(view.drawable());
    EXPECT_EQ(view.mode(), ViewScaleMode::Fit);
    EXPECT_DOUBLE_EQ(view.scale(), 0.5);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().x, 50.0);
    EXPECT_DOUBLE_EQ(view.imageCenterInViewport().y, 50.0);
}

}  // namespace
