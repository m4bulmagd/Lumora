#pragma once

#include <lumora/core/Frame.hpp>

#include <chrono>
#include <optional>

namespace lumora::presentation {

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
    std::optional<core::Orientation> presentationOrientation{std::nullopt};
};

}  // namespace lumora::presentation
