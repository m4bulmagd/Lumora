#include <lumora/camera/sim/FaultScript.hpp>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace lumora::camera::sim {
namespace {

bool validFault(SimulatedFault fault) noexcept {
    switch (fault) {
    case SimulatedFault::Timeout:
    case SimulatedFault::Disconnect:
    case SimulatedFault::MalformedFrame:
    case SimulatedFault::ConfigurationFailure:
        return true;
    }
    return false;
}

core::Error allocationError() {
    return {core::ErrorCategory::ResourceExhaustion, "fault_script_allocation_failed",
            "The evaluation fault script could not be created.",
            "Allocating fault script state failed.", true};
}

}  // namespace

core::Result<std::shared_ptr<FaultScript>> FaultScript::create(std::vector<FaultEvent> events) {
    using ScriptResult = core::Result<std::shared_ptr<FaultScript>>;
    for (const auto& event : events) {
        if (!validFault(event.fault) || event.repeatCount == 0U ||
            ((event.atFrame > 0U) == event.atElapsed.has_value()) ||
            (event.atElapsed && *event.atElapsed < std::chrono::milliseconds::zero())) {
            return ScriptResult::failure({core::ErrorCategory::CameraConfiguration,
                "invalid_fault_script", "The evaluation fault script is invalid.",
                "Require a known fault, positive repeat count, and exactly one nonnegative time or positive frame trigger.", true});
        }
    }
    try {
        std::stable_sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
            if (left.atElapsed.has_value() != right.atElapsed.has_value()) {
                return !left.atElapsed.has_value();
            }
            return left.atElapsed ? *left.atElapsed < *right.atElapsed : left.atFrame < right.atFrame;
        });
        return ScriptResult::success(std::shared_ptr<FaultScript>{new FaultScript{std::move(events)}});
    } catch (const std::bad_alloc&) {
        return ScriptResult::failure(allocationError());
    } catch (const std::length_error&) {
        return ScriptResult::failure(allocationError());
    }
}

FaultScript::FaultScript(std::vector<FaultEvent> events) : events_(std::move(events)) {}

std::optional<FaultScript::Occurrence> FaultScript::match(
    std::uint64_t nextFrameId,
    std::optional<std::chrono::milliseconds> elapsed,
    FaultPoint point) const {
    std::scoped_lock lock(mutex_);
    for (std::size_t index = 0U; index < events_.size(); ++index) {
        const auto& event = events_[index];
        const bool configuration = event.fault == SimulatedFault::ConfigurationFailure;
        const bool due = event.atElapsed ? elapsed && *elapsed >= *event.atElapsed : event.atFrame == nextFrameId;
        if (event.repeatCount > 0U && due && configuration == (point == FaultPoint::Configuration)) {
            return Occurrence{index, event.fault};
        }
    }
    return std::nullopt;
}

void FaultScript::consume(Occurrence occurrence) {
    std::scoped_lock lock(mutex_);
    if (occurrence.index >= events_.size()) {
        return;
    }
    auto& event = events_[occurrence.index];
    if (event.fault == occurrence.fault && event.repeatCount > 0U) {
        --event.repeatCount;
        if (event.fault == SimulatedFault::Disconnect) {
            disconnected_ = true;
        }
    }
}

bool FaultScript::disconnected() const {
    std::scoped_lock lock(mutex_);
    return disconnected_;
}

void FaultScript::restoreConnection() {
    std::scoped_lock lock(mutex_);
    disconnected_ = false;
}

}  // namespace lumora::camera::sim
