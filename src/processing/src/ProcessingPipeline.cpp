#include <lumora/processing/ProcessingPipeline.hpp>

#include <utility>

namespace lumora::processing {

core::Result<void, PipelineValidationError> ProcessingPipeline::activate(
    const PipelineDefinition& definition) {
    using Result = core::Result<void, PipelineValidationError>;
    auto compiled = PipelineCompiler(stageRegistry()).compile(definition);
    if (!compiled.hasValue()) {
        return Result::failure(std::move(compiled).error());
    }
    PipelineValidationError unavailable{"processing_stage_unavailable", {}};
    for (std::size_t index = 0; index < definition.stages.size(); ++index) {
        const auto& stage = definition.stages[index];
        if (stage.enabled && stage.id != StageId::Normalize && stage.id != StageId::WindowLevel) {
            unavailable.violations.push_back({index, unavailable.code,
                std::string(stageName(stage.id)) + " is not available in this executor."});
        }
    }
    if (!unavailable.violations.empty()) {
        return Result::failure(std::move(unavailable));
    }
    auto next = std::make_shared<const CompiledPipeline>(std::move(compiled).value());
    active_.store(std::move(next));
    return Result::success();
}

std::shared_ptr<const CompiledPipeline> ProcessingPipeline::snapshot() const noexcept {
    return active_.load();
}

}  // namespace lumora::processing
