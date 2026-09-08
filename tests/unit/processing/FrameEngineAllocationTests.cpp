#include "AllocationTracker.hpp"
#include "FrameEngineTestAccess.hpp"
#include "FrameObjectPoolState.hpp"
#include "FrameObjectPoolTestSupport.hpp"
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <array>
#include <cstdio>
#include <new>

using namespace lumora;
namespace {
bool preparationFailuresReleaseResources() {
    namespace hooks = core::detail::testing;
    const auto before = hooks::poolCounters();
    auto plan = core::FrameObjectPool::plan({2,4,2,8}).value();
    test::beginAllocationTracking();
    auto pool = core::FrameObjectPool::create(plan);
    const auto allocations = test::endAllocationMeasurement().allocations;
    if (!pool.hasValue()) return false;
    pool.value().reset();
    // Includes each index allocation, State/facade allocation and their controls,
    // in addition to all four aligned slab allocations.
    for (std::size_t failure = 0; failure < allocations; ++failure) {
        test::failOneAllocationAfter(failure);
        auto failed = core::FrameObjectPool::create(plan);
        test::cancelAllocationFailure();
        if (failed.hasValue()) return false;
        const auto after = hooks::poolCounters();
        if (after.liveStates != before.liveStates || after.liveSlabs != before.liveSlabs) return false;
    }
    test::beginAllocationTracking();
    auto extraPlan = core::FrameObjectPool::plan({2,4,2,8});
    const auto planAllocations = test::endAllocationMeasurement().allocations;
    if (!extraPlan.hasValue()) return false;
    for (std::size_t failure = 0; failure < planAllocations; ++failure) {
        test::failOneAllocationAfter(failure);
        auto failed = core::FrameObjectPool::plan({2,4,2,8});
        test::cancelAllocationFailure();
        if (failed.hasValue()) return false;
        const auto after = hooks::poolCounters();
        if (after.liveStates != before.liveStates || after.liveSlabs != before.liveSlabs
            || after.liveProbeAllocations != before.liveProbeAllocations) return false;
    }
    std::printf("Preparation failure injection: %zu create allocations and %zu plan allocations cleaned up.\n",
        allocations, planAllocations);
    return true;
}
bool errorRenderingFailureDoesNotLeak() {
    const auto layout = core::ImageLayout::create(2U, 1U, 4U, core::StorageType::UInt16, 4U).value();
    const core::PipelineVersion version{1,1,1};
    for (std::size_t capacity : {1U, 2U}) {
        auto objects = core::FrameObjectPool::create({capacity,1,1,1}).value();
        auto pixels = core::BufferPool::create(2,4).value();
        auto firstLease = pixels->tryAcquire();
        auto first = core::ProcessedFrame::create(1, layout, std::move(*firstLease).seal(), version, {}, *objects).value();
        auto secondLease = pixels->tryAcquire();
        bool threw = false;
        test::failOneAllocationAfter(0);
        try {
            static_cast<void>(core::ProcessedFrame::create(2, layout,
                std::move(*secondLease).seal(), version, {}, *objects));
        } catch (const std::bad_alloc&) { threw = true; }
        test::cancelAllocationFailure();
        if (!threw || objects->stats().inUse.processedFrames != 1U
            || objects->stats().inUse.controlBlocks != 1U || pixels->stats().inUse != 1U) return false;
        first.reset();
        if (objects->stats().inUse.processedFrames != 0U || pixels->stats().inUse != 0U) return false;
    }
    std::fputs("Object/control exhaustion error-rendering allocation failures reclaimed every unpublished slot and pixel lease.\n", stdout);
    return true;
}
bool arenaExhaustionHasNoHeapFallback() {
    using namespace core::detail;
    auto state = std::make_shared<FrameObjectPoolState>();
    state->beginProbe();
    {
        std::shared_ptr<const core::ProcessedFrame> probe(nullptr,
            PooledFrameDeleter<core::ProcessedFrame>{state, 0}, FrameControlAllocator<std::byte>{state});
    }
    const auto shape = state->finishProbe();
    const auto stride = (shape.bytes + shape.alignment - 1U) / shape.alignment * shape.alignment;
    FramePoolLayout layout;
    layout.capacity = {1,1,1,1};
    layout.slabs.fill({1, stride, shape.alignment, stride});
    layout.shapes.fill(shape);
    state->prepare(layout, {});
    bool correct = true;
    test::beginAllocationTracking();
    for (std::size_t attempt = 0; attempt < 1000; ++attempt) {
        auto slot = state->acquireObject(FrameSlotKind::Processed);
        correct = correct && slot.has_value() && !state->acquireObject(FrameSlotKind::Processed).has_value();
        auto* control = state->allocateControl(shape);
        try { static_cast<void>(state->allocateControl(shape)); correct = false; }
        catch (const FramePoolAllocationFailure& failure) { correct = correct && failure.reason == FramePoolFailure::ControlExhausted; }
        try { static_cast<void>(state->allocateControl({2, shape.bytes * 2U, shape.alignment})); correct = false; }
        catch (const FramePoolAllocationFailure& failure) { correct = correct && failure.reason == FramePoolFailure::UnsupportedShape; }
        state->deallocateControl(control, shape);
        if (slot) state->releaseObject(FrameSlotKind::Processed, *slot);
    }
    const auto counts = test::endAllocationMeasurement();
    std::printf("Arena exhaustion/layout rollback: allocations=%zu deallocations=%zu over 1000 cycles (error strings excluded).\n",
        counts.allocations, counts.deallocations);
    return correct && counts.allocations == 0 && counts.deallocations == 0;
}
bool enginePreparationFailuresReleaseResources() {
    const auto layout=core::ImageLayout::create(4,1,8,core::StorageType::UInt16,8).value();
    auto p=core::BufferPool::create(9,8).value(); auto d=core::BufferPool::create(16,4).value();
    auto definition=processing::defaultPipeline(); definition.stages[3].enabled=true;
    const auto before=core::detail::testing::poolCounters();
    test::beginAllocationTracking();
    auto baseline=processing::FrameProcessingEngine::create(*p,*d,layout,definition);
    const auto allocations=test::endAllocationMeasurement().allocations;
    if(!baseline.hasValue()) return false;
    baseline.value().reset();
    for(std::size_t failure=0;failure<allocations;++failure) {
        test::failOneAllocationAfter(failure);
        auto failed=processing::FrameProcessingEngine::create(*p,*d,layout,definition);
        test::cancelAllocationFailure();
        if(failed.hasValue()) return false;
        const auto after=core::detail::testing::poolCounters();
        if(p->stats().inUse!=0 || d->stats().inUse!=0 || before.liveStates!=after.liveStates || before.liveSlabs!=after.liveSlabs || after.liveProbeAllocations!=0) return false;
    }
    std::printf("Complete engine preparation failure injection: %zu allocation sites reclaimed all unpublished resources.\n",allocations);
    return true;
}
class LatchFault final : public processing::detail::EngineHooks {
public:
    core::Result<void> before(processing::ProcessingOperation operation,std::span<std::byte>) override {
        if(operation==processing::ProcessingOperation::Invert) return core::Result<void>::failure(
            {core::ErrorCategory::Processing,"allocation_probe_latch","Enhancement paused.","Injected before measurement.",true});
        return core::Result<void>::success();
    }
};
bool completeSchedule(bool orientation,bool fallback) {
    const auto layout=core::ImageLayout::create(8,8,16,core::StorageType::UInt16,128).value();
    auto rawPool=core::BufferPool::create(1,128).value(); auto lease=rawPool->tryAcquire();
    for(auto& byte:lease->bytes()) byte=std::byte{0};
    const core::SourcePixelFormat format{"Mono16",0x01100007U,16,65535,core::SourcePacking::Unpacked,core::BitAlignment::LeastSignificant,core::StorageType::UInt16};
    auto settings=core::AcquisitionSettingsSnapshot::create({"Test","Numeric","1","virtual",{}},format,{0,0,8,8},30,30,{},{}).value();
    auto raw=core::RawFrame::create(1,layout,std::move(*lease).seal(),{{},{},{},{},std::move(settings)}).value();
    auto p=core::BufferPool::create(9,128).value(); auto d=core::BufferPool::create(16,64).value();
    auto definition=processing::defaultPipeline(); for(auto& stage:definition.stages) stage.enabled=true;
    definition.stages[4].parameters=processing::ClaheParameters{2,2};
    processing::ProcessingPreparationOptions options;
    if(orientation) options.orientation={true,false,core::Rotation::Degrees90};
    auto hooks=fallback ? std::make_shared<LatchFault>() : std::shared_ptr<LatchFault>{};
    auto made=processing::detail::FrameEngineTestAccess::create(*p,*d,layout,definition,options,hooks);
    if(!made.hasValue()) return false;
    auto& engine=*made.value();
    if(fallback) { if(engine.process(raw).hasValue() || engine.process(raw).hasValue()) return false; }
    auto sentinel=engine.process(raw); if(!sentinel.hasValue()) return false;
    core::LatestValueSlot<core::FrameBundle> latest;
    std::array<std::shared_ptr<const core::FrameBundle>,5> retained{};
    auto cycle=[&](std::size_t index) {
        retained[index%retained.size()].reset();
        auto output=engine.process(raw);
        if(!output.hasValue() || static_cast<bool>(output.value()->enhanced)==fallback) return false;
        auto published=latest.publish(output.value());
        retained[index%retained.size()]=std::move(output).value();
        return published.revision!=0;
    };
    for(std::size_t i=0;i<100;++i) if(!cycle(i)) return false;
    bool successful=true;
    test::beginAllocationTracking();
    for(std::size_t i=0;i<1000;++i) successful=cycle(i) && successful;
    for(auto& owner:retained) owner.reset();
    (void)latest.publish(sentinel.value());
    const auto measured=test::endAllocationMeasurement();
    std::printf("Complete prepared publication: orientation=%d fallback=%d success=%d allocations=%zu bytes=%zu deallocations=%zu frames=1000\n",
        orientation ? 1 : 0,fallback ? 1 : 0,successful ? 1 : 0,measured.allocations,measured.allocatedBytes,measured.deallocations);
    return successful && measured.allocations==0 && measured.deallocations==0;
}
}  // namespace
int main() {
    if (!preparationFailuresReleaseResources() || !arenaExhaustionHasNoHeapFallback()
        || !errorRenderingFailureDoesNotLeak()) return 4;
    test::beginAllocationTracking();
    auto* ordinary = ::operator new(7U);
    auto* aligned = ::operator new(19U, std::align_val_t{64U});
    ::operator delete(ordinary);
    ::operator delete(aligned, std::align_val_t{64U});
    const auto control = test::endAllocationMeasurement();
    if (control.allocations != 2U || control.allocatedBytes != 26U || control.deallocations != 2U) return 1;
    const auto layout = core::ImageLayout::create(4U, 1U, 8U, core::StorageType::UInt16, 8U).value();
    auto rawPool = core::BufferPool::create(1U, 8U).value();
    auto lease = rawPool->tryAcquire();
    for (auto& byte : lease->bytes()) byte = std::byte{0};
    const core::SourcePixelFormat format{"Mono16", 0x01100007U, 16U, 65535U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant, core::StorageType::UInt16};
    auto settings = core::AcquisitionSettingsSnapshot::create(
        {"Test", "Numeric", "1", "virtual", {}}, format, {0, 0, 4, 1}, 30, 30, {}, {}).value();
    auto raw = core::RawFrame::create(1U, layout, std::move(*lease).seal(),
        {{}, {}, {}, {}, std::move(settings)}).value();
    auto processingPool = core::BufferPool::create(9U, 8U).value();
    auto displayPool = core::BufferPool::create(16U, 4U).value();
    auto engine = processing::FrameProcessingEngine::create(*processingPool, *displayPool, layout).value();
    auto sentinel = engine->process(raw).value();
    core::LatestValueSlot<core::FrameBundle> latest;
    std::array<std::shared_ptr<const core::FrameBundle>, 5U> retained{};
    std::uint64_t revision = 0;
    auto cycle = [&](std::size_t index) {
        retained[index % retained.size()].reset();
        auto result = engine->process(raw);
        if (!result.hasValue()) return false;
        const auto published = latest.publish(std::move(result).value());
        if (published.revision == 0U) return false;
        // Alternate immediate consume and unconsumed replacement.
        if (index % 3U != 0U) {
            auto consumed = latest.consumeAfter(revision);
            if (!consumed) return false;
            revision = consumed->revision;
            retained[index % retained.size()] = std::move(consumed->value);
        }
        return true;
    };
    for (std::size_t frame = 0; frame < 100U; ++frame) if (!cycle(frame)) return 2;
    bool successful = true;
    test::beginAllocationTracking();
    for (std::size_t frame = 0; frame < 1000U; ++frame) successful = cycle(frame) && successful;
    for (auto& owner : retained) owner.reset();
    static_cast<void>(latest.publish(sentinel)); // Releases the final measured output before disarming.
    const auto counts = test::endAllocationMeasurement();
    std::printf("Engine publication/retention/release: success=%d allocations=%zu bytes=%zu deallocations=%zu over 1000 frames after 100 warmups\n",
        successful ? 1 : 0, counts.allocations, counts.allocatedBytes, counts.deallocations);
    if(!successful || counts.allocations!=0U) return 3;
    if(!enginePreparationFailuresReleaseResources()) return 6;
    for(bool oriented:{false,true}) for(bool fallback:{false,true}) if(!completeSchedule(oriented,fallback)) return 5;
    return 0;
}
