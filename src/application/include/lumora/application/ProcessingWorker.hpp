#pragma once

#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/processing/IFrameProcessor.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace lumora::application {

struct ProcessingWorkerSnapshot final {
    // Alternative observation of acquisition droppedBeforeProcessing; never sum them.
    std::uint64_t rawFramesSkipped{0U};
    std::uint64_t bundlesReplaced{0U};
    std::uint64_t processingErrors{0U};
    std::uint64_t displayPoolExhaustions{0U};
    std::optional<core::Error> currentError;
    // Enhancement attempts include the third failure that publishes Original.
    // This separate processor metric overlaps returned-frame processingErrors;
    // callers must not sum them as disjoint failure counts.
    processing::ProcessorStatus processorStatus;
};

// Dependencies outlive this worker. The owner serializes start/join and gives
// this worker exclusive publication access to a fresh per-session bundle slot.
// requestStop and snapshot are safe to call concurrently; destruction joins.
class ProcessingWorker final {
public:
    ProcessingWorker(core::LatestValueSlot<core::RawFrame>& rawSlot,
                     core::LatestValueSlot<core::FrameBundle>& bundleSlot,
                     processing::IFrameProcessor& processor);
    ~ProcessingWorker();
    ProcessingWorker(const ProcessingWorker&) = delete;
    ProcessingWorker& operator=(const ProcessingWorker&) = delete;

    [[nodiscard]] core::Result<void> start();
    void requestStop() noexcept;
    void join() noexcept;
    [[nodiscard]] ProcessingWorkerSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::application
