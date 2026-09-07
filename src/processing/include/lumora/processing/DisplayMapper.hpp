#pragma once

#include <lumora/core/Frame.hpp>
#include <lumora/processing/ImageView.hpp>

#include <cstdint>
#include <span>

namespace lumora::processing {

// Terminal conversion from canonical processing storage to a requested display
// representation. The caller owns and bounds the destination storage.
class DisplayMapper final {
public:
    [[nodiscard]] core::Result<core::DisplayMapping> map(
        const ImageView& source,
        const core::ImageLayout& destinationLayout,
        core::DisplayStorage destinationStorage,
        std::span<std::byte> destinationBytes,
        std::uint64_t configurationRevision) const;
};

}  // namespace lumora::processing
