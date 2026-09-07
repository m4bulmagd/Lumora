#pragma once
#include <lumora/camera/sim/SimulatedCameraOptions.hpp>
namespace lumora::app {
[[nodiscard]] camera::CameraConfiguration simulatorConfiguration();
[[nodiscard]] camera::sim::SimulatedCameraOptions simulatorOptions();
}
