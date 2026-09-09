#pragma once

#include <lumora/application/PresetRepository.hpp>
#include <lumora/configuration/PresetIssue.hpp>

#include <QJsonObject>

#include <vector>

namespace lumora::configuration {

struct DecodedPresetState final {
    application::PresetState state;
    QJsonObject legacy;
    std::vector<PresetIssue> issues;
};

class PresetCodec final {
public:
    [[nodiscard]] static core::Result<application::PresetRepository> loadDefaultRepository();
    [[nodiscard]] static core::Result<DecodedPresetState> decode(const QJsonObject& object);
    [[nodiscard]] static core::Result<QJsonObject> encode(
        const application::PresetState& state, const QJsonObject& legacy = {});
};

}  // namespace lumora::configuration
