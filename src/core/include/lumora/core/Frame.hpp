#pragma once

#include <lumora/core/FrameMetadata.hpp>
#include <lumora/core/ImageLayout.hpp>
#include <lumora/core/Result.hpp>
#include <lumora/core/SharedBuffer.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lumora::core {

enum class Rotation {
    Degrees0,
    Degrees90,
    Degrees180,
    Degrees270,
};

struct Orientation final {
    bool flipHorizontal;
    bool flipVertical;
    Rotation rotation;

    [[nodiscard]] bool operator==(const Orientation&) const noexcept = default;
};

struct DisplayMapping final {
    std::uint16_t inputMinimum;
    std::uint16_t inputMaximum;
    std::uint16_t outputMaximum;
    std::uint64_t configurationRevision;

    [[nodiscard]] bool operator==(const DisplayMapping&) const noexcept = default;
};

struct PipelineVersion final {
    std::uint32_t schemaVersion;
    std::uint32_t orderVersion;
    std::uint64_t configurationRevision;

    [[nodiscard]] bool operator==(const PipelineVersion&) const noexcept = default;
};

struct StageTiming final {
    std::string stageId;
    std::chrono::nanoseconds elapsed;
};

struct ProcessingTimings final {
    std::vector<StageTiming> stages;
    std::chrono::nanoseconds total;
};

struct RawFrame final {
    const std::uint64_t frameId;
    const ImageLayout layout;
    const SharedBuffer pixels;
    const FrameMetadata metadata;

    [[nodiscard]] static Result<std::shared_ptr<const RawFrame>> create(
        std::uint64_t frameId,
        ImageLayout layout,
        SharedBuffer pixels,
        FrameMetadata metadata);

private:
    RawFrame(
        std::uint64_t frameId,
        ImageLayout layout,
        SharedBuffer pixels,
        FrameMetadata metadata);
};

struct ProcessedFrame final {
    const std::uint64_t frameId;
    const ImageLayout layout;
    const SharedBuffer pixels;
    const PipelineVersion pipelineVersion;
    const ProcessingTimings timings;

    [[nodiscard]] static Result<std::shared_ptr<const ProcessedFrame>> create(
        std::uint64_t frameId,
        ImageLayout layout,
        SharedBuffer pixels,
        PipelineVersion pipelineVersion,
        ProcessingTimings timings);

private:
    ProcessedFrame(
        std::uint64_t frameId,
        ImageLayout layout,
        SharedBuffer pixels,
        PipelineVersion pipelineVersion,
        ProcessingTimings timings);
};

struct DisplayFrame final {
    const std::uint64_t frameId;
    const ImageLayout layout;
    const SharedBuffer pixels;
    const DisplayStorage storage;
    const DisplayMapping mapping;
    const Orientation presentationOrientation;

    [[nodiscard]] static Result<std::shared_ptr<const DisplayFrame>> create(
        std::uint64_t frameId,
        ImageLayout layout,
        SharedBuffer pixels,
        DisplayStorage storage,
        DisplayMapping mapping,
        Orientation presentationOrientation);

private:
    DisplayFrame(
        std::uint64_t frameId,
        ImageLayout layout,
        SharedBuffer pixels,
        DisplayStorage storage,
        DisplayMapping mapping,
        Orientation presentationOrientation);
};

struct FrameBundle final {
    const std::shared_ptr<const RawFrame> raw;
    const std::shared_ptr<const DisplayFrame> originalDisplay;
    const std::shared_ptr<const ProcessedFrame> processed;
    const std::shared_ptr<const DisplayFrame> enhancedDisplay;

    [[nodiscard]] static Result<std::shared_ptr<const FrameBundle>> create(
        std::shared_ptr<const RawFrame> raw,
        std::shared_ptr<const DisplayFrame> originalDisplay,
        std::shared_ptr<const ProcessedFrame> processed,
        std::shared_ptr<const DisplayFrame> enhancedDisplay);

private:
    FrameBundle(
        std::shared_ptr<const RawFrame> raw,
        std::shared_ptr<const DisplayFrame> originalDisplay,
        std::shared_ptr<const ProcessedFrame> processed,
        std::shared_ptr<const DisplayFrame> enhancedDisplay);
};

}  // namespace lumora::core
