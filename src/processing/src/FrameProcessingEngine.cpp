#include <lumora/processing/FrameProcessingEngine.hpp>

#include <lumora/processing/DisplayMapper.hpp>
#include <lumora/processing/NormalizeStage.hpp>
#include <lumora/processing/WindowLevelStage.hpp>

#include <chrono>
#include <cmath>
#include <utility>

namespace lumora::processing {
namespace {
using BundleResult = core::Result<std::shared_ptr<const core::FrameBundle>>;
using Clock = std::chrono::steady_clock;
core::Error processingError(std::string code, std::string detail) {
    return {core::ErrorCategory::Processing, std::move(code),
        "The frame could not be processed.", std::move(detail), false};
}
core::SharedBuffer seal(std::optional<core::WritableBufferLease>& lease) {
    auto buffer = std::move(*lease).seal();
    lease.reset();
    return buffer;
}
}  // namespace

FrameProcessingEngine::FrameProcessingEngine(core::BufferPool& processingPool, core::BufferPool& displayPool)
    : workspace_(processingPool, displayPool) {}

core::Result<std::unique_ptr<FrameProcessingEngine>> FrameProcessingEngine::create(
    core::BufferPool& processingPool, core::BufferPool& displayPool,
    const core::ImageLayout& sourceLayout, const PipelineDefinition& definition) {
    using Result = core::Result<std::unique_ptr<FrameProcessingEngine>>;
    auto engine = std::unique_ptr<FrameProcessingEngine>(new FrameProcessingEngine(processingPool, displayPool));
    auto active = engine->activate(definition);
    if (!active.hasValue()) {
        std::string detail;
        for (const auto& violation : active.error().violations) {
            if (!detail.empty()) detail += " ";
            detail += violation.detail;
        }
        return Result::failure(processingError(active.error().code, std::move(detail)));
    }
    auto prepared = engine->workspace_.prepare(sourceLayout);
    if (!prepared.hasValue()) return Result::failure(prepared.error());
    return Result::success(std::move(engine));
}

core::Result<void, PipelineValidationError> FrameProcessingEngine::activate(const PipelineDefinition& definition) {
    return pipeline_.activate(definition);
}

BundleResult FrameProcessingEngine::process(std::shared_ptr<const core::RawFrame> raw) {
    const auto started = Clock::now();
    const auto pipeline = pipeline_.snapshot();
    const auto& planned = *workspace_.sourceLayout_;
    if (!raw || raw->layout.width() != planned.width() || raw->layout.height() != planned.height()
        || raw->layout.storage() != planned.storage()) {
        return BundleResult::failure(processingError("processing_source_layout_mismatch",
            "A source dimension or storage change requires stopped-state preparation."));
    }
    const auto& acquisition = raw->metadata.acquisitionSettings;
    if (!std::isfinite(acquisition.actualFps) || acquisition.actualFps <= 0.0) {
        return BundleResult::failure(processingError("processing_metadata_invalid",
            "Actual acquisition FPS must be positive and finite before presentation."));
    }
    auto source = ImageView::create(raw->layout, raw->pixels.bytes(), ImageDomain::SensorNative);
    if (!source.hasValue()) return BundleResult::failure(source.error());
    auto ready = workspace_.replenish();
    if (!ready.hasValue()) return BundleResult::failure(ready.error());
    const auto& canonicalLayout = *workspace_.canonicalLayout_;
    const auto& displayLayout = *workspace_.displayLayout_;
    core::ProcessingTimings timings;
    timings.stages.reserve(4);
    core::DisplayMapping originalMapping{};
    core::DisplayMapping enhancedMapping{};
    std::size_t enhancedIndex = 0;
    {
        // prepare established layout/pool bounds; replenishment provides distinct,
        // exclusive leases. All borrowed views leave scope before sealing.
        auto normalized = MutableImageView::create(canonicalLayout, workspace_.canonical_[0]->bytes(),
            ImageDomain::CanonicalU16).value();
        auto windowed = MutableImageView::create(canonicalLayout, workspace_.canonical_[1]->bytes(),
            ImageDomain::CanonicalU16).value();
        auto timed = [&](std::string id, auto&& operation) {
            const auto before = Clock::now();
            auto result = operation();
            timings.stages.push_back({std::move(id), std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before)});
            return result;
        };
        auto result = timed("normalize", [&] { return NormalizeStage{}.process(source.value(), normalized, acquisition.sourceFormat); });
        if (!result.hasValue()) return BundleResult::failure(result.error());

        WindowLevelParameters parameters;
        bool enhancedWindow = false;
        for (const auto& stage : pipeline->definition().stages) {
            if (stage.id == StageId::WindowLevel) {
                parameters = std::get<WindowLevelParameters>(stage.parameters);
                enhancedWindow = stage.enabled;
                break;
            }
        }
        // Original requires WL even when disabled for Enhanced. When enabled, both
        // routes share this immutable result. The timing ID states which work ran;
        // it is not a list of enabled Enhanced stages.
        result = timed(enhancedWindow ? "shared_window_level" : "original_window_level", [&] {
            return WindowLevelStage{parameters}.process(normalized.asConst(), windowed, acquisition.sourceFormat);
        });
        if (!result.hasValue()) return BundleResult::failure(result.error());
        const auto revision = pipeline->definition().version.configurationRevision;
        const DisplayMapper mapper;
        auto originalMapped = timed("original_display_map", [&] {
            return mapper.map(windowed.asConst(), displayLayout, core::DisplayStorage::Gray8,
                workspace_.display_[0]->bytes(), revision);
        });
        if (!originalMapped.hasValue()) return BundleResult::failure(originalMapped.error());
        originalMapping = originalMapped.value();
        enhancedIndex = enhancedWindow ? 1U : 0U;
        auto enhancedMapped = timed("enhanced_display_map", [&] {
            return mapper.map(enhancedWindow ? windowed.asConst() : normalized.asConst(), displayLayout,
                core::DisplayStorage::Gray8, workspace_.display_[1]->bytes(), revision);
        });
        if (!enhancedMapped.hasValue()) return BundleResult::failure(enhancedMapped.error());
        enhancedMapping = enhancedMapped.value();
    }

    timings.total = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);
    auto enhanced = core::ProcessedFrame::create(raw->frameId, canonicalLayout,
        seal(workspace_.canonical_[enhancedIndex]), pipeline->definition().version, std::move(timings));
    if (!enhanced.hasValue()) return BundleResult::failure(enhanced.error());
    const core::Orientation orientation{false, false, core::Rotation::Degrees0};
    auto original = core::DisplayFrame::create(raw->frameId, displayLayout, seal(workspace_.display_[0]),
        core::DisplayStorage::Gray8, originalMapping, orientation);
    if (!original.hasValue()) return BundleResult::failure(original.error());
    auto enhancedDisplay = core::DisplayFrame::create(raw->frameId, displayLayout, seal(workspace_.display_[1]),
        core::DisplayStorage::Gray8, enhancedMapping, orientation);
    if (!enhancedDisplay.hasValue()) return BundleResult::failure(enhancedDisplay.error());
    return core::FrameBundle::create(std::move(raw), std::move(original).value(),
        std::move(enhanced).value(), std::move(enhancedDisplay).value());
}

}  // namespace lumora::processing
