#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace lumora::core {

template<typename T>
struct PublishedValue final {
    std::uint64_t revision;
    std::shared_ptr<const T> value;
};

template<typename T>
struct PublishResult final {
    std::uint64_t revision;
    bool replacedUnconsumed;
};

template<typename T>
class LatestValueSlot final {
public:
    LatestValueSlot() = default;
    LatestValueSlot(const LatestValueSlot&) = delete;
    LatestValueSlot& operator=(const LatestValueSlot&) = delete;

    [[nodiscard]] PublishResult<T> publish(std::shared_ptr<const T> value) {
        std::unique_lock lock(mutex_);
        if (closed_ || !value) {
            return PublishResult<T>{revision_, false};
        }

        const bool replacedUnconsumed =
            current_ && revision_ > lastConsumedRevision_;
        ++revision_;
        current_ = std::move(value);
        const auto result = PublishResult<T>{revision_, replacedUnconsumed};
        lock.unlock();
        condition_.notify_all();
        return result;
    }

    [[nodiscard]] std::optional<PublishedValue<T>> consumeAfter(
        std::uint64_t lastSeenRevision) {
        std::lock_guard lock(mutex_);
        return consumeAfterLocked(lastSeenRevision);
    }

    [[nodiscard]] std::optional<PublishedValue<T>> waitForNewer(
        std::uint64_t lastSeenRevision,
        std::stop_token stopToken) {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, stopToken, [this, lastSeenRevision] {
            return closed_ || (current_ && revision_ > lastSeenRevision);
        });
        return consumeAfterLocked(lastSeenRevision);
    }

    void close() noexcept {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    [[nodiscard]] bool closed() const noexcept {
        std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    [[nodiscard]] std::optional<PublishedValue<T>> consumeAfterLocked(
        std::uint64_t lastSeenRevision) {
        if (!current_ || revision_ <= lastSeenRevision) {
            return std::nullopt;
        }

        lastConsumedRevision_ = std::max(lastConsumedRevision_, revision_);
        return PublishedValue<T>{revision_, current_};
    }

    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::shared_ptr<const T> current_;
    std::uint64_t revision_{0U};
    std::uint64_t lastConsumedRevision_{0U};
    bool closed_{false};
};

}  // namespace lumora::core
