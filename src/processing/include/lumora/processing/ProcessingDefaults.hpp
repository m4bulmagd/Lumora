#pragma once
#include <lumora/processing/ProcessingConfiguration.hpp>
namespace lumora::processing {
// Explicit characterization preset; does not change application startup defaults.
[[nodiscard]] PipelineDefinition standardPipeline();
[[nodiscard]] bool semanticallyEqualPipelineDefinitions(const PipelineDefinition&, const PipelineDefinition&) noexcept;
}
