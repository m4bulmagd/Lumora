#pragma once

#include <cstddef>

namespace lumora::test {

void beginAllocationTracking() noexcept;
[[nodiscard]] std::size_t endAllocationTracking() noexcept;

}  // namespace lumora::test
