#include "PreparedCpuExecutor.hpp"
#include "FrameEngineTestAccess.hpp"
#include "FrameObjectPoolTestSupport.hpp"
#include "PreparedOwner.hpp"
#include "StageStorage.hpp"
#include <lumora/processing/ToneStages.hpp>
#include <lumora/processing/ProcessingDefaults.hpp>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <barrier>
#include <condition_variable>
#include <cstring>
#include <cfenv>
#include <thread>
namespace lumora::processing {
namespace {
using namespace std::chrono_literals;
struct Gate final {
    std::mutex mutex; std::condition_variable changed; bool entered{},released{};
    void block() { std::unique_lock lock(mutex); entered=true; changed.notify_all(); changed.wait(lock,[&]{return released;}); }
    bool wait() { std::unique_lock lock(mutex); return changed.wait_for(lock,2s,[&]{return entered;}); }
    void release() { std::lock_guard lock(mutex); released=true; changed.notify_all(); }
};
struct Release final { Gate& gate; ~Release() { gate.release(); } };
struct RecordingHook final : detail::EngineHooks {
    std::array<std::atomic<std::size_t>,13> calls{};
    std::array<ProcessingOperation,64> order{};
    std::atomic<std::size_t> count{};
    std::atomic<std::size_t> gammaCreations{};
    std::atomic<unsigned> threadStarts{};
    void beforeCpuThreadStart(std::size_t) override { ++threadStarts; }
    std::atomic<bool> blockFactory{},failFactory{},blockFrame{},blockPrepared{};
    std::mutex historyMutex;
    std::array<std::weak_ptr<const IProcessingStage>,32> gammaHistory{};
    std::size_t liveGammaOwners() {
        std::lock_guard lock(historyMutex); std::size_t liveOwnerCount=0;
        for(const auto& owner:gammaHistory) if(!owner.expired()) ++liveOwnerCount;
        return liveOwnerCount;
    }
    ProcessingOperation blockedOperation{ProcessingOperation::Gamma};
    Gate factoryGate,frameGate;
    std::atomic<int> preparing{},maximumPreparing{};
    core::Result<std::shared_ptr<const IProcessingStage>> prepare(const StageDefinition& stage,const core::ImageLayout& layout,std::size_t scratch,detail::PreparedCpuExecutor& executor) override {
        const auto concurrent=preparing.fetch_add(1)+1;
        auto maximum=maximumPreparing.load();
        while(maximum<concurrent && !maximumPreparing.compare_exchange_weak(maximum,concurrent)) {}
        struct Exit { std::atomic<int>& count; ~Exit(){--count;} } exit{preparing};
        if(blockFactory.exchange(false)) factoryGate.block();
        if(failFactory.exchange(false)) return core::Result<std::shared_ptr<const IProcessingStage>>::failure(
            {core::ErrorCategory::Processing,"factory_refused","Candidate failed.","Original factory diagnostic",false,17});
        auto made=EngineHooks::prepare(stage,layout,scratch,executor);
        if(made.hasValue() && stage.id==StageId::Gamma) {
            const auto index=gammaCreations.fetch_add(1);
            std::lock_guard lock(historyMutex); gammaHistory[index%gammaHistory.size()]=made.value();
        }
        if(blockPrepared.exchange(false)) factoryGate.block();
        return made;
    }
    core::Result<void> before(ProcessingOperation operation,std::span<std::byte>) override {
        ++calls[static_cast<std::size_t>(operation)];
        const auto index=count.fetch_add(1);
        if(index<order.size()) order[index]=operation;
        if(operation==blockedOperation && blockFrame.exchange(false)) frameGate.block();
        return core::Result<void>::success();
    }
};
struct Fixture final {
    std::uint32_t width,height;
    core::ImageLayout layout;
    std::shared_ptr<core::BufferPool> processing,display;
    std::shared_ptr<RecordingHook> hook=std::make_shared<RecordingHook>();
    std::unique_ptr<FrameProcessingEngine> engine;
    Fixture(PipelineDefinition definition,std::uint32_t w=4,std::uint32_t h=2,ProcessingPreparationOptions options={})
        :width(w),height(h),layout(core::ImageLayout::create(w,h,w*2,core::StorageType::UInt16,static_cast<std::size_t>(w)*h*2).value()),
        processing(core::BufferPool::create(9,static_cast<std::size_t>(w)*h*2).value()),display(core::BufferPool::create(16,static_cast<std::size_t>(w)*h).value()),
        engine(detail::FrameEngineTestAccess::create(*processing,*display,layout,definition,options,hook).value()) {}
    std::shared_ptr<const core::RawFrame> raw(std::uint16_t value=1000) {
        const auto stride=width*2+1;
        auto rawLayout=core::ImageLayout::create(width,height,stride,core::StorageType::UInt16,static_cast<std::size_t>(stride)*height).value();
        auto pool=core::BufferPool::create(1,rawLayout.payloadBytes()).value(); auto lease=pool->tryAcquire();
        std::fill(lease->bytes().begin(),lease->bytes().end(),std::byte{0xAB});
        for(std::uint32_t y=0;y<height;++y) for(std::uint32_t x=0;x<width;++x) std::memcpy(lease->bytes().data()+y*stride+x*2,&value,2);
        core::SourcePixelFormat format{"Mono16",0x01100007U,16,65535,core::SourcePacking::Unpacked,core::BitAlignment::LeastSignificant,core::StorageType::UInt16};
        auto settings=core::AcquisitionSettingsSnapshot::create({"Test","Numeric","1","virtual",{}},format,{0,0,width,height},30,30,{},{}).value();
        return core::RawFrame::create(1,rawLayout,std::move(*lease).seal(),{{},{},{},{},std::move(settings)}).value();
    }
};
PipelineDefinition gamma(double value=1,std::uint64_t revision=1) {
    auto result=defaultPipeline(); result.version.configurationRevision=revision;
    result.stages[3].enabled=true; result.stages[3].parameters=GammaParameters{value}; return result;
}

TEST(FrameProcessingEngine, ParallelStageAdmissionUsesFrozenSlotsAndRetainedPayload) {
    auto definition=defaultPipeline();
    definition.stages[4].enabled=true; definition.stages[4].parameters=ClaheParameters{2,2};
    definition.stages[5].enabled=true; definition.stages[5].parameters=DenoiseParameters{DenoiseMode::Gaussian,7,1.25};
    definition.stages[6].enabled=true; definition.stages[6].parameters=SharpenParameters{1.75,5,12.5};
    ProcessingPreparationOptions serialOptions; serialOptions.cpuExecutionSlots=1;
    Fixture serial(definition,257,257,serialOptions);
    const auto baseline=serial.engine->resources();
    for(const auto slots : {2U,4U}) {
        ProcessingPreparationOptions options; options.cpuExecutionSlots=slots;
        Fixture parallel(definition,257,257,options);
        const auto resources=parallel.engine->resources();
        const auto delta=(slots-1U)*(65536U*sizeof(std::int32_t)+257U*31U*sizeof(double));
        EXPECT_EQ(resources.candidateRequiredBytes,baseline.candidateRequiredBytes+delta);
        EXPECT_EQ(resources.actualRetainedStageBytes,baseline.actualRetainedStageBytes+delta);
        // A stopped envelope large enough only for serial payload must reject parallel admission.
        options.activationEnvelopeBytes=baseline.candidateRequiredBytes;
        const auto rejected=FrameProcessingEngine::plan({9,257U*257U*2U},{16,257U*257U},parallel.layout,definition,options);
        ASSERT_TRUE(rejected.error); EXPECT_EQ(rejected.error->code,"processing_resource_budget_exceeded");
    }
}


TEST(FrameProcessingEngine, ParallelJobsAreAttributedAndObserverDetachesBeforeContextDies) {
    for(const auto extent : {64U,257U}) {
        Fixture f(standardPipeline(),extent,extent);
        auto raw=f.raw();
        {
            std::array<std::atomic<unsigned>,6> masks{};
            detail::FrameEngineTestAccess::setCpuWorkObserver(*f.engine,&masks,
                [](void* p,detail::CpuJobKind kind,std::size_t slot,std::size_t,std::size_t) noexcept {
                    if(slot) (*static_cast<std::array<std::atomic<unsigned>,6>*>(p))[static_cast<std::size_t>(kind)].fetch_or(1U<<slot);
                });
            for(unsigned run=0;run<2;++run) {
                for(auto& mask:masks) mask=0;
                ASSERT_TRUE(f.engine->process(raw).hasValue());
                EXPECT_EQ(masks[1],14U); EXPECT_EQ(masks[2],14U);
                for(const auto kind : {3U,4U,5U}) EXPECT_EQ(masks[kind],extent>=257U ? 14U : 0U);
                EXPECT_EQ(masks[0],0U);
            }
            detail::FrameEngineTestAccess::setCpuWorkObserver(*f.engine,nullptr,nullptr);
        }
        ASSERT_TRUE(f.engine->process(raw).hasValue());
    }
}

TEST(FrameProcessingEngine, ParallelActivationKeepsOldScratchAndOneExecutorUntilBarrierCompletes) {
    auto initial=standardPipeline();
    Fixture f(initial,257,257);
    auto raw=f.raw();
    const auto before=f.engine->resources().actualRetainedStageBytes;
    const auto oldClahe=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Clahe);
    const auto oldGaussian=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Denoise);
    const auto oldSharpen=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Sharpen);
    struct Observation { Gate gate; std::barrier<> entered{4}; std::atomic<bool> block{true}; std::atomic<unsigned> calls{}; } observation;
    detail::FrameEngineTestAccess::setCpuWorkObserver(*f.engine,&observation,
        [](void* p,detail::CpuJobKind kind,std::size_t slot,std::size_t,std::size_t) noexcept {
            auto& c=*static_cast<Observation*>(p); ++c.calls;
            if(kind==detail::CpuJobKind::ClaheTiles) c.entered.arrive_and_wait();
            if(kind==detail::CpuJobKind::ClaheTiles && slot==1 && c.block.exchange(false)) c.gate.block();
        });
    bool successful=false;
    std::jthread processing([&]{successful=f.engine->process(raw).hasValue();});
    Release release{observation.gate};
    ASSERT_TRUE(observation.gate.wait());
    auto next=initial; next.version.configurationRevision++;
    next.stages[4].parameters=ClaheParameters{3,3};
    next.stages[5].parameters=DenoiseParameters{DenoiseMode::Gaussian,7,1.25};
    next.stages[6].parameters=SharpenParameters{2,5,1};
    const auto observedBefore=observation.calls.load();
    ASSERT_TRUE(f.engine->activate(next).hasValue());
    EXPECT_EQ(observation.calls.load(),observedBefore); // Preparation never submits to the busy executor.
    EXPECT_EQ(f.hook->threadStarts,3U);
    EXPECT_FALSE(oldClahe.expired()); EXPECT_FALSE(oldGaussian.expired()); EXPECT_FALSE(oldSharpen.expired());
    const auto during=f.engine->resources().actualRetainedStageBytes;
    EXPECT_GT(during,before);
    observation.gate.release(); processing.join();
    ASSERT_TRUE(successful);
    detail::FrameEngineTestAccess::setCpuWorkObserver(*f.engine,nullptr,nullptr);
    EXPECT_TRUE(oldClahe.expired()); EXPECT_TRUE(oldGaussian.expired()); EXPECT_TRUE(oldSharpen.expired());
    const auto after=f.engine->resources().actualRetainedStageBytes;
    const auto oldArrays=detail::StageStorage::claheScratchBytes(std::get<ClaheParameters>(initial.stages[4].parameters),f.layout,4).value()
        +detail::StageStorage::denoiseScratchBytes(std::get<DenoiseParameters>(initial.stages[5].parameters),f.layout,4).value()
        +detail::StageStorage::sharpenScratchBytes(std::get<SharpenParameters>(initial.stages[6].parameters),f.layout,4).value();
    const auto oldOwners=detail::StageStorage::claheOwnerBytes()+detail::StageStorage::denoiseOwnerBytes()+detail::StageStorage::sharpenOwnerBytes()+3U*detail::ownerControlReserve;
    EXPECT_EQ(during-after,oldArrays+oldOwners);
    auto same=next; same.version.configurationRevision++;
    const auto reused=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Sharpen).lock().get();
    ASSERT_TRUE(f.engine->activate(same).hasValue());
    EXPECT_EQ(detail::FrameEngineTestAccess::stage(*f.engine,StageId::Sharpen).lock().get(),reused);
    EXPECT_EQ(f.engine->resources().actualRetainedStageBytes,after);
    ASSERT_TRUE(f.engine->process(raw).hasValue());
}

TEST(FrameProcessingEngine, FullPreparedScheduleHasLiteralConstantOutputAndTwelveTimings) {
    auto definition=defaultPipeline(); for(auto& stage:definition.stages) stage.enabled=true;
    definition.stages[4].parameters=ClaheParameters{2,2};
    ProcessingPreparationOptions options; options.orientation={true,false,core::Rotation::Degrees90};
    Fixture f(definition,8,8,options); auto raw=f.raw();
    auto result=f.engine->process(raw); ASSERT_TRUE(result.hasValue());
    const auto& frame=*result.value()->enhanced;
    for(std::size_t i=0;i<64;++i) { std::uint16_t value; std::memcpy(&value,frame.pixels.bytes().data()+i*2,2); EXPECT_EQ(value,57343U); }
    for(auto byte:result.value()->originalDisplay->pixels.bytes()) EXPECT_EQ(byte,std::byte{4});
    for(auto byte:result.value()->enhancedDisplay->pixels.bytes()) EXPECT_EQ(byte,std::byte{223});
    const std::array<std::string_view,12> names{"normalize","shared_window_level","original_display_map","original_orientation","brightness_contrast","gamma","clahe","denoise","sharpen","invert","enhanced_display_map","enhanced_orientation"};
    ASSERT_EQ(frame.timings.stages.size(),names.size());
    for(std::size_t i=0;i<names.size();++i) EXPECT_EQ(frame.timings.stages[i].stageId,names[i]);
    EXPECT_EQ(f.hook->count,12U);
    EXPECT_EQ(f.engine->resources().orientationBytes,64U);
    EXPECT_EQ(raw,result.value()->raw);
}
TEST(FrameProcessingEngine, DisabledOperationsNeverInvokeAndEachCanonicalSubsequenceExecutes) {
    for(std::size_t enabled=2;enabled<8;++enabled) {
        auto definition=defaultPipeline(); definition.stages[enabled].enabled=true; definition.stages[4].parameters=ClaheParameters{2,2};
        Fixture f(definition,8,8); auto result=f.engine->process(f.raw()); ASSERT_TRUE(result.hasValue());
        for(std::size_t i=2;i<8;++i) EXPECT_EQ(f.hook->calls[i+1].load(),i==enabled ? 1U : 0U);
        EXPECT_EQ(f.hook->calls[static_cast<std::size_t>(ProcessingOperation::OriginalOrientation)],0U);
        EXPECT_EQ(result.value()->enhanced->timings.stages.size(),5U);
    }
}
TEST(FrameProcessingEngine, GammaCacheReusesExactlyOneTableAcrossDisableAndUnrelatedRevisions) {
    Fixture f(gamma()); auto original=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Gamma);
    auto definition=gamma(1,2); definition.stages[2].enabled=true;
    ASSERT_TRUE(f.engine->activate(definition).hasValue());
    EXPECT_EQ(detail::FrameEngineTestAccess::stage(*f.engine,StageId::Gamma).lock(),original.lock());
    definition.stages[3].enabled=false; definition.stages[3].parameters=GammaParameters{2};
    ASSERT_TRUE(f.engine->activate(definition).hasValue()); EXPECT_FALSE(original.expired());
    ASSERT_TRUE(f.engine->activate(gamma(1,3)).hasValue()); EXPECT_EQ(f.hook->gammaCreations,1U);
    ASSERT_TRUE(f.engine->activate(gamma(2,4)).hasValue()); EXPECT_EQ(f.hook->gammaCreations,2U); EXPECT_TRUE(original.expired());
    auto current=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Gamma);
    f.hook->failFactory=true;
    auto failed=f.engine->activate(gamma(3,5)); ASSERT_FALSE(failed.hasValue());
    ASSERT_TRUE(failed.error().preparationError); EXPECT_EQ(failed.error().preparationError->nativeCode,17);
    EXPECT_EQ(detail::FrameEngineTestAccess::stage(*f.engine,StageId::Gamma).lock(),current.lock());
    for(std::uint64_t revision=6;revision<26;++revision) ASSERT_TRUE(f.engine->activate(gamma(revision%2 ? 1 : 2,revision)).hasValue());
    EXPECT_TRUE(current.expired());
    EXPECT_EQ(f.engine->resources().actualRetainedStageBytes,sizeof(GammaStage)+detail::ownerControlReserve);
}
TEST(FrameProcessingEngine, BlockedCandidateKeepsOldFramesRunningAndSerializesOtherActivators) {
    Fixture f(gamma()); auto raw=f.raw(); f.hook->blockFactory=true;
    std::optional<core::Result<void,PipelineValidationError>> first,second;
    std::jthread one([&]{first=f.engine->activate(gamma(2,2));}); Release release{f.hook->factoryGate};
    ASSERT_TRUE(f.hook->factoryGate.wait());
    std::barrier attempted{2};
    std::jthread two([&]{attempted.arrive_and_wait(); second=f.engine->activate(gamma(3,3));});
    // Ensure failure cleanup releases the blocked candidate before either join.
    Release releaseAgain{f.hook->factoryGate}; attempted.arrive_and_wait();
    auto old=f.engine->process(raw); ASSERT_TRUE(old.hasValue()); EXPECT_EQ(old.value()->enhanced->pipelineVersion.configurationRevision,1U);
    f.hook->factoryGate.release(); one.join(); two.join();
    ASSERT_TRUE(first && first->hasValue()); ASSERT_TRUE(second && second->hasValue());
    EXPECT_EQ(f.hook->maximumPreparing,1);
    auto next=f.engine->process(raw); ASSERT_TRUE(next.hasValue()); EXPECT_EQ(next.value()->enhanced->pipelineVersion.configurationRevision,3U);
}
TEST(FrameProcessingEngine, InFlightOldConfigurationSurvivesRepeatedPublicationAndConsumesResetNextFrame) {
    Fixture f(gamma()); auto raw=f.raw(); auto oldGamma=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Gamma);
    f.hook->blockFrame=true;
    std::optional<core::Result<std::shared_ptr<const core::FrameBundle>>> old;
    std::jthread worker([&]{old=f.engine->process(raw);}); Release release{f.hook->frameGate};
    ASSERT_TRUE(f.hook->frameGate.wait());
    ASSERT_TRUE(f.engine->activate(gamma(2,2)).hasValue());
    auto middle=detail::FrameEngineTestAccess::stage(*f.engine,StageId::Gamma);
    f.hook->blockPrepared=true;
    std::optional<core::Result<void,PipelineValidationError>> prepared;
    std::jthread candidate([&]{prepared=f.engine->activate(gamma(3,3));}); Release releaseCandidate{f.hook->factoryGate};
    ASSERT_TRUE(f.hook->factoryGate.wait());
    EXPECT_EQ(f.hook->liveGammaOwners(),3U); // active, fully prepared candidate, old in-flight
    f.hook->factoryGate.release(); candidate.join(); ASSERT_TRUE(prepared && prepared->hasValue());
    EXPECT_TRUE(middle.expired()); EXPECT_FALSE(oldGamma.expired());
    EXPECT_EQ(f.engine->resources().actualRetainedStageBytes,2U*(sizeof(GammaStage)+detail::ownerControlReserve));
    EXPECT_TRUE(f.engine->requestRetry()); EXPECT_EQ(f.engine->status().configurationRevision,1U);
    f.hook->frameGate.release(); worker.join(); ASSERT_TRUE(old && old->hasValue());
    EXPECT_EQ(old->value()->enhanced->pipelineVersion.configurationRevision,1U);
    EXPECT_EQ(old->value()->originalDisplay->mapping.configurationRevision,1U);
    EXPECT_EQ(old->value()->enhancedDisplay->mapping.configurationRevision,1U);
    EXPECT_TRUE(oldGamma.expired()); // Published output retains pixels/version, not a backend.
    EXPECT_TRUE(f.engine->status().retryPending);
    auto next=f.engine->process(raw); ASSERT_TRUE(next.hasValue());
    EXPECT_EQ(next.value()->enhanced->pipelineVersion.configurationRevision,3U);
    EXPECT_EQ(f.engine->status().retriesConsumed,1U);
    EXPECT_FALSE(f.engine->status().retryPending);
}
TEST(FrameProcessingEngine, AggregateRejectionMakesNoStageOrMetadataSlabAndAcquiresNoWorkspaceLease) {
    auto definition=gamma();
    Fixture f(defaultPipeline());
    auto before=core::detail::testing::poolCounters();
    auto pBefore=f.processing->stats(); auto dBefore=f.display->stats();
    ProcessingPreparationOptions options; options.activationEnvelopeBytes=1;
    auto failed=detail::FrameEngineTestAccess::create(*f.processing,*f.display,f.layout,definition,options,f.hook);
    ASSERT_FALSE(failed.hasValue());
    auto after=core::detail::testing::poolCounters();
    EXPECT_EQ(after.slabAllocations,before.slabAllocations);
    EXPECT_EQ(after.liveStates,before.liveStates);
    EXPECT_EQ(after.liveProbeAllocations,before.liveProbeAllocations);
    EXPECT_EQ(f.hook->gammaCreations,0U);
    EXPECT_EQ(f.processing->stats().inUse,pBefore.inUse);
    EXPECT_EQ(f.display->stats().inUse,dBefore.inUse);
}
TEST(FrameProcessingEngine, UnsupportedClaheRoundingEnvironmentIsAnEnhancementFailure) {
    struct Restore { int rounding=std::fegetround(); ~Restore(){std::fesetround(rounding);} } restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST),0);
    auto definition=defaultPipeline(); definition.stages[4].enabled=true; definition.stages[4].parameters=ClaheParameters{2,2};
    Fixture f(definition,8,8); auto raw=f.raw();
    ASSERT_EQ(std::fesetround(FE_DOWNWARD),0);
    for(int i=0;i<2;++i) { auto failed=f.engine->process(raw); ASSERT_FALSE(failed.hasValue()); EXPECT_EQ(failed.error().code,"clahe_rounding_mode_unsupported"); }
    auto fallback=f.engine->process(raw); ASSERT_TRUE(fallback.hasValue()); EXPECT_EQ(fallback.value()->enhanced,nullptr);
    EXPECT_EQ(f.engine->status().failingOperation,ProcessingOperation::Clahe);
    ASSERT_NE(f.engine->status().error,nullptr); EXPECT_EQ(f.engine->status().error->code,"clahe_rounding_mode_unsupported");
}
TEST(FrameProcessingEngine, NewOwnerAllocatorRejectsOversizedReboundRequestsBeforeAllocation) {
    detail::PreparedOwnerAllocator<std::byte> owner{8};
    detail::PreparedOwnerAllocator<std::uint64_t> rebound(owner);
    EXPECT_THROW((void)rebound.allocate(2),std::bad_alloc);
    auto* allocation=rebound.allocate(1); ASSERT_NE(allocation,nullptr); rebound.deallocate(allocation,1);
}
}
TEST(FrameProcessingEngine, CpuExecutionOptionsAreAdmittedFrozenAndAccountedExactlyOnce) {
    const auto layout=core::ImageLayout::create(4,2,8,core::StorageType::UInt16,16).value();
    for(auto slots:{0U,5U}) {
        ProcessingPreparationOptions options; options.cpuExecutionSlots=slots;
        auto a=FrameProcessingEngine::plan({9,16},{16,8},layout,defaultPipeline(),options);
        ASSERT_TRUE(a.error); EXPECT_EQ(a.error->code,"invalid_cpu_execution_slots"); EXPECT_FALSE(a.plan);
    }
    for(auto slots:{1U,4U}) {
        ProcessingPreparationOptions options; options.cpuExecutionSlots=slots; options.activationEnvelopeBytes=1000000;
        const auto a=FrameProcessingEngine::plan({9,16},{16,8},layout,defaultPipeline(),options);
        ASSERT_FALSE(a.error); const auto& r=a.resources;
        EXPECT_EQ(r.cpuExecutionSlots,slots); EXPECT_EQ(r.cpuHelperThreads,slots-1);
        EXPECT_EQ(r.cpuExecutorBytes,sizeof(detail::PreparedCpuExecutor));
        EXPECT_EQ(r.fixedStorageBytes,r.externalSessionBytes+r.processingPoolBytes+r.displayPoolBytes+r.frameObjectBytes+r.orientationBytes+r.engineStateBytes+r.cpuExecutorBytes);
        auto p=core::BufferPool::create(9,16).value(),d=core::BufferPool::create(16,8).value();
        auto engine=FrameProcessingEngine::create(*p,*d,*a.plan).value();
        ASSERT_TRUE(engine->activate(gamma()).hasValue()); EXPECT_EQ(engine->resources().cpuExecutionSlots,slots);
        options.storageBudgetBytes=r.requiredStorageBytes-1;
        EXPECT_TRUE(FrameProcessingEngine::plan({9,16},{16,8},layout,defaultPipeline(),options).error);
    }
}
TEST(FrameProcessingEngine, CpuStartupFailuresAreTypedAndActivationNeverStartsHelpers) {
    struct Hook : detail::EngineHooks { unsigned starts{}; std::size_t failAt{2}; void beforeCpuThreadStart(std::size_t slot) override { ++starts; if(slot==failAt) throw std::runtime_error("injected"); } };
    const auto layout=core::ImageLayout::create(4,2,8,core::StorageType::UInt16,16).value();
    auto p=core::BufferPool::create(9,16).value(),d=core::BufferPool::create(16,8).value(); auto hook=std::make_shared<Hook>();
    auto failed=detail::FrameEngineTestAccess::create(*p,*d,layout,defaultPipeline(),{},hook);
    ASSERT_FALSE(failed.hasValue()); EXPECT_EQ(failed.error().code,"processing_cpu_executor_startup_failed"); EXPECT_EQ(hook->starts,2U);
    hook->starts=0;hook->failAt=0;
    auto engine=detail::FrameEngineTestAccess::create(*p,*d,layout,defaultPipeline(),{},hook).value();
    EXPECT_EQ(hook->starts,3U); EXPECT_EQ(engine->resources().cpuHelperThreads,3U);
    ASSERT_TRUE(engine->activate(gamma()).hasValue()); EXPECT_EQ(hook->starts,3U);
}

TEST(FrameProcessingEngine, BorrowingStageDestructorsStillHaveTheirLiveExecutor) {
    struct Lifetime {
        FrameProcessingEngine* engine{};
        std::atomic<unsigned> mask{},destructors{};
        static void work(void* p,std::size_t slot,std::size_t,std::size_t) noexcept {static_cast<Lifetime*>(p)->mask.fetch_or(1U<<slot);}
    } lifetime;
    struct Borrower : IProcessingStage {
        std::shared_ptr<const IProcessingStage> inner; Lifetime& lifetime;
        Borrower(std::shared_ptr<const IProcessingStage> stage,Lifetime& context):inner(std::move(stage)),lifetime(context) {}
        ~Borrower() override { if(lifetime.engine) {detail::FrameEngineTestAccess::runCpu(*lifetime.engine,4,&lifetime,Lifetime::work);++lifetime.destructors;} }
        StageId id() const noexcept override {return inner->id();}
        const StageTraits& traits() const noexcept override {return inner->traits();}
        core::Result<void> process(const ImageView& source,MutableImageView output,const core::SourcePixelFormat& format) const override {return inner->process(source,output,format);}
    };
    struct Hook : detail::EngineHooks {
        Lifetime& lifetime;explicit Hook(Lifetime& context):lifetime(context) {}
        core::Result<std::shared_ptr<const IProcessingStage>> prepare(const StageDefinition& stage,const core::ImageLayout& layout,std::size_t scratch,detail::PreparedCpuExecutor& executor) override {
            auto inner=EngineHooks::prepare(stage,layout,scratch,executor);if(!inner.hasValue()) return inner;
            return core::Result<std::shared_ptr<const IProcessingStage>>::success(std::make_shared<Borrower>(inner.value(),lifetime));
        }
    };
    auto p=core::BufferPool::create(9,16).value(),d=core::BufferPool::create(16,8).value();
    auto layout=core::ImageLayout::create(4,2,8,core::StorageType::UInt16,16).value();
    auto hook=std::make_shared<Hook>(lifetime);
    auto engine=detail::FrameEngineTestAccess::create(*p,*d,layout,gamma(),{},hook).value();
    lifetime.engine=engine.get();engine.reset();
    EXPECT_EQ(lifetime.destructors,1U);EXPECT_EQ(lifetime.mask,15U);
}

}
