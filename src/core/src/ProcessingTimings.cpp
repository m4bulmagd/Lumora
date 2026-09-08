#include <lumora/core/ProcessingTimings.hpp>

#include <cassert>
#include <cstring>

namespace lumora::core {
std::string_view OwnedStageId::view() const noexcept { return {bytes_.data(), size_}; }
bool OwnedStageId::empty() const noexcept { return size_ == 0; }
bool OwnedStageId::operator==(std::string_view other) const noexcept { return view() == other; }

Result<void, TimingAppendError> StageTimings::append(
    std::string_view stageId, std::chrono::nanoseconds elapsed) noexcept {
    using Result = core::Result<void, TimingAppendError>;
    if (size_ == capacity) return Result::failure(TimingAppendError::CapacityExceeded);
    if (stageId.empty()) return Result::failure(TimingAppendError::EmptyStageId);
    if (stageId.size() > OwnedStageId::maximumLength) return Result::failure(TimingAppendError::StageIdTooLong);
    if (elapsed < std::chrono::nanoseconds::zero()) return Result::failure(TimingAppendError::NegativeElapsed);
    auto& entry = entries_[size_];
    std::memcpy(entry.stageId.bytes_.data(), stageId.data(), stageId.size());
    entry.stageId.bytes_[stageId.size()] = '\0';
    entry.stageId.size_ = static_cast<std::uint8_t>(stageId.size());
    entry.elapsed = elapsed;
    ++size_;
    return Result::success();
}
std::size_t StageTimings::size() const noexcept { return size_; }
bool StageTimings::empty() const noexcept { return size_ == 0; }
const StageTiming& StageTimings::operator[](std::size_t index) const noexcept {
    assert(index < size_);
    return entries_[index];
}
const StageTiming* StageTimings::begin() const noexcept { return entries_.data(); }
const StageTiming* StageTimings::end() const noexcept { return begin() + size_; }
}  // namespace lumora::core
