#pragma once

#include <lumora/presentation/WorkstationState.hpp>

namespace lumora::presentation {

struct CameraActionAvailability final {
    bool visible{false};
    bool enabled{false};
};

// Shared availability and admission facts. Priority cancellation may be enabled
// while its button is hidden (for example Stop during an idle pending Resume).
struct CameraActionPolicy final {
    bool sourceMatches{false};
    bool selectedAvailable{false};
    bool connectedIdle{false};
    bool streaming{false};
    bool applied{false};
    bool confirmed{false};
    bool installationPending{false};
    bool selectionEnabled{false};
    CameraActionAvailability settings;
    CameraActionAvailability installation;
    CameraActionAvailability refresh;
    CameraActionAvailability connect;
    CameraActionAvailability apply;
    CameraActionAvailability confirm;
    CameraActionAvailability start;
    CameraActionAvailability stop;
    CameraActionAvailability disconnect;
    CameraActionAvailability retry;
    CameraActionAvailability resumeLive;

    [[nodiscard]] static CameraActionPolicy evaluate(const WorkstationState& state);
    [[nodiscard]] bool allows(CameraStartupIntent intent) const noexcept;
};

}  // namespace lumora::presentation
