#include "AllocationTracker.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {

std::atomic_bool tracking{false};
std::atomic_size_t allocations{0U};

void recordAllocation() noexcept {
    if (tracking.load(std::memory_order_relaxed)) {
        allocations.fetch_add(1U, std::memory_order_relaxed);
    }
}

[[nodiscard]] void* allocate(std::size_t size) {
    recordAllocation();
    if (auto* memory = std::malloc(size == 0U ? 1U : size)) return memory;
    throw std::bad_alloc{};
}

[[nodiscard]] void* allocateNoThrow(std::size_t size) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}

[[nodiscard]] void* allocateAligned(std::size_t size, std::size_t alignment) {
    recordAllocation();
    void* memory = nullptr;
#if defined(_MSC_VER)
    memory = _aligned_malloc(size == 0U ? 1U : size, alignment);
#else
    if (posix_memalign(&memory, alignment, size == 0U ? 1U : size) != 0) {
        memory = nullptr;
    }
#endif
    if (memory != nullptr) return memory;
    throw std::bad_alloc{};
}

[[nodiscard]] void* allocateAlignedNoThrow(
    std::size_t size,
    std::size_t alignment) noexcept {
    try {
        return allocateAligned(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void deallocateAligned(void* memory) noexcept {
#if defined(_MSC_VER)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

}  // namespace

namespace lumora::test {

void beginAllocationTracking() noexcept {
    allocations.store(0U, std::memory_order_relaxed);
    tracking.store(true, std::memory_order_release);
}

std::size_t endAllocationTracking() noexcept {
    tracking.store(false, std::memory_order_release);
    return allocations.load(std::memory_order_relaxed);
}

}  // namespace lumora::test

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocateNoThrow(size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocateNoThrow(size);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}
void* operator new(
    std::size_t size,
    std::align_val_t alignment,
    const std::nothrow_t&) noexcept {
    return allocateAlignedNoThrow(size, static_cast<std::size_t>(alignment));
}
void* operator new[](
    std::size_t size,
    std::align_val_t alignment,
    const std::nothrow_t&) noexcept {
    return allocateAlignedNoThrow(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}
void operator delete(void* memory, std::align_val_t) noexcept {
    deallocateAligned(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept {
    deallocateAligned(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    deallocateAligned(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    deallocateAligned(memory);
}
void operator delete(
    void* memory,
    std::align_val_t,
    const std::nothrow_t&) noexcept {
    deallocateAligned(memory);
}
void operator delete[](
    void* memory,
    std::align_val_t,
    const std::nothrow_t&) noexcept {
    deallocateAligned(memory);
}
