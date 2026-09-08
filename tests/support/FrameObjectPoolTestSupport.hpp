#pragma once
#include <cstddef>

namespace lumora::core::detail::testing {
struct PoolCounters final {
    std::size_t probes;
    std::size_t slabAllocations;
    std::size_t liveSlabs;
    std::size_t liveStates;
    std::size_t liveProbeAllocations;
};
[[nodiscard]] PoolCounters poolCounters() noexcept;
// Failure injection is preparation-only, disabled by default; tests serialize it.
void failSlabAllocationAfter(std::size_t successfulAllocations) noexcept;
void rejectControlShape(bool reject) noexcept;
void resetPoolFailures() noexcept;
}  // namespace lumora::core::detail::testing
