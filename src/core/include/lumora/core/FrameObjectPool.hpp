#pragma once

#include <lumora/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace lumora::core {
struct ProcessedFrame;
struct DisplayFrame;
struct FrameBundle;
namespace detail { class FrameObjectPoolState; }

struct FrameObjectPoolCapacity final {
    std::size_t processedFrames;
    std::size_t displayFrames;
    std::size_t bundles;
    std::size_t controlBlocks;
};
struct FrameObjectPoolUsage final {
    std::size_t processedFrames;
    std::size_t displayFrames;
    std::size_t bundles;
    std::size_t controlBlocks;
};
struct FrameObjectPoolStats final {
    FrameObjectPoolCapacity capacity;
    FrameObjectPoolUsage inUse;
    FrameObjectPoolUsage highWaterMark;
    std::uint64_t acquisitionFailures;
    std::size_t slotStorageBytes;
};

class FrameObjectPoolPlan final {
public:
    ~FrameObjectPoolPlan();
    FrameObjectPoolPlan(const FrameObjectPoolPlan&) noexcept = default;
    FrameObjectPoolPlan& operator=(const FrameObjectPoolPlan&) noexcept = default;
    [[nodiscard]] FrameObjectPoolCapacity capacity() const noexcept;
    // Requested slabs, free-index arrays, and fixed plan/state/facade objects.
    // Excludes allocator headers and implementation-owned shared_ptr control
    // blocks for those three preparation owners (not the pooled frame controls).
    [[nodiscard]] std::size_t requiredStorageBytes() const noexcept;
    [[nodiscard]] std::size_t slotStorageBytes() const noexcept;
private:
    friend class FrameObjectPool;
    struct Impl;
    explicit FrameObjectPoolPlan(std::shared_ptr<const Impl>) noexcept;
    std::shared_ptr<const Impl> impl_;
};

class FrameObjectPool final {
public:
    [[nodiscard]] static Result<FrameObjectPoolPlan> plan(FrameObjectPoolCapacity capacity);
    [[nodiscard]] static Result<std::shared_ptr<FrameObjectPool>> create(const FrameObjectPoolPlan& plan);
    [[nodiscard]] static Result<std::shared_ptr<FrameObjectPool>> create(FrameObjectPoolCapacity capacity);
    [[nodiscard]] FrameObjectPoolStats stats() const noexcept;
    ~FrameObjectPool();
    FrameObjectPool(const FrameObjectPool&) = delete;
    FrameObjectPool& operator=(const FrameObjectPool&) = delete;
private:
    friend struct ProcessedFrame;
    friend struct DisplayFrame;
    friend struct FrameBundle;
    explicit FrameObjectPool(std::shared_ptr<detail::FrameObjectPoolState>) noexcept;
    std::shared_ptr<detail::FrameObjectPoolState> state_;
};
}  // namespace lumora::core
