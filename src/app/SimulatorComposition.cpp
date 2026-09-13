#include "SimulatorComposition.hpp"
namespace lumora::app {
camera::CameraConfiguration simulatorConfiguration() {
    return {{"Mono12",0x01100005U,12U,4095U,core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant,core::StorageType::UInt16},
        {0U,0U,640U,480U},30.0,{camera::ExposureMode::Manual,1000.0},
        {camera::GainMode::Manual,0.0},camera::AcquisitionMode::Continuous};
}
// These manual controls model register values/readback, not scene brightness
// or an automatic exposure/gain algorithm.
camera::sim::SimulatedCameraOptions simulatorOptions() {
    return {{"SIM-LIVE"},{{simulatorConfiguration().pixelFormat},
        {{0U,0U,1U,1U},{0U,0U,640U,480U},{1U,1U,1U,1U}},
        {1.0,60.0,1.0,camera::ControlAccess::WritableStopped},{1.0,10000.0,1.0,camera::ControlAccess::WritableStopped},{camera::ExposureMode::Manual},
        {0.0,10.0,1.0,camera::ControlAccess::WritableStopped},{camera::GainMode::Manual}},
        camera::sim::SimulationPattern::MovingBar,30.0,0x4C554D4FU,
        camera::sim::SimulationPacingMode::RealTime};
}
}
