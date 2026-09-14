#include "ProcessingAdapter.hpp"
#include "FrameEngineTestAccess.hpp"

#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantMap>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace lumora;
using namespace std::chrono_literals;

camera::CameraConfiguration request() {
    return {{"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant, core::StorageType::UInt8},
        {0, 0, 8, 6}, 30.0, {camera::ExposureMode::Manual, 100.0},
        {camera::GainMode::Manual, 0.0}, camera::AcquisitionMode::Continuous};
}
camera::sim::SimulatedCameraOptions options() {
    return {{"SIM-LIVE"}, {{request().pixelFormat}, {{0,0,1,1},{0,0,8,6},{1,1,1,1}},
        {1,60,1,camera::ControlAccess::WritableStopped},
        {1,1000,1,camera::ControlAccess::WritableStopped},{camera::ExposureMode::Manual},
        {0,10,1,camera::ControlAccess::WritableStopped},{camera::GainMode::Manual}},
        camera::sim::SimulationPattern::MovingBar, 30.0, 0x4C554D4FU,
        camera::sim::SimulationPacingMode::Manual};
}

// Only the external I/O timing/failure is controlled. Successful reads and
// writes use the production codec and a real isolated ConfigurationStore.
class StoreIo final : public configuration::IStartupPreferencesIo {
public:
    explicit StoreIo(std::filesystem::path path) : store(std::move(path)) {}
    configuration::ConfigurationStore store;
    std::atomic<bool> failLoad{false}, failSave{false};
    std::mutex mutex;
    std::condition_variable condition;
    bool loadBlocked{false}, loadEntered{false}, released{false};
    core::Result<configuration::ApplicationConfiguration> load() override {
        {
            std::unique_lock lock(mutex);
            loadEntered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return !loadBlocked || released; });
        }
        if (failLoad) return core::Result<configuration::ApplicationConfiguration>::failure(
            {core::ErrorCategory::Configuration,"load_failed","Settings could not be loaded.","",false});
        return store.load();
    }
    core::Result<void> save(const configuration::ApplicationConfiguration& value) override {
        if (failSave) return core::Result<void>::failure(
            {core::ErrorCategory::Configuration,"save_failed","Settings could not be saved.","",false});
        return store.save(value);
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        condition.notify_all();
    }
};

application::LivePipeline::ProcessorFactory faultFactory(
    const std::shared_ptr<processing::detail::EngineHooks>& hooks) {
    if (!hooks) return {};
    return [hooks](core::BufferPool& processingPool, core::BufferPool& displayPool,
        const core::ImageLayout& layout) {
        auto engine = processing::detail::FrameEngineTestAccess::create(
            processingPool, displayPool, layout, processing::defaultPipeline(), {}, hooks);
        using Result = core::Result<std::unique_ptr<processing::IFrameProcessor>>;
        if (!engine.hasValue()) return Result::failure(engine.error());
        return Result::success(std::move(engine).value());
    };
}

struct Fixture final {
    QTemporaryDir directory;
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider{options(), clock};
    application::LivePipeline pipeline;
    StoreIo* io{};
    configuration::StartupPreferencesService preferences;
    presentation::WorkstationCoordinator coordinator{pipeline, preferences, clock, request()};
    qml::ProcessingAdapter adapter{coordinator};

    explicit Fixture(bool delayed = false, bool failedLoad = false,
        application::PresetState seed = {},
        std::shared_ptr<processing::detail::EngineHooks> hooks = {})
        : pipeline(provider, clock, request(), faultFactory(hooks)),
          preferences(makeIo(delayed, failedLoad, seed)) {}
    explicit Fixture(const std::filesystem::path& existingPath)
        : pipeline(provider, clock, request()), preferences(openIo(existingPath)) {}
    std::unique_ptr<StoreIo> openIo(const std::filesystem::path& path) {
        auto owned = std::make_unique<StoreIo>(path);
        io = owned.get();
        return owned;
    }
    std::unique_ptr<StoreIo> makeIo(bool delayed, bool failedLoad, const application::PresetState& seed) {
        auto owned = std::make_unique<StoreIo>(directory.filePath("preferences.json").toStdString());
        io = owned.get();
        configuration::ApplicationConfiguration configuration;
        configuration.presets = seed;
        if (!io->store.save(configuration).hasValue()) throw std::runtime_error("Cannot seed preferences");
        io->loadBlocked = delayed;
        io->failLoad = failedLoad;
        return owned;
    }
    ~Fixture() {
        io->release();
        coordinator.beginShutdown();
        coordinator.completeRendererShutdown();
        preferences.requestStop();
        preferences.join();
    }
    bool start() {
        return directory.isValid() && preferences.start().hasValue()
            && pipeline.start().hasValue() && coordinator.start().hasValue();
    }
    bool wait(const std::function<bool()>& ready) {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        do {
            coordinator.poll();
            if (const auto handoff = coordinator.pendingContextHandoff()) {
                if (!coordinator.completeContextHandoff(handoff->id).hasValue()) return false;
            }
            adapter.refresh();
            if (ready()) return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    bool loaded() { return wait([&] { return coordinator.processingControls() != nullptr; }); }
    bool settled() {
        return wait([&] {
            return adapter.hasAcknowledged() && !adapter.pending()
                && preferences.latestStatus()->latestSavedPresetRevision.has_value();
        });
    }
};

const processing::WindowLevelParameters& windowLevel(const processing::PipelineDefinition& value) {
    for (const auto& stage : value.stages)
        if (stage.id == processing::StageId::WindowLevel)
            return std::get<processing::WindowLevelParameters>(stage.parameters);
    throw std::runtime_error("Missing window/level stage");
}

application::PresetState savedPresetSeed() {
    application::PresetState seed;
    auto pipeline = processing::standardPipeline();
    // A complete, distinct saved recipe which the real 8x6 source can prepare.
    pipeline.stages[4].enabled = false;
    pipeline.stages[1].parameters = processing::WindowLevelParameters{
        1000.1234567890123, 2000.9876543210987};
    pipeline.stages[3].parameters = processing::GammaParameters{1.25};
    pipeline.stages[6].parameters = processing::SharpenParameters{0.75, 1.5, 13.5};
    seed.customPresets.push_back({{"saved-fractional"}, "Inspection précise", "Fractional detail recipe",
        false, 1U, 7U, std::move(pipeline)});
    return seed;
}

void expectSavedCollection(const application::PresetState& actual,
    const application::PresetState& expected) {
    ASSERT_EQ(actual.customPresets.size(), expected.customPresets.size());
    for (std::size_t i = 0; i < expected.customPresets.size(); ++i) {
        const auto& saved = actual.customPresets[i];
        const auto& seed = expected.customPresets[i];
        EXPECT_EQ(saved.id, seed.id);
        EXPECT_EQ(saved.name, seed.name);
        EXPECT_EQ(saved.description, seed.description);
        EXPECT_EQ(saved.builtIn, seed.builtIn);
        EXPECT_EQ(saved.schemaVersion, seed.schemaVersion);
        EXPECT_EQ(saved.revision, seed.revision);
        EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(saved.pipeline, seed.pipeline));
    }
}

const processing::BrightnessContrastParameters& brightnessContrast(const processing::PipelineDefinition& value) {
    for (const auto& stage : value.stages)
        if (stage.id == processing::StageId::BrightnessContrast)
            return std::get<processing::BrightnessContrastParameters>(stage.parameters);
    throw std::runtime_error("Missing brightness/contrast stage");
}

const processing::GammaParameters& gamma(const processing::PipelineDefinition& value) {
    for (const auto& stage : value.stages)
        if (stage.id == processing::StageId::Gamma)
            return std::get<processing::GammaParameters>(stage.parameters);
    throw std::runtime_error("Missing gamma stage");
}

const processing::ClaheParameters& localContrast(const processing::PipelineDefinition& value) {
    for (const auto& stage : value.stages)
        if (stage.id == processing::StageId::Clahe)
            return std::get<processing::ClaheParameters>(stage.parameters);
    throw std::runtime_error("Missing local contrast stage");
}

application::PresetState toneSeed() {
    auto seed = savedPresetSeed();
    seed.selectedId = {"custom"};
    seed.activePipeline = seed.customPresets.front().pipeline;
    seed.activePipeline.stages[2].enabled = false;
    seed.activePipeline.stages[2].parameters = processing::BrightnessContrastParameters{
        0.12345678901234568, 1.2345678901234567};
    seed.activePipeline.stages[3].parameters = processing::GammaParameters{0.9876543210987654};
    return seed;
}

application::PresetState localContrastSeed() {
    auto seed = toneSeed();
    seed.activePipeline.stages[4].enabled = true;
    seed.activePipeline.stages[4].parameters = processing::ClaheParameters{
        3.141592653589793, 4U};
    return seed;
}

// Default projection, decimal rounding or refresh-triggered submission would
// alter native CLAHE settings before the operator deliberately edits them.
TEST(QmlProcessingAdapter, LocalContrastLoadProjectsExactValuesWithoutSubmittingOnRefresh) {
    const auto seed = localContrastSeed();
    Fixture fixture(true, false, seed);
    ASSERT_TRUE(fixture.start());
    fixture.io->release();
    ASSERT_TRUE(fixture.settled());
    EXPECT_TRUE(fixture.adapter.localContrastEnabled());
    EXPECT_DOUBLE_EQ(fixture.adapter.clipLimit(), 3.141592653589793);
    EXPECT_EQ(fixture.adapter.clipLimitText(), QStringLiteral("3.141592653589793"));
    EXPECT_EQ(fixture.adapter.tileGridSize(), 4);
    EXPECT_EQ(fixture.adapter.tileGridSizeText(), QStringLiteral("4"));
    const auto saved = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    QSignalSpy changes(&fixture.adapter, &qml::ProcessingAdapter::stateChanged);
    for (int i = 0; i < 20; ++i) fixture.adapter.refresh();
    EXPECT_EQ(changes.count(), 0);
    EXPECT_FALSE(fixture.adapter.pending());
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), seed);
}

// Both field commands and the toggle must edit a fresh whole draft, preserving
// the other CLAHE parameter, every unrelated stage and the saved collection.
TEST(QmlProcessingAdapter, LocalContrastExactEditsAndTogglePreserveCompleteDraft) {
    auto seed = localContrastSeed();
    seed.activePipeline.stages[4].enabled = false;
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.loaded());
    ASSERT_TRUE(fixture.adapter.commitClipLimitText("12.345678901234567"));
    ASSERT_TRUE(fixture.adapter.commitTileGridSizeText(" +5 "));
    ASSERT_TRUE(fixture.adapter.setLocalContrastEnabled(true));
    EXPECT_TRUE(fixture.adapter.localContrastEnabled());
    EXPECT_DOUBLE_EQ(fixture.adapter.clipLimit(), 12.345678901234567);
    EXPECT_EQ(fixture.adapter.tileGridSize(), 5);
    auto expected = seed.activePipeline;
    expected.stages[4].enabled = true;
    expected.stages[4].parameters = processing::ClaheParameters{12.345678901234567, 5U};
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, expected));
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), seed);
    ASSERT_TRUE(fixture.adapter.setLocalContrastEnabled(false));
    EXPECT_DOUBLE_EQ(fixture.adapter.clipLimit(), 12.345678901234567);
    EXPECT_EQ(fixture.adapter.tileGridSize(), 5);
}

// Fractional conversion before validation, permissive integer syntax, range
// clamping or image-size clamping would mutate and eventually save this draft.
TEST(QmlProcessingAdapter, LocalContrastRejectsInvalidClipAndGridWithoutMutationOrSave) {
    auto seed = localContrastSeed();
    seed.activePipeline.stages[4].enabled = false;
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto before = fixture.coordinator.processingControls()->draft();
    const auto saved = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    for (const auto* text : {"", " ", "NaN", "inf", "-inf", "1e999", "0.09", "40.01", "1,2", "2x"}) {
        EXPECT_FALSE(fixture.adapter.commitClipLimitText(QString::fromLatin1(text))) << text;
        EXPECT_FALSE(fixture.adapter.validationError().isEmpty()) << text;
    }
    for (const auto value : {0.09, 40.01, std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        EXPECT_FALSE(fixture.adapter.commitClipLimit(value));
        EXPECT_FALSE(fixture.adapter.dragClipLimit(value));
    }
    for (const auto* text : {"", " ", "+", "-2", "+-2", "++2", "--2", "2.0", "2e1",
             "0x10", "2x", "3 0", "1", "33", "4294967296", "999999999999999999999999999999999"}) {
        EXPECT_FALSE(fixture.adapter.commitTileGridSizeText(QString::fromLatin1(text))) << text;
        EXPECT_FALSE(fixture.adapter.validationError().isEmpty()) << text;
    }
    for (const auto value : {1.0, 2.5, 33.0, std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
        EXPECT_FALSE(fixture.adapter.commitTileGridSize(value));
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, before.activePipeline));
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), before);
    for (int i = 0; i < 20; ++i) fixture.coordinator.poll();
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    ASSERT_TRUE(fixture.adapter.commitClipLimit(0.1));
    ASSERT_TRUE(fixture.adapter.commitClipLimitText("40"));
    ASSERT_TRUE(fixture.adapter.commitTileGridSizeText(" +02 "));
    ASSERT_TRUE(fixture.adapter.commitTileGridSize(32.0));
    EXPECT_DOUBLE_EQ(fixture.adapter.clipLimit(), 40.0);
    EXPECT_EQ(fixture.adapter.tileGridSize(), 32);
}

void expectLocalContrastCommandsUnavailable(qml::ProcessingAdapter& adapter) {
    EXPECT_FALSE(adapter.setLocalContrastEnabled(true));
    EXPECT_FALSE(adapter.commitClipLimit(2.5));
    EXPECT_FALSE(adapter.commitClipLimitText(QStringLiteral("2.5")));
    EXPECT_FALSE(adapter.dragClipLimit(2.5));
    EXPECT_FALSE(adapter.releaseClipLimit());
    EXPECT_FALSE(adapter.commitTileGridSize(4.0));
    EXPECT_FALSE(adapter.commitTileGridSizeText(QStringLiteral("4")));
}

// Every command must consult live coordinator authority during loading, after a
// failed load and after shutdown begins, without touching model or persistence.
TEST(QmlProcessingAdapter, LocalContrastCommandsRejectLoadingFailedLoadAndClosing) {
    Fixture fixture(true, false, localContrastSeed());
    expectLocalContrastCommandsUnavailable(fixture.adapter);
    ASSERT_TRUE(fixture.start());
    expectLocalContrastCommandsUnavailable(fixture.adapter);
    EXPECT_FALSE(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision);
    fixture.io->release();
    ASSERT_TRUE(fixture.settled());
    const auto before = fixture.coordinator.processingControls()->draft();
    const auto saved = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    fixture.coordinator.beginShutdown();
    expectLocalContrastCommandsUnavailable(fixture.adapter);
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, before.activePipeline));
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), before);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    Fixture failed(false, true);
    ASSERT_TRUE(failed.start());
    ASSERT_TRUE(failed.wait([&] { return failed.coordinator.processingState().loadCompleted; }));
    expectLocalContrastCommandsUnavailable(failed.adapter);
    EXPECT_FALSE(failed.preferences.latestStatus()->latestAttemptedPresetSaveRevision);
}

// Drag must share the model throttle, while parameterless Release must flush the
// current complete draft after a preset or reset has replaced an obsolete drag.
TEST(QmlProcessingAdapter, LocalContrastDragThrottlesAndReleaseUsesCurrentDraft) {
    Fixture fixture(false, false, localContrastSeed());
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto revision = fixture.adapter.activeRevision();
    ASSERT_TRUE(fixture.adapter.dragClipLimit(4.25));
    ASSERT_TRUE(fixture.adapter.dragClipLimit(4.5));
    for (int i = 0; i < 20; ++i) { fixture.coordinator.poll(); fixture.adapter.refresh(); }
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.activeRevision(), revision);
    ASSERT_TRUE(fixture.adapter.releaseClipLimit());
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    EXPECT_DOUBLE_EQ(localContrast(
        fixture.coordinator.processingControls()->acknowledged()->activePipeline).clipLimit, 4.5);

    const auto seed = savedPresetSeed();
    Fixture replaced(false, false, seed);
    ASSERT_TRUE(replaced.start());
    ASSERT_TRUE(replaced.settled());
    ASSERT_TRUE(replaced.adapter.dragClipLimit(4.75));
    ASSERT_TRUE(replaced.adapter.selectPreset("saved-fractional"));
    ASSERT_TRUE(replaced.adapter.releaseClipLimit());
    EXPECT_EQ(replaced.adapter.selectedPresetId(), QStringLiteral("saved-fractional"));
    EXPECT_DOUBLE_EQ(replaced.adapter.clipLimit(), 2.0);
    EXPECT_EQ(replaced.adapter.tileGridSize(), 8);
    ASSERT_TRUE(replaced.wait([&] { return !replaced.adapter.pending(); }));
    ASSERT_TRUE(replaced.adapter.dragClipLimit(5.25));
    ASSERT_TRUE(replaced.adapter.resetProcessing());
    ASSERT_TRUE(replaced.adapter.releaseClipLimit());
    EXPECT_EQ(replaced.adapter.selectedPresetId(), QStringLiteral("original"));
    EXPECT_DOUBLE_EQ(replaced.adapter.clipLimit(), 2.0);
    EXPECT_EQ(replaced.adapter.tileGridSize(), 8);
    ASSERT_TRUE(replaced.wait([&] { return !replaced.adapter.pending(); }));
    expectSavedCollection(replaced.coordinator.processingControls()->draft(), seed);
}

// Persistence must follow real successful preparation on the 8x6 source and
// retain the exact complete recipe and saved collection across reopen.
TEST(QmlProcessingAdapter, LocalContrastSuccessfulActivationPersistsAndReopens) {
    auto seed = localContrastSeed();
    seed.activePipeline.stages[4].enabled = false;
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    ASSERT_TRUE(fixture.adapter.commitClipLimitText("6.789012345678901"));
    ASSERT_TRUE(fixture.adapter.commitTileGridSizeText("+5"));
    ASSERT_TRUE(fixture.adapter.setLocalContrastEnabled(true));
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending()
            && fixture.preferences.latestStatus()->latestSavedPresetRevision > saved;
    }));
    const auto loaded = fixture.io->store.load();
    ASSERT_TRUE(loaded.hasValue());
    const auto& persisted = loaded.value().presets;
    EXPECT_TRUE(persisted.activePipeline.stages[4].enabled);
    EXPECT_DOUBLE_EQ(localContrast(persisted.activePipeline).clipLimit, 6.789012345678901);
    EXPECT_EQ(localContrast(persisted.activePipeline).tileGridSize, 5U);
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(persisted.activePipeline,
        fixture.coordinator.processingControls()->acknowledged()->activePipeline));
    expectSavedCollection(persisted, seed);
    Fixture reopened(fixture.io->store.path());
    ASSERT_TRUE(reopened.start());
    ASSERT_TRUE(reopened.loaded());
    EXPECT_TRUE(reopened.adapter.localContrastEnabled());
    EXPECT_DOUBLE_EQ(reopened.adapter.clipLimit(), 6.789012345678901);
    EXPECT_EQ(reopened.adapter.tileGridSize(), 5);
    expectSavedCollection(reopened.coordinator.processingControls()->draft(), seed);
}

// A model-valid grid must reach real CLAHE preparation; its 8x6 rejection must
// restore the prior acknowledged custom values and never persist the failed edit.
TEST(QmlProcessingAdapter, LocalContrastImageTooSmallRollsBackAcknowledgedDraftWithoutSave) {
    auto seed = localContrastSeed();
    seed.activePipeline.stages[4].enabled = false;
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto beforeValidSave = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    ASSERT_TRUE(fixture.adapter.commitClipLimitText("7.125"));
    ASSERT_TRUE(fixture.adapter.commitTileGridSize(4.0));
    ASSERT_TRUE(fixture.adapter.setLocalContrastEnabled(true));
    ASSERT_TRUE(fixture.wait([&] {
        const auto status = fixture.preferences.latestStatus();
        return !fixture.adapter.pending()
            && status->latestAttemptedPresetSaveRevision > beforeValidSave
            && status->latestSavedPresetRevision == status->latestAttemptedPresetSaveRevision;
    }));
    ASSERT_TRUE(fixture.adapter.modelError().isEmpty());
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    const auto attempted = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    const auto accepted = *fixture.coordinator.processingControls()->acknowledged();
    ASSERT_TRUE(fixture.adapter.commitTileGridSize(8.0));
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.tileGridSize(), 8);
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && !fixture.adapter.modelError().isEmpty();
    }));
    ASSERT_TRUE(fixture.pipeline.snapshot().processingConfigurationOutcome->error);
    EXPECT_EQ(fixture.pipeline.snapshot().processingConfigurationOutcome->error->code,
        "clahe_image_too_small");
    EXPECT_TRUE(fixture.adapter.localContrastEnabled());
    EXPECT_DOUBLE_EQ(fixture.adapter.clipLimit(), 7.125);
    EXPECT_EQ(fixture.adapter.tileGridSize(), 4);
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, accepted.activePipeline));
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, attempted);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestSavedPresetRevision, saved);
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_DOUBLE_EQ(localContrast(stored.value().presets.activePipeline).clipLimit, 7.125);
    EXPECT_EQ(localContrast(stored.value().presets.activePipeline).tileGridSize, 4U);
    expectSavedCollection(stored.value().presets, seed);
}

// Default snapshots, decimal rounding or refresh-triggered submissions would
// discard loaded native precision before the operator deliberately edits it.
TEST(QmlProcessingAdapter, ToneLoadProjectsExactValuesWithoutSubmittingOnRefresh) {
    const auto seed = toneSeed();
    Fixture fixture(true, false, seed);
    ASSERT_TRUE(fixture.start());
    EXPECT_FALSE(fixture.adapter.available());
    fixture.io->release();
    ASSERT_TRUE(fixture.settled());
    EXPECT_FALSE(fixture.adapter.brightnessContrastEnabled());
    EXPECT_TRUE(fixture.adapter.gammaEnabled());
    EXPECT_DOUBLE_EQ(fixture.adapter.brightness(), 0.12345678901234568);
    EXPECT_DOUBLE_EQ(fixture.adapter.contrast(), 1.2345678901234567);
    EXPECT_DOUBLE_EQ(fixture.adapter.gamma(), 0.9876543210987654);
    EXPECT_EQ(fixture.adapter.brightnessText(), QStringLiteral("0.12345678901234568"));
    EXPECT_EQ(fixture.adapter.contrastText(), QStringLiteral("1.2345678901234567"));
    EXPECT_EQ(fixture.adapter.gammaText(), QStringLiteral("0.9876543210987654"));
    const auto saved = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    QSignalSpy changes(&fixture.adapter, &qml::ProcessingAdapter::stateChanged);
    for (int i = 0; i < 20; ++i) fixture.adapter.refresh();
    EXPECT_EQ(changes.count(), 0);
    EXPECT_FALSE(fixture.adapter.pending());
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), seed);
}

// Each typed member must edit a fresh whole draft; toggling either stage must
// keep its stored parameters and every unrelated stage and saved recipe intact.
TEST(QmlProcessingAdapter, ToneExactCommitsAndTogglesPreserveCompleteDraft) {
    const auto seed = toneSeed();
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.loaded());
    QSignalSpy replacements(&fixture.adapter, &qml::ProcessingAdapter::draftReplaced);
    ASSERT_TRUE(fixture.adapter.setBrightnessContrastEnabled(true));
    EXPECT_DOUBLE_EQ(fixture.adapter.brightness(), 0.12345678901234568);
    EXPECT_DOUBLE_EQ(fixture.adapter.contrast(), 1.2345678901234567);
    ASSERT_TRUE(fixture.adapter.commitBrightnessText("-0.23456789012345678"));
    ASSERT_TRUE(fixture.adapter.commitContrastText("2.3456789012345678"));
    ASSERT_TRUE(fixture.adapter.commitGammaText("1.2345678901234567"));
    EXPECT_DOUBLE_EQ(fixture.adapter.brightness(), -0.23456789012345678);
    EXPECT_DOUBLE_EQ(fixture.adapter.contrast(), 2.3456789012345678);
    EXPECT_DOUBLE_EQ(fixture.adapter.gamma(), 1.2345678901234567);
    EXPECT_DOUBLE_EQ(fixture.adapter.brightnessText().toDouble(), -0.23456789012345678);
    EXPECT_DOUBLE_EQ(fixture.adapter.contrastText().toDouble(), 2.3456789012345678);
    EXPECT_DOUBLE_EQ(fixture.adapter.gammaText().toDouble(), 1.2345678901234567);
    ASSERT_TRUE(fixture.adapter.setBrightnessContrastEnabled(false));
    ASSERT_TRUE(fixture.adapter.setGammaEnabled(false));
    EXPECT_FALSE(fixture.adapter.brightnessContrastEnabled());
    EXPECT_FALSE(fixture.adapter.gammaEnabled());
    auto expected = seed.activePipeline;
    expected.stages[2].parameters = processing::BrightnessContrastParameters{-0.23456789012345678, 2.3456789012345678};
    expected.stages[3].enabled = false;
    expected.stages[3].parameters = processing::GammaParameters{1.2345678901234567};
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, expected));
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), seed);
    ASSERT_TRUE(fixture.adapter.setGammaEnabled(true));
    EXPECT_DOUBLE_EQ(fixture.adapter.gamma(), 1.2345678901234567);
    EXPECT_EQ(replacements.count(), 0);
}

struct ToneField final {
    const char* name;
    bool (qml::ProcessingAdapter::*commit)(double);
    bool (qml::ProcessingAdapter::*commitText)(const QString&);
    bool (qml::ProcessingAdapter::*drag)(double);
    bool (qml::ProcessingAdapter::*release)();
    double (qml::ProcessingAdapter::*value)() const;
    double minimum, maximum;
};
constexpr ToneField toneFields[]{
    {"brightness", &qml::ProcessingAdapter::commitBrightness, &qml::ProcessingAdapter::commitBrightnessText,
        &qml::ProcessingAdapter::dragBrightness, &qml::ProcessingAdapter::releaseBrightness,
        &qml::ProcessingAdapter::brightness, -1.0, 1.0},
    {"contrast", &qml::ProcessingAdapter::commitContrast, &qml::ProcessingAdapter::commitContrastText,
        &qml::ProcessingAdapter::dragContrast, &qml::ProcessingAdapter::releaseContrast,
        &qml::ProcessingAdapter::contrast, 0.0, 4.0},
    {"gamma", &qml::ProcessingAdapter::commitGamma, &qml::ProcessingAdapter::commitGammaText,
        &qml::ProcessingAdapter::dragGamma, &qml::ProcessingAdapter::releaseGamma,
        &qml::ProcessingAdapter::gamma, 0.1, 5.0}};

// Parser permissiveness, nonfinite acceptance or endpoint clamping must not
// mutate or save a rejected draft; valid inclusive endpoints remain exact.
TEST(QmlProcessingAdapter, ToneRejectsMalformedNonfiniteAndOutOfRangeWithoutMutationOrSave) {
    Fixture fixture(false, false, toneSeed());
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto before = fixture.coordinator.processingControls()->draft();
    const auto saved = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    for (const auto& field : toneFields) {
        SCOPED_TRACE(field.name);
        for (const auto* text : {"", " ", "NaN", "inf", "-inf", "1e999", "1,234", "1.2x", "0x1", "+-0", "++0", "--0", "1 2"}) {
            EXPECT_FALSE((fixture.adapter.*field.commitText)(QString::fromLatin1(text))) << text;
            EXPECT_FALSE(fixture.adapter.validationError().isEmpty()) << text;
        }
        for (const auto value : {field.minimum - 0.01, field.maximum + 0.01,
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()}) {
            EXPECT_FALSE((fixture.adapter.*field.commit)(value));
            EXPECT_FALSE((fixture.adapter.*field.drag)(value));
        }
        EXPECT_FALSE((fixture.adapter.*field.commitText)(QString::number(field.minimum - 0.01)));
        EXPECT_FALSE((fixture.adapter.*field.commitText)(QString::number(field.maximum + 0.01)));
    }
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, before.activePipeline));
    EXPECT_EQ(fixture.coordinator.processingControls()->draft().selectedId, before.selectedId);
    for (int i = 0; i < 20; ++i) fixture.coordinator.poll();
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    for (const auto& field : toneFields) {
        SCOPED_TRACE(field.name);
        ASSERT_TRUE((fixture.adapter.*field.commit)(field.minimum));
        EXPECT_DOUBLE_EQ((fixture.adapter.*field.value)(), field.minimum);
        ASSERT_TRUE((fixture.adapter.*field.commitText)(QString::number(field.maximum)));
        EXPECT_DOUBLE_EQ((fixture.adapter.*field.value)(), field.maximum);
        ASSERT_TRUE((fixture.adapter.*field.commitText)(QStringLiteral(" +5e-1 ")));
        EXPECT_DOUBLE_EQ((fixture.adapter.*field.value)(), 0.5);
        EXPECT_TRUE(fixture.adapter.validationError().isEmpty());
    }
    ASSERT_TRUE(fixture.adapter.commitBrightnessText("-0"));
    EXPECT_DOUBLE_EQ(fixture.adapter.brightness(), 0);
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), before);
}

void expectToneCommandsUnavailable(qml::ProcessingAdapter& adapter) {
    EXPECT_FALSE(adapter.setBrightnessContrastEnabled(true));
    EXPECT_FALSE(adapter.setGammaEnabled(true));
    for (const auto& field : toneFields) {
        SCOPED_TRACE(field.name);
        EXPECT_FALSE((adapter.*field.commit)(0.5));
        EXPECT_FALSE((adapter.*field.commitText)(QStringLiteral("0.5")));
        EXPECT_FALSE((adapter.*field.drag)(0.5));
        EXPECT_FALSE((adapter.*field.release)());
    }
}

// A cached available flag is insufficient once shutdown starts; every command
// must consult the live coordinator before touching the model or settings.
TEST(QmlProcessingAdapter, ToneCommandsRejectLoadingFailedLoadAndClosing) {
    Fixture fixture(true, false, toneSeed());
    QSignalSpy replacements(&fixture.adapter, &qml::ProcessingAdapter::draftReplaced);
    expectToneCommandsUnavailable(fixture.adapter);
    ASSERT_TRUE(fixture.start());
    expectToneCommandsUnavailable(fixture.adapter);
    EXPECT_FALSE(fixture.adapter.selectPreset("original"));
    EXPECT_FALSE(fixture.adapter.resetProcessing());
    EXPECT_FALSE(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision);
    fixture.io->release();
    ASSERT_TRUE(fixture.settled());
    const auto before = fixture.coordinator.processingControls()->draft();
    const auto saved = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    fixture.coordinator.beginShutdown();
    // Deliberately do not refresh the adapter first.
    expectToneCommandsUnavailable(fixture.adapter);
    EXPECT_FALSE(fixture.adapter.selectPreset("original"));
    EXPECT_FALSE(fixture.adapter.resetProcessing());
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, before.activePipeline));
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), before);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    EXPECT_EQ(replacements.count(), 0);
    Fixture failed(false, true);
    ASSERT_TRUE(failed.start());
    ASSERT_TRUE(failed.wait([&] { return failed.coordinator.processingState().loadCompleted; }));
    expectToneCommandsUnavailable(failed.adapter);
    EXPECT_FALSE(failed.preferences.latestStatus()->latestAttemptedPresetSaveRevision);
}

// Removing Drag phase throttling or not flushing an unchanged Release would
// publish too early, or leave a final operator change pending indefinitely.
TEST(QmlProcessingAdapter, ToneDragThrottlesAndEveryReleaseFlushesCurrentDraft) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    for (const auto& field : toneFields) {
        SCOPED_TRACE(field.name);
        const auto revision = fixture.adapter.activeRevision();
        ASSERT_TRUE((fixture.adapter.*field.drag)(0.25));
        ASSERT_TRUE((fixture.adapter.*field.drag)(0.5));
        for (int i = 0; i < 20; ++i) { fixture.coordinator.poll(); fixture.adapter.refresh(); }
        EXPECT_TRUE(fixture.adapter.pending());
        EXPECT_EQ(fixture.adapter.activeRevision(), revision);
        ASSERT_TRUE((fixture.adapter.*field.release)());
        ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
        EXPECT_NE(fixture.adapter.activeRevision(), revision);
        EXPECT_DOUBLE_EQ((fixture.adapter.*field.value)(), 0.5);
        const auto& accepted = fixture.coordinator.processingControls()->acknowledged()->activePipeline;
        EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
            accepted, fixture.coordinator.processingControls()->draft().activePipeline));
    }
    const auto revision = fixture.adapter.activeRevision();
    ASSERT_TRUE(fixture.adapter.dragGamma(0.75));
    fixture.clock.advance(34ms);
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    EXPECT_NE(fixture.adapter.activeRevision(), revision);
    EXPECT_DOUBLE_EQ(gamma(fixture.coordinator.processingControls()->acknowledged()->activePipeline).gamma, 0.75);
}

// A release carrying an obsolete slider value after draft replacement would
// restore Custom and overwrite the newly selected recipe or reset values.
TEST(QmlProcessingAdapter, ToneReleaseAfterPresetAndResetNeverRestoresObsoleteDrag) {
    const auto seed = savedPresetSeed();
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    ASSERT_TRUE(fixture.adapter.dragBrightness(0.375));
    ASSERT_TRUE(fixture.adapter.dragContrast(3.125));
    ASSERT_TRUE(fixture.adapter.dragGamma(2.25));
    ASSERT_TRUE(fixture.adapter.selectPreset("saved-fractional"));
    for (const auto& field : toneFields) ASSERT_TRUE((fixture.adapter.*field.release)());
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("saved-fractional"));
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, seed.customPresets.front().pipeline));
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    ASSERT_TRUE(fixture.adapter.dragBrightness(-0.375));
    ASSERT_TRUE(fixture.adapter.dragContrast(2.125));
    ASSERT_TRUE(fixture.adapter.dragGamma(3.25));
    ASSERT_TRUE(fixture.adapter.resetProcessing());
    for (const auto& field : toneFields) ASSERT_TRUE((fixture.adapter.*field.release)());
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("original"));
    EXPECT_DOUBLE_EQ(fixture.adapter.brightness(), 0.0);
    EXPECT_DOUBLE_EQ(fixture.adapter.contrast(), 1.0);
    EXPECT_DOUBLE_EQ(fixture.adapter.gamma(), 1.0);
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    EXPECT_EQ(fixture.coordinator.processingControls()->acknowledged()->selectedId.value, "original");
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), seed);
}

// Preset/reset cancellation needs its own notification even when Reset leaves
// selectedId unchanged; signal observers must already see the replaced draft.
TEST(QmlProcessingAdapter, DraftReplacementNotifiesAfterEveryAdmittedPresetAndReset) {
    Fixture fixture(false, false, savedPresetSeed());
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    QSignalSpy replacements(&fixture.adapter, &qml::ProcessingAdapter::draftReplaced);
    QString notifiedId;
    double notifiedGamma{};
    QStringList notifications;
    QObject::connect(&fixture.adapter, &qml::ProcessingAdapter::stateChanged, [&] {
        notifications.push_back(QStringLiteral("state"));
    });
    QObject::connect(&fixture.adapter, &qml::ProcessingAdapter::draftReplaced, [&] {
        notifications.push_back(QStringLiteral("replacement"));
        notifiedId = fixture.adapter.selectedPresetId();
        notifiedGamma = fixture.adapter.gamma();
    });
    ASSERT_TRUE(fixture.adapter.selectPreset("saved-fractional"));
    EXPECT_EQ(replacements.count(), 1);
    EXPECT_EQ(notifiedId, QStringLiteral("saved-fractional"));
    EXPECT_DOUBLE_EQ(notifiedGamma, 1.25);
    EXPECT_EQ(notifications, (QStringList{"replacement", "state"}));
    notifications.clear();
    ASSERT_TRUE(fixture.adapter.resetProcessing());
    EXPECT_EQ(replacements.count(), 2);
    EXPECT_EQ(notifiedId, QStringLiteral("original"));
    EXPECT_DOUBLE_EQ(notifiedGamma, 1.0);
    EXPECT_EQ(notifications, (QStringList{"replacement", "state"}));
    notifications.clear();
    ASSERT_TRUE(fixture.adapter.resetProcessing());
    EXPECT_EQ(replacements.count(), 3);
    EXPECT_EQ(notifications, (QStringList{"replacement"}));
    EXPECT_FALSE(fixture.adapter.selectPreset("missing-preset"));
    EXPECT_EQ(replacements.count(), 3);
    // Admission succeeds before asynchronous engine preparation rejects CLAHE.
    ASSERT_TRUE(fixture.adapter.selectPreset("standard"));
    EXPECT_EQ(replacements.count(), 4);
    EXPECT_EQ(notifiedId, QStringLiteral("standard"));
}

// Saving the draft before successful acknowledgement, rebuilding only tone
// stages, or lossy text conversion would corrupt the persisted/reopened recipe.
TEST(QmlProcessingAdapter, ToneSuccessfulActivationPersistsExactWholeRecipeAndReopens) {
    const auto seed = toneSeed();
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto active = fixture.adapter.activeSummary();
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    ASSERT_TRUE(fixture.adapter.commitBrightnessText("-0.23456789012345678"));
    ASSERT_TRUE(fixture.adapter.commitContrastText("2.3456789012345678"));
    ASSERT_TRUE(fixture.adapter.commitGammaText("1.2345678901234567"));
    ASSERT_TRUE(fixture.adapter.setBrightnessContrastEnabled(true));
    ASSERT_TRUE(fixture.adapter.setGammaEnabled(false));
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.activeSummary(), active);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && fixture.preferences.latestStatus()->latestSavedPresetRevision > saved;
    }));
    const auto loaded = fixture.io->store.load();
    ASSERT_TRUE(loaded.hasValue());
    const auto& persisted = loaded.value().presets;
    EXPECT_DOUBLE_EQ(brightnessContrast(persisted.activePipeline).brightness, -0.23456789012345678);
    EXPECT_DOUBLE_EQ(brightnessContrast(persisted.activePipeline).contrast, 2.3456789012345678);
    EXPECT_DOUBLE_EQ(gamma(persisted.activePipeline).gamma, 1.2345678901234567);
    EXPECT_TRUE(persisted.activePipeline.stages[2].enabled);
    EXPECT_FALSE(persisted.activePipeline.stages[3].enabled);
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(persisted.activePipeline,
        fixture.coordinator.processingControls()->acknowledged()->activePipeline));
    expectSavedCollection(persisted, seed);
    Fixture reopened(fixture.io->store.path());
    ASSERT_TRUE(reopened.start());
    ASSERT_TRUE(reopened.loaded());
    EXPECT_DOUBLE_EQ(reopened.adapter.brightness(), -0.23456789012345678);
    EXPECT_DOUBLE_EQ(reopened.adapter.contrast(), 2.3456789012345678);
    EXPECT_DOUBLE_EQ(reopened.adapter.gamma(), 1.2345678901234567);
    EXPECT_TRUE(reopened.adapter.brightnessContrastEnabled());
    EXPECT_FALSE(reopened.adapter.gammaEnabled());
    expectSavedCollection(reopened.coordinator.processingControls()->draft(), seed);
}

// Missing rows, reordered IDs, name normalization and spurious list notifications
// would rebind the operator's choice while the shared model is being refreshed.
TEST(QmlProcessingAdapter, PresetRowsAppearAfterLoadAndRemainStableAcrossNumericEdits) {
    const auto seed = savedPresetSeed();
    Fixture fixture(true, false, seed);
    ASSERT_TRUE(fixture.start());
    EXPECT_TRUE(fixture.adapter.presets().isEmpty());
    EXPECT_TRUE(fixture.adapter.selectedPresetId().isEmpty());
    EXPECT_FALSE(fixture.adapter.selectPreset("standard"));
    EXPECT_FALSE(fixture.adapter.resetProcessing());
    EXPECT_FALSE(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision);
    fixture.io->release();
    QSignalSpy listChanges(&fixture.adapter, &qml::ProcessingAdapter::presetsChanged);
    ASSERT_TRUE(fixture.loaded());
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("original"));
    const auto rows = fixture.adapter.presets();
    const QStringList ids{"original", "standard", "high-contrast", "soft-detail", "custom", "saved-fractional"};
    const QStringList names{"Original", "Standard", "High Contrast", "Soft Detail", "Custom", "Inspection précise"};
    ASSERT_EQ(rows.size(), ids.size());
    for (qsizetype i = 0; i < ids.size(); ++i) {
        const auto row = rows[i].toMap();
        EXPECT_EQ(row.value("id").toString(), ids[i]);
        EXPECT_EQ(row.value("name").toString(), names[i]);
        EXPECT_TRUE(row.contains("description"));
    }
    EXPECT_EQ(rows.back().toMap().value("description").toString(), QStringLiteral("Fractional detail recipe"));
    EXPECT_EQ(listChanges.count(), 1);
    listChanges.clear();
    ASSERT_TRUE(fixture.adapter.commitWindow(1234.5));
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("custom"));
    for (int i = 0; i < 20; ++i) fixture.adapter.refresh();
    EXPECT_EQ(fixture.adapter.presets(), rows);
    EXPECT_EQ(listChanges.count(), 0);
    ASSERT_TRUE(fixture.settled());
    EXPECT_EQ(listChanges.count(), 0);
}

// A direct adapter save, completion poll in refresh, or selected-ID-only recipe
// reconstruction would violate acknowledged persistence or lose loaded stages.
TEST(QmlProcessingAdapter, SavedPresetSelectionAcknowledgesFullRecipeAndReopens) {
    const auto seed = savedPresetSeed();
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    const auto active = fixture.adapter.activeSummary();
    const auto revision = fixture.adapter.activeRevision();
    EXPECT_FALSE(fixture.adapter.commitWindowText("bad input"));
    ASSERT_FALSE(fixture.adapter.validationError().isEmpty());
    ASSERT_TRUE(fixture.adapter.selectPreset("saved-fractional"));
    EXPECT_TRUE(fixture.adapter.validationError().isEmpty());
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("saved-fractional"));
    EXPECT_EQ(fixture.adapter.draftPresetName(), QStringLiteral("Inspection précise"));
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.activeSummary(), active);
    EXPECT_EQ(fixture.adapter.activeRevision(), revision);
    EXPECT_DOUBLE_EQ(fixture.adapter.window(), 1000.1234567890123);
    EXPECT_DOUBLE_EQ(fixture.adapter.level(), 2000.9876543210987);
    for (int i = 0; i < 20; ++i) fixture.adapter.refresh();
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && fixture.preferences.latestStatus()->latestSavedPresetRevision > saved;
    }));
    EXPECT_TRUE(fixture.adapter.modelError().isEmpty());
    EXPECT_NE(fixture.adapter.activeRevision(), revision);
    EXPECT_TRUE(fixture.adapter.activeSummary().startsWith("Inspection précise\n"));
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_EQ(stored.value().presets.selectedId.value, "saved-fractional");
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        stored.value().presets.activePipeline, seed.customPresets.front().pipeline));
    expectSavedCollection(stored.value().presets, seed);
    Fixture reopened(fixture.io->store.path());
    ASSERT_TRUE(reopened.start());
    ASSERT_TRUE(reopened.loaded());
    EXPECT_EQ(reopened.adapter.selectedPresetId(), QStringLiteral("saved-fractional"));
    EXPECT_EQ(reopened.adapter.draftPresetName(), QStringLiteral("Inspection précise"));
    EXPECT_DOUBLE_EQ(reopened.adapter.window(), 1000.1234567890123);
    EXPECT_DOUBLE_EQ(reopened.adapter.level(), 2000.9876543210987);
    expectSavedCollection(reopened.coordinator.processingControls()->draft(), seed);
}

TEST(QmlProcessingAdapter, ResetAcknowledgesOriginalAndPreservesSavedRecipes) {
    auto seed = savedPresetSeed();
    seed.selectedId = {"saved-fractional"};
    seed.activePipeline = seed.customPresets.front().pipeline;
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    const auto active = fixture.adapter.activeSummary();
    EXPECT_FALSE(fixture.adapter.commitLevelText("bad input"));
    ASSERT_TRUE(fixture.adapter.resetProcessing());
    EXPECT_TRUE(fixture.adapter.validationError().isEmpty());
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("original"));
    EXPECT_EQ(fixture.adapter.draftPresetName(), QStringLiteral("Original"));
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.activeSummary(), active);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && fixture.preferences.latestStatus()->latestSavedPresetRevision > saved;
    }));
    EXPECT_TRUE(fixture.adapter.activeSummary().startsWith("Original\n"));
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_EQ(stored.value().presets.selectedId.value, "original");
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        stored.value().presets.activePipeline, processing::defaultPipeline()));
    expectSavedCollection(stored.value().presets, seed);
}

TEST(QmlProcessingAdapter, InvalidPresetIdsAndShutdownCannotMutateOrSaveDraft) {
    const auto seed = savedPresetSeed();
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto before = fixture.coordinator.processingControls()->draft();
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    for (const auto* id : {"", "missing", "Standard", " standard", "saved-fractional "}) {
        EXPECT_FALSE(fixture.adapter.selectPreset(QString::fromLatin1(id))) << id;
        EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("original"));
        EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
            fixture.coordinator.processingControls()->draft().activePipeline, before.activePipeline));
        EXPECT_FALSE(fixture.adapter.pending());
    }
    EXPECT_FALSE(fixture.adapter.modelError().isEmpty());
    for (int i = 0; i < 20; ++i) { fixture.coordinator.poll(); fixture.adapter.refresh(); }
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    fixture.coordinator.beginShutdown();
    fixture.adapter.refresh();
    EXPECT_FALSE(fixture.adapter.available());
    EXPECT_FALSE(fixture.adapter.selectPreset("saved-fractional"));
    EXPECT_FALSE(fixture.adapter.resetProcessing());
    EXPECT_EQ(fixture.coordinator.processingControls()->draft().selectedId, before.selectedId);
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, before.activePipeline));
    expectSavedCollection(fixture.coordinator.processingControls()->draft(), seed);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_EQ(stored.value().presets.selectedId.value, "original");
}

TEST(QmlProcessingAdapter, NewerPresetChoiceSupersedesUnsubmittedSelection) {
    Fixture fixture(false, false, savedPresetSeed());
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    const auto active = fixture.adapter.activeSummary();
    ASSERT_TRUE(fixture.adapter.selectPreset("standard"));
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("standard"));
    ASSERT_TRUE(fixture.adapter.selectPreset("saved-fractional"));
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("saved-fractional"));
    EXPECT_EQ(fixture.adapter.activeSummary(), active);
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && fixture.preferences.latestStatus()->latestSavedPresetRevision > saved;
    }));
    EXPECT_TRUE(fixture.adapter.modelError().isEmpty());
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_EQ(stored.value().presets.selectedId.value, "saved-fractional");
}

// Refresh must publish loading completion, not consume or poll it itself.
TEST(QmlProcessingAdapter, BindsAfterDelayedLoadAndOnlyNotifiesForChangedState) {
    Fixture fixture(true);
    ASSERT_TRUE(fixture.start());
    EXPECT_EQ(fixture.adapter.loadingState(), qml::ProcessingAdapter::Loading);
    EXPECT_FALSE(fixture.adapter.available());
    EXPECT_FALSE(fixture.adapter.commitWindowText("1200.25"));
    EXPECT_FALSE(fixture.adapter.validationError().isEmpty());
    fixture.io->release();
    QSignalSpy changes(&fixture.adapter, &qml::ProcessingAdapter::stateChanged);
    ASSERT_TRUE(fixture.loaded());
    EXPECT_EQ(fixture.adapter.loadingState(), qml::ProcessingAdapter::Ready);
    EXPECT_TRUE(fixture.adapter.available());
    EXPECT_GT(changes.count(), 0);
    EXPECT_DOUBLE_EQ(fixture.adapter.level(), 32767.5);
    EXPECT_EQ(fixture.adapter.levelText(), QStringLiteral("32767.5"));
    changes.clear();
    fixture.adapter.refresh();
    fixture.adapter.refresh();
    EXPECT_EQ(changes.count(), 0);
}

TEST(QmlProcessingAdapter, ReportsLoadFailureSeparatelyAndRejectsEditing) {
    Fixture fixture(false, true);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.wait([&] { return fixture.coordinator.processingState().loadCompleted; }));
    EXPECT_EQ(fixture.adapter.loadingState(), qml::ProcessingAdapter::Unavailable);
    EXPECT_FALSE(fixture.adapter.available());
    EXPECT_FALSE(fixture.adapter.loadError().isEmpty());
    EXPECT_FALSE(fixture.adapter.setStageEnabled(true));
    EXPECT_FALSE(fixture.adapter.commitLevel(123.5));
    EXPECT_FALSE(fixture.adapter.hasAcknowledged());
    EXPECT_TRUE(fixture.adapter.presets().isEmpty());
    EXPECT_TRUE(fixture.adapter.selectedPresetId().isEmpty());
    EXPECT_FALSE(fixture.adapter.selectPreset("standard"));
    EXPECT_FALSE(fixture.adapter.resetProcessing());
    EXPECT_FALSE(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision);
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_EQ(stored.value().presets.selectedId.value, "original");
}

// Integer conversion, locale-dependent parsing, or full-pipeline reconstruction
// would lose the exact native value or unrelated loaded stage configuration.
TEST(QmlProcessingAdapter, ExactTextAndEnabledEditsPreserveAllOtherStages) {
    application::PresetState seed;
    seed.selectedId = {"custom"};
    seed.activePipeline = processing::standardPipeline();
    seed.activePipeline.stages[4].enabled = false;
    std::get<processing::GammaParameters>(seed.activePipeline.stages[3].parameters).gamma = 1.25;
    std::get<processing::SharpenParameters>(seed.activePipeline.stages[6].parameters).threshold = 13.5;
    Fixture fixture(false, false, seed);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.loaded());
    ASSERT_TRUE(fixture.adapter.commitWindowText("12345.678901234567"));
    ASSERT_TRUE(fixture.adapter.commitLevelText("32767.500000000004"));
    EXPECT_DOUBLE_EQ(fixture.adapter.window(), 12345.678901234567);
    EXPECT_DOUBLE_EQ(fixture.adapter.level(), 32767.500000000004);
    EXPECT_EQ(fixture.adapter.windowText(), QStringLiteral("12345.678901234567"));
    EXPECT_EQ(fixture.adapter.levelText(), QStringLiteral("32767.500000000004"));
    ASSERT_TRUE(fixture.adapter.setStageEnabled(false));
    EXPECT_FALSE(fixture.adapter.stageEnabled());
    auto expected = seed.activePipeline;
    expected.stages[1].enabled = false;
    expected.stages[1].parameters = processing::WindowLevelParameters{12345.678901234567, 32767.500000000004};
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, expected));
}

TEST(QmlProcessingAdapter, RejectsMalformedNonfiniteAndOutOfRangeWithoutMutatingDraft) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.loaded());
    ASSERT_TRUE(fixture.adapter.commitWindow(1234.5));
    ASSERT_TRUE(fixture.adapter.commitLevel(2345.75));
    const auto before = fixture.coordinator.processingControls()->draft().activePipeline;
    for (const auto* text : {"", " ", "NaN", "inf", "-inf", "1e999", "1,234", "12abc", "0x10", "65536", "-1"}) {
        EXPECT_FALSE(fixture.adapter.commitWindowText(QString::fromLatin1(text))) << text;
        EXPECT_FALSE(fixture.adapter.validationError().isEmpty()) << text;
    }
    EXPECT_FALSE(fixture.adapter.commitWindow(0));
    EXPECT_FALSE(fixture.adapter.dragLevel(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(fixture.adapter.releaseWindow(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(fixture.adapter.commitLevel(-0.5));
    for (const auto* text : {"+-0", "+-0.0", "++0", "--0"}) {
        EXPECT_FALSE(fixture.adapter.commitLevelText(QString::fromLatin1(text))) << text;
        EXPECT_FALSE(fixture.adapter.validationError().isEmpty()) << text;
    }
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(
        fixture.coordinator.processingControls()->draft().activePipeline, before));
    ASSERT_TRUE(fixture.adapter.commitLevelText("0"));
    EXPECT_TRUE(fixture.adapter.validationError().isEmpty());
    EXPECT_DOUBLE_EQ(fixture.adapter.level(), 0);
    ASSERT_TRUE(fixture.adapter.commitWindowText("65535"));
    EXPECT_DOUBLE_EQ(fixture.adapter.window(), 65535);
    ASSERT_TRUE(fixture.adapter.commitLevelText("+1.25e2"));
    EXPECT_DOUBLE_EQ(fixture.adapter.level(), 125);
    ASSERT_TRUE(fixture.adapter.commitLevelText("-0"));
    EXPECT_DOUBLE_EQ(fixture.adapter.level(), 0);
}

TEST(QmlProcessingAdapter, RefreshDoesNotSubmitAndDraftDoesNotReplaceAcknowledgedSummary) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto activeSummary = fixture.adapter.activeSummary();
    const auto activeRevision = fixture.adapter.activeRevision();
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    ASSERT_TRUE(fixture.adapter.commitWindow(2468.125));
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.activeSummary(), activeSummary);
    EXPECT_EQ(fixture.adapter.activeRevision(), activeRevision);
    for (int i = 0; i < 20; ++i) fixture.adapter.refresh();
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    EXPECT_DOUBLE_EQ(windowLevel(fixture.coordinator.processingControls()->acknowledged()->activePipeline).window, 65535);
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    EXPECT_NE(fixture.adapter.activeSummary(), activeSummary);
    EXPECT_NE(fixture.adapter.activeRevision(), activeRevision);
    EXPECT_TRUE(fixture.adapter.activeSummary().contains("2468.125"));
}

TEST(QmlProcessingAdapter, UnchangedSliderReleaseFlushesBeforeDragInterval) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto revision = fixture.adapter.activeRevision();
    ASSERT_TRUE(fixture.adapter.dragWindow(1900.25));
    ASSERT_TRUE(fixture.adapter.dragWindow(1901.125));
    ASSERT_TRUE(fixture.adapter.dragLevel(850.625));
    for (int i = 0; i < 20; ++i) { fixture.coordinator.poll(); fixture.adapter.refresh(); }
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.activeRevision(), revision);
    ASSERT_TRUE(fixture.adapter.releaseWindow(1901.125));
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    const auto& accepted = fixture.coordinator.processingControls()->acknowledged()->activePipeline;
    EXPECT_DOUBLE_EQ(windowLevel(accepted).window, 1901.125);
    EXPECT_DOUBLE_EQ(windowLevel(accepted).level, 850.625);
    EXPECT_NE(fixture.adapter.activeRevision(), revision);
    ASSERT_TRUE(fixture.adapter.dragLevel(851.375));
    ASSERT_TRUE(fixture.adapter.releaseLevel(851.625));
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    EXPECT_DOUBLE_EQ(windowLevel(fixture.coordinator.processingControls()->acknowledged()->activePipeline).level, 851.625);
    EXPECT_EQ(fixture.clock.steadyNow(), std::chrono::steady_clock::time_point{});
}

TEST(QmlProcessingAdapter, SuccessfulActivationPersistsExactValuesAndReopens) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    ASSERT_TRUE(fixture.adapter.commitWindowText("1000.1234567890123"));
    ASSERT_TRUE(fixture.adapter.commitLevelText("2000.9876543210987"));
    ASSERT_TRUE(fixture.adapter.setStageEnabled(false));
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && fixture.preferences.latestStatus()->latestSavedPresetRevision > saved;
    }));
    auto loaded = fixture.io->store.load();
    ASSERT_TRUE(loaded.hasValue());
    const auto persisted = loaded.value().presets;
    EXPECT_DOUBLE_EQ(windowLevel(persisted.activePipeline).window, 1000.1234567890123);
    EXPECT_DOUBLE_EQ(windowLevel(persisted.activePipeline).level, 2000.9876543210987);
    EXPECT_FALSE(persisted.activePipeline.stages[1].enabled);
    // Runtime submission identities are deliberately not persisted by the
    // existing codec; compare the admitted configuration rather than its ID.
    EXPECT_TRUE(processing::semanticallyEqualPipelineDefinitions(persisted.activePipeline,
        fixture.coordinator.processingControls()->acknowledged()->activePipeline));
    const auto attempt = fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    for (int i = 0; i < 20; ++i) fixture.coordinator.poll();
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, attempt);
    Fixture reopened(fixture.io->store.path());
    ASSERT_TRUE(reopened.start());
    ASSERT_TRUE(reopened.loaded());
    EXPECT_DOUBLE_EQ(reopened.adapter.window(), 1000.1234567890123);
    EXPECT_DOUBLE_EQ(reopened.adapter.level(), 2000.9876543210987);
    EXPECT_FALSE(reopened.adapter.stageEnabled());
}

// CLAHE's grid cannot fit this real 8x6 source. A model-valid draft therefore
// reaches actual engine preparation and fails, without a processor mock.
TEST(QmlProcessingAdapter, FailedEngineActivationNeverPersistsOrChangesAcknowledgedValues) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    const auto saved = fixture.preferences.latestStatus()->latestSavedPresetRevision;
    const auto active = fixture.adapter.activeSummary();
    const auto revision = fixture.adapter.activeRevision();
    ASSERT_TRUE(fixture.adapter.selectPreset("standard"));
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("standard"));
    EXPECT_TRUE(fixture.adapter.pending());
    EXPECT_EQ(fixture.adapter.activeSummary(), active);
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending() && !fixture.adapter.modelError().isEmpty(); }));
    ASSERT_TRUE(fixture.pipeline.snapshot().processingConfigurationOutcome->error);
    EXPECT_EQ(fixture.pipeline.snapshot().processingConfigurationOutcome->error->code, "clahe_image_too_small");
    EXPECT_EQ(fixture.adapter.activeSummary(), active);
    EXPECT_EQ(fixture.adapter.activeRevision(), revision);
    EXPECT_DOUBLE_EQ(fixture.adapter.window(), 65535);
    EXPECT_EQ(fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision, saved);
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_DOUBLE_EQ(windowLevel(stored.value().presets.activePipeline).window, 65535);
    EXPECT_EQ(stored.value().presets.selectedId.value, "original");
    EXPECT_EQ(fixture.adapter.selectedPresetId(), QStringLiteral("original"));
    ASSERT_TRUE(fixture.adapter.resetProcessing());
    EXPECT_TRUE(fixture.adapter.modelError().isEmpty());
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.pending(); }));
    EXPECT_TRUE(fixture.adapter.modelError().isEmpty());
}

TEST(QmlProcessingAdapter, PersistenceFailureLeavesAcknowledgedActivationVisible) {
    Fixture fixture;
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    fixture.io->failSave = true;
    ASSERT_TRUE(fixture.adapter.commitLevel(2500.125));
    ASSERT_TRUE(fixture.wait([&] { return !fixture.adapter.persistenceWarning().isEmpty(); }));
    EXPECT_FALSE(fixture.adapter.pending());
    EXPECT_TRUE(fixture.adapter.hasAcknowledged());
    EXPECT_TRUE(fixture.adapter.activeSummary().contains("2500.125"));
    EXPECT_TRUE(fixture.adapter.modelError().isEmpty());
    const auto stored = fixture.io->store.load();
    ASSERT_TRUE(stored.hasValue());
    EXPECT_DOUBLE_EQ(windowLevel(stored.value().presets.activePipeline).level, 32767.5);
    fixture.io->failSave = false;
    ASSERT_TRUE(fixture.adapter.commitLevel(2501.125));
    ASSERT_TRUE(fixture.wait([&] {
        return !fixture.adapter.pending() && fixture.adapter.persistenceWarning().isEmpty()
            && fixture.preferences.latestStatus()->latestSavedPresetRevision
                == fixture.preferences.latestStatus()->latestAttemptedPresetSaveRevision;
    }));
}

TEST(QmlProcessingAdapter, RejectsRetryUntilBackendMakesItAvailable) {
    Fixture fixture;
    EXPECT_FALSE(fixture.adapter.retryEnabled());
    EXPECT_FALSE(fixture.adapter.retry());
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    EXPECT_FALSE(fixture.adapter.fallback());
    EXPECT_FALSE(fixture.adapter.retryEnabled());
    EXPECT_TRUE(fixture.adapter.processingError().isEmpty());
    EXPECT_FALSE(fixture.adapter.retry());
    EXPECT_FALSE(fixture.adapter.processingError().isEmpty());
    fixture.adapter.refresh();
    EXPECT_FALSE(fixture.adapter.processingError().isEmpty());
    ASSERT_TRUE(fixture.wait([&] {
        const auto& state = fixture.coordinator.state();
        return state.cameraStatus && !state.cameraStatus->discoveredDescriptors.empty()
            && !state.ordinaryOperationPending;
    }));
    fixture.coordinator.selectCamera({"SIM-LIVE"});
    ASSERT_TRUE(fixture.coordinator.dispatch(presentation::CameraStartupIntent::Connect).hasValue());
    ASSERT_TRUE(fixture.wait([&] {
        return fixture.coordinator.state().contextBound
            && !fixture.coordinator.state().ordinaryOperationPending && !fixture.adapter.pending();
    }));
    const auto generation = fixture.pipeline.snapshot().context->generation;
    ASSERT_TRUE(fixture.coordinator.dispatch(presentation::CameraStartupIntent::Disconnect).hasValue());
    ASSERT_TRUE(fixture.wait([&] { return !fixture.coordinator.state().ordinaryOperationPending; }));
    ASSERT_TRUE(fixture.coordinator.dispatch(presentation::CameraStartupIntent::Connect).hasValue());
    ASSERT_TRUE(fixture.wait([&] {
        return fixture.coordinator.state().contextBound
            && !fixture.coordinator.state().ordinaryOperationPending && !fixture.adapter.pending();
    }));
    ASSERT_NE(fixture.pipeline.snapshot().context->generation, generation);
    EXPECT_TRUE(fixture.adapter.processingError().isEmpty());
    fixture.coordinator.beginShutdown();
    fixture.adapter.refresh();
    EXPECT_FALSE(fixture.adapter.available());
    EXPECT_FALSE(fixture.adapter.commitWindow(500));
}

class EnhancementFailure final : public processing::detail::EngineHooks {
public:
    std::atomic<bool> enabled{true};
    core::Result<void> before(processing::ProcessingOperation operation,
        std::span<std::byte>) override {
        if (operation == processing::ProcessingOperation::Invert && enabled)
            return core::Result<void>::failure({core::ErrorCategory::Processing,
                "injected_enhancement_failure", "Enhancement failed.", "", true});
        return core::Result<void>::success();
    }
};

TEST(QmlProcessingAdapter, PublishesActualEngineFallbackAndExplicitRetryRecovery) {
    auto fault = std::make_shared<EnhancementFailure>();
    application::PresetState seed;
    seed.selectedId = {"custom"};
    seed.activePipeline.stages.back().enabled = true;
    Fixture fixture(false, false, seed, fault);
    ASSERT_TRUE(fixture.start());
    ASSERT_TRUE(fixture.settled());
    using Intent = presentation::CameraStartupIntent;
    ASSERT_TRUE(fixture.wait([&] {
        const auto& status = fixture.coordinator.state();
        return status.cameraStatus && !status.cameraStatus->discoveredDescriptors.empty()
            && !status.ordinaryOperationPending;
    }));
    fixture.coordinator.selectCamera({"SIM-LIVE"});
    for (const auto intent : {Intent::Connect, Intent::Apply, Intent::Confirm, Intent::Start}) {
        ASSERT_TRUE(fixture.coordinator.dispatch(intent).hasValue());
        ASSERT_TRUE(fixture.wait([&] { return !fixture.coordinator.state().ordinaryOperationPending; }));
    }
    ASSERT_TRUE(fixture.wait([&] {
        fixture.clock.advance(34ms);
        return fixture.adapter.fallback();
    }));
    EXPECT_FALSE(fixture.adapter.processingError().isEmpty());
    EXPECT_TRUE(fixture.adapter.retryEnabled());
    fault->enabled = false;
    ASSERT_TRUE(fixture.adapter.retry());
    EXPECT_TRUE(fixture.adapter.retryPending());
    EXPECT_FALSE(fixture.adapter.retryEnabled());
    const auto backendError = fixture.coordinator.processingState().processorStatus.error;
    ASSERT_TRUE(backendError);
    EXPECT_FALSE(fixture.adapter.retry());
    EXPECT_EQ(fixture.adapter.processingError(), QString::fromStdString(backendError->operatorSummary));
    ASSERT_TRUE(fixture.wait([&] {
        fixture.clock.advance(34ms);
        return !fixture.adapter.fallback() && !fixture.adapter.retryPending();
    }));
    EXPECT_TRUE(fixture.adapter.processingError().isEmpty());
    EXPECT_FALSE(fixture.adapter.retryEnabled());
}
} // namespace
