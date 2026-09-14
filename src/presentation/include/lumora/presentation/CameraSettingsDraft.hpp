#pragma once

#include <lumora/presentation/WorkstationState.hpp>

namespace lumora::presentation {

// Source-bound editor state only. The coordinator owns command admission and persistence.
class CameraSettingsDraft final {
public:
    struct SourceToken {
        std::uint64_t sessionGeneration;
        camera::CameraId cameraId;
    };
    struct ApplyRequest {
        SourceToken source;
        camera::CameraConfiguration requested;
    };

    void update(const WorkstationState&);
    [[nodiscard]] const WorkstationState& presentation() const noexcept { return presentation_; }
    [[nodiscard]] const std::shared_ptr<const application::CameraStatusSnapshot>& source() const noexcept {
        return source_;
    }
    [[nodiscard]] const std::optional<camera::CameraConfiguration>& configuration() const noexcept {
        return draft_;
    }
    [[nodiscard]] std::optional<camera::CameraConfiguration> readback() const;
    [[nodiscard]] bool editable() const;
    [[nodiscard]] bool invalidated() const noexcept { return invalidated_; }
    [[nodiscard]] bool rebindCompleted() const noexcept { return rebindCompleted_; }
    [[nodiscard]] bool normalizationError() const noexcept { return normalizationFailed_; }
    [[nodiscard]] std::optional<SourceToken> beginEditing();
    // Retains even invalid numeric candidates; prepareApply validates the complete request.
    bool setConfiguration(camera::CameraConfiguration);
    [[nodiscard]] bool canApply() const;
    [[nodiscard]] std::optional<ApplyRequest> prepareApply();

private:
    void initialize();
    WorkstationState presentation_;
    std::shared_ptr<const application::CameraStatusSnapshot> source_;
    std::optional<camera::CameraConfiguration> draft_;
    std::optional<camera::CameraConfiguration> observedRequest_;
    std::optional<camera::CameraConfiguration> submitted_;
    std::uint64_t observedRevision_{};
    bool initialized_{};
    bool normalizationFailed_{};
    bool editingIntentReported_{};
    bool submissionAdmitted_{};
    bool invalidated_{};
    bool rebindCompleted_{};
    std::optional<std::uint64_t> completedGeneration_;
};

}  // namespace lumora::presentation
