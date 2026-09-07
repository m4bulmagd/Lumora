#pragma once

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace lumora::processing {

enum class StageId {
    Normalize,
    WindowLevel,
    BrightnessContrast,
    Gamma,
    Clahe,
    Denoise,
    Sharpen,
    Invert,
};

enum class ImageDomain { SensorNative, CanonicalU16 };
enum class DenoiseMode { Gaussian, Median };
enum class ExecutionBackend { Cpu };

struct NormalizationParameters final {};

struct WindowLevelParameters final {
    double window{65535.0};
    double level{32767.5};
};

struct BrightnessContrastParameters final {
    double brightness{0.0};
    double contrast{1.0};
};

struct GammaParameters final {
    double gamma{1.0};
};

struct ClaheParameters final {
    double clipLimit{2.0};
    std::uint32_t tileGridSize{8};
};

struct DenoiseParameters final {
    DenoiseMode mode{DenoiseMode::Gaussian};
    std::uint32_t kernelSize{3};
};

struct SharpenParameters final {
    double amount{1.0};
    double radius{1.0};
    double threshold{0.0};
};

struct InvertParameters final {};

using StageParameters = std::variant<
    NormalizationParameters,
    WindowLevelParameters,
    BrightnessContrastParameters,
    GammaParameters,
    ClaheParameters,
    DenoiseParameters,
    SharpenParameters,
    InvertParameters>;

struct StageDefinition final {
    StageId id{StageId::Normalize};
    bool enabled{true};
    StageParameters parameters{NormalizationParameters{}};
};

struct PipelineDefinition final {
    std::uint32_t schemaVersion{1};
    std::uint32_t orderVersion{1};
    std::uint64_t configurationRevision{0};
    std::vector<StageDefinition> stages;
};

// Owned metadata only. Availability of algorithms is checked by the executor.
struct StageTraits final {
    StageId id;
    ImageDomain inputDomain;
    ImageDomain outputDomain;
    bool changesDimensions{false};
    std::uint32_t scratchImages{0};
    std::uint32_t historyFrames{0};
    bool requiresCalibrationAsset{false};
    ExecutionBackend backend{ExecutionBackend::Cpu};
};

// Static storage; diagnostic names never borrow configuration memory.
[[nodiscard]] std::string_view stageName(StageId id) noexcept;
[[nodiscard]] PipelineDefinition defaultPipeline();
[[nodiscard]] std::vector<StageTraits> stageRegistry();

}  // namespace lumora::processing
