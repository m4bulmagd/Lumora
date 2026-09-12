#pragma once

#include <lumora/application/AcquisitionWorker.hpp>
#include <lumora/application/LiveSessionContext.hpp>
#include <lumora/application/ProcessingWorker.hpp>

#include <functional>
#include <lumora/processing/ProcessingPreparation.hpp>

namespace lumora::application {

struct ProcessingConfigurationCommand final {
    std::uint64_t sessionGeneration;
    processing::PipelineDefinition definition;
};

struct ProcessingConfigurationOutcome final {
    std::uint64_t sessionGeneration;
    std::uint64_t configurationRevision;
    std::optional<processing::PipelineValidationError> error;
};

struct LivePipelineSnapshot final {
    std::shared_ptr<const CameraStatusSnapshot> camera;
    std::shared_ptr<LiveSessionContext> context;
    bool contextBound{false};
    // Capacity-one completion values; admission never implies command success.
    // A lifecycle intent may supersede an ordinary controller continuation even
    // when its already executing device operation ultimately succeeds.
    std::optional<CameraCommandOutcome> ordinaryOutcome;
    std::optional<CameraCommandOutcome> priorityOutcome;
    std::optional<ProcessingConfigurationOutcome> processingConfigurationOutcome;
    bool processingConfigurationPending{false};
    ProcessingWorkerSnapshot processing;
    std::optional<processing::ProcessingResources> resources;
    bool processingRetryPending{false};
    bool processingAvailable{false};
    std::optional<core::Error> error;
};

// Provider and clock outlive the pipeline. The owner serializes start/shutdown;
// post, snapshot and acknowledgement are safe concurrently and never join or
// allocate pools. The control thread prepares aligned 10 raw / 9 U16 / 16 Gray8
// pools. Stopped ROI/native-format changes replace processing resources on the
// same camera worker; compatible exposure/gain/FPS edits retain their context.
// Each session uses the configured budget. One old plus one candidate session
// may coexist within twice that budget; custom private storage remains unknown.
class LivePipeline final {
public:
    // Runs on the control thread. The returned processor may borrow both pools
    // until destruction after ProcessingWorker joins. The layout is borrowed
    // only for the factory call; copy any retained facts. No pixel fallback
    // allocation is permitted. Default composition creates FrameProcessingEngine.
    using ProcessorFactory = std::function<core::Result<std::unique_ptr<processing::IFrameProcessor>>(
        core::BufferPool& processingPool, core::BufferPool& displayPool,
        const core::ImageLayout& sourceLayout)>;
    LivePipeline(camera::ICameraProvider& provider, core::IClock& clock,
                 camera::CameraConfiguration fixedRequest, ProcessorFactory factory = {},
                 processing::ProcessingPreparationOptions options = {});
    ~LivePipeline();
    LivePipeline(const LivePipeline&) = delete;
    LivePipeline& operator=(const LivePipeline&) = delete;
    [[nodiscard]] core::Result<void> start();
    [[nodiscard]] core::Result<void> post(CameraCommand command);
    [[nodiscard]] LivePipelineSnapshot snapshot() const;
    [[nodiscard]] core::Result<void> requestProcessingRetry(std::uint64_t sessionGeneration);
    [[nodiscard]] core::Result<void> setProcessingConfiguration(
        ProcessingConfigurationCommand command);
    // The consumer resets its presenter first, then acknowledges this generation.
    // The control thread releases the retiring context after acknowledgement.
    [[nodiscard]] core::Result<void> acknowledgeContext(std::uint64_t generation);
    // Terminal: joins camera before processing. External context/image handles
    // must subsequently be released before asserting zero pool leases.
    void shutdown() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::application
