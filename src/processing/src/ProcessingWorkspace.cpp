#include <lumora/processing/ProcessingWorkspace.hpp>

#include <lumora/core/CheckedMath.hpp>
#include <lumora/processing/IFrameProcessor.hpp>
#include <lumora/processing/OrientationTransform.hpp>

#include <utility>

namespace lumora::processing {
namespace {
core::Error resourceError(std::string code, std::string detail) {
    return {core::ErrorCategory::ResourceExhaustion, std::move(code),
        "The processing workspace could not acquire bounded storage.", std::move(detail), true};
}
core::Result<core::ImageLayout> tightLayout(const core::ImageLayout& source,
    core::StorageType storage, std::size_t sampleBytes) {
    auto stride = core::checkedMultiply(source.width(), sampleBytes);
    if (!stride.hasValue()) return core::Result<core::ImageLayout>::failure(stride.error());
    auto bytes = core::checkedMultiply(stride.value(), source.height());
    if (!bytes.hasValue()) return core::Result<core::ImageLayout>::failure(bytes.error());
    return core::ImageLayout::create(source.width(), source.height(), stride.value(), storage, bytes.value());
}
}  // namespace

ProcessingWorkspace::ProcessingWorkspace(core::BufferPool& processingPool, core::BufferPool& displayPool) noexcept
    : processingPool_(processingPool), displayPool_(displayPool) {}

core::Result<void> ProcessingWorkspace::prepare(const core::ImageLayout& sourceLayout,core::Orientation orientation,std::size_t orientationBytes) {
    using Result = core::Result<void>;
    auto canonical = tightLayout(sourceLayout, core::StorageType::UInt16, 2U);
    if (!canonical.hasValue()) return Result::failure(canonical.error());
    auto display = tightLayout(sourceLayout, core::StorageType::UInt8, 1U);
    if (!display.hasValue()) return Result::failure(display.error());
    if (processingPool_.stats().bytesPerBuffer < canonical.value().payloadBytes()) {
        return Result::failure(resourceError("processing_buffer_too_small", "U16 pool blocks do not fit the prepared image."));
    }
    if (displayPool_.stats().bytesPerBuffer < display.value().payloadBytes()) {
        return Result::failure(resourceError("display_buffer_too_small", "Gray8 pool blocks do not fit the prepared image."));
    }
    auto oriented = OrientationTransform::outputLayout(display.value(), core::DisplayStorage::Gray8, orientation);
    if (!oriented.hasValue()) return Result::failure(oriented.error());
    if (orientationBytes) orientationScratch_ = std::make_unique<std::byte[]>(orientationBytes);
    auto ready = replenish();
    if (!ready.hasValue()) return ready;
    sourceLayout_ = sourceLayout;
    canonicalLayout_ = canonical.value();
    nativeDisplayLayout_ = display.value();
    displayLayout_ = oriented.value();
    return Result::success();
}

core::Result<void> ProcessingWorkspace::replenish(bool enhanced) {
    using Result = core::Result<void>;
    // Keep acquisitions transactional: shortage in either pool releases every
    // newly acquired lease, while the previous private workspace stays intact.
    std::array<std::optional<core::WritableBufferLease>, 2> canonical;
    std::array<std::optional<core::WritableBufferLease>, 2> display;
    for (std::size_t index = 0; index < canonical.size(); ++index) {
        if (!canonical_[index]) {
            canonical[index] = processingPool_.tryAcquire();
            if (!canonical[index]) return Result::failure(resourceError(
                std::string(processingBufferPoolExhaustedCode), "No pooled canonical U16 buffer is available."));
        }
    }
    for (std::size_t index = 0; index < (enhanced ? display.size() : 1U); ++index) {
        if (!display_[index]) {
            display[index] = displayPool_.tryAcquire();
            if (!display[index]) return Result::failure(resourceError(
                std::string(displayBufferPoolExhaustedCode), "No pooled Gray8 display buffer is available."));
        }
    }
    for (std::size_t index = 0; index < canonical.size(); ++index) {
        if (canonical[index]) canonical_[index] = std::move(canonical[index]);
        if (display[index]) display_[index] = std::move(display[index]);
    }
    return Result::success();
}

}  // namespace lumora::processing
