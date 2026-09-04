#pragma once

#include <lumora/core/Result.hpp>
#include <lumora/core/SharedBuffer.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace lumora::core {

namespace detail {
class BufferPoolState;
}

struct BufferPoolStats final {
    std::size_t capacity;
    std::size_t bytesPerBuffer;
    std::size_t inUse;
    std::size_t available;
    std::size_t highWaterMark;
    std::size_t acquisitionFailures;
};

class WritableBufferLease final {
public:
    WritableBufferLease(const WritableBufferLease&) = delete;
    WritableBufferLease& operator=(const WritableBufferLease&) = delete;
    WritableBufferLease(WritableBufferLease&& other) noexcept;
    WritableBufferLease& operator=(WritableBufferLease&& other) noexcept;
    ~WritableBufferLease();

    // The returned view is borrowed and is valid only until this lease is
    // moved, sealed, or destroyed. Retaining it beyond that boundary violates
    // the lease contract.
    [[nodiscard]] std::span<std::byte> bytes() noexcept;
    [[nodiscard]] SharedBuffer seal() && noexcept;

private:
    friend class BufferPool;

    WritableBufferLease(
        std::shared_ptr<detail::BufferPoolState> state,
        std::size_t blockIndex) noexcept;

    void reset() noexcept;

    std::shared_ptr<detail::BufferPoolState> state_;
    std::size_t blockIndex_{0U};
};

class BufferPool final {
public:
    [[nodiscard]] static Result<std::shared_ptr<BufferPool>> create(
        std::size_t capacity,
        std::size_t bytesPerBuffer);

    [[nodiscard]] std::optional<WritableBufferLease> tryAcquire() noexcept;
    [[nodiscard]] BufferPoolStats stats() const noexcept;

private:
    explicit BufferPool(std::shared_ptr<detail::BufferPoolState> state) noexcept;

    std::shared_ptr<detail::BufferPoolState> state_;
};

}  // namespace lumora::core
