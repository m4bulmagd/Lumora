#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/FrameObjectPool.hpp>
#include "FrameObjectPoolState.hpp"
#include "FrameObjectPoolTestSupport.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <limits>
#include <thread>
#include <vector>

namespace lumora::core {
namespace {
using namespace std::chrono_literals;
namespace hooks = detail::testing;
const PipelineVersion version{1, 1, 7};
const DisplayMapping mapping{0, 65535, 255, 7};
const Orientation orientation{false, false, Rotation::Degrees0};
ImageLayout layout(StorageType storage, std::uint32_t width = 2) {
    const std::size_t stride = width * (storage == StorageType::UInt16 ? 2U : 1U);
    return ImageLayout::create(width, 1, stride, storage, stride).value();
}
struct Pixels {
    std::shared_ptr<BufferPool> pool = BufferPool::create(64, 8).value();
    SharedBuffer take(std::byte fill = std::byte{0x33}) {
        auto lease = pool->tryAcquire();
        EXPECT_TRUE(lease.has_value());
        if (!lease) return {};
        std::ranges::fill(lease->bytes(), fill);
        return std::move(*lease).seal();
    }
    std::shared_ptr<const RawFrame> raw() {
        const SourcePixelFormat format{"Mono8", 0x01080001U, 8, 255,
            SourcePacking::Unpacked, BitAlignment::LeastSignificant, StorageType::UInt8};
        auto settings = AcquisitionSettingsSnapshot::create(
            {"Test", "Model", "1", "virtual", {}}, format, {0, 0, 2, 1}, 30, 30, {}, {}).value();
        return RawFrame::create(42, layout(StorageType::UInt8), take(std::byte{0x11}),
            {{}, {}, {}, {}, std::move(settings)}).value();
    }
    Result<std::shared_ptr<const ProcessedFrame>> processed(FrameObjectPool& poolObjects, std::uint64_t id = 42) {
        ProcessingTimings timings;
        EXPECT_TRUE(timings.stages.append("normalize", 1ns).hasValue());
        return ProcessedFrame::create(id, layout(StorageType::UInt16), take(), version, timings, poolObjects);
    }
    Result<std::shared_ptr<const DisplayFrame>> display(FrameObjectPool& poolObjects, std::uint64_t id = 42) {
        return DisplayFrame::create(id, layout(StorageType::UInt8), take(std::byte{0x55}),
            DisplayStorage::Gray8, mapping, orientation, poolObjects);
    }
    Result<std::shared_ptr<const FrameBundle>> bundle(FrameObjectPool& poolObjects,
        const std::shared_ptr<const RawFrame>& rawFrame) {
        auto processedFrame = processed(poolObjects);
        if (!processedFrame.hasValue()) return Result<std::shared_ptr<const FrameBundle>>::failure(processedFrame.error());
        auto original = display(poolObjects);
        if (!original.hasValue()) return Result<std::shared_ptr<const FrameBundle>>::failure(original.error());
        auto enhanced = display(poolObjects);
        if (!enhanced.hasValue()) return Result<std::shared_ptr<const FrameBundle>>::failure(enhanced.error());
        return FrameBundle::create(rawFrame, std::move(original).value(),
            std::move(processedFrame).value(), std::move(enhanced).value(), poolObjects);
    }
};
void expectEmpty(const FrameObjectPool& pool) {
    const auto usage = pool.stats().inUse;
    EXPECT_EQ(usage.processedFrames, 0U); EXPECT_EQ(usage.displayFrames, 0U);
    EXPECT_EQ(usage.bundles, 0U); EXPECT_EQ(usage.controlBlocks, 0U);
}
template<typename T> detail::ControlShape controlShape(const std::shared_ptr<detail::FrameObjectPoolState>& state) {
    state->beginProbe();
    {
        std::shared_ptr<const T> owner(nullptr, detail::PooledFrameDeleter<T>{state, 0},
            detail::FrameControlAllocator<std::byte>{state});
        std::weak_ptr<const T> weak = owner;
        owner.reset();
        EXPECT_TRUE(weak.expired());
    }
    return state->finishProbe();
}

TEST(FrameObjectPool, PlanIsBoundedReusableAndAccountsExactRequestedStorage) {
    const auto before = hooks::poolCounters();
    const FrameObjectPoolCapacity capacity{9, 16, 9, 34};
    auto plan = FrameObjectPool::plan(capacity).value();
    const auto planned = hooks::poolCounters();
    EXPECT_EQ(planned.probes - before.probes, 3U);
    EXPECT_EQ(planned.slabAllocations, before.slabAllocations);
    EXPECT_EQ(planned.liveStates, before.liveStates);
    EXPECT_EQ(planned.liveProbeAllocations, before.liveProbeAllocations);
    const auto probeState = std::make_shared<detail::FrameObjectPoolState>();
    const std::array shapes{controlShape<ProcessedFrame>(probeState), controlShape<DisplayFrame>(probeState), controlShape<FrameBundle>(probeState)};
    std::size_t controlBytes = 0, alignment = 1;
    for (const auto shape : shapes) {
        EXPECT_EQ(shape.count, 1U);
        EXPECT_GT(shape.bytes, 0U);
        EXPECT_EQ(shape.alignment & (shape.alignment - 1U), 0U);
        controlBytes = std::max(controlBytes, shape.bytes); alignment = std::max(alignment, shape.alignment);
    }
    const auto stride = (controlBytes + alignment - 1U) / alignment * alignment;
    const auto slots = 9U * sizeof(ProcessedFrame) + 16U * sizeof(DisplayFrame)
        + 9U * sizeof(FrameBundle) + 34U * stride;
    EXPECT_EQ(plan.slotStorageBytes(), slots);
    // Impl contains exactly the immutable layout, and State retains its owner.
    EXPECT_EQ(plan.requiredStorageBytes(), slots + 68U * sizeof(std::size_t)
        + sizeof(detail::FramePoolLayout) + sizeof(detail::FrameObjectPoolState) + sizeof(FrameObjectPool));
    std::printf("Frame pool: processed=%zu display=%zu bundle=%zu control stride=%zu alignment=%zu slots=%zu requested=%zu\n",
        sizeof(ProcessedFrame), sizeof(DisplayFrame), sizeof(FrameBundle), stride, alignment,
        plan.slotStorageBytes(), plan.requiredStorageBytes());
    const auto beforeCreate = hooks::poolCounters();
    auto first = FrameObjectPool::create(plan).value();
    auto copied = plan;
    auto second = FrameObjectPool::create(copied).value();
    EXPECT_EQ(hooks::poolCounters().probes, beforeCreate.probes);
    EXPECT_EQ(hooks::poolCounters().slabAllocations - beforeCreate.slabAllocations, 8U);
    EXPECT_EQ(first->stats().slotStorageBytes, plan.slotStorageBytes());
    EXPECT_EQ(first->stats().capacity.controlBlocks, 34U);
    EXPECT_EQ(first->stats().capacity.processedFrames, 9U);
    EXPECT_EQ(first->stats().capacity.displayFrames, 16U);
    EXPECT_EQ(first->stats().capacity.bundles, 9U);
    EXPECT_EQ(first->stats().acquisitionFailures, 0U);
    EXPECT_EQ(first->stats().highWaterMark.controlBlocks, 0U);
    Pixels pixels;
    auto owner = pixels.processed(*first).value();
    EXPECT_EQ(first->stats().inUse.processedFrames, 1U);
    expectEmpty(*second);
    // A caller may reject admission after plan without constructing any slab.
    const auto rejected = hooks::poolCounters();
    { auto unadmitted = FrameObjectPool::plan({1000, 2000, 1000, 4000}).value();
      EXPECT_GT(unadmitted.requiredStorageBytes(), plan.requiredStorageBytes()); }
    EXPECT_EQ(hooks::poolCounters().slabAllocations, rejected.slabAllocations);
}
TEST(FrameObjectPool, RejectsImpossibleCapacityBeforeProbesAndCleansPreparationFailures) {
    const auto before = hooks::poolCounters();
    for (const auto capacity : {FrameObjectPoolCapacity{0,1,1,1}, {1,0,1,1}, {1,1,0,1}, {1,1,1,0},
            {std::numeric_limits<std::size_t>::max(),1,1,1}, {1,1,1,std::numeric_limits<std::size_t>::max()}})
        EXPECT_FALSE(FrameObjectPool::plan(capacity).hasValue());
    EXPECT_EQ(hooks::poolCounters().probes, before.probes);
    EXPECT_EQ(hooks::poolCounters().slabAllocations, before.slabAllocations);
    hooks::rejectControlShape(true);
    auto unsupported = FrameObjectPool::plan({1,1,1,1});
    hooks::resetPoolFailures();
    ASSERT_FALSE(unsupported.hasValue());
    EXPECT_EQ(unsupported.error().code, "frame_control_allocator_shape_unsupported");
    EXPECT_EQ(hooks::poolCounters().liveProbeAllocations, before.liveProbeAllocations);
    EXPECT_EQ(hooks::poolCounters().liveStates, before.liveStates);
    auto plan = FrameObjectPool::plan({2,4,2,8}).value();
    for (std::size_t failure = 0; failure < 4; ++failure) {
        hooks::failSlabAllocationAfter(failure);
        auto result = FrameObjectPool::create(plan);
        hooks::resetPoolFailures();
        EXPECT_FALSE(result.hasValue());
        EXPECT_EQ(hooks::poolCounters().liveSlabs, before.liveSlabs);
        EXPECT_EQ(hooks::poolCounters().liveStates, before.liveStates);
    }
    EXPECT_TRUE(FrameObjectPool::create(plan).hasValue());
}
TEST(FrameObjectPool, ReadyAllocatorEnforcesAdmittedShapeAndAlignmentWithoutFallback) {
    const auto state = std::make_shared<detail::FrameObjectPoolState>();
    const auto shape = controlShape<ProcessedFrame>(state);
    detail::FramePoolLayout layoutPlan;
    layoutPlan.capacity = {1,1,1,3};
    const auto stride = (shape.bytes + shape.alignment - 1U) / shape.alignment * shape.alignment;
    layoutPlan.shapes.fill(shape);
    layoutPlan.slabs.fill({1, stride, shape.alignment, stride});
    layoutPlan.slabs[3] = {3, stride, shape.alignment, 3 * stride};
    state->prepare(layoutPlan, {});
    std::array<void*, 3> pointers{};
    for (auto& pointer : pointers) {
        pointer = state->allocateControl(shape);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % shape.alignment, 0U);
    }
    EXPECT_THROW(static_cast<void>(state->allocateControl(shape)), detail::FramePoolAllocationFailure);
    for (const auto invalid : {detail::ControlShape{2, shape.bytes * 2U, shape.alignment},
            {1, shape.bytes + 1U, shape.alignment}, {1, shape.bytes, shape.alignment * 2U}}) {
        try { static_cast<void>(state->allocateControl(invalid)); FAIL(); }
        catch (const detail::FramePoolAllocationFailure& failure) {
            EXPECT_EQ(failure.reason, detail::FramePoolFailure::UnsupportedShape);
        }
    }
    for (auto* pointer : pointers) state->deallocateControl(pointer, shape);
    EXPECT_EQ(state->stats().inUse.controlBlocks, 0U);
}
TEST(FrameObjectPool, RealBundlesAndDetachedChildrenSurviveFacadeAndPlanAcrossThreads) {
    const auto before = hooks::poolCounters();
    Pixels pixels;
    auto raw = pixels.raw();
    std::array<std::shared_ptr<const FrameBundle>, 3> bundles;
    std::shared_ptr<const ProcessedFrame> child;
    std::weak_ptr<const FrameBundle> weak;
    {
        const auto plan = FrameObjectPool::plan({3,6,3,12}).value();
        auto objects = FrameObjectPool::create(plan).value();
        for (auto& bundle : bundles) bundle = pixels.bundle(*objects, raw).value();
        child = bundles[0]->enhanced;
        weak = bundles[1];
        EXPECT_EQ(objects->stats().inUse.controlBlocks, 12U);
        EXPECT_EQ(objects->stats().highWaterMark.displayFrames, 6U);
        for (const auto& bundle : bundles) {
            EXPECT_EQ(bundle->raw, raw);
            EXPECT_EQ(bundle->raw.use_count(), 4);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(bundle.get()) % alignof(FrameBundle), 0U);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(bundle->enhanced.get()) % alignof(ProcessedFrame), 0U);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(bundle->originalDisplay.get()) % alignof(DisplayFrame), 0U);
        }
    }
    for (const auto& bundle : bundles) {
        EXPECT_EQ(bundle->enhanced->pixels.bytes()[0], std::byte{0x33});
        EXPECT_EQ(bundle->originalDisplay->pixels.bytes()[0], std::byte{0x55});
        EXPECT_EQ(bundle->enhanced->timings.stages[0].stageId, "normalize");
    }
    std::jthread release([owners = std::move(bundles)]() mutable { for (auto& owner : owners) owner.reset(); });
    release.join();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(pixels.pool->stats().inUse, 2U); // Raw and separately retained ProcessedFrame.
    EXPECT_EQ(child->pixels.bytes()[0], std::byte{0x33});
    child.reset();
    EXPECT_EQ(pixels.pool->stats().inUse, 1U);
    EXPECT_EQ(hooks::poolCounters().liveStates, before.liveStates + 1U);
    std::jthread releaseWeak([owner = std::move(weak)]() mutable { owner.reset(); });
    releaseWeak.join();
    EXPECT_EQ(hooks::poolCounters().liveStates, before.liveStates);
    EXPECT_EQ(hooks::poolCounters().liveSlabs, before.liveSlabs);
    raw.reset();
    EXPECT_EQ(pixels.pool->stats().inUse, 0U);
}
TEST(FrameObjectPool, WeakProcessedAndDisplayOwnersNeverReviveAtReusedAddresses) {
    Pixels pixels;
    auto objects = FrameObjectPool::create({1,1,1,2}).value();
    auto first = pixels.processed(*objects, 1).value();
    const auto* address = first.get();
    std::weak_ptr<const ProcessedFrame> weak = first;
    first.reset();
    EXPECT_EQ(objects->stats().inUse.processedFrames, 0U);
    EXPECT_EQ(objects->stats().inUse.controlBlocks, 1U);
    auto second = pixels.processed(*objects, 2).value();
    EXPECT_EQ(second.get(), address);
    EXPECT_EQ(second->sourceFrameId, 2U);
    EXPECT_FALSE(weak.lock());
    std::weak_ptr<const ProcessedFrame> secondWeak = second;
    second.reset();
    auto exhausted = pixels.processed(*objects);
    ASSERT_FALSE(exhausted.hasValue());
    EXPECT_EQ(exhausted.error().code, "frame_control_pool_exhausted");
    EXPECT_EQ(objects->stats().inUse.processedFrames, 0U);
    EXPECT_EQ(pixels.pool->stats().inUse, 0U);
    weak.reset();
    EXPECT_TRUE(pixels.processed(*objects).hasValue());
    secondWeak.reset();
    auto display = pixels.display(*objects, 3).value();
    const auto* displayAddress = display.get();
    std::weak_ptr<const DisplayFrame> weakDisplay = display;
    display.reset();
    auto displayAgain = pixels.display(*objects, 4).value();
    EXPECT_EQ(displayAgain.get(), displayAddress);
    EXPECT_EQ(displayAgain->sourceFrameId, 4U);
    EXPECT_FALSE(weakDisplay.lock());
    displayAgain.reset(); weakDisplay.reset();
    expectEmpty(*objects);
}
TEST(FrameObjectPool, WeakBundleReuseKeepsExpiredIdentityAndReleasesNestedOwners) {
    Pixels pixels;
    auto objects = FrameObjectPool::create({1,2,1,5}).value();
    auto raw = pixels.raw();
    auto first = pixels.bundle(*objects, raw).value();
    const auto* address = first.get();
    std::weak_ptr<const FrameBundle> weak = first;
    first.reset();
    EXPECT_EQ(objects->stats().inUse.controlBlocks, 1U);
    EXPECT_EQ(pixels.pool->stats().inUse, 1U);
    auto second = pixels.bundle(*objects, raw).value();
    EXPECT_EQ(second.get(), address);
    EXPECT_FALSE(weak.lock());
    EXPECT_EQ(second->raw, raw);
    second.reset(); weak.reset();
    expectEmpty(*objects);
}
TEST(FrameObjectPool, IndependentObjectShortagesAndFinalControlShortageRollBackExactly) {
    Pixels pixels;
    auto raw = pixels.raw();
    for (const auto capacity : {FrameObjectPoolCapacity{1,4,2,8}, {2,2,2,8}, {2,3,2,8}, {2,4,1,8}, {2,4,2,7}}) {
        auto objects = FrameObjectPool::create(capacity).value();
        auto published = pixels.bundle(*objects, raw).value();
        for (std::size_t retry = 0; retry < 20; ++retry) {
            const auto result = pixels.bundle(*objects, raw);
            ASSERT_FALSE(result.hasValue());
            EXPECT_EQ(result.error().category, ErrorCategory::ResourceExhaustion);
            EXPECT_EQ(result.error().code, capacity.controlBlocks == 7 ? "frame_control_pool_exhausted" : "frame_object_pool_exhausted");
            EXPECT_EQ(objects->stats().inUse.processedFrames, 1U);
            EXPECT_EQ(objects->stats().inUse.displayFrames, 2U);
            EXPECT_EQ(objects->stats().inUse.bundles, 1U);
            EXPECT_EQ(objects->stats().inUse.controlBlocks, 4U);
            EXPECT_EQ(pixels.pool->stats().inUse, 4U);
            EXPECT_EQ(published->enhanced->pixels.bytes()[0], std::byte{0x33});
            EXPECT_EQ(published->raw, raw);
        }
        EXPECT_EQ(objects->stats().acquisitionFailures, 20U);
        published.reset();
        expectEmpty(*objects);
        auto recovered = pixels.bundle(*objects, raw);
        ASSERT_TRUE(recovered.hasValue());
        recovered.value().reset();
        expectEmpty(*objects);
    }
}
TEST(FrameObjectPool, ControlFailureAtEveryTransactionPositionReturnsAllUnpublishedLeases) {
    Pixels pixels;
    auto raw = pixels.raw();
    for (std::size_t controls = 1; controls < 4; ++controls) {
        auto objects = FrameObjectPool::create({1,2,1,controls}).value();
        for (std::size_t retry = 0; retry < 20; ++retry) {
            auto result = pixels.bundle(*objects, raw);
            ASSERT_FALSE(result.hasValue());
            EXPECT_EQ(result.error().code, "frame_control_pool_exhausted");
            expectEmpty(*objects);
            EXPECT_EQ(pixels.pool->stats().inUse, 1U);
        }
    }
}
TEST(FrameObjectPool, PooledAndNonpooledValidationMatchBeforeAnySlotAcquisition) {
    Pixels pixels;
    auto objects = FrameObjectPool::create({1,1,1,1}).value();
    const auto buffer = pixels.take();
    for (std::size_t invalid = 0; invalid < 5; ++invalid) {
        auto imageLayout = layout(invalid == 0 ? StorageType::UInt8 : StorageType::UInt16);
        auto imagePixels = invalid == 1 ? SharedBuffer{} : buffer;
        auto pipelineVersion = version;
        if (invalid == 2) pipelineVersion.schemaVersion = 0;
        if (invalid == 3) pipelineVersion.orderVersion = 0;
        ProcessingTimings timings;
        if (invalid == 4) timings.total = -1ns;
        auto normal = ProcessedFrame::create(42, imageLayout, imagePixels, pipelineVersion, timings);
        auto pooled = ProcessedFrame::create(42, imageLayout, imagePixels, pipelineVersion, timings, *objects);
        ASSERT_FALSE(normal.hasValue()); ASSERT_FALSE(pooled.hasValue());
        EXPECT_EQ(normal.error().code, pooled.error().code);
        EXPECT_EQ(normal.error().category, pooled.error().category);
        expectEmpty(*objects);
    }
    for (std::size_t invalid = 0; invalid < 7; ++invalid) {
        auto displayStorage = invalid == 0 ? static_cast<DisplayStorage>(999) : DisplayStorage::Gray8;
        auto imageLayout = layout(invalid == 1 ? StorageType::UInt16 : StorageType::UInt8);
        auto displayMapping = mapping;
        if (invalid == 2) displayMapping.inputMaximum = 0;
        if (invalid == 3) displayMapping.outputMaximum = 0;
        if (invalid == 4) displayMapping.outputMaximum = 256;
        auto displayOrientation = orientation;
        if (invalid == 5) displayOrientation.rotation = static_cast<Rotation>(999);
        auto imagePixels = invalid == 6 ? SharedBuffer{} : buffer;
        auto normal = DisplayFrame::create(42, imageLayout, imagePixels, displayStorage, displayMapping, displayOrientation);
        auto pooled = DisplayFrame::create(42, imageLayout, imagePixels, displayStorage, displayMapping, displayOrientation, *objects);
        ASSERT_FALSE(normal.hasValue()); ASSERT_FALSE(pooled.hasValue());
        EXPECT_EQ(normal.error().code, pooled.error().code);
        expectEmpty(*objects);
    }
    EXPECT_EQ(objects->stats().acquisitionFailures, 0U);
    // Even an exhausted pool reports validation failures first.
    auto held = pixels.processed(*objects).value();
    auto invalid = ProcessedFrame::create(42, layout(StorageType::UInt16), {}, version, {}, *objects);
    EXPECT_EQ(invalid.error().code, "frame_buffer_missing");
    EXPECT_EQ(objects->stats().acquisitionFailures, 0U);
}
TEST(FrameObjectPool, BundleValidationParityCoversIdsDimensionsMappingsAndRequiredOwners) {
    Pixels pixels;
    auto objects = FrameObjectPool::create({1,2,1,4}).value();
    const auto raw = pixels.raw();
    const auto pixelBuffer = pixels.take();
    for (std::size_t invalid = 0; invalid < 12; ++invalid) {
        auto rawOwner = invalid == 0 ? std::shared_ptr<const RawFrame>{} : raw;
        auto displayOrientation = orientation;
        auto enhancedOrientation = orientation;
        if (invalid == 7) enhancedOrientation.flipHorizontal = true;
        auto displayMapping = mapping;
        auto enhancedMapping = mapping;
        if (invalid == 8) ++enhancedMapping.configurationRevision;
        auto processedVersion = version;
        if (invalid == 9) ++processedVersion.configurationRevision;
        auto original = DisplayFrame::create(invalid == 3 ? 43 : 42,
            layout(StorageType::UInt8, invalid == 4 ? 1 : 2), pixelBuffer,
            DisplayStorage::Gray8, displayMapping, displayOrientation).value();
        auto processed = ProcessedFrame::create(invalid == 10 ? 43 : 42,
            layout(StorageType::UInt16, invalid == 5 ? 1 : 2), pixelBuffer, processedVersion, {}).value();
        auto enhanced = DisplayFrame::create(invalid == 11 ? 43 : 42,
            layout(StorageType::UInt8, invalid == 6 ? 1 : 2), pixelBuffer,
            DisplayStorage::Gray8, enhancedMapping, enhancedOrientation).value();
        if (invalid == 1) original.reset();
        if (invalid == 2) enhanced.reset();
        auto normal = FrameBundle::create(rawOwner, original, processed, enhanced);
        auto pooled = FrameBundle::create(rawOwner, original, processed, enhanced, *objects);
        ASSERT_FALSE(normal.hasValue()); ASSERT_FALSE(pooled.hasValue());
        EXPECT_EQ(normal.error().code, pooled.error().code);
        EXPECT_EQ(normal.error().category, pooled.error().category);
        expectEmpty(*objects);
    }
    auto original = pixels.display(*objects).value();
    auto normal = FrameBundle::create(raw, original, nullptr, nullptr).value();
    auto pooled = FrameBundle::create(raw, original, nullptr, nullptr, *objects).value();
    EXPECT_EQ(normal->raw, pooled->raw);
    EXPECT_EQ(pooled->raw, raw);
    EXPECT_EQ(normal->originalDisplay, pooled->originalDisplay);
    EXPECT_EQ(objects->stats().acquisitionFailures, 0U);
}
TEST(FrameObjectPool, ValidGray16RotationAndTimingNamesArePreservedByBothFactories) {
    Pixels pixels;
    auto objects = FrameObjectPool::create({1,1,1,2}).value();
    auto buffer = pixels.take();
    const DisplayMapping gray16{0,65535,65535,7};
    const Orientation rotated{true,true,Rotation::Degrees270};
    auto normal = DisplayFrame::create(42, layout(StorageType::UInt16), buffer,
        DisplayStorage::Gray16, gray16, rotated).value();
    auto pooled = DisplayFrame::create(42, layout(StorageType::UInt16), buffer,
        DisplayStorage::Gray16, gray16, rotated, *objects).value();
    EXPECT_EQ(normal->mapping, pooled->mapping);
    EXPECT_EQ(normal->presentationOrientation, pooled->presentationOrientation);
    EXPECT_EQ(normal->storage, pooled->storage);
    ProcessingTimings timings;
    {
        std::string name = "a dynamically allocated published stage identifier";
        ASSERT_TRUE(timings.stages.append(name, 10ns).hasValue());
        name.assign(100, 'x');
    }
    auto frame = ProcessedFrame::create(42, layout(StorageType::UInt16), buffer, version, timings, *objects).value();
    auto allocating = ProcessedFrame::create(42, layout(StorageType::UInt16), buffer, version, timings).value();
    EXPECT_EQ(frame->timings.stages[0].stageId.view(), "a dynamically allocated published stage identifier");
    EXPECT_EQ(frame->timings.stages[0].stageId.view(), allocating->timings.stages[0].stageId.view());
    EXPECT_EQ(frame->pipelineVersion, allocating->pipelineVersion);
    // A real short buffer is rejected identically; no invented invalid span.
    auto shortPixels = BufferPool::create(1,1).value();
    auto shortLease = shortPixels->tryAcquire();
    auto shortBuffer = std::move(*shortLease).seal();
    auto invalidNormal = ProcessedFrame::create(42, layout(StorageType::UInt16), shortBuffer, version, {});
    auto invalidPooled = ProcessedFrame::create(42, layout(StorageType::UInt16), shortBuffer, version, {}, *objects);
    EXPECT_EQ(invalidNormal.error().code, "frame_buffer_too_small");
    EXPECT_EQ(invalidNormal.error().code, invalidPooled.error().code);
    EXPECT_EQ(objects->stats().acquisitionFailures, 0U);
}
}  // namespace
}  // namespace lumora::core
