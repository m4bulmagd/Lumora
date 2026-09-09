#include <lumora/application/LivePipeline.hpp>

#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/processing/Mono8PassThroughProcessor.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using namespace std::chrono_literals;
namespace application = lumora::application;
namespace camera = lumora::camera;
namespace core = lumora::core;
namespace processing = lumora::processing;

[[nodiscard]] camera::CameraConfiguration cameraRequest() {
    return {{"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
                core::BitAlignment::LeastSignificant, core::StorageType::UInt8},
        {0, 0, 8, 6}, 30.0, {camera::ExposureMode::Manual, 100.0},
        {camera::GainMode::Manual, 0.0}, camera::AcquisitionMode::Continuous};
}

[[nodiscard]] camera::sim::SimulatedCameraOptions cameraOptions() {
    return {{"SIM-LIVE"},
        {{cameraRequest().pixelFormat}, {{0, 0, 1, 1}, {0, 0, 8, 6}, {1, 1, 1, 1}},
            {1, 60, 1, false}, {1, 1000, 1, false},
            {camera::ExposureMode::Manual}, {0, 10, 1, false},
            {camera::GainMode::Manual}},
        camera::sim::SimulationPattern::MovingBar, 30.0, 0x4C554D4FU,
        camera::sim::SimulationPacingMode::Manual};
}

[[nodiscard]] processing::PipelineDefinition definition(std::uint64_t revision) {
    auto value = processing::standardPipeline();
    value.version.configurationRevision = revision;
    return value;
}

[[nodiscard]] bool waitUntil(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        if (condition()) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

class Gate final {
public:
    void block() {
        std::unique_lock lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [&] { return released_; });
    }

    [[nodiscard]] bool wait() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 2s, [&] { return entered_; });
    }

    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{false};
    bool released_{false};
};

class ReleaseGate final {
public:
    explicit ReleaseGate(Gate& gate) : gate_(gate) {}
    ~ReleaseGate() { gate_.release(); }

private:
    Gate& gate_;
};

class ActivationProcessor final : public processing::IFrameProcessor {
public:
    explicit ActivationProcessor(Gate* gate = nullptr,
        std::optional<std::uint64_t> rejectedRevision = std::nullopt,
        std::optional<std::uint64_t> standardThrowRevision = std::nullopt,
        std::optional<std::uint64_t> unknownThrowRevision = std::nullopt)
        : gate_(gate), rejectedRevision_(rejectedRevision),
          standardThrowRevision_(standardThrowRevision),
          unknownThrowRevision_(unknownThrowRevision) {}

    [[nodiscard]] core::Result<void, processing::PipelineValidationError> activate(
        const processing::PipelineDefinition& candidate) override {
        calls_.fetch_add(1U);
        if (gate_ != nullptr) gate_->block();
        const auto revision = candidate.version.configurationRevision;
        if (standardThrowRevision_ == revision) {
            throw std::runtime_error("scripted activation exception");
        }
        if (unknownThrowRevision_ == revision) throw 17;
        if (rejectedRevision_ == revision) {
            return core::Result<void, processing::PipelineValidationError>::failure({
                "test_activation_rejected",
                {{3U, "invalid_gamma", "Synthetic activation rejection."}}});
        }
        activeRevision_.store(revision);
        return core::Result<void, processing::PipelineValidationError>::success();
    }

    [[nodiscard]] core::Result<std::shared_ptr<const core::FrameBundle>> process(
        std::shared_ptr<const core::RawFrame>) override {
        return core::Result<std::shared_ptr<const core::FrameBundle>>::failure({
            core::ErrorCategory::Processing, "unexpected_frame",
            "No frame was expected.", "The no-frame activation test received a frame.", false});
    }

    [[nodiscard]] unsigned calls() const noexcept { return calls_.load(); }
    [[nodiscard]] std::uint64_t activeRevision() const noexcept {
        return activeRevision_.load();
    }

private:
    Gate* gate_;
    std::optional<std::uint64_t> rejectedRevision_;
    std::optional<std::uint64_t> standardThrowRevision_;
    std::optional<std::uint64_t> unknownThrowRevision_;
    std::atomic<unsigned> calls_{0U};
    std::atomic<std::uint64_t> activeRevision_{0U};
};

struct PipelineFixture final {
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider{cameraOptions(), clock};
    std::atomic<ActivationProcessor*> observed{nullptr};
    Gate* gate{};
    std::optional<std::uint64_t> rejectedRevision;
    std::optional<std::uint64_t> standardThrowRevision;
    std::optional<std::uint64_t> unknownThrowRevision;
    application::LivePipeline pipeline;

    explicit PipelineFixture(Gate* activationGate = nullptr,
        std::optional<std::uint64_t> rejected = std::nullopt,
        std::optional<std::uint64_t> standardThrow = std::nullopt,
        std::optional<std::uint64_t> unknownThrow = std::nullopt)
        : gate(activationGate), rejectedRevision(rejected),
          standardThrowRevision(standardThrow), unknownThrowRevision(unknownThrow),
          pipeline(provider, clock, cameraRequest(),
              [&](core::BufferPool&, core::BufferPool&, const core::ImageLayout&) {
                  auto processor = std::make_unique<ActivationProcessor>(gate,
                      rejectedRevision, standardThrowRevision, unknownThrowRevision);
                  observed.store(processor.get());
                  std::unique_ptr<processing::IFrameProcessor> erased = std::move(processor);
                  return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(
                      std::move(erased));
              }) {}

    ~PipelineFixture() { pipeline.shutdown(); }

    [[nodiscard]] bool start() {
        return pipeline.start().hasValue() && waitUntil([&] {
            return pipeline.snapshot().context != nullptr && observed.load() != nullptr;
        });
    }
};

// Treating admission as completion or waiting for a frame loses the only
// acknowledgment available while the camera is idle.
TEST(ProcessingConfiguration, PublishesNoFrameOutcomeAndRejectsConcurrentAdmission) {
    Gate gate;
    PipelineFixture fixture(&gate);
    ReleaseGate release(gate);
    ASSERT_TRUE(fixture.start());
    const auto before = fixture.pipeline.snapshot();
    ASSERT_NE(before.context, nullptr);
    ASSERT_NE(before.camera, nullptr);
    EXPECT_EQ(before.camera->acquisitionCounters.acquired, 0U);

    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {before.context->generation, definition(1U)}).hasValue());
    ASSERT_TRUE(gate.wait());
    auto executing = fixture.pipeline.snapshot();
    EXPECT_TRUE(executing.processingConfigurationPending);
    EXPECT_FALSE(executing.processingConfigurationOutcome.has_value());

    const auto busy = fixture.pipeline.setProcessingConfiguration(
        {before.context->generation, definition(2U)});
    ASSERT_FALSE(busy.hasValue());
    EXPECT_EQ(busy.error().code, "processing_configuration_busy");

    gate.release();
    ASSERT_TRUE(waitUntil([&] {
        const auto snapshot = fixture.pipeline.snapshot();
        return snapshot.processingConfigurationOutcome.has_value()
            && snapshot.processingConfigurationOutcome->configurationRevision == 1U;
    }));
    const auto completed = fixture.pipeline.snapshot();
    EXPECT_FALSE(completed.processingConfigurationPending);
    ASSERT_TRUE(completed.processingConfigurationOutcome.has_value());
    EXPECT_EQ(completed.processingConfigurationOutcome->sessionGeneration,
        before.context->generation);
    EXPECT_FALSE(completed.processingConfigurationOutcome->error.has_value());
    EXPECT_EQ(fixture.observed.load()->calls(), 1U);
    EXPECT_EQ(fixture.observed.load()->activeRevision(), 1U);
    EXPECT_EQ(completed.camera->acquisitionCounters.acquired, 0U);
}

// A custom adapter exception is an activation failure, not a control-thread
// failure. Both exception forms settle the command and leave the session usable.
TEST(ProcessingConfiguration, ContainsAdapterExceptionsAndContinuesSession) {
    PipelineFixture fixture(nullptr, std::nullopt, 1U, 3U);
    ASSERT_TRUE(fixture.start());
    const auto initial = fixture.pipeline.snapshot();
    ASSERT_NE(initial.context, nullptr);
    ASSERT_NE(initial.camera, nullptr);
    const auto generation = initial.context->generation;

    const auto expectFailure = [&](std::uint64_t revision,
                                   std::string_view code,
                                   std::string_view detail) {
        ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
            {generation, definition(revision)}).hasValue());
        ASSERT_TRUE(waitUntil([&] {
            const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
            return outcome && outcome->configurationRevision == revision;
        }));
        const auto snapshot = fixture.pipeline.snapshot();
        EXPECT_FALSE(snapshot.processingConfigurationPending);
        EXPECT_TRUE(snapshot.processingAvailable);
        EXPECT_FALSE(snapshot.error.has_value());
        EXPECT_EQ(snapshot.context, initial.context);
        ASSERT_NE(snapshot.camera, nullptr);
        EXPECT_EQ(snapshot.camera->sessionGeneration, generation);
        ASSERT_TRUE(snapshot.processingConfigurationOutcome->error.has_value());
        const auto& error = *snapshot.processingConfigurationOutcome->error;
        EXPECT_EQ(error.code, code);
        ASSERT_EQ(error.violations.size(), 1U);
        EXPECT_EQ(error.violations[0].code, code);
        EXPECT_EQ(error.violations[0].detail, detail);
    };

    expectFailure(1U, "processing_configuration_exception",
        "scripted activation exception");
    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {generation, definition(2U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->configurationRevision == 2U;
    }));
    EXPECT_EQ(fixture.observed.load()->activeRevision(), 2U);

    expectFailure(3U, "processing_configuration_unknown_exception",
        "An unknown exception occurred while validating or activating the processing configuration.");
    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {generation, definition(4U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->configurationRevision == 4U;
    }));
    EXPECT_EQ(fixture.observed.load()->activeRevision(), 4U);
}

// Generation and revision fences are admission errors and must not call the
// processor or erase the last executed outcome.
TEST(ProcessingConfiguration, RejectsStaleZeroAndNonIncreasingRevisionsBeforeActivation) {
    PipelineFixture fixture;
    ASSERT_TRUE(fixture.start());
    const auto generation = fixture.pipeline.snapshot().context->generation;

    auto rejected = fixture.pipeline.setProcessingConfiguration(
        {generation + 1U, definition(1U)});
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, "stale_processing_session");
    rejected = fixture.pipeline.setProcessingConfiguration(
        {generation, definition(0U)});
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, "processing_configuration_revision_required");
    EXPECT_EQ(fixture.observed.load()->calls(), 0U);

    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {generation, definition(2U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->configurationRevision == 2U;
    }));
    EXPECT_EQ(fixture.observed.load()->calls(), 1U);

    for (const auto revision : {2U, 1U}) {
        rejected = fixture.pipeline.setProcessingConfiguration(
            {generation, definition(revision)});
        ASSERT_FALSE(rejected.hasValue());
        EXPECT_EQ(rejected.error().code,
            "processing_configuration_revision_not_increasing");
    }
    EXPECT_EQ(fixture.observed.load()->calls(), 1U);
    EXPECT_EQ(fixture.pipeline.snapshot()
            .processingConfigurationOutcome->configurationRevision,
        2U);
}

// Flattening validation failures into admission errors hides preparation detail;
// committing a rejected candidate would replace the last active definition.
TEST(ProcessingConfiguration, PublishesStructuredFailureAndPreservesActiveRevision) {
    PipelineFixture fixture(nullptr, 2U);
    ASSERT_TRUE(fixture.start());
    const auto generation = fixture.pipeline.snapshot().context->generation;

    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {generation, definition(1U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->configurationRevision == 1U;
    }));
    EXPECT_EQ(fixture.observed.load()->activeRevision(), 1U);

    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {generation, definition(2U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->configurationRevision == 2U;
    }));
    auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
    ASSERT_TRUE(outcome->error.has_value());
    EXPECT_EQ(outcome->error->code, "test_activation_rejected");
    ASSERT_EQ(outcome->error->violations.size(), 1U);
    EXPECT_EQ(outcome->error->violations[0].stageIndex, 3U);
    EXPECT_EQ(outcome->error->violations[0].code, "invalid_gamma");
    EXPECT_EQ(outcome->error->violations[0].detail,
        "Synthetic activation rejection.");
    EXPECT_EQ(fixture.observed.load()->activeRevision(), 1U);

    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {generation, definition(3U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto current = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return current && current->configurationRevision == 3U;
    }));
    outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
    EXPECT_FALSE(outcome->error.has_value());
    EXPECT_EQ(fixture.observed.load()->activeRevision(), 3U);
}

// Custom adapters are not validation authorities. The application seam rejects
// incomplete and compiler-invalid definitions before invoking any adapter.
TEST(ProcessingConfiguration, RejectsInvalidDefinitionBeforeAdapter) {
    PipelineFixture fixture;
    ASSERT_TRUE(fixture.start());
    const auto generation = fixture.pipeline.snapshot().context->generation;

    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {generation, definition(1U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->configurationRevision == 1U;
    }));
    EXPECT_EQ(fixture.observed.load()->activeRevision(), 1U);
    EXPECT_EQ(fixture.observed.load()->calls(), 1U);

    const auto expectRejected = [&](processing::PipelineDefinition invalid,
                                    std::string_view code) {
        const auto revision = invalid.version.configurationRevision;
        ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
            {generation, std::move(invalid)}).hasValue());
        ASSERT_TRUE(waitUntil([&] {
            const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
            return outcome && outcome->configurationRevision == revision;
        }));
        const auto snapshot = fixture.pipeline.snapshot();
        ASSERT_TRUE(snapshot.processingConfigurationOutcome->error.has_value());
        const auto& error = *snapshot.processingConfigurationOutcome->error;
        EXPECT_EQ(error.code, code);
        ASSERT_FALSE(error.violations.empty());
        EXPECT_EQ(error.violations[0].code, code);
        EXPECT_FALSE(snapshot.processingConfigurationPending);
        EXPECT_EQ(fixture.observed.load()->calls(), 1U);
        EXPECT_EQ(fixture.observed.load()->activeRevision(), 1U);
    };

    auto incomplete = definition(2U);
    incomplete.stages.pop_back();
    expectRejected(std::move(incomplete), "preset_pipeline_incomplete");

    auto invalidDisabledGamma = definition(3U);
    invalidDisabledGamma.stages[3].enabled = false;
    std::get<processing::GammaParameters>(
        invalidDisabledGamma.stages[3].parameters).gamma = 0.0;
    expectRejected(std::move(invalidDisabledGamma), "invalid_gamma");

    auto disabledNormalize = definition(4U);
    disabledNormalize.stages[0].enabled = false;
    expectRejected(std::move(disabledNormalize), "normalization_disabled");

    auto unsupportedSchema = definition(5U);
    unsupportedSchema.version.schemaVersion = 2U;
    expectRejected(std::move(unsupportedSchema), "unsupported_schema_version");
}

// Existing adapters remain source-compatible and report unsupported activation
// through the asynchronous outcome instead of rejecting admission.
TEST(ProcessingConfiguration, DefaultProcessorAdapterReportsUnsupportedOutcome) {
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider(cameraOptions(), clock);
    application::LivePipeline pipeline(provider, clock, cameraRequest(),
        [](core::BufferPool&, core::BufferPool& display,
            const core::ImageLayout&) {
            std::unique_ptr<processing::IFrameProcessor> processor =
                std::make_unique<processing::Mono8PassThroughProcessor>(display);
            return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(
                std::move(processor));
        });
    ASSERT_TRUE(pipeline.start().hasValue());
    ASSERT_TRUE(waitUntil([&] { return pipeline.snapshot().context != nullptr; }));
    const auto generation = pipeline.snapshot().context->generation;

    ASSERT_TRUE(pipeline.setProcessingConfiguration(
        {generation, definition(1U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        return pipeline.snapshot().processingConfigurationOutcome.has_value();
    }));
    const auto snapshot = pipeline.snapshot();
    EXPECT_FALSE(snapshot.processingConfigurationPending);
    ASSERT_TRUE(snapshot.processingConfigurationOutcome->error.has_value());
    EXPECT_EQ(snapshot.processingConfigurationOutcome->error->code,
        "processor_activation_unsupported");
    pipeline.shutdown();
}

// Stop leaves the prepared session configurable. Disconnect retires it, and a
// replacement accepts only its own generation and a fresh revision sequence.
TEST(ProcessingConfiguration, StopPreservesActivationWhileReplacementFencesOldSession) {
    PipelineFixture fixture;
    ASSERT_TRUE(fixture.start());
    const auto firstGeneration = fixture.pipeline.snapshot().context->generation;

    const auto sendOrdinary = [&](application::CameraCommand command) {
        const auto requestId = command.requestId;
        if (!fixture.pipeline.post(std::move(command)).hasValue()) return false;
        return waitUntil([&] {
            const auto outcome = fixture.pipeline.snapshot().ordinaryOutcome;
            return outcome && outcome->requestId == requestId && !outcome->error.has_value();
        });
    };
    ASSERT_TRUE(sendOrdinary({5U, application::Connect{{"SIM-LIVE"}}}));
    ASSERT_TRUE(sendOrdinary({6U, application::ApplyConfiguration{
        firstGeneration, cameraRequest(), 1U}}));
    ASSERT_TRUE(sendOrdinary({7U, application::ConfirmConfiguration{
        firstGeneration, 1U}}));
    ASSERT_TRUE(fixture.pipeline.acknowledgeContext(firstGeneration).hasValue());
    ASSERT_TRUE(sendOrdinary({8U, application::StartStream{
        firstGeneration, 1U}}));

    ASSERT_TRUE(fixture.pipeline.post({10U, application::StopStream{}}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().priorityOutcome;
        return outcome && outcome->requestId == 10U && !outcome->error.has_value();
    }));
    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {firstGeneration, definition(1U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->configurationRevision == 1U;
    }));

    ASSERT_TRUE(fixture.pipeline.post({11U, application::Disconnect{}}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto snapshot = fixture.pipeline.snapshot();
        return snapshot.priorityOutcome && snapshot.priorityOutcome->requestId == 11U
            && !snapshot.processingAvailable;
    }));
    auto rejected = fixture.pipeline.setProcessingConfiguration(
        {firstGeneration, definition(2U)});
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, "processing_configuration_unavailable");

    ASSERT_TRUE(fixture.pipeline.acknowledgeContext(firstGeneration).hasValue());
    ASSERT_TRUE(fixture.pipeline.post(
        {12U, application::Connect{{"SIM-LIVE"}}}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto snapshot = fixture.pipeline.snapshot();
        return snapshot.ordinaryOutcome && snapshot.ordinaryOutcome->requestId == 12U
            && snapshot.context && snapshot.context->generation > firstGeneration;
    }));
    const auto secondGeneration = fixture.pipeline.snapshot().context->generation;
    rejected = fixture.pipeline.setProcessingConfiguration(
        {firstGeneration, definition(2U)});
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.error().code, "stale_processing_session");
    ASSERT_TRUE(fixture.pipeline.setProcessingConfiguration(
        {secondGeneration, definition(1U)}).hasValue());
    ASSERT_TRUE(waitUntil([&] {
        const auto outcome = fixture.pipeline.snapshot().processingConfigurationOutcome;
        return outcome && outcome->sessionGeneration == secondGeneration
            && outcome->configurationRevision == 1U;
    }));
}

}  // namespace
