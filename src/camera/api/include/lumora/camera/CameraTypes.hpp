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

struct NumericCapability final {
    double minimum;
    double maximum;
    double increment;
    bool writableWhileStreaming;
};

struct RegionOfInterestCapability final {
    core::RegionOfInterest minimum;
    core::RegionOfInterest maximum;
    core::RegionOfInterest increment;
};

enum class ExposureMode {
    Manual,
    Auto,
};

struct ExposureConfiguration final {
    ExposureMode mode;
    std::optional<double> requestedMicroseconds;
};

enum class GainMode {
    Manual,
    Auto,
};

struct GainConfiguration final {
    GainMode mode;
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
