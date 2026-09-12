#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

namespace {
using namespace lumora;
using namespace std::chrono_literals;
core::SourcePixelFormat mono8() { return {"Mono8",0x01080001U,8U,255U,core::SourcePacking::Unpacked,core::BitAlignment::LeastSignificant,core::StorageType::UInt8}; }
core::SourcePixelFormat mono12() { return {"Mono12",0x01100005U,12U,4095U,core::SourcePacking::Unpacked,core::BitAlignment::LeastSignificant,core::StorageType::UInt16}; }
camera::CameraConfiguration initialRequest() {
    return {mono8(),{0,0,64,48},30.0,{camera::ExposureMode::Manual,100.0},{camera::GainMode::Manual,0.0},camera::AcquisitionMode::Continuous};
}
camera::CameraConfiguration changedRequest() { auto r=initialRequest();r.roi={4,6,32,24};r.pixelFormat=mono12();return r; }
camera::sim::SimulatedCameraOptions simulatorOptions() {
    return {{"ROI-SIM"},{{mono8(),mono12()},{{0,0,1,1},{128,96,128,96},{1,1,1,1}},
        {1,60,1,false},{1,1000,1,false},{camera::ExposureMode::Manual},{0,10,1,false},{camera::GainMode::Manual}},
        camera::sim::SimulationPattern::Gradient,30.0,42U,camera::sim::SimulationPacingMode::RealTime};
}
// Forward every operation to a real simulator. Only deliberately faulty camera
// readback/mutation cases are injected here; frames and processing stay real.
struct DeviceObservations {
    std::atomic<unsigned> opens{0}, applies{0};
    std::atomic<bool> mismatch{false}, rejectAfterMutation{false}, failRestore{false}, driftRestore{false};
    std::atomic<bool> blockApply{false}, applyEntered{false}, releaseApply{false};
};
class ObservedDevice final : public camera::ICameraDevice {
    std::unique_ptr<camera::ICameraDevice> inner;
    DeviceObservations& observations;
public:
    ObservedDevice(std::unique_ptr<camera::ICameraDevice> d,DeviceObservations& o):inner(std::move(d)),observations(o) {}
    core::Result<void> open() override { ++observations.opens;return inner->open(); }
    core::Result<camera::CameraCapabilities> capabilities() override { return inner->capabilities(); }
    core::Result<camera::AppliedCameraConfiguration> applyConfiguration(const camera::CameraConfiguration& request) override {
        ++observations.applies;
        if(observations.failRestore && request.roi.width==64U) return core::Result<camera::AppliedCameraConfiguration>::failure(
            {core::ErrorCategory::CameraConfiguration,"restore_failed","Restore failed.","",false});
        auto result=inner->applyConfiguration(request);
        if(result.hasValue() && request.roi.width==32U) {
            if(observations.blockApply) {
                observations.applyEntered=true;
                while(!observations.releaseApply) std::this_thread::sleep_for(1ms);
            }
            if(observations.rejectAfterMutation) return core::Result<camera::AppliedCameraConfiguration>::failure(
                {core::ErrorCategory::CameraConfiguration,"apply_failed","Apply failed after mutation.","",false});
            if(observations.mismatch) result.value().actual.roi.x=5;
        }
        if(result.hasValue() && observations.driftRestore && request.roi.width==64U) result.value().actual.requestedFps=29.0;
        return result;
    }
    core::Result<void> startStream() override { return inner->startStream(); }
    core::Result<std::shared_ptr<const core::RawFrame>> retrieve(std::chrono::milliseconds timeout,core::BufferPool& pool,std::stop_token stop) override { return inner->retrieve(timeout,pool,stop); }
    core::Result<void> stopStream() noexcept override { return inner->stopStream(); }
    core::Result<void> close() noexcept override { return inner->close(); }
};
class ObservedProvider final : public camera::ICameraProvider {
    camera::sim::SimulatedCameraProvider inner;
public:
    DeviceObservations observations;
    explicit ObservedProvider(core::IClock& clock):inner(simulatorOptions(),clock) {}
    core::Result<std::vector<camera::CameraDescriptor>> discover(std::stop_token stop) override { return inner.discover(stop); }
    core::Result<std::unique_ptr<camera::ICameraDevice>> create(const camera::CameraId& id) override {
        auto result=inner.create(id);
        if(!result.hasValue()) return result;
        return core::Result<std::unique_ptr<camera::ICameraDevice>>::success(std::make_unique<ObservedDevice>(std::move(result).value(),observations));
    }
};
struct ReconfigurationFixture {
    core::SystemClock clock;
    ObservedProvider provider{clock};
    application::LivePipeline pipeline;
    std::uint64_t nextId{0};
    explicit ReconfigurationFixture(processing::ProcessingPreparationOptions options={},application::LivePipeline::ProcessorFactory factory={}):pipeline(provider,clock,initialRequest(),std::move(factory),options) {}
    ~ReconfigurationFixture() { pipeline.shutdown(); }
    bool wait(const std::function<bool()>& predicate) {
        const auto deadline=std::chrono::steady_clock::now()+3s;
        do { if(predicate()) return true;std::this_thread::sleep_for(1ms); } while(std::chrono::steady_clock::now()<deadline);
        return false;
    }
    bool command(application::CameraCommand command,bool success=true) {
        const auto id=command.requestId;auto posted=pipeline.post(std::move(command));
        if(!posted.hasValue()) { ADD_FAILURE()<<posted.error().code;return false; }
        if(!wait([&]{auto s=pipeline.snapshot();return (s.ordinaryOutcome && s.ordinaryOutcome->requestId==id)||(s.priorityOutcome && s.priorityOutcome->requestId==id);})) return false;
        auto s=pipeline.snapshot();const auto& o=s.ordinaryOutcome && s.ordinaryOutcome->requestId==id ? s.ordinaryOutcome : s.priorityOutcome;
        if(success && o->error) { ADD_FAILURE()<<o->error->code;return false; }
        return success ? !o->error : o->error.has_value();
    }
    std::uint64_t generation() { return pipeline.snapshot().camera->sessionGeneration; }
    bool initialize() {
        return pipeline.start().hasValue() && wait([&]{return pipeline.snapshot().context!=nullptr;})
            && pipeline.acknowledgeContext(generation()).hasValue()
            && command({++nextId,application::Connect{{"ROI-SIM"}}})
            && command({++nextId,application::ApplyConfiguration{generation(),initialRequest(),1}});
    }
    bool start(std::uint64_t revision) {
        return command({++nextId,application::ConfirmConfiguration{generation(),revision}})
            && command({++nextId,application::StartStream{generation(),revision}});
    }
    std::shared_ptr<const core::FrameBundle> frame() {
        auto context=pipeline.snapshot().context;if(!context) return {};
        auto result=context->bundleSlot.consumeAfter(0);return result ? result->value : nullptr;
    }
};
TEST(CameraReconfiguration, ChangesFullRoiAndStorageOnSameDeviceWithIncreasingFramesAndFreshConfirmation) {
    ReconfigurationFixture f;ASSERT_TRUE(f.initialize());ASSERT_TRUE(f.start(1));ASSERT_TRUE(f.wait([&]{return f.frame()!=nullptr;}));
    auto old=f.frame();auto oldContext=f.pipeline.snapshot().context;auto oldBytes=std::vector<std::byte>(old->raw->pixels.bytes().begin(),old->raw->pixels.bytes().end());
    ASSERT_TRUE(f.command({++f.nextId,application::StopStream{}}));auto oldGeneration=f.generation();
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{oldGeneration,changedRequest(),2}}));
    auto s=f.pipeline.snapshot();ASSERT_NE(s.context,oldContext);EXPECT_EQ(s.camera->sessionGeneration,s.context->generation);EXPECT_GT(s.context->generation,oldGeneration);
    EXPECT_FALSE(s.contextBound);EXPECT_FALSE(s.camera->confirmedRevision);EXPECT_EQ(s.camera->appliedConfiguration->actual.roi.x,4U);
    EXPECT_FALSE(f.pipeline.acknowledgeContext(oldGeneration).hasValue());
    ASSERT_TRUE(f.command({++f.nextId,application::StartStream{f.generation(),2}},false));
    ASSERT_TRUE(f.pipeline.acknowledgeContext(f.generation()).hasValue());
    ASSERT_TRUE(f.command({++f.nextId,application::StartStream{oldGeneration,1}},false));
    ASSERT_TRUE(f.command({++f.nextId,application::StartStream{f.generation(),2}},false));
    ASSERT_TRUE(f.start(2));ASSERT_TRUE(f.wait([&]{return f.frame()!=nullptr;}));
    auto current=f.frame();ASSERT_EQ(current->raw->layout.width(),32U);EXPECT_EQ(current->raw->layout.height(),24U);EXPECT_EQ(current->raw->layout.storage(),core::StorageType::UInt16);
    EXPECT_EQ(current->raw->metadata.acquisitionSettings.roi.y,6U);EXPECT_EQ(current->raw->metadata.acquisitionSettings.sourceFormat.validBits,12U);
    EXPECT_GT(current->sourceFrameId(),old->sourceFrameId());EXPECT_EQ(f.provider.observations.opens,1U);
    EXPECT_EQ(std::vector<std::byte>(old->raw->pixels.bytes().begin(),old->raw->pixels.bytes().end()),oldBytes);
}
TEST(CameraReconfiguration, PreservesAcceptedProcessingDefinitionAndSameModeContext) {
    ReconfigurationFixture f;ASSERT_TRUE(f.initialize());auto definition=processing::defaultPipeline();definition.version.configurationRevision=7;definition.stages.back().enabled=true;
    ASSERT_TRUE(f.pipeline.setProcessingConfiguration({f.generation(),definition}).hasValue());
    ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().processingConfigurationOutcome.has_value();}));ASSERT_FALSE(f.pipeline.snapshot().processingConfigurationOutcome->error);
    auto context=f.pipeline.snapshot().context;auto same=initialRequest();same.requestedFps=20;
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),same,2}}));EXPECT_EQ(f.pipeline.snapshot().context,context);
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),changedRequest(),3}}));
    ASSERT_TRUE(f.pipeline.acknowledgeContext(f.generation()).hasValue());ASSERT_TRUE(f.start(3));ASSERT_TRUE(f.wait([&]{return f.frame()!=nullptr;}));
    auto frame=f.frame();ASSERT_TRUE(frame->enhanced);EXPECT_EQ(frame->enhanced->pipelineVersion.configurationRevision,7U);
}
TEST(CameraReconfiguration, CandidateOverBudgetIsRejectedBeforeCameraMutation) {
    processing::ProcessingPreparationOptions options;options.activationEnvelopeBytes=1024U*1024U;
    ReconfigurationFixture probe(options);ASSERT_TRUE(probe.initialize());options.storageBudgetBytes=probe.pipeline.snapshot().resources->requiredStorageBytes;
    ReconfigurationFixture f(options);ASSERT_TRUE(f.initialize());auto context=f.pipeline.snapshot().context;
    auto oversized=initialRequest();oversized.roi={0,0,128,96};
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),oversized,2}},false));
    EXPECT_EQ(f.provider.observations.applies,1U);EXPECT_EQ(f.pipeline.snapshot().context,context);EXPECT_EQ(f.pipeline.snapshot().camera->appliedRevision,1U);
    EXPECT_EQ(f.pipeline.snapshot().ordinaryOutcome->error->code,"processing_resource_budget_exceeded");
}
TEST(CameraReconfiguration, MismatchedReadbackRestoresPreviousActualAndClearsConfirmation) {
    ReconfigurationFixture f;ASSERT_TRUE(f.initialize());ASSERT_TRUE(f.command({++f.nextId,application::ConfirmConfiguration{f.generation(),1}}));
    auto context=f.pipeline.snapshot().context;f.provider.observations.mismatch=true;
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),changedRequest(),2}},false));
    auto s=f.pipeline.snapshot();EXPECT_EQ(s.context,context);EXPECT_EQ(s.camera->state,application::CameraSessionState::ConnectedIdle);EXPECT_FALSE(s.camera->confirmedRevision);EXPECT_EQ(f.provider.observations.applies,3U);
    EXPECT_EQ(s.camera->appliedConfiguration->actual.roi.width,64U);
    ASSERT_TRUE(f.command({++f.nextId,application::ConfirmConfiguration{f.generation(),1}},false));
    f.provider.observations.mismatch=false;ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),initialRequest(),3}}));ASSERT_TRUE(f.start(3));ASSERT_TRUE(f.wait([&]{return f.frame()!=nullptr;}));EXPECT_EQ(f.frame()->raw->layout.width(),64U);
}
TEST(CameraReconfiguration, RejectedApplyAfterMutationRestoresPreviousActual) {
    ReconfigurationFixture f;ASSERT_TRUE(f.initialize());f.provider.observations.rejectAfterMutation=true;
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),changedRequest(),2}},false));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);EXPECT_EQ(f.provider.observations.applies,3U);EXPECT_EQ(f.provider.observations.opens,1U);
}
TEST(CameraReconfiguration, UnverifiableRestoreRetiresUnsafeDevice) {
    for(bool drift:{false,true}) {
        ReconfigurationFixture f;ASSERT_TRUE(f.initialize());f.provider.observations.mismatch=true;
        f.provider.observations.failRestore=!drift;f.provider.observations.driftRestore=drift;
        ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),changedRequest(),2}},false));
        auto s=f.pipeline.snapshot();EXPECT_TRUE(s.camera->sourceReplacementRequired);EXPECT_FALSE(s.camera->appliedConfiguration);EXPECT_EQ(s.camera->state,application::CameraSessionState::Error);
    }
}
TEST(CameraReconfiguration, StaleAndStreamingAppliesNeverMutateCameraAndHandoffBlocksSecondRebind) {
    ReconfigurationFixture f;ASSERT_TRUE(f.initialize());auto original=f.generation();
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{original+10,changedRequest(),2}},false));EXPECT_EQ(f.provider.observations.applies,1U);
    ASSERT_TRUE(f.start(1));EXPECT_FALSE(f.pipeline.post({++f.nextId,application::ApplyConfiguration{original,changedRequest(),2}}).hasValue());EXPECT_EQ(f.provider.observations.applies,1U);
    ASSERT_TRUE(f.command({++f.nextId,application::StopStream{}}));ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{original,changedRequest(),2}}));
    auto posted=f.pipeline.post({++f.nextId,application::ApplyConfiguration{f.generation(),initialRequest(),3}});
    if(posted.hasValue()) { ASSERT_TRUE(f.wait([&]{auto s=f.pipeline.snapshot();return s.ordinaryOutcome && s.ordinaryOutcome->requestId==f.nextId;}));EXPECT_TRUE(f.pipeline.snapshot().ordinaryOutcome->error); }
    EXPECT_EQ(f.provider.observations.applies,2U);
}

TEST(CameraReconfiguration, ProcessorPreparationFailureLeavesCameraAndContextUntouched) {
    unsigned preparations=0;
    ReconfigurationFixture f({},[&](core::BufferPool& p,core::BufferPool& d,const core::ImageLayout& layout) {
        if(++preparations==2U) return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(
            {core::ErrorCategory::ResourceExhaustion,"candidate_allocation_failed","Candidate unavailable.","",true});
        auto engine=processing::FrameProcessingEngine::create(p,d,layout,processing::defaultPipeline());
        if(!engine.hasValue()) return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(engine.error());
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::move(engine).value());
    });
    ASSERT_TRUE(f.initialize());auto context=f.pipeline.snapshot().context;
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),changedRequest(),2}},false));
    EXPECT_EQ(f.provider.observations.applies,1U);EXPECT_EQ(f.pipeline.snapshot().context,context);
    EXPECT_EQ(f.pipeline.snapshot().ordinaryOutcome->error->code,"candidate_allocation_failed");
}
TEST(CameraReconfiguration, DisconnectDuringCandidatePreparationCancelsApplyWithoutMutation) {
    std::atomic<unsigned> preparations{0};std::atomic<bool> preparing{false},release{false};
    ReconfigurationFixture f({},[&](core::BufferPool& p,core::BufferPool& d,const core::ImageLayout& layout) {
        if(++preparations==2U) {
            preparing=true;
            while(!release) std::this_thread::sleep_for(1ms);
        }
        auto engine=processing::FrameProcessingEngine::create(p,d,layout,processing::defaultPipeline());
        if(!engine.hasValue()) return core::Result<std::unique_ptr<processing::IFrameProcessor>>::failure(engine.error());
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::move(engine).value());
    });
    // Release a blocked factory before fixture teardown, even after an assertion.
    struct Release { std::atomic<bool>& flag;~Release() { flag=true; } } releaseOnExit{release};
    ASSERT_TRUE(f.initialize());auto applyId=++f.nextId;
    ASSERT_TRUE(f.pipeline.post({applyId,application::ApplyConfiguration{f.generation(),changedRequest(),2}}).hasValue());
    ASSERT_TRUE(f.wait([&]{return preparing.load();}));auto disconnectId=++f.nextId;
    ASSERT_TRUE(f.pipeline.post({disconnectId,application::Disconnect{}}).hasValue());release=true;
    ASSERT_TRUE(f.wait([&]{auto s=f.pipeline.snapshot();return s.priorityOutcome && s.priorityOutcome->requestId==disconnectId;}));
    auto s=f.pipeline.snapshot();ASSERT_TRUE(s.ordinaryOutcome);EXPECT_EQ(s.ordinaryOutcome->requestId,applyId);ASSERT_TRUE(s.ordinaryOutcome->error);
    EXPECT_EQ(s.ordinaryOutcome->error->category,core::ErrorCategory::Cancelled);EXPECT_EQ(f.provider.observations.applies,1U);
    EXPECT_EQ(s.camera->state,application::CameraSessionState::Disconnected);
}

TEST(CameraReconfiguration, InFlightApplyStillPublishesMatchingContextWhenStopSupersedesItsOutcome) {
    ReconfigurationFixture f;ASSERT_TRUE(f.initialize());auto old=f.pipeline.snapshot();
    f.provider.observations.blockApply=true;
    struct Release { std::atomic<bool>& flag;~Release() { flag=true; } } releaseOnExit{f.provider.observations.releaseApply};
    const auto applyId=++f.nextId;
    ASSERT_TRUE(f.pipeline.post({applyId,application::ApplyConfiguration{f.generation(),changedRequest(),2}}).hasValue());
    ASSERT_TRUE(f.wait([&]{return f.provider.observations.applyEntered.load();}));
    const auto stopId=++f.nextId;ASSERT_TRUE(f.pipeline.post({stopId,application::StopStream{}}).hasValue());
    ASSERT_TRUE(f.wait([&]{auto s=f.pipeline.snapshot();return s.ordinaryOutcome && s.ordinaryOutcome->requestId==applyId;}));
    ASSERT_TRUE(f.pipeline.snapshot().ordinaryOutcome->error);
    f.provider.observations.releaseApply=true;
    bool coherent=true;
    ASSERT_TRUE(f.wait([&]{auto s=f.pipeline.snapshot();coherent=coherent && s.camera->sessionGeneration==s.context->generation;
        return s.priorityOutcome && s.priorityOutcome->requestId==stopId;}));
    auto current=f.pipeline.snapshot();EXPECT_TRUE(coherent);ASSERT_NE(current.context,old.context);
    EXPECT_EQ(current.camera->state,application::CameraSessionState::ConnectedIdle);EXPECT_FALSE(current.contextBound);
    ASSERT_TRUE(current.ordinaryOutcome->error);EXPECT_EQ(current.ordinaryOutcome->error->category,core::ErrorCategory::Cancelled);
    ASSERT_TRUE(f.pipeline.acknowledgeContext(f.generation()).hasValue());ASSERT_TRUE(f.start(2));ASSERT_TRUE(f.wait([&]{return f.frame()!=nullptr;}));
    EXPECT_EQ(f.frame()->raw->layout.width(),32U);EXPECT_EQ(f.provider.observations.opens,1U);
}
TEST(CameraReconfiguration, MismatchedFirstApplyRetiresDeviceWithoutPreviousActualToRestore) {
    ReconfigurationFixture f;ASSERT_TRUE(f.pipeline.start().hasValue());ASSERT_TRUE(f.wait([&]{return f.pipeline.snapshot().context!=nullptr;}));
    ASSERT_TRUE(f.pipeline.acknowledgeContext(f.generation()).hasValue());ASSERT_TRUE(f.command({++f.nextId,application::Connect{{"ROI-SIM"}}}));
    f.provider.observations.mismatch=true;
    ASSERT_TRUE(f.command({++f.nextId,application::ApplyConfiguration{f.generation(),changedRequest(),1}},false));
    auto s=f.pipeline.snapshot();EXPECT_TRUE(s.camera->sourceReplacementRequired);EXPECT_FALSE(s.camera->appliedConfiguration);
    EXPECT_EQ(s.camera->state,application::CameraSessionState::Error);EXPECT_EQ(f.provider.observations.applies,1U);
}
}
