#pragma once

#include <lumora/core/Frame.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/core/Result.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace lumora::core {
class IClock;
}

namespace lumora::tools {

class SimulatorFeed final {
public:
    SimulatorFeed(core::LatestValueSlot<core::FrameBundle>& slot,
                  core::IClock& clock);
    ~SimulatorFeed();
    SimulatorFeed(const SimulatorFeed&) = delete;
    SimulatorFeed& operator=(const SimulatorFeed&) = delete;

    void start();
    void stop() noexcept;
    [[nodiscard]] core::Result<void> result() const;
    [[nodiscard]] std::uint64_t timeoutCount() const noexcept;

private:
    void run(std::stop_token stopToken) noexcept;
    void recordFailure(core::Error error) noexcept;
    void recordTimeout(core::Error error) noexcept;

    core::LatestValueSlot<core::FrameBundle>* slot_;
    core::IClock* clock_;
    mutable std::mutex mutex_;
    std::optional<core::Error> failure_;
    std::atomic<std::uint64_t> timeoutCount_{0U};
    std::jthread worker_;
};

}  // namespace lumora::tools
