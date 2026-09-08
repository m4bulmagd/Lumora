#pragma once

#include <lumora/core/Frame.hpp>
#include <lumora/processing/ProcessorStatus.hpp>
#include <lumora/core/Result.hpp>

#include <memory>
#include <string_view>

namespace lumora::processing {

inline constexpr std::string_view displayBufferPoolExhaustedCode =
    "display_buffer_pool_exhausted";

class IFrameProcessor {
public:
    virtual ~IFrameProcessor() = default;
    [[nodiscard]] virtual ProcessorStatus status() const noexcept { return {}; }
    [[nodiscard]] virtual bool requestRetry() noexcept { return false; }

    [[nodiscard]] virtual core::Result<std::shared_ptr<const core::FrameBundle>>
    process(std::shared_ptr<const core::RawFrame> raw) = 0;
};

}  // namespace lumora::processing
