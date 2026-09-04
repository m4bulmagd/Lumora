#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace lumora::core {

class IClock {
public:
    virtual ~IClock() = default;

    [[nodiscard]] virtual std::chrono::steady_clock::time_point steadyNow()
        const noexcept = 0;
    [[nodiscard]] virtual std::chrono::system_clock::time_point utcNow()
        const noexcept = 0;
    [[nodiscard]] virtual bool waitUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token stopToken) const = 0;
};

class SystemClock final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point steadyNow()
        const noexcept override;
    [[nodiscard]] std::chrono::system_clock::time_point utcNow()
        const noexcept override;
    [[nodiscard]] bool waitUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token stopToken) const override;
};

class ManualClock final : public IClock {
public:
    explicit ManualClock(
        std::chrono::steady_clock::time_point steadyTime = {},
        std::chrono::system_clock::time_point utcTime = {}) noexcept;

    [[nodiscard]] std::chrono::steady_clock::time_point steadyNow()
        const noexcept override;
    [[nodiscard]] std::chrono::system_clock::time_point utcNow()
        const noexcept override;
    [[nodiscard]] bool waitUntil(
        std::chrono::steady_clock::time_point deadline,
        std::stop_token stopToken) const override;

    void advance(std::chrono::nanoseconds elapsed) noexcept;
    void setUtc(std::chrono::system_clock::time_point utcTime) noexcept;

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable_any advanced_;
    std::chrono::steady_clock::time_point steadyTime_;
    std::chrono::system_clock::time_point utcTime_;
};

}  // namespace lumora::core
