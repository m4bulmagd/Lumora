#pragma once

#include <cstddef>
#include <memory>
#include <span>

namespace lumora::core {

namespace detail {
class BufferPoolState;
}

class WritableBufferLease;

class SharedBuffer final {
public:
    SharedBuffer() noexcept = default;
    SharedBuffer(const SharedBuffer& other) noexcept;
    SharedBuffer& operator=(const SharedBuffer& other) noexcept;
    SharedBuffer(SharedBuffer&& other) noexcept;
    SharedBuffer& operator=(SharedBuffer&& other) noexcept;
    ~SharedBuffer();

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    friend class WritableBufferLease;

    struct AdoptSealedReference final {};

    SharedBuffer(
        std::shared_ptr<detail::BufferPoolState> state,
        std::size_t blockIndex,
        AdoptSealedReference) noexcept;

    void reset() noexcept;
    void swap(SharedBuffer& other) noexcept;

    std::shared_ptr<detail::BufferPoolState> state_;
    std::size_t blockIndex_{0U};
};

}  // namespace lumora::core
