#pragma once

#include <lumora/core/Result.hpp>
#include <lumora/processing/ProcessingConfiguration.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace lumora::processing {

struct PipelineViolation final {
    // No stage index denotes a pipeline-wide violation.
    std::optional<std::size_t> stageIndex;
    std::string code;
    std::string detail;
};

struct PipelineValidationError final {
    // Convenience code from the first violation. Pipeline-wide errors precede
    // stage errors; stage errors follow input order and parameter field order.
    std::string code;
    std::vector<PipelineViolation> violations;
};

struct CompiledStage final {
    StageDefinition definition;
    StageTraits traits;
};

// Both configuration (including disabled stages) and execution metadata are
// owned copies. No references to the caller's definition or registry survive.
class CompiledPipeline final {
public:
    [[nodiscard]] const PipelineDefinition& definition() const noexcept {
        return definition_;
    }
    [[nodiscard]] const std::vector<CompiledStage>& enabledStages() const noexcept {
        return stages_;
    }

private:
    friend class PipelineCompiler;
    CompiledPipeline(PipelineDefinition definition, std::vector<CompiledStage> stages)
        : definition_(std::move(definition)), stages_(std::move(stages)) {}

    PipelineDefinition definition_;
    std::vector<CompiledStage> stages_;
};

class PipelineCompiler final {
public:
    explicit PipelineCompiler(std::vector<StageTraits> registry)
        : registry_(std::move(registry)) {}

    [[nodiscard]] core::Result<CompiledPipeline, PipelineValidationError> compile(
        const PipelineDefinition& definition) const;

private:
    std::vector<StageTraits> registry_;
};

}  // namespace lumora::processing
