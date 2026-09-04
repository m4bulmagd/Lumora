#include <lumora/core/Clock.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace lumora::core {

std::chrono::steady_clock::time_point SystemClock::steadyNow() const noexcept {
    return std::chrono::steady_clock::now();
}

std::chrono::system_clock::time_point SystemClock::utcNow() const noexcept {
    return std::chrono::system_clock::now();
}

bool SystemClock::waitUntil(
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stopToken) const {
    std::condition_variable_any deadlineReached;
    std::mutex mutex;
    std::unique_lock lock(mutex);
    deadlineReached.wait_until(lock, stopToken, deadline, [] { return false; });
    return std::chrono::steady_clock::now() >= deadline;
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

bool ManualClock::waitUntil(
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stopToken) const {
    std::unique_lock lock(mutex_);
    return advanced_.wait(
        lock, stopToken, [this, deadline] { return steadyTime_ >= deadline; });
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
