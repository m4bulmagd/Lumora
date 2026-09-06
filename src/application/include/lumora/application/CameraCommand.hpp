#pragma once

#include <lumora/camera/CameraTypes.hpp>

#include <cstdint>
#include <variant>

namespace lumora::application {

struct Discover final {};
struct Connect final { camera::CameraId cameraId; };
struct Disconnect final {};
struct ApplyConfiguration final {
    std::uint64_t sessionGeneration;
    camera::CameraConfiguration configuration;
    std::uint64_t requestRevision;
};
struct ConfirmConfiguration final {
    std::uint64_t sessionGeneration;
    std::uint64_t appliedRequestRevision;
};
struct StartStream final {
    std::uint64_t sessionGeneration;
    std::uint64_t confirmedAppliedRequestRevision;
};
struct StopStream final {};
struct Retry final {};
struct Shutdown final {};

struct CameraCommand final {
    std::uint64_t requestId;
    std::variant<Discover, Connect, Disconnect, ApplyConfiguration,
        ConfirmConfiguration, StartStream, StopStream, Retry, Shutdown> payload;
};

}  // namespace lumora::application
