#pragma once

#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/LatestValueSlot.hpp>

namespace lumora::application {

// Borrow the slots only while retaining this handle. Workers are the sole
// publishers. Pixel storage is bounded by the three session-owned pools.
struct LiveSessionContext final {
    std::uint64_t generation{0};
    std::shared_ptr<core::BufferPool> rawPool;
    std::shared_ptr<core::BufferPool> processingPool;
    std::shared_ptr<core::BufferPool> displayPool;
    core::LatestValueSlot<core::RawFrame> rawSlot;
    core::LatestValueSlot<core::FrameBundle> bundleSlot;
};

}  // namespace lumora::application
