#include <lumora/ui/ProcessingPanel.hpp>
#include <lumora/ui/ProcessingControlsModel.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/ImageViewport.hpp>
#include <lumora/ui/MainWindow.hpp>
#include <lumora/ui/WorkstationView.hpp>
#include <lumora/configuration/PresetCodec.hpp>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <gtest/gtest.h>
namespace {
using namespace lumora;
TEST(ProcessingPanel, ExposesNamedNumericControlsAndPreservesLoadedPrecision) {
    auto repository=configuration::PresetCodec::loadDefaultRepository().value();
    auto definition=repository.snapshot().activePipeline;
    std::get<processing::GammaParameters>(definition.stages[3].parameters).gamma=1.234567891234567;
    ASSERT_TRUE(repository.edit(definition).hasValue());
    core::ManualClock clock; ui::ProcessingControlsModel model(std::move(repository),clock);
    ui::ProcessingPanel panel(model); panel.refresh();
    for(const auto* name:{"window","level","brightness","contrast","gamma","claheClip","claheGrid","denoiseSigma","sharpenAmount","sharpenRadius","sharpenThreshold"}) {
        auto* entry=panel.findChild<QDoubleSpinBox*>(QString::fromLatin1(name));
        ASSERT_NE(entry,nullptr) << name; EXPECT_FALSE(entry->accessibleName().isEmpty());
        EXPECT_FALSE(entry->keyboardTracking());
    }
    EXPECT_DOUBLE_EQ(std::get<processing::GammaParameters>(model.draft().activePipeline.stages[3].parameters).gamma,1.234567891234567);
    auto* gamma=panel.findChild<QDoubleSpinBox*>("gamma");
    EXPECT_DOUBLE_EQ(gamma->locale().toDouble(gamma->text()),1.234567891234567);
    gamma->interpretText();
    EXPECT_DOUBLE_EQ(std::get<processing::GammaParameters>(model.draft().activePipeline.stages[3].parameters).gamma,1.234567891234567);
    EXPECT_DOUBLE_EQ(gamma->minimum(),0.1); EXPECT_DOUBLE_EQ(gamma->maximum(),5);
    auto* slider=panel.findChild<QSlider*>("gammaSlider"); ASSERT_NE(slider,nullptr);
    EXPECT_FALSE(slider->accessibleName().isEmpty());
    bool buddy=false; for(auto* label:panel.findChildren<QLabel*>()) buddy=buddy || label->buddy()==gamma;
    EXPECT_TRUE(buddy);
}
TEST(ProcessingPanel, PresetAndNumericChangesUseModelAndResetKeepsCustomRecipes) {
    auto repository=configuration::PresetCodec::loadDefaultRepository().value();
    ASSERT_TRUE(repository.saveCustom({"saved"},"Saved recipe","").hasValue());
    core::ManualClock clock; ui::ProcessingControlsModel model(std::move(repository),clock);
    ui::ProcessingPanel panel(model); panel.refresh();
    auto* presets=panel.findChild<QComboBox*>("presetSelector"); ASSERT_NE(presets,nullptr);
    presets->setCurrentIndex(presets->findData("standard"));
    EXPECT_EQ(model.draft().selectedId.value,"standard");
    auto* gamma=panel.findChild<QDoubleSpinBox*>("gamma"); ASSERT_NE(gamma,nullptr);
    gamma->setValue(2.3); EXPECT_EQ(model.draft().selectedId.value,"custom");
    EXPECT_DOUBLE_EQ(std::get<processing::GammaParameters>(model.draft().activePipeline.stages[3].parameters).gamma,2.3);
    auto* reset=panel.findChild<QPushButton*>("resetProcessing"); ASSERT_NE(reset,nullptr); reset->click();
    EXPECT_EQ(model.draft().selectedId.value,"original"); ASSERT_EQ(model.draft().customPresets.size(),1U);
}
TEST(ProcessingPanel, MedianModeAtomicallyRepairsGaussianOnlyParameters) {
    auto repository=configuration::PresetCodec::loadDefaultRepository().value();
    auto definition=repository.snapshot().activePipeline;
    std::get<processing::DenoiseParameters>(definition.stages[5].parameters)={processing::DenoiseMode::Gaussian,7,2.5};
    ASSERT_TRUE(repository.edit(definition).hasValue());
    core::ManualClock clock; ui::ProcessingControlsModel model(std::move(repository),clock);
    ui::ProcessingPanel panel(model); panel.refresh();
    auto* mode=panel.findChild<QComboBox*>("denoiseMode"); ASSERT_NE(mode,nullptr);
    mode->setCurrentIndex(mode->findData(static_cast<int>(processing::DenoiseMode::Median)));
    const auto parameters=std::get<processing::DenoiseParameters>(model.draft().activePipeline.stages[5].parameters);
    EXPECT_EQ(parameters.mode,processing::DenoiseMode::Median); EXPECT_EQ(parameters.kernelSize,5U); EXPECT_DOUBLE_EQ(parameters.sigma,0);
    EXPECT_TRUE(application::validatePresetPipeline(model.draft().activePipeline).hasValue());
}

TEST(ProcessingPanel, CompleteWorkstationKeepsEveryControlReachableAtSupportedSizes) {
    auto repository = configuration::PresetCodec::loadDefaultRepository().value();
    ASSERT_TRUE(repository.apply({"standard"}).hasValue());
    core::ManualClock clock;
    ui::ProcessingControlsModel model(std::move(repository), clock);
    ui::MainWindow window;
    auto& view = window.workstationView();
    auto* panel = new ui::ProcessingPanel(model);
    view.addSidebarPanel(panel);
    const auto scrolls = view.sidebar()->findChildren<QScrollArea*>();
    ASSERT_EQ(scrolls.size(), 1);
    auto* scroll = scrolls.front();
    EXPECT_TRUE(scroll->widget()->isAncestorOf(&window.cameraStartupPanel()));
    EXPECT_TRUE(scroll->widget()->isAncestorOf(panel));

    for (const QSize size : {QSize{900, 600}, QSize{1280, 800}}) {
        SCOPED_TRACE(::testing::Message() << size.width() << 'x' << size.height());
        window.resize(size);
        window.show();
        QCoreApplication::processEvents();
        EXPECT_EQ(window.size(), size);
        EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
        EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);
        EXPECT_GT(view.imageViewport()->width(), view.sidebar()->width());

        for (const auto* name : {
                 "presetSelector", "processingStage1", "window", "windowSlider", "level", "levelSlider",
                 "processingStage2", "brightness", "brightnessSlider", "contrast", "contrastSlider", "processingStage3",
                 "gamma", "gammaSlider", "processingStage4", "claheClip", "claheClipSlider", "claheGrid",
                 "processingStage5", "denoiseMode", "denoiseKernel", "denoiseSigma",
                 "processingStage6", "sharpenAmount", "sharpenAmountSlider", "sharpenRadius", "sharpenThreshold",
                 "processingStage7", "resetProcessing"}) {
            SCOPED_TRACE(name);
            const auto controls = panel->findChildren<QWidget*>(QString::fromLatin1(name));
            ASSERT_EQ(controls.size(), 1);
            auto* control = controls.front();
            EXPECT_TRUE(control->isVisibleTo(&window));
            scroll->ensureWidgetVisible(control, 0, 0);
            QCoreApplication::processEvents();
            const QRect visibleControl{
                control->mapTo(scroll->viewport(), QPoint{0, 0}), control->size()};
            EXPECT_TRUE(scroll->viewport()->rect().contains(visibleControl));
        }
        auto* reset = panel->findChild<QPushButton*>(QStringLiteral("resetProcessing"));
        ASSERT_NE(reset, nullptr);
        EXPECT_TRUE(reset->isEnabled());
    }

    const auto screenshotPath = qEnvironmentVariable("LUMORA_CONTROLS_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        scroll->verticalScrollBar()->setValue(
            panel->mapTo(scroll->widget(), QPoint{0, 0}).y());
        QCoreApplication::processEvents();
        EXPECT_TRUE(window.grab().save(screenshotPath));
    }
}
}
