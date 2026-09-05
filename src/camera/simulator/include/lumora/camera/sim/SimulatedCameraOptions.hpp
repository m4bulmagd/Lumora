#pragma once

#include <lumora/camera/CameraTypes.hpp>
#include <lumora/camera/sim/SequenceSource.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace lumora::camera::sim {

class FaultScript;

enum class SimulationPattern {
    Ramp,
    Gradient,
    Checkerboard,
    ImpulseNoise,
    MovingBar,
};

enum class SimulationPacingMode {
    Fastest,
    RealTime,
    Manual,
};

struct SimulatedCameraOptions final {
    CameraId id;
    CameraCapabilities capabilities;
    SimulationPattern pattern;
    double defaultFps;
    std::uint64_t seed;
    SimulationPacingMode pacing{SimulationPacingMode::Fastest};
    std::function<void()> pacingSlipHook{};
    // Absent: preserve generated pattern behavior. Present: descriptor and
    // one-pixel ROI increments derive from files; numeric capabilities remain.
    std::optional<SequenceReplayOptions> sequence{};
    std::shared_ptr<FaultScript> faults{};
};

}  // namespace lumora::camera::sim
