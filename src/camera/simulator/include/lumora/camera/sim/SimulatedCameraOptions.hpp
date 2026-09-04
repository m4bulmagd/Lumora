#pragma once

#include <lumora/camera/CameraTypes.hpp>

#include <cstdint>
#include <functional>

namespace lumora::camera::sim {

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
};

}  // namespace lumora::camera::sim
