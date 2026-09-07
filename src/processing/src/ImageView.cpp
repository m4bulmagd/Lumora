#include <lumora/processing/ImageView.hpp>

#include <cstdint>
#include <utility>

namespace lumora::processing {
namespace {
core::Result<void> validateView(const core::ImageLayout& layout, std::size_t size, ImageDomain domain) {
    const auto failure = [](std::string code, std::string detail) {
        return core::Result<void>::failure({core::ErrorCategory::InvalidFrame,
            std::move(code), "The processing image view is invalid.", std::move(detail), false});
    };
    if (size < layout.payloadBytes())
        return failure("image_view_span_too_small", "The backing span must cover the complete declared payload.");
    if (domain != ImageDomain::SensorNative && domain != ImageDomain::CanonicalU16)
        return failure("unknown_image_domain", "The view has an unknown image domain.");
    if (domain == ImageDomain::CanonicalU16 && layout.storage() != core::StorageType::UInt16)
        return failure("image_view_storage_mismatch", "Canonical images require unsigned 16-bit storage.");
    return core::Result<void>::success();
}
} // namespace

core::Result<ImageView> ImageView::create(core::ImageLayout layout,
    std::span<const std::byte> bytes, ImageDomain domain) {
    auto validation = validateView(layout, bytes.size(), domain);
    if (!validation.hasValue()) return core::Result<ImageView>::failure(validation.error());
    return core::Result<ImageView>::success(ImageView(layout, bytes.first(layout.payloadBytes()), domain));
}

core::Result<MutableImageView> MutableImageView::create(core::ImageLayout layout,
    std::span<std::byte> bytes, ImageDomain domain) {
    auto validation = validateView(layout, bytes.size(), domain);
    if (!validation.hasValue()) return core::Result<MutableImageView>::failure(validation.error());
    return core::Result<MutableImageView>::success(MutableImageView(layout, bytes.first(layout.payloadBytes()), domain));
}

std::span<const std::byte> ImageView::row(std::uint32_t y) const noexcept {
    if (y >= layout_.height()) return {};
    return bytes_.subspan(static_cast<std::size_t>(y) * layout_.strideBytes(), layout_.rowBytes());
}

std::span<std::byte> MutableImageView::row(std::uint32_t y) const noexcept {
    if (y >= layout_.height()) return {};
    return bytes_.subspan(static_cast<std::size_t>(y) * layout_.strideBytes(), layout_.rowBytes());
}

ImageView MutableImageView::asConst() const {
    return ImageView::create(layout_, bytes_, domain_).value();
}

bool overlaps(const ImageView& source, const MutableImageView& destination) noexcept {
    const auto sourceStart = reinterpret_cast<std::uintptr_t>(source.bytes().data());
    const auto destinationStart = reinterpret_cast<std::uintptr_t>(destination.bytes().data());
    // Subtraction avoids end-address overflow and ordering unrelated pointers.
    return sourceStart <= destinationStart
        ? destinationStart - sourceStart < source.bytes().size()
        : sourceStart - destinationStart < destination.bytes().size();
}
} // namespace lumora::processing
