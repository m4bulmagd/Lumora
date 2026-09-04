#pragma once

#include <lumora/core/PixelFormat.hpp>
#include <lumora/core/Result.hpp>

#include <cstddef>
#include <cstdint>

namespace lumora::core {

class ImageLayout final {
public:
    [[nodiscard]] static Result<ImageLayout> create(
        std::uint32_t width,
        std::uint32_t height,
        std::size_t strideBytes,
        StorageType storage,
        std::size_t payloadBytes);

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::size_t rowBytes() const noexcept;
    [[nodiscard]] std::size_t strideBytes() const noexcept;
    [[nodiscard]] std::size_t requiredBytes() const noexcept;
    [[nodiscard]] std::size_t payloadBytes() const noexcept;
    [[nodiscard]] StorageType storage() const noexcept;

private:
    ImageLayout(
        std::uint32_t width,
        std::uint32_t height,
        std::size_t rowBytes,
        std::size_t strideBytes,
        std::size_t requiredBytes,
        std::size_t payloadBytes,
        StorageType storage) noexcept;

    std::uint32_t width_;
    std::uint32_t height_;
    std::size_t rowBytes_;
    std::size_t strideBytes_;
    std::size_t requiredBytes_;
    std::size_t payloadBytes_;
    StorageType storage_;
};

}  // namespace lumora::core
