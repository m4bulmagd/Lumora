#pragma once

#include <lumora/presentation/WorkstationState.hpp>

namespace lumora::presentation {

// Local installation editor lifetime; command and durable profile authority stay
// with WorkstationCoordinator and the installation profile service.
class InstallationSettingsDraft final {
public:
    struct SaveRequest {
        std::uint64_t sessionGeneration;
        camera::CameraId cameraId;
        core::Orientation orientation;
        bool confirmed;
        bool repairInvalid;
    };

    void update(const WorkstationState&);
    [[nodiscard]] const WorkstationState& presentation() const noexcept { return presentation_; }
    [[nodiscard]] const std::shared_ptr<const application::CameraStatusSnapshot>& source() const noexcept {
        return source_;
    }
    [[nodiscard]] const std::optional<core::CameraIdentity>& identity() const noexcept { return identity_; }
    [[nodiscard]] core::Orientation orientation() const noexcept { return orientation_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool invalidated() const noexcept { return invalidated_; }
    [[nodiscard]] bool confirmationChecked() const noexcept { return confirmed_; }
    [[nodiscard]] bool repairChecked() const noexcept { return repair_; }
    [[nodiscard]] bool editable() const;
    [[nodiscard]] bool pending() const;
    [[nodiscard]] bool repairVisible() const;
    [[nodiscard]] bool saveEnabled() const;
    bool setOrientation(core::Orientation);
    bool setConfirmation(bool);
    bool setRepairConsent(bool);
    [[nodiscard]] std::optional<SaveRequest> prepareSave();
    // A synchronous coordinator rejection may publish no outcome. It still
    // consumes confirmation and must release this editor's duplicate-save latch.
    void rejectSubmission();

private:
    WorkstationState presentation_;
    std::shared_ptr<const application::CameraStatusSnapshot> source_;
    std::optional<core::CameraIdentity> identity_;
    core::Orientation orientation_{false, false, core::Rotation::Degrees0};
    bool initialized_{};
    bool invalidated_{};
    bool confirmed_{};
    bool repair_{};
    bool localPending_{};
    std::optional<std::uint64_t> outcomeAtSubmission_;
};

}  // namespace lumora::presentation
