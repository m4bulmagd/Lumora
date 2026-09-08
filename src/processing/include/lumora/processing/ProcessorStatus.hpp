#pragma once
#include <lumora/core/Error.hpp>
#include <cstdint>
#include <memory>
#include <optional>
namespace lumora::processing {
enum class ProcessingOperation {
    Normalize, SharedWindowLevel, OriginalWindowLevel, BrightnessContrast,
    Gamma, Clahe, Denoise, Sharpen, Invert, OriginalDisplayMap,
    OriginalOrientation, EnhancedDisplayMap, EnhancedOrientation
};
enum class ProcessorMode { Enhanced, OriginalOnlyLatched };
// One coherent value; copying retains a diagnostic owner without copying strings.
// Diagnostic payloads are error-path storage outside prepared-resource admission.
struct ProcessorStatus final {
    ProcessorMode mode{ProcessorMode::Enhanced};
    std::uint64_t activationSerial{};
    std::uint64_t configurationRevision{};
    std::uint8_t consecutiveEnhancementFailures{};
    std::uint64_t enhancementFailures{};
    std::optional<ProcessingOperation> failingOperation;
    std::shared_ptr<const core::Error> error;
    bool retrySupported{false};
    bool retryPending{false};
    std::uint64_t retriesConsumed{};
};
}
