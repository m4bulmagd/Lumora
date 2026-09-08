#include <lumora/processing/FrameProcessingEngine.hpp>
#include "FrameEngineTestAccess.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <barrier>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <stdexcept>

namespace lumora::processing {
namespace {
using namespace lumora;

core::SourcePixelFormat format(unsigned bits) {
    return {"Mono" + std::to_string(bits), bits == 8 ? 0x01080001U : 0x01100005U,
        static_cast<std::uint8_t>(bits), static_cast<std::uint16_t>((1U << bits) - 1U),
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        bits == 8 ? core::StorageType::UInt8 : core::StorageType::UInt16};
}
core::ImageLayout layout(unsigned bits, std::uint32_t width = 4, std::uint32_t height = 1,
                         std::size_t padding = 0) {
    const auto stride = width * (bits == 8 ? 1U : 2U) + padding;
    return core::ImageLayout::create(width, height, stride, format(bits).applicationStorage,
        stride * height).value();
}
std::shared_ptr<const core::RawFrame> rawFrame(unsigned bits,
    const std::vector<std::uint16_t>& samples, std::uint64_t id = 1,
    std::size_t padding = 0, std::uint32_t height = 1, double fps = 30.0) {
    const auto width = static_cast<std::uint32_t>(samples.size() / height);
    const auto sourceLayout = layout(bits, width, height, padding);
    auto pool = core::BufferPool::create(1, sourceLayout.payloadBytes()).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), std::byte{0xAB});
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto sample = samples[y * width + x];
            auto* destination = lease->bytes().data() + y * sourceLayout.strideBytes() + x * (bits == 8 ? 1U : 2U);
            if (bits == 8) *destination = static_cast<std::byte>(sample);
            else std::memcpy(destination, &sample, sizeof(sample));
        }
    }
    auto settings = core::AcquisitionSettingsSnapshot::create(
        {"Test", "Numeric", "1", "virtual", {}}, format(bits), {0, 0, width, height}, 30, 30, {}, {}).value();
    settings.actualFps = fps;
    return core::RawFrame::create(id, sourceLayout, std::move(*lease).seal(),
        {{}, {}, {}, {}, std::move(settings)}).value();
}
std::vector<std::byte> copyBytes(const core::SharedBuffer& buffer) {
    return {buffer.bytes().begin(), buffer.bytes().end()};
}
std::uint64_t digest(const std::vector<std::byte>& bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (auto byte : bytes) { value ^= std::to_integer<unsigned>(byte); value *= 1099511628211ULL; }
    return value;
}
std::vector<std::uint16_t> samples(const core::ProcessedFrame& frame) {
    std::vector<std::uint16_t> values;
    for (std::uint32_t y = 0; y < frame.layout.height(); ++y) {
        for (std::uint32_t x = 0; x < frame.layout.width(); ++x) {
            std::uint16_t value;
            std::memcpy(&value, frame.pixels.bytes().data() + y * frame.layout.strideBytes() + x * 2U, 2);
            values.push_back(value);
        }
    }
    return values;
}
struct Fixture {
    std::shared_ptr<core::BufferPool> u16;
    std::shared_ptr<core::BufferPool> gray;
    std::unique_ptr<FrameProcessingEngine> engine;
    explicit Fixture(unsigned bits = 12, std::uint32_t width = 4, std::uint32_t height = 1,
                     std::size_t u16Count = 9, std::size_t grayCount = 16)
        : u16(core::BufferPool::create(u16Count, width * height * 2U).value()),
          gray(core::BufferPool::create(grayCount, width * height).value()),
          engine(FrameProcessingEngine::create(*u16, *gray, layout(bits, width, height)).value()) {}
};
PipelineDefinition windowDefinition(bool enabled, std::uint64_t revision = 4) {
    auto definition = defaultPipeline();
    definition.version.configurationRevision = revision;
    definition.stages[1].enabled = enabled;
    definition.stages[1].parameters = WindowLevelParameters{32768, 16384};
    return definition;
}

struct FailureHook : detail::EngineHooks {
    ProcessingOperation failure{ProcessingOperation::Invert};
    std::size_t calls{};
    bool enabled{true};
    bool failPreparation{false};
    int exceptionKind{};
    int preparationExceptionKind{};
    core::ErrorCategory category{core::ErrorCategory::Processing};
    core::Result<std::shared_ptr<const IProcessingStage>> prepare(const StageDefinition& stage,const core::ImageLayout& image,std::size_t scratch) override {
        if(preparationExceptionKind==1) throw std::runtime_error("backend construction failed");
        if(preparationExceptionKind==2) throw std::bad_alloc{};
        if(preparationExceptionKind==3) throw 17;
        if(preparationExceptionKind==4) throw std::length_error("backend storage exceeds limits");
        if(failPreparation) return core::Result<std::shared_ptr<const IProcessingStage>>::failure(
            {core::ErrorCategory::Processing,"injected_prepare_fault","Preparation failed.","Original factory diagnostic",false,17});
        return EngineHooks::prepare(stage,image,scratch);
    }
    core::Result<void> before(ProcessingOperation operation,std::span<std::byte> destination) override {
        if(operation!=failure) return core::Result<void>::success();
        ++calls;
        if(!enabled) return core::Result<void>::success();
        std::ranges::fill(destination,std::byte{0xDE});
        if(exceptionKind==1) throw std::runtime_error("injected backend exception");
        if(exceptionKind==2) throw std::bad_alloc{};
        if(exceptionKind==3) throw 17;
        return core::Result<void>::failure({category,"injected_stage_fault","Injected failure.","Original diagnostic",true,23});
    }
};
TEST(FrameProcessingEngine, ThirdEnhancementFailurePublishesOnlyOriginalAndRetryWaitsForNextFrame) {
    auto u16=core::BufferPool::create(9,8).value();
    auto gray=core::BufferPool::create(16,4).value();
    auto hook=std::make_shared<FailureHook>();
    auto definition=defaultPipeline(); definition.stages.back().enabled=true;
    auto made=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),definition,{},hook);
    ASSERT_TRUE(made.hasValue()); auto& engine=*made.value();
    auto raw=rawFrame(16,{0,1000,32768,65535});
    EXPECT_FALSE(engine.process(raw).hasValue());
    EXPECT_FALSE(engine.process(raw).hasValue());
    auto third=engine.process(raw); ASSERT_TRUE(third.hasValue());
    EXPECT_EQ(third.value()->enhanced,nullptr);
    EXPECT_EQ(third.value()->enhancedDisplay,nullptr);
    EXPECT_EQ(copyBytes(third.value()->originalDisplay->pixels),(std::vector<std::byte>{std::byte{0},std::byte{4},std::byte{128},std::byte{255}}));
    EXPECT_EQ(engine.status().mode,ProcessorMode::OriginalOnlyLatched);
    EXPECT_EQ(engine.status().enhancementFailures,3U);
    auto diagnostic=engine.status().error; ASSERT_NE(diagnostic,nullptr);
    EXPECT_EQ(diagnostic->diagnosticDetail,"Original diagnostic"); EXPECT_EQ(diagnostic->nativeCode,23);
    EXPECT_TRUE(engine.process(raw).hasValue()); EXPECT_EQ(hook->calls,3U);
    EXPECT_EQ(engine.status().error,diagnostic);
    EXPECT_TRUE(engine.requestRetry()); EXPECT_TRUE(engine.requestRetry());
    EXPECT_TRUE(engine.status().retryPending);
    EXPECT_EQ(engine.status().mode,ProcessorMode::OriginalOnlyLatched);
    hook->enabled=false;
    auto recovered=engine.process(raw); ASSERT_TRUE(recovered.hasValue());
    EXPECT_NE(recovered.value()->enhanced,nullptr);
    EXPECT_EQ(hook->calls,4U);
    EXPECT_EQ(engine.status().mode,ProcessorMode::Enhanced);
    EXPECT_FALSE(engine.status().retryPending);
    EXPECT_EQ(engine.status().retriesConsumed,1U);
    EXPECT_EQ(engine.status().error,nullptr);
}

TEST(FrameProcessingEngine, OriginalAndResourceFaultsDoNotAdvanceOrClearEnhancementAttempts) {
    auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(16,4).value();
    auto hook=std::make_shared<FailureHook>();
    auto definition=defaultPipeline(); definition.stages.back().enabled=true;
    auto engine=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),definition,{},hook).value();
    auto raw=rawFrame(16,{0,1000,32768,65535});
    ASSERT_FALSE(engine->process(raw).hasValue());
    for(auto operation:{ProcessingOperation::Normalize,ProcessingOperation::SharedWindowLevel,ProcessingOperation::OriginalDisplayMap}) {
        hook->failure=operation;
        EXPECT_FALSE(engine->process(raw).hasValue());
        EXPECT_EQ(engine->status().consecutiveEnhancementFailures,1U);
    }
    hook->failure=ProcessingOperation::Invert; hook->category=core::ErrorCategory::ResourceExhaustion;
    EXPECT_FALSE(engine->process(raw).hasValue()); EXPECT_EQ(engine->status().consecutiveEnhancementFailures,1U);
    hook->category=core::ErrorCategory::Processing;
    EXPECT_FALSE(engine->process(raw).hasValue());
    auto latched=engine->process(raw); ASSERT_TRUE(latched.hasValue()); EXPECT_EQ(latched.value()->enhanced,nullptr);
    auto warning=engine->status().error;
    hook->failure=ProcessingOperation::OriginalDisplayMap;
    EXPECT_FALSE(engine->process(raw).hasValue()); EXPECT_EQ(engine->status().error,warning);
    hook->enabled=false;
    auto fallback=engine->process(raw); ASSERT_TRUE(fallback.hasValue()); EXPECT_EQ(fallback.value()->enhanced,nullptr);
    EXPECT_EQ(engine->status().enhancementFailures,3U);
}
TEST(FrameProcessingEngine, OnlyValidActivationResetsLatchIncludingSameRevision) {
    auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(16,4).value();
    auto hook=std::make_shared<FailureHook>(); auto definition=defaultPipeline(); definition.stages.back().enabled=true;
    ProcessingPreparationOptions options; options.activationEnvelopeBytes=20000;
    auto engine=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),definition,options,hook).value();
    auto raw=rawFrame(16,{0,1000,32768,65535});
    (void)engine->process(raw); (void)engine->process(raw); (void)engine->process(raw);
    ASSERT_EQ(engine->status().mode,ProcessorMode::OriginalOnlyLatched);
    const auto serial=engine->status().activationSerial;
    auto invalid=definition; invalid.stages[0].enabled=false;
    EXPECT_FALSE(engine->activate(invalid).hasValue());
    auto tooBig=definition; tooBig.stages[3].enabled=true;
    auto rejected=engine->activate(tooBig); ASSERT_FALSE(rejected.hasValue());
    ASSERT_TRUE(rejected.error().preparationError.has_value());
    ASSERT_TRUE(rejected.error().preparationResources.has_value());
    EXPECT_GT(rejected.error().preparationResources->candidateRequiredBytes,rejected.error().preparationResources->activationEnvelopeBytes);
    EXPECT_EQ(rejected.error().preparationResources->activationEnvelopeBytes,20000U);
    EXPECT_EQ(rejected.error().preparationError->category,core::ErrorCategory::ResourceExhaustion);
    EXPECT_EQ(engine->status().activationSerial,serial);
    EXPECT_EQ(engine->status().mode,ProcessorMode::OriginalOnlyLatched);
    ASSERT_TRUE(engine->activate(definition).hasValue());
    EXPECT_EQ(engine->status().mode,ProcessorMode::OriginalOnlyLatched);
    hook->enabled=false;
    auto recovered=engine->process(raw); ASSERT_TRUE(recovered.hasValue()); EXPECT_NE(recovered.value()->enhanced,nullptr);
    EXPECT_GT(engine->status().activationSerial,serial);
    EXPECT_EQ(engine->status().mode,ProcessorMode::Enhanced);
}

TEST(FrameProcessingEngine, LatchedOriginalOnlyReleasesUnneededEnhancedDisplayLease) {
    auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(2,4).value();
    auto hook=std::make_shared<FailureHook>(); auto definition=defaultPipeline(); definition.stages.back().enabled=true;
    auto engine=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),definition,{},hook).value();
    auto raw=rawFrame(16,{0,1000,32768,65535});
    (void)engine->process(raw); (void)engine->process(raw);
    auto retained=engine->process(raw); ASSERT_TRUE(retained.hasValue());
    auto next=engine->process(raw); ASSERT_TRUE(next.hasValue());
    EXPECT_EQ(next.value()->enhanced,nullptr);
    EXPECT_EQ(gray->stats().inUse,2U);
    EXPECT_EQ(hook->calls,3U);
}

TEST(FrameProcessingEngine, TerminalEnhancementFaultsLatchButOriginalOrientationNeverDoes) {
    for(auto operation:{ProcessingOperation::OriginalOrientation,ProcessingOperation::EnhancedDisplayMap,ProcessingOperation::EnhancedOrientation}) {
        auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(16,4).value();
        auto hook=std::make_shared<FailureHook>(); hook->failure=operation;
        ProcessingPreparationOptions options; options.orientation={false,false,core::Rotation::Degrees90};
        auto engine=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),defaultPipeline(),options,hook).value();
        auto raw=rawFrame(16,{0,1000,32768,65535});
        EXPECT_FALSE(engine->process(raw).hasValue()); EXPECT_FALSE(engine->process(raw).hasValue());
        auto third=engine->process(raw);
        if(operation==ProcessingOperation::OriginalOrientation) {
            EXPECT_FALSE(third.hasValue()); EXPECT_EQ(engine->status().enhancementFailures,0U);
        } else {
            ASSERT_TRUE(third.hasValue()); EXPECT_EQ(third.value()->enhanced,nullptr);
            EXPECT_EQ(engine->status().failingOperation,operation);
            EXPECT_EQ(copyBytes(third.value()->originalDisplay->pixels),(std::vector<std::byte>{std::byte{0},std::byte{4},std::byte{128},std::byte{255}}));
            EXPECT_TRUE(engine->process(raw).hasValue()); EXPECT_EQ(hook->calls,3U);
        }
    }
}
TEST(FrameProcessingEngine, StageExceptionsAreContainedAndAllocationExceptionsRemainResourceFaults) {
    for(auto operation:{ProcessingOperation::Invert,ProcessingOperation::OriginalDisplayMap}) {
        for(int kind:{1,2,3}) {
            SCOPED_TRACE(::testing::Message() << "operation=" << static_cast<int>(operation) << " exception=" << kind);
            auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(16,4).value();
            auto hook=std::make_shared<FailureHook>(); hook->exceptionKind=kind; hook->failure=operation;
            auto definition=defaultPipeline(); definition.stages.back().enabled=true;
            auto engine=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),definition,{},hook).value();
            auto raw=rawFrame(16,{0,1000,32768,65535});
            const bool enhancementFailure=operation==ProcessingOperation::Invert && kind!=2;
            const std::string identity=operation==ProcessingOperation::Invert ? "operation=invert" : "operation=original_display_map";
            for(int call=0;call<3;++call) {
                auto result=engine->process(raw);
                const core::Error* diagnostic{};
                auto status=engine->status();
                if(enhancementFailure && call==2) {
                    ASSERT_TRUE(result.hasValue()); EXPECT_EQ(result.value()->enhanced,nullptr);
                    diagnostic=status.error.get();
                } else {
                    ASSERT_FALSE(result.hasValue()); diagnostic=&result.error();
                }
                ASSERT_NE(diagnostic,nullptr);
                EXPECT_EQ(diagnostic->category,kind==2 ? core::ErrorCategory::ResourceExhaustion : core::ErrorCategory::Processing);
                EXPECT_EQ(diagnostic->recoverable,kind==2);
                EXPECT_NE(diagnostic->diagnosticDetail.find(identity),std::string::npos);
                if(kind==1) { EXPECT_NE(diagnostic->diagnosticDetail.find("injected backend exception"),std::string::npos); }
                if(kind==2) { EXPECT_NE(diagnostic->diagnosticDetail.find(std::bad_alloc{}.what()),std::string::npos); }
            }
            EXPECT_EQ(engine->status().enhancementFailures,enhancementFailure ? 3U : 0U);
            if(!enhancementFailure) { EXPECT_EQ(engine->status().error,nullptr); }
        }
    }
}
TEST(FrameProcessingEngine, HealthyEnhancementClearsAttemptCountButFactoryFailureCannotClearLatch) {
    auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(16,4).value();
    auto hook=std::make_shared<FailureHook>(); auto definition=defaultPipeline(); definition.stages.back().enabled=true;
    auto engine=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),definition,{},hook).value();
    auto raw=rawFrame(16,{0,1000,32768,65535});
    EXPECT_FALSE(engine->process(raw).hasValue()); EXPECT_FALSE(engine->process(raw).hasValue());
    hook->enabled=false; ASSERT_TRUE(engine->process(raw).hasValue()); EXPECT_EQ(engine->status().consecutiveEnhancementFailures,0U);
    hook->enabled=true;
    EXPECT_FALSE(engine->process(raw).hasValue()); EXPECT_FALSE(engine->process(raw).hasValue()); ASSERT_TRUE(engine->process(raw).hasValue());
    auto warning=engine->status().error;
    hook->failPreparation=true; definition.stages[3].enabled=true;
    auto rejected=engine->activate(definition); ASSERT_FALSE(rejected.hasValue());
    ASSERT_TRUE(rejected.error().preparationResources); EXPECT_GT(rejected.error().preparationResources->candidateRequiredBytes,131072U);
    ASSERT_TRUE(rejected.error().preparationError);
    EXPECT_EQ(rejected.error().preparationError->diagnosticDetail,"Original factory diagnostic");
    EXPECT_EQ(rejected.error().preparationError->nativeCode,17);
    EXPECT_EQ(engine->status().error,warning); EXPECT_EQ(engine->status().enhancementFailures,5U);
    auto fallback=engine->process(raw); ASSERT_TRUE(fallback.hasValue()); EXPECT_EQ(fallback.value()->enhanced,nullptr);
}

TEST(FrameProcessingEngine, FactoryExceptionsKeepTheirFailureClassAndPreviousActivation) {
    for(int kind:{1,2,3,4}) {
        auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(16,4).value();
        auto hook=std::make_shared<FailureHook>(); hook->enabled=false;
        auto engine=detail::FrameEngineTestAccess::create(*u16,*gray,layout(16),defaultPipeline(),{},hook).value();
        hook->preparationExceptionKind=kind;
        auto definition=defaultPipeline(); definition.version.configurationRevision=42; definition.stages[3].enabled=true;
        auto rejected=engine->activate(definition); ASSERT_FALSE(rejected.hasValue());
        ASSERT_TRUE(rejected.error().preparationError);
        EXPECT_EQ(rejected.error().preparationError->category,(kind==2 || kind==4) ? core::ErrorCategory::ResourceExhaustion : core::ErrorCategory::Processing);
        const auto& diagnostic=*rejected.error().preparationError;
        EXPECT_EQ(diagnostic.recoverable,kind==2 || kind==4);
        EXPECT_NE(diagnostic.diagnosticDetail.find("stage_id=3 (Gamma)"),std::string::npos);
        if(kind==1) { EXPECT_NE(diagnostic.diagnosticDetail.find("backend construction failed"),std::string::npos); }
        if(kind==2) { EXPECT_NE(diagnostic.diagnosticDetail.find(std::bad_alloc{}.what()),std::string::npos); }
        if(kind==4) { EXPECT_NE(diagnostic.diagnosticDetail.find("backend storage exceeds limits"),std::string::npos); }
        ASSERT_TRUE(rejected.error().preparationResources);
        auto retained=engine->process(rawFrame(16,{0,1000,32768,65535})); ASSERT_TRUE(retained.hasValue());
        EXPECT_EQ(retained.value()->enhanced->pipelineVersion.configurationRevision,0U);
    }
}

TEST(FrameProcessingEngine, AggregatePlanReportsAdmissionBeforeBulkOwnership) {
    ProcessingPreparationOptions options;
    options.externalSessionStorageBytes = 1234;
    options.activationEnvelopeBytes = 200000;
    auto planned = FrameProcessingEngine::plan({9,8}, {16,4}, layout(16), defaultPipeline(), options);
    ASSERT_TRUE(planned.plan.has_value());
    EXPECT_FALSE(planned.error.has_value());
    const auto& r = planned.resources;
    EXPECT_EQ(r.externalSessionBytes, 1234U);
    EXPECT_EQ(r.processingPoolBytes, core::BufferPool::plan(9,8).value().requiredStorageBytes);
    EXPECT_EQ(r.displayPoolBytes, core::BufferPool::plan(16,4).value().requiredStorageBytes);
    EXPECT_GT(r.frameObjectBytes, 0U);
    EXPECT_EQ(r.orientationBytes, 0U);
    EXPECT_EQ(r.fixedStorageBytes, r.externalSessionBytes + r.processingPoolBytes + r.displayPoolBytes + r.frameObjectBytes + r.orientationBytes + r.engineStateBytes);
    EXPECT_EQ(r.activationReserveBytes, 600000U);
    EXPECT_GT(r.gammaCacheReserveBytes, 131072U);
    EXPECT_EQ(r.requiredStorageBytes, r.fixedStorageBytes + r.activationReserveBytes + r.gammaCacheReserveBytes);
    options.storageBudgetBytes = r.requiredStorageBytes;
    EXPECT_TRUE(FrameProcessingEngine::plan({9,8}, {16,4}, layout(16), defaultPipeline(), options).plan.has_value());
    --options.storageBudgetBytes;
    auto rejected = FrameProcessingEngine::plan({9,8}, {16,4}, layout(16), defaultPipeline(), options);
    ASSERT_TRUE(rejected.error.has_value());
    EXPECT_EQ(rejected.error->category, core::ErrorCategory::ResourceExhaustion);
    EXPECT_EQ(rejected.resources.requiredStorageBytes, r.requiredStorageBytes);
    EXPECT_FALSE(rejected.plan.has_value());
}
TEST(FrameProcessingEngine, ImmutableOrientationRotatesBothDisplaysOnly) {
    auto u16 = core::BufferPool::create(9,12).value();
    auto gray = core::BufferPool::create(16,6).value();
    ProcessingPreparationOptions options;
    options.orientation = {false,false,core::Rotation::Degrees90};
    auto made = FrameProcessingEngine::create(*u16,*gray,layout(8,3,2),defaultPipeline(),options);
    ASSERT_TRUE(made.hasValue());
    auto raw = rawFrame(8,{0,40,80,120,160,200},1,1,2);
    auto result = made.value()->process(raw);
    ASSERT_TRUE(result.hasValue());
    for (const auto& display : {result.value()->originalDisplay,result.value()->enhancedDisplay}) {
        EXPECT_EQ(display->layout.width(),2U);
        EXPECT_EQ(display->layout.height(),3U);
        EXPECT_EQ(copyBytes(display->pixels),(std::vector<std::byte>{std::byte{120},std::byte{0},std::byte{160},std::byte{40},std::byte{200},std::byte{80}}));
        EXPECT_EQ(display->presentationOrientation,options.orientation);
    }
    EXPECT_EQ(result.value()->enhanced->layout.width(),3U);
    EXPECT_EQ(result.value()->raw,raw);
}

TEST(FrameProcessingEngine, EnabledInvertExecutesAfterOriginalWithoutChangingRaw) {
    Fixture fixture(16);
    auto definition = defaultPipeline();
    definition.stages.back().enabled = true;
    ASSERT_TRUE(fixture.engine->activate(definition).hasValue());
    auto raw = rawFrame(16, {0, 1000, 32768, 65535});
    auto output = fixture.engine->process(raw);
    ASSERT_TRUE(output.hasValue());
    EXPECT_EQ(samples(*output.value()->enhanced), (std::vector<std::uint16_t>{65535,64535,32767,0}));
    EXPECT_EQ(copyBytes(output.value()->originalDisplay->pixels), (std::vector<std::byte>{std::byte{0},std::byte{4},std::byte{128},std::byte{255}}));
    EXPECT_EQ(output.value()->raw, raw);
}

TEST(FrameProcessingEngine, PreservesRawAndPairsExactCanonicalAndGrayAtAllDepths) {
    for (unsigned bits : {8U, 10U, 12U, 16U}) {
        SCOPED_TRACE(bits);
        Fixture fixture(bits);
        const auto maximum = format(bits).sampleMaximum;
        const std::vector<std::uint16_t> input{0, 1, static_cast<std::uint16_t>(maximum / 2), maximum};
        auto raw = rawFrame(bits, input);
        const auto before = copyBytes(raw->pixels);
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        const auto& bundle = result.value();
        ASSERT_NE(bundle->enhanced, nullptr);
        ASSERT_NE(bundle->enhancedDisplay, nullptr);
        EXPECT_EQ(bundle->raw, raw);
        EXPECT_EQ(bundle->originalDisplay->sourceFrameId, raw->frameId);
        EXPECT_EQ(bundle->enhanced->sourceFrameId, raw->frameId);
        EXPECT_EQ(bundle->enhancedDisplay->sourceFrameId, raw->frameId);
        EXPECT_EQ(bundle->enhanced->layout.storage(), core::StorageType::UInt16);
        EXPECT_EQ(bundle->originalDisplay->storage, core::DisplayStorage::Gray8);
        EXPECT_EQ(bundle->enhancedDisplay->storage, core::DisplayStorage::Gray8);
        std::vector<std::uint16_t> expected;
        std::vector<std::byte> display;
        for (auto value : input) {
            const auto canonical = (static_cast<std::uint64_t>(value) * 65535U + maximum / 2U) / maximum;
            expected.push_back(static_cast<std::uint16_t>(canonical));
            display.push_back(static_cast<std::byte>((canonical + 128U) / 257U));
        }
        EXPECT_EQ(samples(*bundle->enhanced), expected);
        EXPECT_EQ(copyBytes(bundle->originalDisplay->pixels), display);
        EXPECT_EQ(copyBytes(bundle->enhancedDisplay->pixels), display);
        EXPECT_EQ(bundle->originalDisplay->mapping, bundle->enhancedDisplay->mapping);
        EXPECT_EQ(bundle->originalDisplay->mapping.configurationRevision, 0U);
        EXPECT_EQ(bundle->enhanced->pipelineVersion.configurationRevision, 0U);
        EXPECT_EQ(copyBytes(raw->pixels), before);
        EXPECT_EQ(digest(copyBytes(raw->pixels)), digest(before));
    }
}
TEST(FrameProcessingEngine, PreparesExactlyTwoCanonicalAndTwoDisplayLeases) {
    Fixture fixture;
    EXPECT_EQ(fixture.u16->stats().inUse, 2U);
    EXPECT_EQ(fixture.gray->stats().inUse, 2U);
    fixture.engine.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, DisabledWindowStillMapsOriginalAndOmittedWindowIsIdentity) {
    Fixture fixture(16);
    const auto raw = rawFrame(16, {0, 16384, 32768, 65535});
    ASSERT_TRUE(fixture.engine->activate(windowDefinition(false)).hasValue());
    auto result = fixture.engine->process(raw);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(samples(*result.value()->enhanced), (std::vector<std::uint16_t>{0,16384,32768,65535}));
    ASSERT_EQ(result.value()->enhanced->timings.stages.size(), 4U);
    EXPECT_EQ(result.value()->enhanced->timings.stages[1].stageId, "original_window_level");
    EXPECT_EQ(copyBytes(result.value()->originalDisplay->pixels),
        (std::vector<std::byte>{std::byte{0},std::byte{128},std::byte{255},std::byte{255}}));
    EXPECT_EQ(copyBytes(result.value()->enhancedDisplay->pixels),
        (std::vector<std::byte>{std::byte{0},std::byte{64},std::byte{128},std::byte{255}}));
    ASSERT_TRUE(fixture.engine->activate(windowDefinition(true, 5)).hasValue());
    result = fixture.engine->process(raw);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(samples(*result.value()->enhanced), (std::vector<std::uint16_t>{0,32768,65535,65535}));
    EXPECT_EQ(result.value()->enhanced->pipelineVersion.configurationRevision, 5U);
    EXPECT_EQ(result.value()->enhanced->timings.stages[1].stageId, "shared_window_level");
    EXPECT_GE(result.value()->enhanced->timings.total, std::chrono::nanoseconds::zero());
    auto omitted = defaultPipeline(); omitted.stages.erase(omitted.stages.begin() + 1);
    ASSERT_TRUE(fixture.engine->activate(omitted).hasValue());
    result = fixture.engine->process(raw);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(copyBytes(result.value()->originalDisplay->pixels), copyBytes(result.value()->enhancedDisplay->pixels));
    EXPECT_EQ(samples(*result.value()->enhanced), (std::vector<std::uint16_t>{0,16384,32768,65535}));
}
TEST(FrameProcessingEngine, ActivationOwnsMetadataSnapshotsAndRejectsInvalidDefinitions) {
    ProcessingPipeline pipeline;
    auto definition=windowDefinition(true);
    ASSERT_TRUE(pipeline.activate(definition).hasValue());
    auto previous=pipeline.snapshot();
    definition.version.configurationRevision=99;
    for(auto& stage:definition.stages) stage.enabled=true;
    ASSERT_TRUE(pipeline.activate(definition).hasValue());
    EXPECT_EQ(previous->definition().version.configurationRevision,4U);
    auto current=pipeline.snapshot();
    EXPECT_EQ(current->enabledStages().size(),8U);
    auto invalid=definition; std::swap(invalid.stages[0],invalid.stages[1]);
    EXPECT_FALSE(pipeline.activate(invalid).hasValue());
    invalid=definition; invalid.stages[1].parameters=WindowLevelParameters{0,1};
    EXPECT_FALSE(pipeline.activate(invalid).hasValue());
    EXPECT_EQ(pipeline.snapshot(),current);
}
TEST(FrameProcessingEngine, OpaquePlanRemainsReusableAfterMoveExpression) {
    auto assessed=FrameProcessingEngine::plan({9,8},{16,4},layout(16)); ASSERT_TRUE(assessed.plan);
    auto first=*assessed.plan;
    auto second=std::move(first);
    auto u16=core::BufferPool::create(9,8).value(); auto gray=core::BufferPool::create(16,4).value();
    auto engine=FrameProcessingEngine::create(*u16,*gray,first);
    ASSERT_TRUE(engine.hasValue());
    EXPECT_EQ(first.resources().requiredStorageBytes,second.resources().requiredStorageBytes);
}
TEST(FrameProcessingEngine, RetainedRawCanonicalAndDisplaysNeverChangeAcrossFrames) {
    Fixture fixture(12, 2, 2);
    auto raw = rawFrame(12, {0,100,2048,4095}, 1, 1, 2);
    const auto before = copyBytes(raw->pixels);
    auto first = fixture.engine->process(raw);
    ASSERT_TRUE(first.hasValue());
    const auto canonical = copyBytes(first.value()->enhanced->pixels);
    const auto original = copyBytes(first.value()->originalDisplay->pixels);
    const auto enhanced = copyBytes(first.value()->enhancedDisplay->pixels);
    for (std::uint64_t id = 2; id < 12; ++id) {
        auto next = fixture.engine->process(rawFrame(12, {4095,2048,100,0}, id, 3, 2));
        ASSERT_TRUE(next.hasValue());
        EXPECT_EQ(copyBytes(raw->pixels), before);
        EXPECT_EQ(digest(copyBytes(raw->pixels)), digest(before));
        EXPECT_EQ(copyBytes(first.value()->enhanced->pixels), canonical);
        EXPECT_EQ(copyBytes(first.value()->originalDisplay->pixels), original);
        EXPECT_EQ(copyBytes(first.value()->enhancedDisplay->pixels), enhanced);
    }
    first.value().reset(); fixture.engine.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, ReplenishmentIsBoundedTypedAndRecoversAfterRetainedOwnersRelease) {
    for (bool exhaustU16 : {true, false}) {
        Fixture fixture(12, 4, 1, exhaustU16 ? 2 : 9, exhaustU16 ? 16 : 2);
        auto first = fixture.engine->process(rawFrame(12, {0,100,2048,4095}));
        ASSERT_TRUE(first.hasValue());
        auto next = fixture.engine->process(rawFrame(12, {1,2,3,4}, 2));
        ASSERT_FALSE(next.hasValue());
        EXPECT_EQ(next.error().category, core::ErrorCategory::ResourceExhaustion);
        EXPECT_EQ(next.error().code, exhaustU16 ? processingBufferPoolExhaustedCode : displayBufferPoolExhaustedCode);
        // A failed Gray8 replenishment must not retain its new U16 acquisition.
        EXPECT_EQ(fixture.u16->stats().inUse, 2U);
        EXPECT_EQ(fixture.gray->stats().inUse, 2U);
        first.value().reset();
        ASSERT_TRUE(fixture.engine->process(rawFrame(12, {1,2,3,4}, 3)).hasValue());
        fixture.engine.reset();
        EXPECT_EQ(fixture.u16->stats().inUse, 0U);
        EXPECT_EQ(fixture.gray->stats().inUse, 0U);
    }
}
TEST(FrameProcessingEngine, RejectsInvalidInputWithoutPublishingPartiallyWrittenSamples) {
    Fixture fixture;
    EXPECT_FALSE(fixture.engine->process(nullptr).hasValue());
    EXPECT_FALSE(fixture.engine->process(rawFrame(8, {0,1,2,3})).hasValue());
    EXPECT_FALSE(fixture.engine->process(rawFrame(12, {0,1})).hasValue());
    for (double fps : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        EXPECT_FALSE(fixture.engine->process(rawFrame(12, {0,1,2,3}, 1, 0, 1, fps)).hasValue());
    EXPECT_FALSE(fixture.engine->process(rawFrame(12, {0,100,4096,2})).hasValue());
    auto valid = fixture.engine->process(rawFrame(12, {0,0,0,0}));
    ASSERT_TRUE(valid.hasValue());
    EXPECT_EQ(samples(*valid.value()->enhanced), (std::vector<std::uint16_t>{0,0,0,0}));
}
TEST(FrameProcessingEngine, PreparationFailsForSmallPoolsWithoutLeakingPartialAcquisitions) {
    auto u16 = core::BufferPool::create(2, 8).value();
    auto gray = core::BufferPool::create(1, 4).value();
    auto created = FrameProcessingEngine::create(*u16, *gray, layout(12));
    EXPECT_FALSE(created.hasValue());
    EXPECT_EQ(u16->stats().inUse, 0U); EXPECT_EQ(gray->stats().inUse, 0U);
    auto tiny = core::BufferPool::create(4, 1).value();
    EXPECT_FALSE(FrameProcessingEngine::create(*tiny, *gray, layout(12)).hasValue());
    EXPECT_FALSE(FrameProcessingEngine::create(*u16, *tiny, layout(12)).hasValue());
    auto bad = defaultPipeline(); bad.stages[2].enabled = true;
    EXPECT_FALSE(FrameProcessingEngine::create(*u16, *gray, layout(12), bad).hasValue());
    EXPECT_EQ(u16->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, ConcurrentActivationKeepsEveryBundleOnOneConfiguration) {
    Fixture fixture(16, 257, 63);
    auto raw = rawFrame(16, std::vector<std::uint16_t>(257 * 63, 16384), 1, 0, 63);
    std::barrier start{2};
    std::jthread writer([&] {
        start.arrive_and_wait();
        for (std::uint64_t revision = 1; revision <= 64; ++revision)
            EXPECT_TRUE(fixture.engine->activate(windowDefinition(revision % 2 != 0, revision)).hasValue());
    });
    start.arrive_and_wait();
    for (int frame = 0; frame < 32; ++frame) {
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        const auto& bundle = result.value();
        const auto revision = bundle->enhanced->pipelineVersion.configurationRevision;
        EXPECT_EQ(bundle->originalDisplay->mapping.configurationRevision, revision);
        EXPECT_EQ(bundle->enhancedDisplay->mapping.configurationRevision, revision);
        const std::uint16_t expected = revision % 2 ? 32768 : 16384;
        EXPECT_TRUE(std::ranges::all_of(samples(*bundle->enhanced), [&](auto value) { return value == expected; }));
        EXPECT_TRUE(std::ranges::all_of(bundle->originalDisplay->pixels.bytes(), [&](auto value) {
            return value == (revision ? std::byte{128} : std::byte{64});
        }));
    }
    writer.join();
    auto final = fixture.engine->process(raw);
    ASSERT_TRUE(final.hasValue());
    EXPECT_EQ(final.value()->enhanced->pipelineVersion.configurationRevision, 64U);
}
TEST(FrameProcessingEngine, ProductionEnvelopeRetainsEightBundlesAndDetachedChildrenAfterEngineDestruction) {
    Fixture fixture;
    auto raw = rawFrame(12, {0,100,2048,4095});
    std::array<std::shared_ptr<const core::FrameBundle>, 8> retained{};
    for (auto& output : retained) {
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        output = std::move(result).value();
    }
    EXPECT_EQ(fixture.u16->stats().inUse, 9U);
    EXPECT_EQ(fixture.gray->stats().inUse, 16U);
    EXPECT_FALSE(fixture.engine->process(raw).hasValue());
    auto child = retained[0]->enhanced;
    const auto expected = copyBytes(child->pixels);
    retained[1].reset();
    ASSERT_TRUE(fixture.engine->process(raw).hasValue());
    fixture.engine.reset();
    for (const auto& output : retained) {
        if (!output) continue;
        EXPECT_EQ(output->raw, raw);
        EXPECT_EQ(copyBytes(output->enhanced->pixels), expected);
        const auto& timings = output->enhanced->timings.stages;
        ASSERT_EQ(timings.size(), 4U);
        EXPECT_EQ(timings[0].stageId, "normalize");
        EXPECT_EQ(timings[1].stageId, "shared_window_level");
        EXPECT_EQ(timings[2].stageId, "original_display_map");
        EXPECT_EQ(timings[3].stageId, "enhanced_display_map");
    }
    std::jthread release([owners = std::move(retained)]() mutable { for (auto& owner : owners) owner.reset(); });
    release.join();
    EXPECT_EQ(fixture.u16->stats().inUse, 1U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
    EXPECT_EQ(copyBytes(child->pixels), expected);
    child.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, ExpiredWeakOwnersExhaustFinalBundleControlAndRecoveryPreservesPublishedOutput) {
    Fixture fixture;
    auto raw = rawFrame(12, {0,100,2048,4095});
    auto published = fixture.engine->process(raw).value();
    const auto expected = copyBytes(published->enhanced->pixels);
    std::array<std::weak_ptr<const core::FrameBundle>, 27> weak{};
    for (auto& retained : weak) {
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        retained = result.value();
    }
    // 27 expired controls + 4 published controls leave exactly 3: all children
    // construct, then final bundle control acquisition fails in the 34-slot pool.
    for (int retry = 0; retry < 10; ++retry) {
        auto failed = fixture.engine->process(raw);
        ASSERT_FALSE(failed.hasValue());
        EXPECT_EQ(failed.error().code, "frame_control_pool_exhausted");
        EXPECT_EQ(copyBytes(published->enhanced->pixels), expected);
        EXPECT_EQ(published->raw, raw);
        EXPECT_EQ(fixture.u16->stats().inUse, 2U);
        EXPECT_EQ(fixture.gray->stats().inUse, 2U);
    }
    weak[0].reset();
    ASSERT_TRUE(fixture.engine->process(raw).hasValue());
    published.reset();
    fixture.engine.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
    for (auto& retained : weak) { EXPECT_TRUE(retained.expired()); retained.reset(); }
}
}  // namespace
}  // namespace lumora::processing
