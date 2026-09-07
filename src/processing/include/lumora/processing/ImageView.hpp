#pragma once

#include <lumora/core/ImageLayout.hpp>
#include <lumora/processing/ProcessingConfiguration.hpp>

#include <span>

namespace lumora::processing {

// Non-owning views: the caller must keep backing storage alive and stationary
// for the whole operation. A mutable view additionally requires exclusive write
// access. Layout is copied; byte rows support unaligned U16 storage safely via
// memcpy, never by casting the byte pointer to a uint16_t pointer.
class ImageView final {
public:
    [[nodiscard]] static core::Result<ImageView> create(
        core::ImageLayout layout,
        std::span<const std::byte> bytes,
        ImageDomain domain);
    [[nodiscard]] const core::ImageLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] ImageDomain domain() const noexcept { return domain_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    // Returns pixel bytes only (excludes padding); out-of-range rows are empty.
    [[nodiscard]] std::span<const std::byte> row(std::uint32_t y) const noexcept;

private:
    ImageView(core::ImageLayout layout, std::span<const std::byte> bytes, ImageDomain domain)
        : layout_(layout), bytes_(bytes), domain_(domain) {}
    core::ImageLayout layout_;
    std::span<const std::byte> bytes_;
    ImageDomain domain_;
};

class MutableImageView final {
public:
    [[nodiscard]] static core::Result<MutableImageView> create(
        core::ImageLayout layout,
        std::span<std::byte> bytes,
        ImageDomain domain);
    [[nodiscard]] const core::ImageLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] ImageDomain domain() const noexcept { return domain_; }
    [[nodiscard]] std::span<std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::span<std::byte> row(std::uint32_t y) const noexcept;
    [[nodiscard]] ImageView asConst() const;

private:
    MutableImageView(core::ImageLayout layout, std::span<std::byte> bytes, ImageDomain domain)
        : layout_(layout), bytes_(bytes), domain_(domain) {}
    core::ImageLayout layout_;
    std::span<std::byte> bytes_;
    ImageDomain domain_;
};

// Includes row padding, excluding unused bytes beyond the declared payload.
[[nodiscard]] bool overlaps(const ImageView& source, const MutableImageView& destination) noexcept;

}  // namespace lumora::processing
