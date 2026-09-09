#pragma once

#include <lumora/core/Result.hpp>
#include <lumora/processing/ProcessingConfiguration.hpp>

#include <QJsonObject>

namespace lumora::configuration::detail {

// Private wire adapter. Semantic completeness/range validation stays in Application.
class PipelineDefinitionCodec final {
public:
    [[nodiscard]] static core::Result<processing::PipelineDefinition> decode(const QJsonObject&);
    [[nodiscard]] static core::Result<QJsonObject> encode(const processing::PipelineDefinition&);
};

}  // namespace lumora::configuration::detail
