#pragma once

#include <cstdint>
#include <limits>

namespace lumora::application::detail {

constexpr void saturatingAdd(
    std::uint64_t& value, std::uint64_t amount) noexcept {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    value = amount > maximum - value ? maximum : value + amount;
}

constexpr void saturatingIncrement(std::uint64_t& value) noexcept {
    saturatingAdd(value, 1U);
}

}  // namespace lumora::application::detail
