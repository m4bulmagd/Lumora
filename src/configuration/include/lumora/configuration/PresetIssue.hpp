#pragma once

#include <lumora/application/Preset.hpp>
#include <lumora/core/Error.hpp>

#include <cstddef>
#include <optional>

namespace lumora::configuration {

struct PresetIssue final {
    std::optional<std::size_t> entryIndex;
    std::optional<application::PresetId> presetId;
    core::Error error;
};

}  // namespace lumora::configuration
