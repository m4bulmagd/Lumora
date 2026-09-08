#pragma once

#include <cstddef>

namespace lumora::test {

struct AllocationCounts final {
    std::size_t allocations;
    std::size_t allocatedBytes;
    std::size_t deallocations;
};

[[nodiscard]] AllocationCounts endAllocationMeasurement() noexcept;
// Fail one allocation after the requested number of successful allocation calls.
// The failure automatically disarms so typed error rendering can still allocate.
void failOneAllocationAfter(std::size_t count) noexcept;
void cancelAllocationFailure() noexcept;
void beginAllocationTracking() noexcept;
[[nodiscard]] std::size_t endAllocationTracking() noexcept;

}  // namespace lumora::test
