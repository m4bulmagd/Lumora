#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/DisplayMode.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/MainWindow.hpp>
#include <lumora/ui/ProcessingControlsModel.hpp>
#include <lumora/ui/ProcessingPanel.hpp>
#include <lumora/ui/WorkstationView.hpp>
#include <lumora/configuration/PresetCodec.hpp>
#include <lumora/core/LatestValueSlot.hpp>

#include "ViewportTestSupport.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QLabel>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace {

using namespace lumora;
using namespace std::chrono_literals;

std::shared_ptr<const core::FrameBundle> makePairedBundle(
    std::uint64_t id, core::IClock& clock) {
    // Distinct constant planes make the expected pixels independent of scaling.
    const auto original = test::makeBundle(128U, 64U, id, clock);
    const auto& displayLayout = original->originalDisplay->layout;
    auto displayPool = core::BufferPool::create(1U, displayLayout.payloadBytes()).value();
    auto displayLease = displayPool->tryAcquire();
    std::ranges::fill(displayLease->bytes(), std::byte{0xE0});
    auto display = core::DisplayFrame::create(
        id, displayLayout, std::move(*displayLease).seal(), core::DisplayStorage::Gray8,
        original->originalDisplay->mapping,
        original->originalDisplay->presentationOrientation).value();

    const auto processedLayout = core::ImageLayout::create(
        128U, 64U, 256U, core::StorageType::UInt16, 16384U).value();
    auto processedPool = core::BufferPool::create(1U, processedLayout.payloadBytes()).value();
    auto processedLease = processedPool->tryAcquire();
    std::ranges::fill(processedLease->bytes(), std::byte{0xE0});
    auto processed = core::ProcessedFrame::create(
        id, processedLayout, std::move(*processedLease).seal(), {1U, 1U, 1U}, {}).value();
    return core::FrameBundle::create(
        original->raw, original->originalDisplay, std::move(processed), std::move(display)).value();
}

void expectPairedPixels(const QImage& pixels) {
    ASSERT_FALSE(pixels.isNull());
    // Each sample is at the center of its pane, clear of labels and divider.
    EXPECT_EQ(pixels.pixelColor(pixels.width() / 4, pixels.height() / 2).red(), 128);
    EXPECT_EQ(pixels.pixelColor(3 * pixels.width() / 4, pixels.height() / 2).red(), 224);
}

void expectFullyVisible(QWidget& widget, QWidget& container) {
    EXPECT_TRUE(widget.isVisibleTo(&container));
    EXPECT_GT(widget.width(), 0);
    EXPECT_GT(widget.height(), 0);
    EXPECT_TRUE(container.rect().contains(
        QRect{widget.mapTo(&container, QPoint{0, 0}), widget.size()}));
}

void expectModeAndPauseControlsVisible(ui::WorkstationView& view, QWidget& window) {
    for (const auto* name : {"originalModeAction", "enhancedModeAction", "compareModeAction"}) {
        SCOPED_TRACE(name);
        auto* action = view.findChild<QAction*>(QString::fromLatin1(name));
        ASSERT_NE(action, nullptr);
        EXPECT_TRUE(action->isEnabled());
        QToolButton* visibleButton = nullptr;
        for (auto* button : view.findChildren<QToolButton*>()) {
            if (button->defaultAction() == action) {
                ASSERT_EQ(visibleButton, nullptr);
                visibleButton = button;
            }
        }
        ASSERT_NE(visibleButton, nullptr);
        EXPECT_FALSE(visibleButton->accessibleName().isEmpty());
        expectFullyVisible(*visibleButton, *view.sidebar());
        expectFullyVisible(*visibleButton, window);
    }
    auto* pause = view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    ASSERT_NE(pause, nullptr);
    EXPECT_TRUE(pause->isEnabled());
    expectFullyVisible(*pause, *view.sidebar());
    expectFullyVisible(*pause, window);
}

void saveScreenshotIfRequested(ui::MainWindow& window, const QSize size, const QString& state) {
    const auto prefix = qEnvironmentVariable("LUMORA_COMPARE_SCREENSHOT");
    if (prefix.isEmpty()) {
        return;
    }
    const auto path = prefix + QStringLiteral("-%1x%2-%3.bmp")
        .arg(size.width()).arg(size.height()).arg(state);
    EXPECT_TRUE(window.grab().save(path, "BMP")) << path.toStdString();
}

TEST(CompareWorkstation, RealControlsKeepPairedPixelsAndPausedSafetyVisibleAtSupportedSizes) {
    for (const QSize size : {QSize{900, 600}, QSize{1280, 800}}) {
        SCOPED_TRACE(::testing::Message() << size.width() << 'x' << size.height());
        auto repository = configuration::PresetCodec::loadDefaultRepository().value();
        ASSERT_TRUE(repository.apply({"standard"}).hasValue());
        const auto frameUtc = std::chrono::sys_days{std::chrono::year{2026} / 9 / 10}
            + 12h + 34min + 56s + 789ms;
        core::ManualClock clock({}, frameUtc);
        ui::ProcessingControlsModel model(std::move(repository), clock);
        core::LatestValueSlot<core::FrameBundle> slot;
        ui::MainWindow window;
        auto& view = window.workstationView();
        auto* panel = new ui::ProcessingPanel(model);
        view.addSidebarPanel(panel);
        ui::FramePresenter presenter(slot, view, clock);
        const auto scrolls = view.sidebar()->findChildren<QScrollArea*>();
        ASSERT_EQ(scrolls.size(), 1);
        auto* scroll = scrolls.front();
        EXPECT_TRUE(scroll->widget()->isAncestorOf(&window.cameraStartupPanel()));
        EXPECT_TRUE(scroll->widget()->isAncestorOf(panel));

        window.resize(size);
        window.show();
        QCoreApplication::processEvents();
        const auto bundle = makePairedBundle(42U, clock);
        static_cast<void>(slot.publish(bundle));
        presenter.refresh();
        static_cast<void>(test::paintWidget(*view.imageViewport()));
        ASSERT_EQ(presenter.presentedBundle(), bundle);

        auto* compare = view.findChild<QAction*>(QStringLiteral("compareModeAction"));
        ASSERT_NE(compare, nullptr);
        ASSERT_TRUE(compare->isEnabled());
        compare->trigger();
        expectPairedPixels(test::paintWidget(*view.imageViewport()));
        EXPECT_EQ(presenter.displayMode(), ui::DisplayMode::Compare);
        EXPECT_TRUE(compare->isChecked());
        ASSERT_EQ(presenter.presentedBundle(), bundle);
        EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 42U);
        EXPECT_EQ(view.imageViewport()->presentedFrameId(), 42U);
        EXPECT_EQ(presenter.displayedFrameCount(), 1U);

        auto* overlay = view.findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
        auto* banner = view.findChild<QLabel*>(QStringLiteral("releaseClassBanner"));
        auto* pause = view.findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
        ASSERT_NE(overlay, nullptr);
        ASSERT_NE(banner, nullptr);
        ASSERT_NE(pause, nullptr);
        EXPECT_FALSE(overlay->isVisibleTo(&window));
        EXPECT_EQ(view.status().freshness, ui::FrameFreshness::Current);

        // Show the real processing section in captures; persistent controls must
        // remain accessible while camera/processing content is scrolled.
        scroll->verticalScrollBar()->setValue(
            panel->mapTo(scroll->widget(), QPoint{0, 0}).y());
        QCoreApplication::processEvents();
        EXPECT_EQ(window.size(), size);
        EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
        EXPECT_GT(view.imageViewport()->width(), view.sidebar()->width());
        expectModeAndPauseControlsVisible(view, window);
        expectFullyVisible(*banner, window);
        saveScreenshotIfRequested(window, size, QStringLiteral("live"));

        pause->click();
        ASSERT_EQ(view.viewerState(), ui::ViewerState::Paused);
        static_cast<void>(slot.publish(test::makeBundle(128U, 64U, 43U, clock)));
        clock.advance(1234ms);
        presenter.refresh();
        QCoreApplication::processEvents();
        expectPairedPixels(test::paintWidget(*view.imageViewport()));
        EXPECT_EQ(presenter.presentedBundle(), bundle);
        EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 42U);
        EXPECT_EQ(presenter.displayedFrameCount(), 1U);
        EXPECT_EQ(presenter.displayMode(), ui::DisplayMode::Compare);
        EXPECT_EQ(view.status().frameUtc, bundle->raw->metadata.acquisitionUtcTime);
        EXPECT_EQ(view.status().frameAge, 1234ms);
        EXPECT_TRUE(overlay->text().contains(QStringLiteral("PAUSED")));
        EXPECT_TRUE(overlay->text().contains(QStringLiteral("2026-09-10T12:34:56.789Z")));
        EXPECT_TRUE(overlay->text().contains(QStringLiteral("Age: 1234 ms")));
        expectFullyVisible(*overlay, window);
        expectModeAndPauseControlsVisible(view, window);

        clock.advance(766ms);
        presenter.refresh();
        QCoreApplication::processEvents();
        EXPECT_EQ(view.status().frameAge, 2000ms);
        EXPECT_TRUE(overlay->text().contains(QStringLiteral("Age: 2000 ms")));
        EXPECT_TRUE(overlay->text().contains(QStringLiteral("2026-09-10T12:34:56.789Z")));
        EXPECT_EQ(presenter.presentedBundle(), bundle);
        EXPECT_TRUE(compare->isChecked());
        EXPECT_EQ(pause->text(), QStringLiteral("Live"));
        EXPECT_EQ(window.size(), size);
        EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
        EXPECT_GT(view.imageViewport()->width(), view.sidebar()->width());
        expectFullyVisible(*overlay, window);
        expectFullyVisible(*banner, window);
        expectModeAndPauseControlsVisible(view, window);
        saveScreenshotIfRequested(window, size, QStringLiteral("paused"));
    }
}

}  // namespace
