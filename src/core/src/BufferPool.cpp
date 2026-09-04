#include <lumora/core/BufferPool.hpp>

#include <lumora/core/CheckedMath.hpp>
#include <lumora/core/Error.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lumora::core {
namespace {

[[nodiscard]] Error poolError(std::string code, std::string detail) {
    return Error{
        ErrorCategory::ResourceExhaustion,
        std::move(code),
        "Image buffer memory could not be reserved.",
        std::move(detail),
        false,
    };
}

}  // namespace

namespace detail {

class BufferPoolState final {
public:
    BufferPoolState(
        std::size_t capacity,
        std::size_t bytesPerBuffer,
        std::size_t blockStride,
        std::size_t totalBytes)
        : capacity_(capacity),
          bytesPerBuffer_(bytesPerBuffer),
          blockStride_(blockStride),
          storage_(static_cast<std::byte*>(::operator new(
              totalBytes, std::align_val_t{alignof(std::max_align_t)}))),
          sealedReferences_(std::make_unique<std::atomic_size_t[]>(capacity)) {
        freeBlocks_.reserve(capacity_);
        for (std::size_t index = 0U; index < capacity_; ++index) {
            freeBlocks_.push_back(capacity_ - index - 1U);
            sealedReferences_[index].store(0U, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] std::optional<std::size_t> tryAcquire() noexcept {
        std::lock_guard lock(mutex_);
        if (freeBlocks_.empty()) {
            ++acquisitionFailures_;
            return std::nullopt;
        }

        const auto blockIndex = freeBlocks_.back();
        freeBlocks_.pop_back();
        ++inUse_;
        highWaterMark_ = std::max(highWaterMark_, inUse_);
        return blockIndex;
    }

    [[nodiscard]] std::span<std::byte> writableBytes(
        std::size_t blockIndex) noexcept {
        return std::span<std::byte>(
            storage_.get() + (blockIndex * blockStride_), bytesPerBuffer_);
    }

    [[nodiscard]] std::span<const std::byte> immutableBytes(
        std::size_t blockIndex) const noexcept {
        return std::span<const std::byte>(
            storage_.get() + (blockIndex * blockStride_), bytesPerBuffer_);
    }

    void beginSealedOwnership(std::size_t blockIndex) noexcept {
        const auto previous = sealedReferences_[blockIndex].exchange(
            1U, std::memory_order_acq_rel);
        assert(previous == 0U);
        static_cast<void>(previous);
    }

    void retainSealed(std::size_t blockIndex) noexcept {
        const auto previous = sealedReferences_[blockIndex].fetch_add(
            1U, std::memory_order_relaxed);
        assert(previous > 0U);
        static_cast<void>(previous);
    }

    void releaseSealed(std::size_t blockIndex) noexcept {
        const auto previous = sealedReferences_[blockIndex].fetch_sub(
            1U, std::memory_order_acq_rel);
        assert(previous > 0U);
        if (previous == 1U) {
            releaseBlock(blockIndex);
        }
    }

    void releaseWritable(std::size_t blockIndex) noexcept {
        assert(sealedReferences_[blockIndex].load(std::memory_order_relaxed) == 0U);
        releaseBlock(blockIndex);
    }

    [[nodiscard]] BufferPoolStats stats() const noexcept {
        std::lock_guard lock(mutex_);
        return BufferPoolStats{
            capacity_,
            bytesPerBuffer_,
            inUse_,
            freeBlocks_.size(),
            highWaterMark_,
            acquisitionFailures_,
        };
    }

private:
    void releaseBlock(std::size_t blockIndex) noexcept {
        std::lock_guard lock(mutex_);
        assert(inUse_ > 0U);
        assert(freeBlocks_.size() < capacity_);
        --inUse_;
        freeBlocks_.push_back(blockIndex);
    }

    const std::size_t capacity_;
    const std::size_t bytesPerBuffer_;
    const std::size_t blockStride_;
    struct AlignedDelete final {
        void operator()(std::byte* pointer) const noexcept {
            ::operator delete(
                pointer, std::align_val_t{alignof(std::max_align_t)});
        }
    };
    std::unique_ptr<std::byte, AlignedDelete> storage_;
    std::unique_ptr<std::atomic_size_t[]> sealedReferences_;
    mutable std::mutex mutex_;
    std::vector<std::size_t> freeBlocks_;
    std::size_t inUse_{0U};
    std::size_t highWaterMark_{0U};
    std::size_t acquisitionFailures_{0U};
};

}  // namespace detail

SharedBuffer::SharedBuffer(
    std::shared_ptr<detail::BufferPoolState> state,
    std::size_t blockIndex,
    AdoptSealedReference) noexcept
    : state_(std::move(state)), blockIndex_(blockIndex) {}

SharedBuffer::SharedBuffer(const SharedBuffer& other) noexcept
    : state_(other.state_), blockIndex_(other.blockIndex_) {
    if (state_) {
        state_->retainSealed(blockIndex_);
    }
}

SharedBuffer& SharedBuffer::operator=(const SharedBuffer& other) noexcept {
    if (this != &other) {
        SharedBuffer copy(other);
        swap(copy);
    }
    return *this;
}

SharedBuffer::SharedBuffer(SharedBuffer&& other) noexcept
    : state_(std::move(other.state_)), blockIndex_(other.blockIndex_) {
    other.blockIndex_ = 0U;
}

SharedBuffer& SharedBuffer::operator=(SharedBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        blockIndex_ = other.blockIndex_;
        other.blockIndex_ = 0U;
    }
    return *this;
}

SharedBuffer::~SharedBuffer() {
    reset();
}

std::span<const std::byte> SharedBuffer::bytes() const noexcept {
    if (!state_) {
        return {};
    }
    return state_->immutableBytes(blockIndex_);
}

std::size_t SharedBuffer::size() const noexcept {
    return bytes().size();
}

SharedBuffer::operator bool() const noexcept {
    return state_ != nullptr;
}

void SharedBuffer::reset() noexcept {
    if (state_) {
        state_->releaseSealed(blockIndex_);
        state_.reset();
        blockIndex_ = 0U;
    }
}

void SharedBuffer::swap(SharedBuffer& other) noexcept {
    state_.swap(other.state_);
    std::swap(blockIndex_, other.blockIndex_);
}

WritableBufferLease::WritableBufferLease(
    std::shared_ptr<detail::BufferPoolState> state,
    std::size_t blockIndex) noexcept
    : state_(std::move(state)), blockIndex_(blockIndex) {}

WritableBufferLease::WritableBufferLease(WritableBufferLease&& other) noexcept
    : state_(std::move(other.state_)), blockIndex_(other.blockIndex_) {
    other.blockIndex_ = 0U;
}

WritableBufferLease& WritableBufferLease::operator=(
    WritableBufferLease&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        blockIndex_ = other.blockIndex_;
        other.blockIndex_ = 0U;
    }
    return *this;
}

WritableBufferLease::~WritableBufferLease() {
    reset();
}

std::span<std::byte> WritableBufferLease::bytes() noexcept {
    if (!state_) {
        return {};
    }
    return state_->writableBytes(blockIndex_);
}

SharedBuffer WritableBufferLease::seal() && noexcept {
    if (!state_) {
        return {};
    }

    state_->beginSealedOwnership(blockIndex_);
    auto state = std::move(state_);
    const auto blockIndex = std::exchange(blockIndex_, 0U);
    return SharedBuffer(
        std::move(state), blockIndex, SharedBuffer::AdoptSealedReference{});
}

void WritableBufferLease::reset() noexcept {
    if (state_) {
        state_->releaseWritable(blockIndex_);
        state_.reset();
        blockIndex_ = 0U;
    }
}

BufferPool::BufferPool(std::shared_ptr<detail::BufferPoolState> state) noexcept
    : state_(std::move(state)) {}

Result<std::shared_ptr<BufferPool>> BufferPool::create(
    std::size_t capacity,
    std::size_t bytesPerBuffer) {
    if (capacity == 0U) {
        return Result<std::shared_ptr<BufferPool>>::failure(poolError(
            "buffer_pool_zero_capacity",
            "A buffer pool must contain at least one block."));
    }
    if (bytesPerBuffer == 0U) {
        return Result<std::shared_ptr<BufferPool>>::failure(poolError(
            "buffer_pool_zero_block_size",
            "A buffer-pool block must contain at least one byte."));
    }

    constexpr auto blockAlignment = alignof(std::max_align_t);
    const auto remainder = bytesPerBuffer % blockAlignment;
    const auto padding = remainder == 0U ? 0U : blockAlignment - remainder;
    const auto blockStride = checkedAdd(bytesPerBuffer, padding);
    if (!blockStride.hasValue()) {
        return Result<std::shared_ptr<BufferPool>>::failure(poolError(
            "buffer_pool_size_overflow",
            "Aligning the requested block size overflows size_t."));
    }

    const auto totalBytes = checkedMultiply(capacity, blockStride.value());
    if (!totalBytes.hasValue()) {
        return Result<std::shared_ptr<BufferPool>>::failure(poolError(
            "buffer_pool_size_overflow",
            "The requested capacity times block size overflows size_t."));
    }

    try {
        auto state = std::make_shared<detail::BufferPoolState>(
            capacity, bytesPerBuffer, blockStride.value(), totalBytes.value());
        return Result<std::shared_ptr<BufferPool>>::success(
            std::shared_ptr<BufferPool>(new BufferPool(std::move(state))));
    } catch (const std::bad_alloc&) {
        return Result<std::shared_ptr<BufferPool>>::failure(poolError(
            "buffer_pool_allocation_failed",
            "The configured image-buffer memory could not be allocated."));
    } catch (const std::length_error&) {
        return Result<std::shared_ptr<BufferPool>>::failure(poolError(
            "buffer_pool_allocation_failed",
            "The configured image-buffer memory exceeds container limits."));
    }
}

std::optional<WritableBufferLease> BufferPool::tryAcquire() noexcept {
    const auto blockIndex = state_->tryAcquire();
    if (!blockIndex.has_value()) {
        return std::nullopt;
    }
    return WritableBufferLease(state_, *blockIndex);
}

BufferPoolStats BufferPool::stats() const noexcept {
    return state_->stats();
}

}  // namespace lumora::core
