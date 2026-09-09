#pragma once

#include <lumora/application/Preset.hpp>
#include <lumora/core/Result.hpp>

#include <string>
#include <vector>

namespace lumora::application {

[[nodiscard]] core::Result<void> validatePresetPipeline(
    const processing::PipelineDefinition& definition);

[[nodiscard]] core::Result<void> validateCustomPreset(const Preset& preset);

class PresetRepository final {
public:
    [[nodiscard]] static core::Result<PresetRepository> create(
        std::vector<Preset> shipped, PresetState state = {});

    [[nodiscard]] std::vector<Preset> list() const;
    [[nodiscard]] core::Result<Preset> find(const PresetId& id) const;
    [[nodiscard]] core::Result<processing::PipelineDefinition> apply(const PresetId& id);
    [[nodiscard]] core::Result<void> edit(processing::PipelineDefinition definition);
    [[nodiscard]] core::Result<PresetId> classify(
        const processing::PipelineDefinition& definition) const;
    [[nodiscard]] core::Result<void> saveCustom(
        PresetId id, std::string name, std::string description);
    [[nodiscard]] core::Result<void> deleteCustom(const PresetId& id);
    [[nodiscard]] core::Result<void> restore(PresetState state);
    [[nodiscard]] PresetState snapshot() const;

private:
    PresetRepository(std::vector<Preset> shipped, PresetState state);

    std::vector<Preset> shipped_;
    PresetState state_;
};

}  // namespace lumora::application
