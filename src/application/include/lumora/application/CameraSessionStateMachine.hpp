#pragma once

#include <lumora/application/ApplicationState.hpp>
#include <lumora/core/Result.hpp>

namespace lumora::application {

class CameraSessionStateMachine final {
public:
    [[nodiscard]] core::Result<void> apply(CameraSessionEvent event);
    [[nodiscard]] CameraSessionState state() const noexcept { return state_; }

private:
    CameraSessionState state_{CameraSessionState::Disconnected};
};

}  // namespace lumora::application
