#pragma once
#include <lumora/application/LivePipeline.hpp>
namespace lumora::application::detail {
struct LiveResourcePreparation final {
    std::optional<core::ImageLayout> sourceLayout;
    std::size_t rawBytes{};
    std::size_t canonicalBytes{};
    std::size_t displayBytes{};
    processing::ProcessingResources resources;
    std::optional<processing::ProcessingPreparationPlan> enginePlan;
    std::optional<core::Error> error;
};
LiveResourcePreparation prepareLiveResources(const camera::CameraConfiguration&,
    processing::ProcessingPreparationOptions,bool customFactory,
    const processing::PipelineDefinition& definition = processing::defaultPipeline());

struct PreparedLiveSession final {
    std::shared_ptr<LiveSessionContext> context;
    std::unique_ptr<processing::IFrameProcessor> processor;
    std::unique_ptr<ProcessingWorker> worker;
    ~PreparedLiveSession();
};
core::Result<std::unique_ptr<PreparedLiveSession>> prepareLiveSession(
    const LiveResourcePreparation& plan, std::uint64_t generation,
    const LivePipeline::ProcessorFactory& factory,
    const std::optional<processing::PipelineDefinition>& acceptedDefinition);
}
