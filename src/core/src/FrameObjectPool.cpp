#include <lumora/core/FrameObjectPool.hpp>
#include <lumora/core/Frame.hpp>

#include "FrameObjectPoolState.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lumora::core {
struct FrameObjectPoolPlan::Impl final {
    detail::FramePoolLayout layout;
};
namespace {
std::size_t add(std::size_t first, std::size_t second) {
    if (second > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) - first)
        throw std::length_error("frame pool storage overflow");
    return first + second;
}
std::size_t multiply(std::size_t count, std::size_t stride) {
    if (count > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / stride)
        throw std::length_error("frame pool storage overflow");
    return count * stride;
}
detail::SlabLayout slab(std::size_t capacity, std::size_t bytes, std::size_t alignment) {
    const auto stride = add(bytes, alignment - 1U) / alignment * alignment;
    return {capacity, stride, alignment, multiply(capacity, stride)};
}
template<typename T>
detail::ControlShape probe(const std::shared_ptr<detail::FrameObjectPoolState>& state) {
    state->beginProbe();
    {
        const std::shared_ptr<const T> owner(nullptr,
            detail::PooledFrameDeleter<T>{state, 0U}, detail::FrameControlAllocator<std::byte>{state});
        const std::weak_ptr<const T> weak = owner;
    }
    return state->finishProbe();
}
Error preparationError(const char* code) {
    return {ErrorCategory::ResourceExhaustion, code, "Frame object pool preparation failed.",
        "Capacities must be nonzero and all requested storage must be representable and available.", true};
}
}
FrameObjectPoolPlan::FrameObjectPoolPlan(std::shared_ptr<const Impl> impl) noexcept : impl_(std::move(impl)) {}
FrameObjectPoolPlan::~FrameObjectPoolPlan() = default;
FrameObjectPoolCapacity FrameObjectPoolPlan::capacity() const noexcept { return impl_->layout.capacity; }
std::size_t FrameObjectPoolPlan::requiredStorageBytes() const noexcept { return impl_->layout.requiredBytes; }
std::size_t FrameObjectPoolPlan::slotStorageBytes() const noexcept { return impl_->layout.slotBytes; }

Result<FrameObjectPoolPlan> FrameObjectPool::plan(FrameObjectPoolCapacity capacity) {
    using Result = core::Result<FrameObjectPoolPlan>;
    if (!capacity.processedFrames || !capacity.displayFrames || !capacity.bundles || !capacity.controlBlocks)
        return Result::failure(preparationError("invalid_frame_object_pool_capacity"));
    try {
        detail::FramePoolLayout layout;
        layout.capacity = capacity;
        layout.slabs[0] = slab(capacity.processedFrames, sizeof(ProcessedFrame), alignof(ProcessedFrame));
        layout.slabs[1] = slab(capacity.displayFrames, sizeof(DisplayFrame), alignof(DisplayFrame));
        layout.slabs[2] = slab(capacity.bundles, sizeof(FrameBundle), alignof(FrameBundle));
        // Reject capacity-sized bookkeeping and minimum control storage overflow
        // before any probes. The exact rebound control shape is discovered below.
        std::size_t indices = 0;
        for (auto count : {capacity.processedFrames, capacity.displayFrames, capacity.bundles, capacity.controlBlocks})
            indices = add(indices, multiply(count, sizeof(std::size_t)));
        std::size_t objectBytes = 0;
        for (std::size_t index = 0; index < 3; ++index) objectBytes = add(objectBytes, layout.slabs[index].bytes);
        static_cast<void>(add(objectBytes, indices));
        const auto state = std::make_shared<detail::FrameObjectPoolState>();
        layout.shapes = {probe<ProcessedFrame>(state), probe<DisplayFrame>(state), probe<FrameBundle>(state)};
        std::size_t controlBytes = 0, controlAlignment = 1;
        for (const auto shape : layout.shapes) {
            controlBytes = std::max(controlBytes, shape.bytes);
            controlAlignment = std::max(controlAlignment, shape.alignment);
        }
        layout.slabs[3] = slab(capacity.controlBlocks, controlBytes, controlAlignment);
        layout.slotBytes = add(objectBytes, layout.slabs[3].bytes);
        layout.requiredBytes = add(add(layout.slotBytes, indices),
            sizeof(FrameObjectPoolPlan::Impl) + sizeof(detail::FrameObjectPoolState) + sizeof(FrameObjectPool));
        auto impl = std::shared_ptr<const FrameObjectPoolPlan::Impl>(new FrameObjectPoolPlan::Impl{layout});
        return Result::success(FrameObjectPoolPlan{std::move(impl)});
    } catch (const detail::FramePoolAllocationFailure& failure) {
        return Result::failure(detail::framePoolError(failure.reason));
    } catch (const std::length_error&) {
        return Result::failure(preparationError("frame_object_pool_capacity_overflow"));
    } catch (const std::exception&) {
        return Result::failure(preparationError("frame_object_pool_allocation_failed"));
    }
}
FrameObjectPool::FrameObjectPool(std::shared_ptr<detail::FrameObjectPoolState> state) noexcept : state_(std::move(state)) {}
FrameObjectPool::~FrameObjectPool() = default;
Result<std::shared_ptr<FrameObjectPool>> FrameObjectPool::create(const FrameObjectPoolPlan& plan) {
    using Result = core::Result<std::shared_ptr<FrameObjectPool>>;
    try {
        auto state = std::shared_ptr<detail::FrameObjectPoolState>(new detail::FrameObjectPoolState);
        state->prepare(plan.impl_->layout, plan.impl_);
        return Result::success(std::shared_ptr<FrameObjectPool>(new FrameObjectPool(std::move(state))));
    } catch (const std::exception&) {
        return Result::failure(preparationError("frame_object_pool_allocation_failed"));
    }
}
Result<std::shared_ptr<FrameObjectPool>> FrameObjectPool::create(FrameObjectPoolCapacity capacity) {
    auto prepared = plan(capacity);
    if (!prepared.hasValue()) return Result<std::shared_ptr<FrameObjectPool>>::failure(prepared.error());
    return create(prepared.value());
}
FrameObjectPoolStats FrameObjectPool::stats() const noexcept { return state_->stats(); }
}  // namespace lumora::core
