#include <lumora/presentation/PresentationProtocol.hpp>

#include <cmath>

namespace lumora::presentation {
namespace {
core::Result<void> invalid(const char* code, const char* detail) {
    return core::Result<void>::failure({core::ErrorCategory::InvalidFrame,
        code, "The image cannot be presented.", detail, true});
}
}

core::Result<void> validatePresentation(const PresentationSubmission& submission) {
    if (!submission.bundle) {
        return invalid("presentation_bundle_missing", "A presentation requires an immutable bundle.");
    }
    if (submission.ticket.sessionGeneration == 0 ||
        submission.ticket.presentationRevision == 0 ||
        submission.ticket.sourceFrameId != submission.bundle->sourceFrameId()) {
        return invalid("presentation_ticket_invalid", "Ticket generation, revision or source identity is invalid.");
    }
    switch (submission.mode) {
    case DisplayMode::Original:
        break;
    case DisplayMode::Enhanced:
    case DisplayMode::Compare:
        if (!submission.bundle->enhancedDisplay) {
            return invalid("presentation_enhanced_missing", "This mode requires an enhanced display plane.");
        }
        break;
    default:
        return invalid("presentation_mode_invalid", "The requested display mode is unknown.");
    }
    // FrameBundle and DisplayFrame are immutable and constructible only through
    // core validation. Reuse those guarantees instead of allocating another
    // bundle or duplicating their identity/buffer/dimension/orientation checks.
    const auto& bundle = *submission.bundle;
    if (bundle.originalDisplay->storage != core::DisplayStorage::Gray8 ||
        (bundle.enhancedDisplay && bundle.enhancedDisplay->storage != core::DisplayStorage::Gray8)) {
        return invalid("presentation_storage_unsupported", "Both display planes must use Gray8 storage.");
    }
    const auto fps = bundle.raw->metadata.acquisitionSettings.actualFps;
    if (!std::isfinite(fps) || fps <= 0) {
        return invalid("presentation_fps_invalid", "Actual frame rate must be finite and positive.");
    }
    return core::Result<void>::success();
}

}  // namespace lumora::presentation
