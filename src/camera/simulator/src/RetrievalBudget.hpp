#pragma once

#include <lumora/core/Error.hpp>

#include <chrono>
#include <optional>
#include <stop_token>

namespace lumora::camera::sim {

// A real-steady deadline is shared across every phase of one retrieval, even
// when the injected pacing clock advances manually or has another epoch.
class RetrievalBudget final {
public:
    RetrievalBudget(std::chrono::steady_clock::time_point deadline, std::stop_token stopToken) noexcept
        : deadline_(deadline), stopToken_(stopToken) {}

    [[nodiscard]] std::optional<core::Error> interruption() const {
        if (stopToken_.stop_requested()) {
            return core::Error{core::ErrorCategory::Cancelled, "cancelled",
                "Frame retrieval was cancelled.",
                "The stop token was requested before frame publication.", true};
        }
        if (std::chrono::steady_clock::now() >= deadline_) {
            return core::Error{core::ErrorCategory::Acquisition, "acquisition_timeout",
                "No camera frame arrived before the timeout.",
                "The retrieval budget expired before frame publication.", true};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::chrono::milliseconds remainingWait() const noexcept {
        using Clock = std::chrono::steady_clock;
        if (deadline_ == Clock::time_point::max()) {
            return std::chrono::milliseconds::max();
        }
        const auto now = Clock::now();
        return now >= deadline_ ? std::chrono::milliseconds::zero()
                               : std::chrono::ceil<std::chrono::milliseconds>(deadline_ - now);
    }

private:
    std::chrono::steady_clock::time_point deadline_;
    std::stop_token stopToken_;
};

}  // namespace lumora::camera::sim
