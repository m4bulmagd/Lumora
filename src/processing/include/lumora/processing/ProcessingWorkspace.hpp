#pragma once

#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/processing/ImageView.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace lumora::processing {

inline constexpr std::string_view processingBufferPoolExhaustedCode =
    "processing_buffer_pool_exhausted";

class FrameProcessingEngine;

// Single-worker scratch storage. Pools outlive this object. prepare is allowed
// only while stopped; published leases are sealed and never made writable again.
class ProcessingWorkspace final {
public:
    ProcessingWorkspace(core::BufferPool& processingPool, core::BufferPool& displayPool) noexcept;
    [[nodiscard]] core::Result<void> prepare(const core::ImageLayout& sourceLayout,
        core::Orientation orientation = {false,false,core::Rotation::Degrees0}, std::size_t orientationBytes = 0);

private:
    friend class FrameProcessingEngine;
    [[nodiscard]] core::Result<void> replenish(bool enhanced = true);
    core::BufferPool& processingPool_;
    core::BufferPool& displayPool_;
    std::optional<core::ImageLayout> sourceLayout_;
    std::optional<core::ImageLayout> canonicalLayout_;
    std::optional<core::ImageLayout> displayLayout_;
    std::optional<core::ImageLayout> nativeDisplayLayout_;
    std::unique_ptr<std::byte[]> orientationScratch_;
    std::array<std::optional<core::WritableBufferLease>, 2> canonical_;
    std::array<std::optional<core::WritableBufferLease>, 2> display_;
};

}  // namespace lumora::processing
