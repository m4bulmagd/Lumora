#pragma once

#include <lumora/processing/ImageView.hpp>

#include <cstdint>
#include <cstring>

namespace lumora::processing::detail {

inline void copyActiveRows(
    const ImageView& source,
    MutableImageView destination) noexcept {
    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        const auto sourceRow = source.row(y);
        const auto destinationRow = destination.row(y);
        std::memcpy(destinationRow.data(), sourceRow.data(), sourceRow.size());
    }
}

}  // namespace lumora::processing::detail
