#pragma once

#include <lumora/core/Result.hpp>

#include <cstddef>

namespace lumora::core {

[[nodiscard]] Result<std::size_t> checkedMultiply(
    std::size_t left,
    std::size_t right);

[[nodiscard]] Result<std::size_t> checkedAdd(
    std::size_t left,
    std::size_t right);

}  // namespace lumora::core
