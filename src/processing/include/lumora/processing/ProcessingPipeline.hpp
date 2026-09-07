#pragma once

#include <lumora/processing/PipelineCompiler.hpp>

#include <atomic>
#include <memory>

namespace lumora::processing {

// Activation publishes an owned immutable configuration. A frame takes one
// snapshot at entry and retains it through publication, even during activation.
class ProcessingPipeline final {
public:
    [[nodiscard]] core::Result<void, PipelineValidationError> activate(
        const PipelineDefinition& definition);
    [[nodiscard]] std::shared_ptr<const CompiledPipeline> snapshot() const noexcept;

private:
    std::atomic<std::shared_ptr<const CompiledPipeline>> active_;
};

}  // namespace lumora::processing
