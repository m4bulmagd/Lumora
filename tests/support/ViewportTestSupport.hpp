#pragma once

#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Frame.hpp>

#include <QImage>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace lumora::test {

inline std::shared_ptr<const core::DisplayFrame> makeDisplayFrame(
    std::uint32_t width, std::uint32_t height, std::uint64_t id) {
    const auto stride = (static_cast<std::size_t>(width) + 3U) & ~std::size_t{3U};
    const auto layout = core::ImageLayout::create(
        width, height, stride, core::StorageType::UInt8,
        stride * static_cast<std::size_t>(height)).value();
    auto pool = core::BufferPool::create(1U, layout.payloadBytes()).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), std::byte{0x80});
    return core::DisplayFrame::create(
        id, layout, std::move(*lease).seal(), core::DisplayStorage::Gray8,
        core::DisplayMapping{0U, 255U, 255U, 1U},
        core::Orientation{false, false, core::Rotation::Degrees0}).value();
}

inline QImage paintWidget(QWidget& widget, double dpr = 1.0) {
    QImage result(
        static_cast<int>(std::ceil(widget.width() * dpr)),
        static_cast<int>(std::ceil(widget.height() * dpr)),
        QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(dpr);
    result.fill(Qt::transparent);
    widget.render(&result);
    return result;
}

}  // namespace lumora::test
