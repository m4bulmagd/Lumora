#include <lumora/processing/ProcessingPipeline.hpp>
#include <utility>
namespace lumora::processing {
core::Result<void,PipelineValidationError> ProcessingPipeline::activate(const PipelineDefinition& definition) {
    using Result=core::Result<void,PipelineValidationError>;
    try {
        auto compiled=PipelineCompiler(stageRegistry()).compile(definition);
        if(!compiled.hasValue()) return Result::failure(std::move(compiled).error());
        auto next=std::make_shared<const CompiledPipeline>(std::move(compiled).value());
        active_.store(std::move(next));
        return Result::success();
    } catch(...) {
        core::Error error{core::ErrorCategory::ResourceExhaustion,"processing_preparation_allocation_failed",
            "Pipeline validation failed.","Owned configuration metadata could not be allocated.",true};
        return Result::failure({error.code,{{std::nullopt,error.code,error.diagnosticDetail}},error});
    }
}
std::shared_ptr<const CompiledPipeline> ProcessingPipeline::snapshot() const noexcept { return active_.load(); }
}
