#pragma once

#include <lumora/core/Frame.hpp>
#include <lumora/presentation/DisplayMode.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace lumora::presentation {

struct PresentationTicket {
    std::uint64_t sessionGeneration;
    std::uint64_t sourceFrameId;
    std::uint64_t presentationRevision;
    bool operator==(const PresentationTicket&) const noexcept = default;
};
struct PresentationSubmission {
    PresentationTicket ticket;
    std::shared_ptr<const core::FrameBundle> bundle;
    DisplayMode mode;
};
// Completion time is captured by the renderer, never by GUI event delivery.
struct PresentationReceipt {
    PresentationTicket ticket;
    std::chrono::steady_clock::time_point completedAt;
};
// Terminal for the exact admitted ticket; can also invalidate the last
// completed ticket. With previousImageRetained=false no image remains visible.
// Preserve receipt-before-invalidation ordering if both await GUI delivery.
struct PresentationFailure {
    PresentationTicket ticket;
    core::Error error;
    bool previousImageRetained;
};
struct PresentationRetired { std::uint64_t retirementId; };
using PresentationEvent = std::variant<PresentationReceipt,
    PresentationFailure, PresentationRetired>;

// Called only on the frontend thread. Implementations bound their event storage;
// events carry no bundle owners. Render-thread synchronization belongs to sinks.
class IPresentationSink {
public:
    virtual ~IPresentationSink() = default;
    // Effective surface readiness and bounded admission capacity. A false value
    // prevents source reads/recovery attempts; submit still checks independently.
    [[nodiscard]] virtual bool ready() const = 0;
    virtual core::Result<void> submit(PresentationSubmission) = 0;
    // True proves this exact ticket was never consumed, cannot complete, and
    // its owners are released. False requires a terminal event or retirement.
    virtual bool cancelPending(PresentationTicket) = 0;
    // Retired proves ALL prior CPU/render/upload owners released; it supersedes
    // old terminal events and must also work without a visible surface or swap.
    virtual void retire(std::uint64_t retirementId) = 0;
    virtual std::optional<PresentationEvent> takeEvent() = 0;
};

// Generation/revision must be nonzero; source frame zero is valid. Immutable
// core factories enforce plane identity, dimensions, buffers and orientation.
[[nodiscard]] core::Result<void> validatePresentation(const PresentationSubmission&);

}  // namespace lumora::presentation
