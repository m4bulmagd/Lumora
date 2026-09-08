#include "FrameObjectPoolState.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

#ifdef LUMORA_FRAME_POOL_TESTS
#include "FrameObjectPoolTestSupport.hpp"
#include <atomic>
#endif

namespace lumora::core::detail {
#ifdef LUMORA_FRAME_POOL_TESTS
namespace {
std::atomic_size_t probes{}, slabAllocations{}, liveSlabs{}, liveStates{}, liveProbeAllocations{};
std::atomic_size_t slabFailureCountdown{std::numeric_limits<std::size_t>::max()};
std::atomic_bool rejectShape{};
}
namespace testing {
PoolCounters poolCounters() noexcept {
    return {probes.load(), slabAllocations.load(), liveSlabs.load(), liveStates.load(), liveProbeAllocations.load()};
}
void failSlabAllocationAfter(std::size_t count) noexcept { slabFailureCountdown = count; }
void rejectControlShape(bool reject) noexcept { rejectShape = reject; }
void resetPoolFailures() noexcept {
    slabFailureCountdown = std::numeric_limits<std::size_t>::max();
    rejectShape = false;
}
}
#endif

FrameSlab::~FrameSlab() {
    if (storage_) {
        ::operator delete(storage_, std::align_val_t{layout_.alignment});
#ifdef LUMORA_FRAME_POOL_TESTS
        --liveSlabs;
#endif
    }
}
void FrameSlab::prepare(SlabLayout layout) {
    layout_ = layout;
#ifdef LUMORA_FRAME_POOL_TESTS
    if (slabFailureCountdown != std::numeric_limits<std::size_t>::max()) {
        if (slabFailureCountdown == 0U) throw std::bad_alloc{};
        --slabFailureCountdown;
    }
#endif
    storage_ = ::operator new(layout.bytes, std::align_val_t{layout.alignment});
#ifdef LUMORA_FRAME_POOL_TESTS
    ++slabAllocations;
    ++liveSlabs;
#endif
    freeIndices_ = std::make_unique<std::size_t[]>(layout.capacity);
    available_ = layout.capacity;
    for (std::size_t index = 0; index < available_; ++index) freeIndices_[index] = available_ - index - 1U;
}
std::optional<std::size_t> FrameSlab::acquire() noexcept {
    if (!available_) return std::nullopt;
    const auto index = freeIndices_[--available_];
    highWaterMark_ = std::max(highWaterMark_, inUse());
    return index;
}
void FrameSlab::release(std::size_t index) noexcept {
    assert(index < layout_.capacity && available_ < layout_.capacity);
    freeIndices_[available_++] = index;
}
void* FrameSlab::address(std::size_t index) const noexcept {
    assert(index < layout_.capacity);
    return static_cast<std::byte*>(storage_) + index * layout_.stride;
}
std::size_t FrameSlab::indexOf(void* pointer) const noexcept {
    const auto offset = static_cast<std::size_t>(static_cast<std::byte*>(pointer) - static_cast<std::byte*>(storage_));
    assert(offset < layout_.bytes && offset % layout_.stride == 0);
    return offset / layout_.stride;
}
std::size_t FrameSlab::inUse() const noexcept { return layout_.capacity - available_; }
std::size_t FrameSlab::highWaterMark() const noexcept { return highWaterMark_; }

FrameObjectPoolState::FrameObjectPoolState() {
#ifdef LUMORA_FRAME_POOL_TESTS
    ++liveStates;
#endif
}
FrameObjectPoolState::~FrameObjectPoolState() {
    assert(liveProbes_ == 0);
#ifdef LUMORA_FRAME_POOL_TESTS
    --liveStates;
#endif
}
void FrameObjectPoolState::prepare(FramePoolLayout layout, std::shared_ptr<const void> planOwner) {
    layout_ = layout;
    planOwner_ = std::move(planOwner);
    for (std::size_t index = 0; index < slabs_.size(); ++index) slabs_[index].prepare(layout.slabs[index]);
    ready_ = true;
}
void FrameObjectPoolState::recordFailure() noexcept {
    if (failures_ != std::numeric_limits<std::uint64_t>::max()) ++failures_;
}
std::optional<std::size_t> FrameObjectPoolState::acquireObject(FrameSlotKind kind) noexcept {
    std::lock_guard lock(mutex_);
    auto acquired = slabs_[static_cast<std::size_t>(kind)].acquire();
    if (!acquired) recordFailure();
    return acquired;
}
void* FrameObjectPoolState::objectAddress(FrameSlotKind kind, std::size_t index) const noexcept {
    return slabs_[static_cast<std::size_t>(kind)].address(index);
}
void FrameObjectPoolState::releaseObject(FrameSlotKind kind, std::size_t index) noexcept {
    std::lock_guard lock(mutex_);
    slabs_[static_cast<std::size_t>(kind)].release(index);
}
void* FrameObjectPoolState::allocateControl(ControlShape shape) {
    if (!ready_) {
        ++probeRequests_;
        if (probeRequests_ != 1 || shape.count != 1 || !shape.bytes || !shape.alignment)
            throw FramePoolAllocationFailure{FramePoolFailure::UnsupportedShape};
        probeShape_ = shape;
        auto* memory = ::operator new(shape.bytes, std::align_val_t{shape.alignment});
        ++liveProbes_;
#ifdef LUMORA_FRAME_POOL_TESTS
        ++probes;
        ++liveProbeAllocations;
#endif
        return memory;
    }
    std::lock_guard lock(mutex_);
    if (std::find(layout_.shapes.begin(), layout_.shapes.end(), shape) == layout_.shapes.end()) {
        recordFailure();
        throw FramePoolAllocationFailure{FramePoolFailure::UnsupportedShape};
    }
    auto& slab = slabs_[static_cast<std::size_t>(FrameSlotKind::Control)];
    auto slot = slab.acquire();
    if (!slot) {
        recordFailure();
        throw FramePoolAllocationFailure{FramePoolFailure::ControlExhausted};
    }
    auto* memory = slab.address(*slot);
    assert(reinterpret_cast<std::uintptr_t>(memory) % shape.alignment == 0);
    return memory;
}
void FrameObjectPoolState::deallocateControl(void* memory, ControlShape shape) noexcept {
    if (!ready_) {
        ::operator delete(memory, std::align_val_t{shape.alignment});
        --liveProbes_;
#ifdef LUMORA_FRAME_POOL_TESTS
        --liveProbeAllocations;
#endif
        return;
    }
    std::lock_guard lock(mutex_);
    auto& slab = slabs_[static_cast<std::size_t>(FrameSlotKind::Control)];
    slab.release(slab.indexOf(memory));
}
FrameObjectPoolStats FrameObjectPoolState::stats() const noexcept {
    std::lock_guard lock(mutex_);
    return {layout_.capacity,
        {slabs_[0].inUse(), slabs_[1].inUse(), slabs_[2].inUse(), slabs_[3].inUse()},
        {slabs_[0].highWaterMark(), slabs_[1].highWaterMark(), slabs_[2].highWaterMark(), slabs_[3].highWaterMark()},
        failures_, layout_.slotBytes};
}
void FrameObjectPoolState::beginProbe() noexcept {
    assert(!ready_ && liveProbes_ == 0);
    probeRequests_ = 0;
    probeShape_ = {};
}
ControlShape FrameObjectPoolState::finishProbe() const {
#ifdef LUMORA_FRAME_POOL_TESTS
    if (rejectShape) throw FramePoolAllocationFailure{FramePoolFailure::UnsupportedShape};
#endif
    if (probeRequests_ != 1 || liveProbes_ != 0)
        throw FramePoolAllocationFailure{FramePoolFailure::UnsupportedShape};
    return probeShape_;
}
Error framePoolError(FramePoolFailure failure) {
    const char* code = "frame_object_pool_exhausted";
    if (failure == FramePoolFailure::ControlExhausted) code = "frame_control_pool_exhausted";
    if (failure == FramePoolFailure::UnsupportedShape) code = "frame_control_allocator_shape_unsupported";
    return {ErrorCategory::ResourceExhaustion, code, "Frame metadata storage is unavailable.",
        "The prepared immutable frame pool cannot satisfy this ownership request.", true};
}
}  // namespace lumora::core::detail
