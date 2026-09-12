#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/processing/Mono8PassThroughProcessor.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace lumora;
using namespace std::chrono_literals;
const core::Orientation rotated{true,false,core::Rotation::Degrees90};
camera::CameraConfiguration request() {
    return {{"Mono8",0x01080001U,8U,255U,core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant,core::StorageType::UInt8},
        {0,0,8,6},30.0,{camera::ExposureMode::Manual,100.0},
        {camera::GainMode::Manual,0.0},camera::AcquisitionMode::Continuous};
}
camera::sim::SimulatedCameraOptions options() {
    return {{"SIM-INSTALL"}, {{request().pixelFormat},{{0,0,1,1},{0,0,8,6},{1,1,1,1}},
        {1,60,1,false},{1,1000,1,false},{camera::ExposureMode::Manual},
        {0,10,1,false},{camera::GainMode::Manual}},camera::sim::SimulationPattern::Ramp,
        30.0,0x4C554D4FU,camera::sim::SimulationPacingMode::Manual};
}
class Repository final : public application::IInstallationProfiles {
    mutable std::mutex mutex;
    application::InstallationProfilesSnapshot state;
    std::optional<application::InstallationCameraProfile> pending;
    std::uint64_t pendingId{};
public:
    Repository() { state.loadCompleted=true;state.administratorMode=true;
        state.policy=application::InstallationProfilePolicy::SimulatorIdentityFallback; }
    std::shared_ptr<const application::InstallationProfilesSnapshot> latestStatus() const override {
        std::lock_guard lock(mutex);return std::make_shared<const application::InstallationProfilesSnapshot>(state);
    }
    core::Result<void> postSave(std::uint64_t id,application::InstallationCameraProfile profile,bool) override {
        std::lock_guard lock(mutex);pending=std::move(profile);pendingId=id;state.savePending=true;
        return core::Result<void>::success();
    }
    void edit(const std::function<void(application::InstallationProfilesSnapshot&)>& action) {
        std::lock_guard lock(mutex);action(state);
    }
    bool submitted() const { std::lock_guard lock(mutex);return pending.has_value(); }
    void finish(bool success=true) {
        std::lock_guard lock(mutex);
        if(success) { state.profiles={*pending};state.latestSaveOutcome=application::InstallationSaveOutcome{pendingId,pending,{}}; }
        else state.latestSaveOutcome=application::InstallationSaveOutcome{pendingId,{},core::Error{core::ErrorCategory::Configuration,"save_failed","Save failed.","",true}};
        pending.reset();state.savePending=false;
    }
};
struct Harness {
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider{options(),clock};
    Repository repository;
    application::LivePipeline pipeline;
    std::uint64_t sequence{},revision{};
    Harness(application::LivePipeline::ProcessorFactory factory={},processing::ProcessingPreparationOptions preparation={})
        :pipeline(provider,clock,request(),std::move(factory),preparation,&repository) {}
    ~Harness() { pipeline.shutdown(); }
    bool wait(const std::function<bool()>& predicate) {
        const auto deadline=std::chrono::steady_clock::now()+3s;
        do { if(predicate()) return true;std::this_thread::yield(); } while(std::chrono::steady_clock::now()<deadline);
        return false;
    }
    std::uint64_t generation() { return pipeline.snapshot().camera->sessionGeneration; }
    template<class Payload> bool send(Payload payload,bool priority=false) {
        const auto id=++sequence;
        if(!pipeline.post({id,std::move(payload)}).hasValue()) return false;
        if(!wait([&]{auto s=pipeline.snapshot();auto o=priority?s.priorityOutcome:s.ordinaryOutcome;return o&&o->requestId==id;})) return false;
        auto s=pipeline.snapshot();return !(priority?s.priorityOutcome:s.ordinaryOutcome)->error;
    }
    bool connect() {
        if(!pipeline.start().hasValue() || !wait([&]{return pipeline.snapshot().context!=nullptr;})) return false;
        if(!pipeline.acknowledgeContext(generation()).hasValue()) return false;
        return send(application::Discover{})&&send(application::Connect{{"SIM-INSTALL"}});
    }
    bool apply() { return send(application::ApplyConfiguration{generation(),request(),++revision}); }
    bool confirm() { return send(application::ConfirmConfiguration{generation(),revision}); }
    bool start() { return send(application::StartStream{generation(),revision}); }
    bool save(core::Orientation orientation=rotated) {
        return pipeline.saveInstallationProfile({++sequence,generation(),{"SIM-INSTALL"},orientation,true}).hasValue();
    }
    std::shared_ptr<const core::FrameBundle> frame() {
        auto value=pipeline.snapshot().context->bundleSlot.consumeAfter(0);return value?value->value:nullptr;
    }
};
TEST(InstallationPipeline, RequiredMissingProfileBlocksApply) {
    Harness f;f.repository.edit([](auto& s){s.policy=application::InstallationProfilePolicy::Required;});
    ASSERT_TRUE(f.connect());EXPECT_FALSE(f.apply());EXPECT_FALSE(f.pipeline.snapshot().camera->appliedConfiguration);
}
TEST(InstallationPipeline, InvalidRepositoryNeverUsesSimulatorFallback) {
    Harness f;f.repository.edit([](auto& s){s.loadError=core::Error{core::ErrorCategory::Configuration,"invalid_machine","Invalid machine file.","",false};});
    ASSERT_TRUE(f.connect());EXPECT_FALSE(f.apply());
}
TEST(InstallationPipeline, SaveIsDurableOnlyAndRequiresApplyBeforeConfirmAndStart) {
    Harness f;ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());ASSERT_TRUE(f.confirm());
    auto context=f.pipeline.snapshot().context;
    ASSERT_TRUE(f.save());ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));
    EXPECT_TRUE(f.pipeline.snapshot().installationProfilePending);
    EXPECT_FALSE(f.apply());EXPECT_FALSE(f.confirm());EXPECT_FALSE(f.start());
    f.repository.finish();ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));
    EXPECT_EQ(f.pipeline.snapshot().context,context);EXPECT_FALSE(f.pipeline.snapshot().activeInstallationProfile);
    EXPECT_FALSE(f.confirm());EXPECT_FALSE(f.start());
    ASSERT_TRUE(f.apply());EXPECT_NE(f.pipeline.snapshot().context,context);
    EXPECT_EQ(f.pipeline.snapshot().activeOrientation,rotated);
    ASSERT_TRUE(f.pipeline.snapshot().activeInstallationProfile);EXPECT_EQ(f.pipeline.snapshot().activeInstallationProfile->revision,1U);
    ASSERT_TRUE(f.confirm());EXPECT_FALSE(f.start());
    ASSERT_TRUE(f.pipeline.acknowledgeContext(f.generation()).hasValue());ASSERT_TRUE(f.start());
}
TEST(InstallationPipeline, LateSaveAfterDisconnectNeverActivates) {
    Harness f;ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());ASSERT_TRUE(f.save());
    ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));
    ASSERT_TRUE(f.send(application::StopStream{},true));ASSERT_TRUE(f.send(application::Disconnect{},true));
    f.repository.finish();ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));
    EXPECT_FALSE(f.pipeline.snapshot().activeInstallationProfile);
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
}
TEST(InstallationPipeline, RejectedInstallationCommandsNeverReachRepository) {
    Harness f;ASSERT_TRUE(f.connect());
    for(int condition=0;condition<4;++condition) {
        application::InstallationProfileCommand command{++f.sequence,f.generation(),{"SIM-INSTALL"},rotated,true};
        if(condition==0) command.sessionGeneration++;
        if(condition==1) command.cameraId={"OTHER"};
        if(condition==2) command.confirmed=false;
        if(condition==3) f.repository.edit([](auto& s){s.administratorMode=false;});
        auto admitted=f.pipeline.saveInstallationProfile(command);
        if(admitted.hasValue()) { ASSERT_TRUE(f.wait([&]{auto o=f.pipeline.snapshot().installationProfileOutcome;return o&&o->requestId==command.requestId;})); }
        EXPECT_FALSE(f.repository.submitted());
    }
}
TEST(InstallationPipeline, OrientationReconfigurationPreservesRawFramesAndAcceptedProcessing) {
    Harness f;ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());ASSERT_TRUE(f.confirm());ASSERT_TRUE(f.start());
    ASSERT_TRUE(f.wait([&]{return f.frame()!=nullptr;}));auto before=f.frame();
    ASSERT_TRUE(f.send(application::StopStream{},true));
    auto definition=processing::defaultPipeline();definition.version.configurationRevision=17;definition.stages.back().enabled=true;
    ASSERT_TRUE(f.pipeline.setProcessingConfiguration({f.generation(),definition}).hasValue());
    ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().processingConfigurationPending;}));
    ASSERT_TRUE(f.save());ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));f.repository.finish();
    ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));ASSERT_TRUE(f.apply());
    ASSERT_TRUE(f.pipeline.acknowledgeContext(f.generation()).hasValue());ASSERT_TRUE(f.confirm());ASSERT_TRUE(f.start());
    f.clock.advance(34ms);ASSERT_TRUE(f.wait([&]{return f.frame()!=nullptr;}));auto after=f.frame();
    EXPECT_GT(after->sourceFrameId(),before->sourceFrameId());
    EXPECT_EQ(after->raw->layout.width(),8U);EXPECT_EQ(after->raw->layout.height(),6U);
    EXPECT_EQ(after->enhanced->layout.width(),8U);EXPECT_EQ(after->enhanced->layout.height(),6U);
    EXPECT_EQ(after->originalDisplay->layout.width(),6U);EXPECT_EQ(after->originalDisplay->layout.height(),8U);
    EXPECT_EQ(after->originalDisplay->presentationOrientation,rotated);EXPECT_EQ(after->enhancedDisplay->presentationOrientation,rotated);
    EXPECT_EQ(after->enhanced->pipelineVersion.configurationRevision,17U);
    EXPECT_EQ(std::vector<std::byte>(after->raw->pixels.bytes().begin(),after->raw->pixels.bytes().end()),
        std::vector<std::byte>(before->raw->pixels.bytes().begin(),before->raw->pixels.bytes().end()));
    for(std::size_t y=0;y<6;++y) for(std::size_t x=0;x<8;++x) {
        const auto source=before->originalDisplay->pixels.bytes()[y*before->originalDisplay->layout.strideBytes()+x];
        const auto transformed=after->originalDisplay->pixels.bytes()[(7-x)*after->originalDisplay->layout.strideBytes()+5-y];
        EXPECT_EQ(transformed,source);
    }
}
TEST(InstallationPipeline, FailedSaveRetainsConfirmedActiveBinding) {
    Harness f;ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());ASSERT_TRUE(f.confirm());
    ASSERT_TRUE(f.save());ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));f.repository.finish(false);
    ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));
    ASSERT_TRUE(f.start());EXPECT_FALSE(f.pipeline.snapshot().activeInstallationProfile);
}
TEST(InstallationPipeline, CapabilityDriftBlocksApplyAndStartEvenWithSavedConfirmation) {
    Harness f;ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());ASSERT_TRUE(f.confirm());
    ASSERT_TRUE(f.save({false,false,core::Rotation::Degrees0}));
    ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));f.repository.finish();
    ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));ASSERT_TRUE(f.apply());ASSERT_TRUE(f.confirm());
    f.repository.edit([](auto& status){status.profiles.front().capabilities.frameRate.maximum=59;});
    EXPECT_FALSE(f.start());EXPECT_FALSE(f.apply());
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
}
TEST(InstallationPipeline, StreamingAndDuplicateSavesNeverReachRepository) {
    Harness f;ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());ASSERT_TRUE(f.confirm());ASSERT_TRUE(f.start());
    ASSERT_TRUE(f.save());ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));
    EXPECT_FALSE(f.repository.submitted());ASSERT_TRUE(f.pipeline.snapshot().installationProfileOutcome->error);
    ASSERT_TRUE(f.send(application::StopStream{},true));ASSERT_TRUE(f.save());
    ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));EXPECT_FALSE(f.save());
    f.repository.finish();ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));
}
TEST(InstallationPipeline, OrientationResourceFailurePreservesCameraAndActiveContext) {
    processing::ProcessingPreparationOptions preparation;
    {
        Harness measure;
        ASSERT_TRUE(measure.connect()); ASSERT_TRUE(measure.apply()); ASSERT_TRUE(measure.confirm());
        const auto measured = measure.pipeline.snapshot().resources;
        ASSERT_TRUE(measured); EXPECT_EQ(measured->orientationBytes, 0U);
        // Freeze the admitted activation envelope. The automatic envelope shrinks
        // to absorb orientation scratch, so its total alone is not a failure fixture.
        preparation.activationEnvelopeBytes = measured->activationEnvelopeBytes;
        preparation.storageBudgetBytes = measured->requiredStorageBytes;
    }
    Harness f({},preparation);ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());ASSERT_TRUE(f.confirm());
    const auto before=f.pipeline.snapshot();
    ASSERT_TRUE(f.save());ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));f.repository.finish();
    ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));EXPECT_FALSE(f.apply());
    const auto after=f.pipeline.snapshot();
    EXPECT_EQ(after.context,before.context);EXPECT_EQ(after.camera->appliedRevision,before.camera->appliedRevision);
    EXPECT_EQ(after.camera->sessionGeneration,before.camera->sessionGeneration);
    EXPECT_EQ(after.activeInstallationProfile,before.activeInstallationProfile);
    ASSERT_TRUE(after.ordinaryOutcome->error);EXPECT_EQ(after.ordinaryOutcome->error->code,"processing_resource_budget_exceeded");
    EXPECT_FALSE(f.start());
}
TEST(InstallationPipeline, CustomProcessorCannotSilentlyIgnoreInstallationOrientation) {
    Harness f([](core::BufferPool&,core::BufferPool& display,const core::ImageLayout&) {
        return core::Result<std::unique_ptr<processing::IFrameProcessor>>::success(std::make_unique<processing::Mono8PassThroughProcessor>(display));
    });
    ASSERT_TRUE(f.connect());ASSERT_TRUE(f.apply());auto context=f.pipeline.snapshot().context;
    ASSERT_TRUE(f.save());ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));f.repository.finish();
    ASSERT_TRUE(f.wait([&]{return !f.pipeline.snapshot().installationProfilePending;}));EXPECT_FALSE(f.apply());
    EXPECT_EQ(f.pipeline.snapshot().context,context);EXPECT_FALSE(f.pipeline.snapshot().activeInstallationProfile);
}
}
