#pragma once

#include <lumora/core/BufferPool.hpp>
#include <lumora/processing/IFrameProcessor.hpp>

namespace lumora::processing {

class Mono8PassThroughProcessor final : public IFrameProcessor {
public:
    explicit Mono8PassThroughProcessor(core::BufferPool& displayPool) noexcept;

    [[nodiscard]] core::Result<std::shared_ptr<const core::FrameBundle>>
    process(std::shared_ptr<const core::RawFrame> raw) override;

private:
    core::BufferPool* displayPool_;
};

}  // namespace lumora::processing
