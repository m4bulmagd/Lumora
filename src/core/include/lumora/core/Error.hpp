#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace lumora::core {

enum class ErrorCategory {
    CameraDiscovery,
    CameraConnection,
    CameraConfiguration,
    Acquisition,
    InvalidFrame,
    Processing,
    Configuration,
    Encoding,
    Storage,
    ResourceExhaustion,
    Cancelled,
    Internal,
};

struct Error final {
    ErrorCategory category;
    std::string code;
    std::string operatorSummary;
    std::string diagnosticDetail;
    bool recoverable;
    std::optional<std::int64_t> nativeCode{};
};

}  // namespace lumora::core
