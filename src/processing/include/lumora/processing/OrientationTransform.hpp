#pragma once

#include <lumora/core/Frame.hpp>

#include <cstddef>
#include <span>

namespace lumora::processing {

class OrientationTransform final {
public:
    [[nodiscard]] static core::Result<core::ImageLayout> outputLayout(
        const core::ImageLayout& sourceLayout,
        core::DisplayStorage storage,
        core::Orientation orientation);

    [[nodiscard]] core::Result<void> apply(
        const core::ImageLayout& sourceLayout,
        core::DisplayStorage storage,
        std::span<const std::byte> sourceBytes,
        const core::ImageLayout& destinationLayout,
        std::span<std::byte> destinationBytes,
        core::Orientation orientation) const;
};

}  // namespace lumora::processing
