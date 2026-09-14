#include "ProcessingAdapter.hpp"
#include "FrameEngineTestAccess.hpp"

#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    ASSERT_TRUE(fixture.coordinator.processingControls()->selectPreset({"standard"}).hasValue());
    fixture.adapter.refresh();
    ASSERT_TRUE(fixture.adapter.commitWindow(1234.5));
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
