#include "SimulatorComposition.hpp"
namespace lumora::app {
camera::CameraConfiguration simulatorConfiguration() {
    return {{"Mono8",0x01080001U,8U,255U,core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant,core::StorageType::UInt8},
        {0U,0U,640U,480U},30.0,{camera::ExposureMode::Manual,1000.0},
        {camera::GainMode::Manual,0.0},camera::AcquisitionMode::Continuous};
}
camera::sim::SimulatedCameraOptions simulatorOptions() {
    return {{"SIM-LIVE"},{{simulatorConfiguration().pixelFormat},
        {{0U,0U,1U,1U},{0U,0U,640U,480U},{1U,1U,1U,1U}},
        {1.0,60.0,1.0,false},{1.0,10000.0,1.0,false},{camera::ExposureMode::Manual},
        {0.0,10.0,1.0,false},{camera::GainMode::Manual}},
        camera::sim::SimulationPattern::MovingBar,30.0,0x4C554D4FU,
        camera::sim::SimulationPacingMode::RealTime};
}
}
