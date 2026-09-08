#include "AllocationTracker.hpp"
#include "FrameEngineTestAccess.hpp"
#include "FrameObjectPoolState.hpp"
#include "FrameObjectPoolTestSupport.hpp"
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <array>
#include <cstdio>
#include <new>
#include <cstring>
#include <exception>
#include <string>
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
#include <windows.h>
#endif

using namespace lumora;
namespace {
void progress(const char* phase) {
    std::puts(phase);
    std::fflush(stdout);
}
void injectionProgress(const char* phase, std::size_t index, const char* state) {
    std::printf("%s failure index=%zu %s\n", phase, index, state);
    std::fflush(stdout);
}
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
    std::printf("FrameObjectPool create allocation sites=%zu\n", allocations);
    std::fflush(stdout);
    for (std::size_t failure = 0; failure < allocations; ++failure) {
        injectionProgress("FrameObjectPool create", failure, "start");
        test::failOneAllocationAfter(failure);
        {
        auto failed = core::FrameObjectPool::create(plan);
        test::cancelAllocationFailure();
        progress("Injected call returned; checking cleanup");
        if (failed.hasValue()) return false;
        const auto after = hooks::poolCounters();
        if (after.liveStates != before.liveStates || after.liveSlabs != before.liveSlabs) return false;
        } // Destroy the failed Result before reporting cleanup complete.
        injectionProgress("FrameObjectPool create", failure, "complete");
    }
    test::beginAllocationTracking();
    auto extraPlan = core::FrameObjectPool::plan({2,4,2,8});
    const auto planAllocations = test::endAllocationMeasurement().allocations;
    if (!extraPlan.hasValue()) return false;
    std::printf("FrameObjectPool plan allocation sites=%zu\n", planAllocations);
    std::fflush(stdout);
    for (std::size_t failure = 0; failure < planAllocations; ++failure) {
        injectionProgress("FrameObjectPool plan", failure, "start");
        test::failOneAllocationAfter(failure);
        {
        auto failed = core::FrameObjectPool::plan({2,4,2,8});
        test::cancelAllocationFailure();
        progress("Injected call returned; checking cleanup");
        if (failed.hasValue()) return false;
        const auto after = hooks::poolCounters();
        if (after.liveStates != before.liveStates || after.liveSlabs != before.liveSlabs
            || after.liveProbeAllocations != before.liveProbeAllocations) return false;
        } // Destroy the failed Result before reporting cleanup complete.
        injectionProgress("FrameObjectPool plan", failure, "complete");
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
    std::fflush(stdout);
    return correct && counts.allocations == 0 && counts.deallocations == 0;
}
template<class CreateEngine>
bool sweepEngineCreationFailures(const char* label, CreateEngine create,
    core::BufferPool& processingPool, core::BufferPool& displayPool) {
    const auto before = core::detail::testing::poolCounters();
    test::beginAllocationTracking();
    auto baseline = create();
    const auto allocations = test::endAllocationMeasurement().allocations;
    progress("Engine baseline create-returned");
    if (!baseline.hasValue()) return false;
    progress("Engine baseline reset-start");
    baseline.value().reset();
    progress("Engine baseline reset-complete");
    std::printf("%s allocation sites=%zu\n", label, allocations);
    std::fflush(stdout);
    for (std::size_t failure = 0; failure < allocations; ++failure) {
        injectionProgress(label, failure, "start");
        test::failOneAllocationAfter(failure);
        {
            auto failed = create();
            test::cancelAllocationFailure();
            progress("Injected engine call returned; checking cleanup");
            if (failed.hasValue()) return false;
            const auto after = core::detail::testing::poolCounters();
            if (processingPool.stats().inUse != 0 || displayPool.stats().inUse != 0
                || before.liveStates != after.liveStates || before.liveSlabs != after.liveSlabs
                || after.liveProbeAllocations != before.liveProbeAllocations) return false;
        } // The failed Result is destroyed before reporting cleanup complete.
        injectionProgress(label, failure, "complete");
    }
    std::printf("%s failure injection: %zu allocation sites reclaimed all unpublished resources.\n", label, allocations);
    std::fflush(stdout);
    return true;
}

#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
constexpr const char* debugProxyControlArgument = "--msvc-debug-proxy-termination-control";
int debugProxyTerminationControl() {
    std::printf("MSVC Debug proxy control: compiler=%d full=%d iterator-debug=%d\n",
        _MSC_VER, _MSC_FULL_VER, _ITERATOR_DEBUG_LEVEL);
#ifdef _MSVC_STL_VERSION
    std::printf("MSVC STL version=%d\n", _MSVC_STL_VERSION);
#endif
#ifdef _MSVC_STL_UPDATE
    std::printf("MSVC STL update=%ld\n", static_cast<long>(_MSVC_STL_UPDATE));
#endif
    std::fflush(stdout);
    std::set_terminate([] {
        constexpr char marker[] = "Expected unrecoverable MSVC Debug stageRegistry vector-proxy termination; not engine recovery.\n";
        DWORD written = 0;
        (void)WriteFile(GetStdHandle(STD_ERROR_HANDLE), marker,
            static_cast<DWORD>(sizeof(marker) - 1U), &written, nullptr);
        ExitProcess(86);
    });
    try {
        test::failOneAllocationAfter(0);
        auto registry = processing::stageRegistry();
        test::cancelAllocationFailure();
        (void)registry;
        progress("Debug proxy control unexpectedly returned");
        return 88;
    } catch (const std::bad_alloc&) {
        test::cancelAllocationFailure();
        progress("Debug proxy control caught bad_alloc instead of termination");
        return 87;
    }
}

bool requireDebugProxyTerminationControl() {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
        static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return false;
    std::wstring command = L"\"" + std::wstring(executable.data(), length)
        + L"\" --msvc-debug-proxy-termination-control";
    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    progress("Starting bounded MSVC Debug proxy termination control (10-second limit)");
    if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
    const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
    DWORD exitCode = 0;
    bool observed = false;
    if (wait == WAIT_OBJECT_0) {
        observed = GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == 86;
    } else {
        // This handle belongs only to the child started above.
        (void)TerminateProcess(process.hProcess, 89);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::printf("MSVC Debug proxy control wait=%lu exit=%lu expected-termination-observed=%d\n",
        static_cast<unsigned long>(wait), static_cast<unsigned long>(exitCode), observed ? 1 : 0);
    std::fflush(stdout);
    return observed;
}
#endif

bool enginePreparationFailuresReleaseResources() {
    const auto layout = core::ImageLayout::create(4, 1, 8, core::StorageType::UInt16, 8).value();
    auto processingPool = core::BufferPool::create(9, 8).value();
    auto displayPool = core::BufferPool::create(16, 4).value();
    auto definition = processing::defaultPipeline();
    definition.stages[3].enabled = true;
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
    if (!requireDebugProxyTerminationControl()) return false;
    progress("MSVC iterator-Debug complete create+plan global failure sweep UNEXERCISED: verified nonrecoverable STL metadata proxy allocation. This is not recovered engine failure evidence.");
#else
    if (!sweepEngineCreationFailures("Complete engine preparation", [&] {
            return processing::FrameProcessingEngine::create(*processingPool, *displayPool, layout, definition);
        }, *processingPool, *displayPool)) return false;
#endif
    // Admission and compiler/STL metadata are prepared before the recoverable
    // ownership sweep. The creation callable never recreates this plan.
    const auto admitted = processing::FrameProcessingEngine::plan({9, 8}, {16, 4}, layout, definition);
    if (!admitted.plan) return false;
    return sweepEngineCreationFailures("Pre-admitted engine creation", [&] {
        return processing::FrameProcessingEngine::create(*processingPool, *displayPool, *admitted.plan);
    }, *processingPool, *displayPool);
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
    std::printf("Profile preparation-start orientation=%d fallback=%d extent=8x8 CLAHE-grid=2 warmup=100 measured=1000\n", orientation ? 1 : 0, fallback ? 1 : 0);
    std::fflush(stdout);
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
    progress("Profile prepared");
    if(fallback) { if(engine.process(raw).hasValue() || engine.process(raw).hasValue()) return false; }
    auto sentinel=engine.process(raw); if(!sentinel.hasValue()) return false;
    progress("Profile latch/sentinel-complete");
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
    progress("Profile warmup-complete/measurement-start");
    bool successful=true;
    test::beginAllocationTracking();
    for(std::size_t i=0;i<1000;++i) successful=cycle(i) && successful;
    for(auto& owner:retained) owner.reset();
    (void)latest.publish(sentinel.value());
    const auto measured=test::endAllocationMeasurement();
    progress("Profile measurement-complete");
    std::printf("Complete prepared publication: orientation=%d fallback=%d success=%d allocations=%zu bytes=%zu deallocations=%zu frames=1000\n",
        orientation ? 1 : 0,fallback ? 1 : 0,successful ? 1 : 0,measured.allocations,measured.allocatedBytes,measured.deallocations);
    std::fflush(stdout);
    return successful && measured.allocations==0 && measured.deallocations==0;
}
}  // namespace
int main(int argc, char** argv) {
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
    if (argc == 2 && std::strcmp(argv[1], debugProxyControlArgument) == 0)
        return debugProxyTerminationControl();
#else
    (void)argc;
    (void)argv;
#endif
    progress("Phase1 FrameObjectPool preparation failure cleanup start");
    if (!preparationFailuresReleaseResources()) return 4;
    progress("Phase1 complete; Phase2 arena exhaustion start");
    if (!arenaExhaustionHasNoHeapFallback()) return 4;
    progress("Phase2 arena complete; error-rendering cleanup start");
    if (!errorRenderingFailureDoesNotLeak()) return 4;
    progress("Phase2 error-rendering complete; positive controls start");
    test::beginAllocationTracking();
    auto* ordinary = ::operator new(7U);
    auto* aligned = ::operator new(19U, std::align_val_t{64U});
    ::operator delete(ordinary);
    ::operator delete(aligned, std::align_val_t{64U});
    const auto control = test::endAllocationMeasurement();
    if (control.allocations != 2U || control.allocatedBytes != 26U || control.deallocations != 2U) return 1;
    progress("Phase2 complete; Phase3 default-engine publication start");
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
    progress("Phase3 warmup-complete/measurement-start");
    bool successful = true;
    test::beginAllocationTracking();
    for (std::size_t frame = 0; frame < 1000U; ++frame) successful = cycle(frame) && successful;
    for (auto& owner : retained) owner.reset();
    static_cast<void>(latest.publish(sentinel)); // Releases the final measured output before disarming.
    const auto counts = test::endAllocationMeasurement();
    std::printf("Engine publication/retention/release: success=%d allocations=%zu bytes=%zu deallocations=%zu over 1000 frames after 100 warmups\n",
        successful ? 1 : 0, counts.allocations, counts.allocatedBytes, counts.deallocations);
    if(!successful || counts.allocations!=0U) return 3;
    progress("Phase3 complete; Phase4 complete-engine preparation failures start");
    if(!enginePreparationFailuresReleaseResources()) return 6;
    progress("Phase4 complete; Phase5 full-enabled profiles start");
    for(bool oriented:{false,true}) for(bool fallback:{false,true}) {
        if(!completeSchedule(oriented,fallback)) return 5;
        progress("Profile complete");
    }
    progress("Phase5 complete");
    return 0;
}
