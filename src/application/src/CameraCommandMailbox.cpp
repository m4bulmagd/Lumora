#include <lumora/application/CameraCommandMailbox.hpp>

#include <limits>
#include <type_traits>
#include <utility>

namespace lumora::application {
static_assert(std::is_nothrow_move_constructible_v<CameraCommand>);
static_assert(std::is_nothrow_move_assignable_v<CameraCommand>);
namespace {
template<typename T>
bool is(const CameraCommand& command) noexcept {
    return std::holds_alternative<T>(command.payload);
}

int priority(const CameraCommand& command) noexcept {
    if (is<Shutdown>(command)) { return 3; }
    if (is<Disconnect>(command)) { return 2; }
    if (is<StopStream>(command)) { return 1; }
    return 0;
}

void increment(std::uint64_t& count) noexcept {
    if (count != std::numeric_limits<std::uint64_t>::max()) { ++count; }
}

core::Result<void> cancelled() {
    return core::Result<void>::failure({core::ErrorCategory::Cancelled,
        "cancelled", "Camera command was cancelled.", "", false});
}
}  // namespace

void CameraCommandMailbox::erase(std::size_t index) noexcept {
    for (std::size_t i = index + 1U; i < size_; ++i) {
        entries_[i - 1U] = std::move(entries_[i]);
    }
    entries_[--size_].reset();
}

core::Result<void> CameraCommandMailbox::post(CameraCommand command) {
    std::unique_lock lock(mutex_);
    if (closed_) { return cancelled(); }
    if (sealed_) {
        if (is<Shutdown>(command)) {
            increment(stats_.coalesced);
            return core::Result<void>::success();
        }
        return cancelled();
    }
    if (is<Shutdown>(command)) {
        while (size_ > 0U) {
            erase(size_ - 1U);
            increment(stats_.cancelled);
        }
        sealed_ = true;
    } else {
        bool stopFence = inFlightBarrier_.has_value();
        bool disconnectFence = inFlightBarrier_ && inFlightBarrier_->disconnect;
        for (std::size_t i = 0U; i < size_; ++i) {
            stopFence = stopFence || is<StopStream>(entries_[i]->command);
            disconnectFence = disconnectFence || is<Disconnect>(entries_[i]->command);
        }
        if ((stopFence && (is<StartStream>(command) || is<ConfirmConfiguration>(command)))
            || (disconnectFence && (is<Connect>(command) || is<Retry>(command)
                || is<ApplyConfiguration>(command) || is<ConfirmConfiguration>(command)
                || is<StartStream>(command)))) {
            return cancelled();
        }
        if (const auto* replacement = std::get_if<ApplyConfiguration>(&command.payload)) {
            for (std::size_t i = 0U; i < size_; ++i) {
                const auto* previous = std::get_if<ApplyConfiguration>(&entries_[i]->command.payload);
                if (!entries_[i]->coalescible || previous == nullptr
                    || previous->sessionGeneration != replacement->sessionGeneration) { continue; }
                const auto generation = previous->sessionGeneration;
                const auto revision = previous->requestRevision;
                entries_[i]->command = std::move(command);
                increment(stats_.coalesced);
                // Only this suffix can contain a confirmation for the replaced
                // Apply. Other generations and revisions remain independent.
                for (std::size_t j = i + 1U; j < size_;) {
                    const auto* confirmation = std::get_if<ConfirmConfiguration>(&entries_[j]->command.payload);
                    if (confirmation != nullptr && confirmation->sessionGeneration == generation
                        && confirmation->appliedRequestRevision == revision) {
                        erase(j);
                        increment(stats_.cancelled);
                    } else {
                        ++j;
                    }
                }
                return core::Result<void>::success();
            }
        }
        if (is<StopStream>(command) || is<Disconnect>(command)) {
            for (std::size_t i = 0U; i < size_;) {
                const auto& pending = entries_[i]->command;
                const bool superseded = is<Disconnect>(command)
                    ? !is<Discover>(pending) && !is<Disconnect>(pending)
                    : is<StartStream>(pending) || is<ConfirmConfiguration>(pending);
                if (superseded) {
                    erase(i);
                    increment(stats_.cancelled);
                } else {
                    ++i;
                }
            }
            for (std::size_t i = 0U; i < size_; ++i) {
                if (entries_[i]->command.payload.index() == command.payload.index()) {
                    increment(stats_.coalesced);
                    return core::Result<void>::success();
                }
            }
        }
    }
    if (size_ == entries_.size()) {
        if (priority(command) == 0) {
            return core::Result<void>::failure({core::ErrorCategory::ResourceExhaustion,
                "camera_mailbox_full", "Camera command mailbox is full.", "", true});
        }
        for (std::size_t i = 0U; i < size_; ++i) {
            if (priority(entries_[i]->command) == 0) {
                erase(i);
                increment(stats_.cancelled);
                break;
            }
        }
    }
    // A priority barrier may leave older Apply entries behind when it is
    // popped. Seal them on insertion so dequeue cannot erase the boundary.
    // A duplicate pending priority coalesces without adding another barrier.
    if (is<Connect>(command) || is<StartStream>(command)
        || is<StopStream>(command) || is<Disconnect>(command)) {
        for (std::size_t i = 0U; i < size_; ++i) { entries_[i]->coalescible = false; }
    }
    entries_[size_++].emplace(Entry{std::move(command)});
    lock.unlock();
    condition_.notify_all();
    return core::Result<void>::success();
}

std::optional<CameraCommand> CameraCommandMailbox::popLocked() {
    if (size_ == 0U || (inFlightBarrier_ && !sealed_)) { return std::nullopt; }
    std::size_t selected = 0U;
    for (std::size_t i = 1U; i < size_; ++i) {
        if (priority(entries_[i]->command) > priority(entries_[selected]->command)) {
            selected = i;
        }
    }
    auto command = std::move(entries_[selected]->command);
    erase(selected);
    if (is<StopStream>(command) || is<Disconnect>(command)) {
        inFlightBarrier_ = Barrier{command.requestId, is<Disconnect>(command)};
    }
    return command;
}

std::optional<CameraCommand> CameraCommandMailbox::tryPop() {
    std::lock_guard lock(mutex_);
    return popLocked();
}

std::optional<CameraCommand> CameraCommandMailbox::waitPop(std::stop_token stopToken) {
    std::unique_lock lock(mutex_);
    condition_.wait(lock, stopToken, [this] {
        return closed_ || sealed_ || (size_ > 0U && !inFlightBarrier_);
    });
    if (closed_ || stopToken.stop_requested()) { return std::nullopt; }
    return popLocked();
}

void CameraCommandMailbox::completeBarrier(std::uint64_t requestId) noexcept {
    {
        std::lock_guard lock(mutex_);
        if (inFlightBarrier_ && inFlightBarrier_->requestId == requestId) {
            inFlightBarrier_.reset();
        }
    }
    condition_.notify_all();
}

void CameraCommandMailbox::close() noexcept {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
        while (size_ > 0U) {
            erase(size_ - 1U);
            increment(stats_.cancelled);
        }
    }
    condition_.notify_all();
}

std::size_t CameraCommandMailbox::size() const noexcept {
    std::lock_guard lock(mutex_);
    return size_;
}

CameraMailboxStats CameraCommandMailbox::stats() const noexcept {
    std::lock_guard lock(mutex_);
    return stats_;
}

}  // namespace lumora::application
