#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace lumora::core {

template<typename T>
class BoundedQueue final {
public:
    explicit BoundedQueue(std::size_t capacity) : slots_(capacity) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    [[nodiscard]] bool tryPush(T value) {
        std::unique_lock lock(mutex_);
        if (closed_ || size_ == slots_.size()) {
            return false;
        }

        slots_[tail_].emplace(std::move(value));
        tail_ = increment(tail_);
        ++size_;
        lock.unlock();
        condition_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> waitPop(std::stop_token stopToken) {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, stopToken, [this] {
            return size_ > 0U || closed_;
        });
        if (size_ == 0U) {
            return std::nullopt;
        }

        std::optional<T> value(std::move(slots_[head_]));
        slots_[head_].reset();
        head_ = increment(head_);
        --size_;
        return value;
    }

    void close() noexcept {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return slots_.size();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard lock(mutex_);
        return size_;
    }

    [[nodiscard]] bool closed() const noexcept {
        std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    [[nodiscard]] std::size_t increment(std::size_t index) const noexcept {
        if (slots_.empty() || index + 1U == slots_.size()) {
            return 0U;
        }
        return index + 1U;
    }

    std::vector<std::optional<T>> slots_;
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::size_t head_{0U};
    std::size_t tail_{0U};
    std::size_t size_{0U};
    bool closed_{false};
};

}  // namespace lumora::core
