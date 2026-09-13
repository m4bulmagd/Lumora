#pragma once

#include <lumora/core/FrameMetadata.hpp>
#include <lumora/core/PixelFormat.hpp>

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lumora::camera {

struct CameraId final {
    std::string value;

    auto operator<=>(const CameraId&) const = default;
};

struct CameraDescriptor final {
    CameraId id;
    core::CameraIdentity identity;
    bool available;
};

enum class ControlAccess { Unavailable, ReadOnly, WritableStopped, WritableStreaming };

struct NumericCapability final {
    double minimum;
    double maximum;
    double increment;
    ControlAccess access{ControlAccess::WritableStopped};
};

struct RegionOfInterestCapability final {
    core::RegionOfInterest minimum;
    core::RegionOfInterest maximum;
    core::RegionOfInterest increment;
    ControlAccess access{ControlAccess::WritableStopped};
};

enum class ExposureMode {
    Manual,
    Auto,
};

struct ExposureConfiguration final {
    std::optional<ExposureMode> mode;
    std::optional<double> requestedMicroseconds;
};

enum class GainMode {
    Manual,
    Auto,
};

struct GainConfiguration final {
    std::optional<GainMode> mode;
    std::optional<double> requestedDb;
};

enum class AcquisitionMode {
    Continuous,
    Triggered,
};

struct CameraCapabilities final {
    std::vector<core::SourcePixelFormat> pixelFormats;
    RegionOfInterestCapability roi;
    NumericCapability frameRate;
    NumericCapability exposure;
    std::vector<ExposureMode> exposureModes;
    NumericCapability gain;
    std::vector<GainMode> gainModes;
    ControlAccess pixelFormatAccess{ControlAccess::WritableStopped};
    ControlAccess exposureModeAccess{ControlAccess::WritableStopped};
    ControlAccess gainModeAccess{ControlAccess::WritableStopped};
};

struct CameraConfiguration final {
    core::SourcePixelFormat pixelFormat;
    core::RegionOfInterest roi;
    std::optional<double> requestedFps;
    ExposureConfiguration exposure;
    GainConfiguration gain;
    AcquisitionMode acquisitionMode;
};

struct AppliedCameraConfiguration final {
    CameraConfiguration requested;
    CameraConfiguration actual;
};

}  // namespace lumora::camera
