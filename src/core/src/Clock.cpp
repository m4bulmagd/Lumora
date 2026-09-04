#include <lumora/core/Clock.hpp>

#include <chrono>
#include <mutex>

namespace lumora::core {

std::chrono::steady_clock::time_point SystemClock::steadyNow() const noexcept {
    return std::chrono::steady_clock::now();
}

std::chrono::system_clock::time_point SystemClock::utcNow() const noexcept {
    return std::chrono::system_clock::now();
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

void ManualClock::advance(std::chrono::nanoseconds elapsed) noexcept {
    if (elapsed < std::chrono::nanoseconds::zero()) {
        return;
    }

    std::lock_guard lock(mutex_);
    steadyTime_ +=
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(elapsed);
    utcTime_ +=
        std::chrono::duration_cast<std::chrono::system_clock::duration>(elapsed);
}

void ManualClock::setUtc(
    std::chrono::system_clock::time_point utcTime) noexcept {
    std::lock_guard lock(mutex_);
    utcTime_ = utcTime;
}

}  // namespace lumora::core
