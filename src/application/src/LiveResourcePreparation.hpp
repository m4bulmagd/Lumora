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
    processing::ProcessingPreparationOptions,bool customFactory);
}
