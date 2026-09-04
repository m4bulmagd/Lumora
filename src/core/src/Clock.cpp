#include <lumora/core/Clock.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace lumora::core {

bool IClock::waitUntil(
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stopToken) const {
    return waitUntil(
               deadline,
               stopToken,
               std::chrono::milliseconds::max()) ==
           ClockWaitOutcome::DeadlineReached;
}

std::chrono::steady_clock::time_point SystemClock::steadyNow() const noexcept {
    return std::chrono::steady_clock::now();
}

std::chrono::system_clock::time_point SystemClock::utcNow() const noexcept {
    return std::chrono::system_clock::now();
}

ClockWaitOutcome SystemClock::waitUntil(
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stopToken,
    std::chrono::milliseconds maximumRealWait) const {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return ClockWaitOutcome::DeadlineReached;
    }

    std::condition_variable_any deadlineReached;
    std::mutex mutex;
    std::unique_lock lock(mutex);
    if (maximumRealWait == std::chrono::milliseconds::max() ||
        std::chrono::duration<long double>(deadline - now) <=
            std::chrono::duration<long double>(maximumRealWait)) {
        deadlineReached.wait_until(lock, stopToken, deadline, [] { return false; });
    } else {
        deadlineReached.wait_for(
            lock,
            stopToken,
            std::max(maximumRealWait, std::chrono::milliseconds::zero()),
            [] { return false; });
    }

    if (std::chrono::steady_clock::now() >= deadline) {
        return ClockWaitOutcome::DeadlineReached;
    }
    return stopToken.stop_requested()
               ? ClockWaitOutcome::Cancelled
               : ClockWaitOutcome::MaximumWaitElapsed;
}

ManualClock::ManualClock(
    std::chrono::steady_clock::time_point steadyTime,
    std::chrono::system_clock::time_point utcTime) noexcept
    : steadyTime_(steadyTime), utcTime_(utcTime) {}

std::chrono::steady_clock::time_point ManualClock::steadyNow() const noexcept {
    std::lock_guard lock(mutex_);
    return steadyTime_;
}

std::chrono::system_clock::time_point ManualClock::utcNow() const noexcept {
    std::lock_guard lock(mutex_);
    return utcTime_;
}

ClockWaitOutcome ManualClock::waitUntil(
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stopToken,
    std::chrono::milliseconds maximumRealWait) const {
    std::unique_lock lock(mutex_);
    const auto reached = [this, deadline] { return steadyTime_ >= deadline; };
    if (reached()) {
        return ClockWaitOutcome::DeadlineReached;
    }

    if (maximumRealWait == std::chrono::milliseconds::max()) {
        return advanced_.wait(lock, stopToken, reached)
                   ? ClockWaitOutcome::DeadlineReached
                   : ClockWaitOutcome::Cancelled;
    }

    constexpr auto maximumWaitChunk = std::chrono::hours{24};
    auto remaining =
        std::max(maximumRealWait, std::chrono::milliseconds::zero());
    while (remaining > std::chrono::milliseconds::zero()) {
        const auto chunk = std::min(
            remaining,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                maximumWaitChunk));
        if (advanced_.wait_for(lock, stopToken, chunk, reached)) {
            return ClockWaitOutcome::DeadlineReached;
        }
        if (stopToken.stop_requested()) {
            return ClockWaitOutcome::Cancelled;
        }
        remaining -= chunk;
    }
    return ClockWaitOutcome::MaximumWaitElapsed;
}

void ManualClock::advance(std::chrono::nanoseconds elapsed) noexcept {
    if (elapsed < std::chrono::nanoseconds::zero()) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        steadyTime_ +=
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(elapsed);
        utcTime_ +=
            std::chrono::duration_cast<std::chrono::system_clock::duration>(elapsed);
    }
    advanced_.notify_all();
}

void ManualClock::setUtc(
    std::chrono::system_clock::time_point utcTime) noexcept {
    std::lock_guard lock(mutex_);
    utcTime_ = utcTime;
}

}  // namespace lumora::core
