#pragma once

#include <lumora/core/Result.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lumora::core {

enum class TimingAppendError { CapacityExceeded, EmptyStageId, StageIdTooLong, NegativeElapsed };

class OwnedStageId final {
public:
    static constexpr std::size_t maximumLength = 63;
    [[nodiscard]] std::string_view view() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool operator==(std::string_view other) const noexcept;
private:
    friend class StageTimings;
    std::array<char, maximumLength + 1> bytes_{};
    std::uint8_t size_{0};
};

struct StageTiming final {
    OwnedStageId stageId;
    std::chrono::nanoseconds elapsed{};
};

class StageTimings final {
public:
    static constexpr std::size_t capacity = 16;
    [[nodiscard]] Result<void, TimingAppendError> append(
        std::string_view stageId, std::chrono::nanoseconds elapsed) noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const StageTiming& operator[](std::size_t index) const noexcept;
    [[nodiscard]] const StageTiming* begin() const noexcept;
    [[nodiscard]] const StageTiming* end() const noexcept;
private:
    std::array<StageTiming, capacity> entries_{};
    std::size_t size_{0};
};

struct ProcessingTimings final {
    StageTimings stages;
    std::chrono::nanoseconds total{};
};

}  // namespace lumora::core
