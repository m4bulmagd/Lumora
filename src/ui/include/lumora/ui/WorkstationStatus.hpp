#pragma once

#include <chrono>
#include <optional>

namespace lumora::ui {

enum class ViewerState {
    Live,
    Paused,
};

enum class FrameFreshness {
    WaitingForFrame,
    Current,
    Stale,
};

struct WorkstationStatus final {
    ViewerState viewerState{ViewerState::Live};
    FrameFreshness freshness{FrameFreshness::WaitingForFrame};
    std::optional<std::chrono::system_clock::time_point> frameUtc;
    std::chrono::milliseconds frameAge{0};
};

}  // namespace lumora::ui
