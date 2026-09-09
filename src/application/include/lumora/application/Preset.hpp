#pragma once

#include <lumora/processing/ProcessingConfiguration.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace lumora::application {

struct PresetId final {
    std::string value;

    bool operator==(const PresetId&) const = default;
};

struct Preset final {
    PresetId id;
    std::string name;
    std::string description;
    bool builtIn{false};
    std::uint32_t schemaVersion{1U};
    std::uint64_t revision{1U};
    processing::PipelineDefinition pipeline{processing::defaultPipeline()};
};

struct PresetState final {
    std::vector<Preset> customPresets;
    PresetId selectedId{"original"};
    processing::PipelineDefinition activePipeline{processing::defaultPipeline()};
};

}  // namespace lumora::application
