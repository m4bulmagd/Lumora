#pragma once

#include <lumora/core/PixelFormat.hpp>
#include <lumora/core/Result.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace lumora::core {

struct CameraIdentity final {
    std::string manufacturer;
    std::string model;
    std::string serial;
    std::string transport;
    std::optional<std::string> firmware;
};

struct RegionOfInterest final {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
};

struct AcquisitionSettingsSnapshot final {
    CameraIdentity camera;
    SourcePixelFormat sourceFormat;
    RegionOfInterest roi;
    double requestedFps;
    double actualFps;
    std::optional<double> exposureMicroseconds;
    std::optional<double> gainDb;

    [[nodiscard]] static Result<AcquisitionSettingsSnapshot> create(
        CameraIdentity camera,
        SourcePixelFormat sourceFormat,
        RegionOfInterest roi,
        double requestedFps,
        double actualFps,
        std::optional<double> exposureMicroseconds,
        std::optional<double> gainDb);
};

struct FrameMetadata final {
    std::optional<std::uint64_t> cameraFrameId;
    std::chrono::steady_clock::time_point hostReceiptTime;
    std::chrono::system_clock::time_point acquisitionUtcTime;
    std::optional<std::uint64_t> deviceTimestamp;
    AcquisitionSettingsSnapshot acquisitionSettings;
};

}  // namespace lumora::core
