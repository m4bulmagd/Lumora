#pragma once

#include <lumora/core/FrameObjectPool.hpp>

#include <array>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

namespace lumora::core::detail {
enum class FrameSlotKind : std::size_t { Processed, Display, Bundle, Control };
enum class FramePoolFailure { ObjectExhausted, ControlExhausted, UnsupportedShape };
class FramePoolAllocationFailure final : public std::bad_alloc {
public:
    explicit FramePoolAllocationFailure(FramePoolFailure reason) noexcept : reason(reason) {}
    FramePoolFailure reason;
};
struct ControlShape final {
    std::size_t count{};
    std::size_t bytes{};
    std::size_t alignment{};
    [[nodiscard]] bool operator==(const ControlShape&) const noexcept = default;
};
struct SlabLayout final {
    std::size_t capacity{};
    std::size_t stride{};
    std::size_t alignment{};
    std::size_t bytes{};
};
struct FramePoolLayout final {
    FrameObjectPoolCapacity capacity{};
    std::array<SlabLayout, 4> slabs{};
    std::array<ControlShape, 3> shapes{};
    std::size_t slotBytes{};
    std::size_t requiredBytes{};
};

// Each slab owns raw memory only: no shared owners into its own storage.
class FrameSlab final {
public:
    FrameSlab() = default;
    ~FrameSlab();
    FrameSlab(const FrameSlab&) = delete;
    FrameSlab& operator=(const FrameSlab&) = delete;
    void prepare(SlabLayout layout);
    [[nodiscard]] std::optional<std::size_t> acquire() noexcept;
    void release(std::size_t index) noexcept;
    [[nodiscard]] void* address(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t indexOf(void* pointer) const noexcept;
    [[nodiscard]] std::size_t inUse() const noexcept;
    [[nodiscard]] std::size_t highWaterMark() const noexcept;
private:
    SlabLayout layout_{};
    void* storage_{};
    std::unique_ptr<std::size_t[]> freeIndices_;
    std::size_t available_{};
    std::size_t highWaterMark_{};
};

class FrameObjectPoolState final {
public:
    FrameObjectPoolState();
    ~FrameObjectPoolState();
    void prepare(FramePoolLayout layout, std::shared_ptr<const void> planOwner);
    [[nodiscard]] std::optional<std::size_t> acquireObject(FrameSlotKind kind) noexcept;
    [[nodiscard]] void* objectAddress(FrameSlotKind kind, std::size_t index) const noexcept;
    void releaseObject(FrameSlotKind kind, std::size_t index) noexcept;
    [[nodiscard]] void* allocateControl(ControlShape shape);
    void deallocateControl(void* memory, ControlShape shape) noexcept;
    [[nodiscard]] FrameObjectPoolStats stats() const noexcept;
    // Probe mode never prepares a slab and accepts exactly one request per owner.
    void beginProbe() noexcept;
    [[nodiscard]] ControlShape finishProbe() const;
private:
    void recordFailure() noexcept;
    mutable std::mutex mutex_;
    FramePoolLayout layout_{};
    std::shared_ptr<const void> planOwner_;
    std::array<FrameSlab, 4> slabs_;
    std::uint64_t failures_{};
    bool ready_{};
    ControlShape probeShape_{};
    std::size_t probeRequests_{};
    std::size_t liveProbes_{};
};

[[nodiscard]] Error framePoolError(FramePoolFailure failure);

template<typename T> struct FrameKind;
template<> struct FrameKind<ProcessedFrame> { static constexpr auto value = FrameSlotKind::Processed; };
template<> struct FrameKind<DisplayFrame> { static constexpr auto value = FrameSlotKind::Display; };
template<> struct FrameKind<FrameBundle> { static constexpr auto value = FrameSlotKind::Bundle; };

template<typename T>
struct PooledFrameDeleter final {
    std::shared_ptr<FrameObjectPoolState> state;
    std::size_t objectSlot;
    void operator()(const T* object) const noexcept {
        if (!object) return;
        object->~T();
        state->releaseObject(FrameKind<T>::value, objectSlot);
    }
};

template<typename T>
class FrameControlAllocator final {
public:
    using value_type = T;
    explicit FrameControlAllocator(std::shared_ptr<FrameObjectPoolState> owner) noexcept
        : state(std::move(owner)) {}
    template<typename U>
    FrameControlAllocator(const FrameControlAllocator<U>& other) noexcept : state(other.state) {}
    [[nodiscard]] T* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw FramePoolAllocationFailure{FramePoolFailure::UnsupportedShape};
        return static_cast<T*>(state->allocateControl({count, count * sizeof(T), alignof(T)}));
    }
    void deallocate(T* memory, std::size_t count) noexcept {
        // The rebound allocator is the last state owner after final weak release.
        auto keepAlive = state;
        keepAlive->deallocateControl(memory, {count, count * sizeof(T), alignof(T)});
    }
    template<typename U>
    [[nodiscard]] bool operator==(const FrameControlAllocator<U>& other) const noexcept {
        return state == other.state;
    }
    std::shared_ptr<FrameObjectPoolState> state;
};

// Construction stays in the typed frame factory's access boundary. Before the
// shared_ptr handoff, this helper owns rollback; the standard constructor then
// guarantees deleter invocation even when control allocation throws.
template<typename T, typename Construct>
[[nodiscard]] Result<std::shared_ptr<const T>> allocatePooledFrame(
    const std::shared_ptr<FrameObjectPoolState>& state, Construct&& construct) {
    const auto slot = state->acquireObject(FrameKind<T>::value);
    if (!slot) return Result<std::shared_ptr<const T>>::failure(framePoolError(FramePoolFailure::ObjectExhausted));
    T* object = nullptr;
    try {
        object = construct(state->objectAddress(FrameKind<T>::value, *slot));
    } catch (...) {
        state->releaseObject(FrameKind<T>::value, *slot);
        throw;
    }
    try {
        return Result<std::shared_ptr<const T>>::success(std::shared_ptr<const T>(object,
            PooledFrameDeleter<T>{state, *slot}, FrameControlAllocator<std::byte>{state}));
    } catch (const FramePoolAllocationFailure& failure) {
        return Result<std::shared_ptr<const T>>::failure(framePoolError(failure.reason));
    } catch (const std::bad_alloc&) {
        return Result<std::shared_ptr<const T>>::failure(framePoolError(FramePoolFailure::UnsupportedShape));
    }
}
}  // namespace lumora::core::detail
