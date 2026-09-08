#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <utility>
namespace lumora::processing::detail {
inline constexpr std::size_t ownerControlReserve = 4096U;
// Preparation only. Rebinding preserves the enforced byte ceiling, so a new
// standard-library control shape cannot silently exceed the admission reserve.
template<class T> struct PreparedOwnerAllocator {
    using value_type = T;
    std::size_t maximumBytes;
    explicit PreparedOwnerAllocator(std::size_t maximum) noexcept : maximumBytes(maximum) {}
    template<class U> PreparedOwnerAllocator(const PreparedOwnerAllocator<U>& other) noexcept : maximumBytes(other.maximumBytes) {}
    T* allocate(std::size_t count) {
        if (count > maximumBytes / sizeof(T)) throw std::bad_alloc{};
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* value, std::size_t count) noexcept { std::allocator<T>{}.deallocate(value,count); }
    template<class U> bool operator==(const PreparedOwnerAllocator<U>& other) const noexcept { return maximumBytes == other.maximumBytes; }
};
template<class T, class... Args> std::shared_ptr<T> makePreparedOwner(Args&&... args) {
    return std::allocate_shared<T>(PreparedOwnerAllocator<T>{sizeof(T)+ownerControlReserve},std::forward<Args>(args)...);
}
}
