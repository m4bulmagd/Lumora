#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/ui/WorkstationController.hpp>
#include <lumora/ui/CameraStartupPanel.hpp>
#include <lumora/ui/CameraSettingsDialog.hpp>
#include <lumora/ui/WorkstationView.hpp>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
namespace {
using namespace lumora;
using Intent = ui::CameraStartupIntent;
camera::CameraConfiguration request() {
    return {{"Mono8",0x01080001U,8U,255U,core::SourcePacking::Unpacked,
        core::BitAlignment::LeastSignificant,core::StorageType::UInt8},
        {0,0,8,6},30.0,{camera::ExposureMode::Manual,100.0},
        {camera::GainMode::Manual,0.0},camera::AcquisitionMode::Continuous};
}
camera::sim::SimulatedCameraOptions options() {
    return {{"SIM-PROFILES"}, {{request().pixelFormat},{{0,0,1,1},{0,0,8,6},{1,1,1,1}},
        {1,60,1,camera::ControlAccess::WritableStopped},{1,1000,1,camera::ControlAccess::WritableStopped},{camera::ExposureMode::Manual},
        {0,10,1,camera::ControlAccess::WritableStopped},{camera::GainMode::Manual}},camera::sim::SimulationPattern::Ramp,
        30.0,123,camera::sim::SimulationPacingMode::Manual};
}
class Memory final : public configuration::IStartupPreferencesIo {
public:
    std::mutex mutex;
    configuration::ApplicationConfiguration initial;
    configuration::ApplicationConfiguration saved;
    std::function<void()> beforeLoad;
    core::Result<configuration::ApplicationConfiguration> load() override {
        if(beforeLoad) beforeLoad();
        return core::Result<configuration::ApplicationConfiguration>::success(initial);
    }
    core::Result<void> save(const configuration::ApplicationConfiguration& value) override {
        std::lock_guard lock(mutex); saved=value; return core::Result<void>::success();
    }
};
class Repository final : public application::IInstallationProfiles {
public:
    mutable std::mutex mutex;
    application::InstallationProfilesSnapshot value;
    std::optional<application::InstallationCameraProfile> pending;
    std::uint64_t pendingId{};
    std::size_t saves{};
    Repository() {value.loadCompleted=true;value.administratorMode=true;
        value.policy=application::InstallationProfilePolicy::SimulatorIdentityFallback;}
    std::shared_ptr<const application::InstallationProfilesSnapshot> latestStatus() const override {
        std::lock_guard lock(mutex);return std::make_shared<const application::InstallationProfilesSnapshot>(value);
    }
    core::Result<void> postSave(std::uint64_t id,application::InstallationCameraProfile record,bool) override {
        std::lock_guard lock(mutex);pending=std::move(record);pendingId=id;++saves;value.savePending=true;
        return core::Result<void>::success();
    }
    bool submitted() const {std::lock_guard lock(mutex);return pending.has_value();}
    void finish() {
        std::lock_guard lock(mutex);value.profiles={*pending};
        value.latestSaveOutcome=application::InstallationSaveOutcome{pendingId,pending,{}};
        pending.reset();value.savePending=false;
    }
    void change(application::InstallationCameraProfile record) {
        std::lock_guard lock(mutex);value.profiles={std::move(record)};
    }
};
struct Harness {
    core::ManualClock clock;
    camera::sim::SimulatedCameraProvider provider{options(),clock};
    Repository repository;
    application::LivePipeline pipeline{provider,clock,request(),{},{},&repository};
    Memory* memory;
    configuration::StartupPreferencesService preferences;
    ui::WorkstationView view;
    ui::CameraStartupPanel panel;
    ui::WorkstationController controller{pipeline,preferences,view,panel,clock,request()};
    explicit Harness(std::unique_ptr<Memory> io=std::make_unique<Memory>()) : memory(io.get()),preferences(std::move(io)) {}
    ~Harness(){controller.shutdown();preferences.requestStop();preferences.join();}
    bool wait(const std::function<bool()>& condition) {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        do {controller.poll();QCoreApplication::processEvents();if(condition())return true;std::this_thread::yield();}
        while(std::chrono::steady_clock::now()<end);return false;
    }
    bool act(Intent intent) {
        if(!controller.dispatch(intent).hasValue())return false;
        return wait([&]{return !panel.presentation().ordinaryOperationPending;});
    }
    bool begin() {
        if(!preferences.start().hasValue()||!pipeline.start().hasValue()||!controller.start().hasValue())return false;
        if(!wait([&]{return panel.presentation().preferencesLoadCompleted&&panel.presentation().cameraStatus&&
            !panel.presentation().cameraStatus->discoveredDescriptors.empty()&&!panel.presentation().ordinaryOperationPending;}))return false;
        controller.selectCamera({"SIM-PROFILES"});return act(Intent::Connect)&&act(Intent::Apply)&&act(Intent::Confirm);
    }
};
TEST(InstallationController, CurrentRunConfirmationRestoresOwnCameraWithoutChangingIndependentSelection) {
    Harness f;ASSERT_TRUE(f.begin());
    auto edited=request();edited.exposure.requestedMicroseconds=250;
    ASSERT_TRUE(f.controller.applyCameraSettings(f.pipeline.snapshot().camera->sessionGeneration,{"SIM-PROFILES"},edited).hasValue());
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));ASSERT_TRUE(f.act(Intent::Confirm));
    f.controller.selectCamera({"camera-b"});
    ASSERT_TRUE(f.wait([&]{std::lock_guard lock(f.memory->mutex);
        return f.memory->saved.cameraProfiles.lastSelectedCameraId==camera::CameraId{"camera-b"};}));
    {std::lock_guard lock(f.memory->mutex);const auto& profiles=f.memory->saved.cameraProfiles.profiles;
        ASSERT_EQ(profiles.size(),1U);EXPECT_EQ(profiles.front().requested.exposure.requestedMicroseconds,250);}
    f.controller.selectCamera({"SIM-PROFILES"});
    EXPECT_EQ(f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,250);
    EXPECT_TRUE(f.panel.presentation().resumeLiveAvailable);
    ASSERT_TRUE(f.act(Intent::ResumeLive));EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Streaming);
}
TEST(InstallationController, SelectionWithoutConfirmationPersistsNoFabricatedProfile) {
    Harness f;ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(f.controller.start().hasValue());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted&&!f.panel.presentation().ordinaryOperationPending;}));
    f.controller.selectCamera({"camera-b"});
    ASSERT_TRUE(f.wait([&]{std::lock_guard lock(f.memory->mutex);
        return f.memory->saved.cameraProfiles.lastSelectedCameraId==camera::CameraId{"camera-b"};}));
    std::lock_guard lock(f.memory->mutex);EXPECT_TRUE(f.memory->saved.cameraProfiles.profiles.empty());
}
TEST(InstallationController, DurableBindingChangeRevokesResumeBeforeApplyAndConfirmationPersistsActiveReference) {
    Harness f;ASSERT_TRUE(f.begin());
    EXPECT_TRUE(f.panel.presentation().resumeLiveAvailable);
    auto camera=f.pipeline.snapshot().camera;
    application::InstallationCameraProfile record;
    record.identity=camera->discoveredDescriptors.front().identity;record.capabilities=*camera->capabilities;
    record.revision=1;record.confirmed=true;record.orientation={true,false,core::Rotation::Degrees90};
    f.repository.change(record);
    ASSERT_TRUE(f.wait([&]{auto s=f.panel.presentation().installationProfiles;return s&&!s->profiles.empty();}));
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
    EXPECT_FALSE(f.controller.dispatch(Intent::ResumeLive).hasValue());
    ASSERT_TRUE(f.act(Intent::Apply));ASSERT_TRUE(f.act(Intent::Confirm));
    ASSERT_TRUE(f.wait([&]{std::lock_guard lock(f.memory->mutex);const auto& p=f.memory->saved.cameraProfiles.profiles;
        return !p.empty()&&p.front().installationProfile.has_value();}));
    std::lock_guard lock(f.memory->mutex);
    EXPECT_EQ(f.memory->saved.cameraProfiles.profiles.front().installationProfile,application::installationProfileReference(record));
}
}

namespace {
TEST(InstallationController, InitialSelectionWithoutProfileRestoresSelectionWithoutConnecting) {
    auto memory=std::make_unique<Memory>();memory->initial.cameraProfiles.lastSelectedCameraId=camera::CameraId{"SIM-PROFILES"};
    Harness f(std::move(memory));ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(f.controller.start().hasValue());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted&&!f.panel.presentation().ordinaryOperationPending;}));
    EXPECT_EQ(f.panel.presentation().selectedCameraId,camera::CameraId{"SIM-PROFILES"});
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::Disconnected);
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
}
}

namespace {
TEST(InstallationController, PanelSaveChecksCurrentTargetAndKeepsDurableSaveSeparateFromActivation) {
    Harness f;ASSERT_TRUE(f.begin());
    const auto camera=f.pipeline.snapshot().camera;
    const core::Orientation rotated{true,false,core::Rotation::Degrees90};
    f.panel.installationSaveRequested(camera->sessionGeneration+1,{"SIM-PROFILES"},rotated,true,false);
    EXPECT_FALSE(f.repository.submitted());
    f.panel.installationSaveRequested(camera->sessionGeneration,{"SIM-PROFILES"},rotated,false,false);
    EXPECT_FALSE(f.repository.submitted());
    f.panel.installationSaveRequested(camera->sessionGeneration,{"SIM-PROFILES"},rotated,true,false);
    EXPECT_TRUE(f.panel.presentation().installationProfilePending);
    ASSERT_TRUE(f.wait([&]{return f.repository.submitted();}));
    EXPECT_FALSE(f.controller.dispatch(Intent::Apply).hasValue());
    f.panel.installationSaveRequested(camera->sessionGeneration,{"SIM-PROFILES"},rotated,true,false);
    {std::lock_guard lock(f.repository.mutex);EXPECT_EQ(f.repository.saves,1U);}
    f.repository.finish();
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().installationProfilePending;}));
    EXPECT_FALSE(f.pipeline.snapshot().activeInstallationProfile);
    EXPECT_EQ(f.pipeline.snapshot().activeOrientation,(core::Orientation{false,false,core::Rotation::Degrees0}));
    EXPECT_EQ(f.pipeline.snapshot().camera->state,application::CameraSessionState::ConnectedIdle);
    EXPECT_FALSE(f.controller.dispatch(Intent::Confirm).hasValue());
    EXPECT_FALSE(f.controller.dispatch(Intent::Start).hasValue());
    ASSERT_TRUE(f.act(Intent::Apply));ASSERT_TRUE(f.act(Intent::Confirm));
    EXPECT_EQ(f.pipeline.snapshot().activeOrientation,rotated);
}
TEST(InstallationController, StableIdentityRestoresRequestButChangedLogicalIdRequiresReview) {
    auto io=std::make_unique<Memory>();
    application::StartupPreferences record;
    record.cameraId={"old-logical-id"};record.identity={"Lumora","Generated Camera","SIM-PROFILES","Simulator",{}};
    record.confirmedCapabilities=options().capabilities;record.requested=request();
    record.requested.exposure.requestedMicroseconds=250;record.lastApplied=record.requested;record.confirmed=true;
    io->initial.cameraProfiles.lastSelectedCameraId=camera::CameraId{"SIM-PROFILES"};
    io->initial.cameraProfiles.profiles={record};
    Harness f(std::move(io));ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(f.pipeline.start().hasValue());
    ASSERT_TRUE(f.controller.start().hasValue());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted&&!f.panel.presentation().ordinaryOperationPending;}));
    f.controller.selectCamera({"SIM-PROFILES"});ASSERT_TRUE(f.act(Intent::Connect));
    EXPECT_EQ(f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,250);
    EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
    EXPECT_FALSE(f.controller.dispatch(Intent::ResumeLive).hasValue());
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
}
}

namespace {
TEST(InstallationController, IdentityRemappingRestoresTheSameRequestForBothLoadDiscoveryOrdersWithoutReselection) {
    for(const bool preferencesFirst : {true,false}) {
        SCOPED_TRACE(preferencesFirst);
        auto io=std::make_unique<Memory>();
        application::StartupPreferences record;
        record.cameraId={"old-logical-id"};record.identity={"Lumora","Generated Camera","SIM-PROFILES","Simulator",{}};
        record.confirmedCapabilities=options().capabilities;record.requested=request();
        record.requested.exposure.requestedMicroseconds=250;record.lastApplied=record.requested;record.confirmed=true;
        io->initial.cameraProfiles.lastSelectedCameraId=camera::CameraId{"SIM-PROFILES"};
        io->initial.cameraProfiles.profiles={record};
        Harness f(std::move(io));
        if(preferencesFirst) {
            ASSERT_TRUE(f.preferences.start().hasValue());
            ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted;}));
            ASSERT_EQ(f.panel.presentation().selectedCameraId,camera::CameraId{"SIM-PROFILES"});
            ASSERT_TRUE(!f.panel.presentation().cameraStatus ||
                f.panel.presentation().cameraStatus->discoveredDescriptors.empty());
        }
        ASSERT_TRUE(f.pipeline.start().hasValue());ASSERT_TRUE(f.controller.start().hasValue());
        ASSERT_TRUE(f.wait([&]{return f.panel.presentation().cameraStatus &&
            !f.panel.presentation().cameraStatus->discoveredDescriptors.empty() && !f.panel.presentation().ordinaryOperationPending;}));
        if(!preferencesFirst) {
            ASSERT_FALSE(f.panel.presentation().preferencesLoadCompleted);
            ASSERT_TRUE(f.preferences.start().hasValue());
            ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted;}));
        }
        EXPECT_EQ(f.panel.presentation().selectedCameraId,camera::CameraId{"SIM-PROFILES"});
        EXPECT_EQ(f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,250);
        EXPECT_FALSE(f.panel.presentation().resumeLiveAvailable);
        ASSERT_TRUE(f.act(Intent::Connect));
        EXPECT_EQ(f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,250);
        EXPECT_FALSE(f.controller.dispatch(Intent::ResumeLive).hasValue());
        EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
    }
}
}

namespace {
TEST(InstallationController, LatePreferencesPreserveLocallyEditedCameraSettingsBeforeApply) {
    struct LoadGate final {
        std::mutex mutex;std::condition_variable changed;bool entered{false};bool released{false};
        void block() {std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});}
        bool wait() {std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(3),[&]{return entered;});}
        void release() {std::lock_guard lock(mutex);released=true;changed.notify_all();}
    } gate;
    auto io=std::make_unique<Memory>();
    application::StartupPreferences record;
    record.cameraId={"SIM-PROFILES"};record.identity={"Lumora","Generated Camera","SIM-PROFILES","Simulator",{}};
    record.confirmedCapabilities=options().capabilities;record.requested=request();
    record.requested.exposure.requestedMicroseconds=250;record.lastApplied=record.requested;record.confirmed=true;
    io->initial.cameraProfiles.lastSelectedCameraId=camera::CameraId{"SIM-PROFILES"};
    io->initial.cameraProfiles.profiles={record};io->beforeLoad=[&]{gate.block();};
    Harness f(std::move(io));
    struct Release final {LoadGate& gate;~Release(){gate.release();}} release{gate};
    ASSERT_TRUE(f.preferences.start().hasValue());ASSERT_TRUE(gate.wait());
    ASSERT_TRUE(f.pipeline.start().hasValue());ASSERT_TRUE(f.controller.start().hasValue());
    ASSERT_TRUE(f.wait([&]{return f.panel.presentation().cameraStatus &&
        !f.panel.presentation().cameraStatus->discoveredDescriptors.empty()&&!f.panel.presentation().ordinaryOperationPending;}));
    f.controller.selectCamera({"SIM-PROFILES"});ASSERT_TRUE(f.act(Intent::Connect));
    auto* open=f.panel.findChild<QPushButton*>("cameraSettingsButton");ASSERT_NE(open,nullptr);ASSERT_TRUE(open->isEnabled());open->click();
    auto* dialog=f.panel.findChild<ui::CameraSettingsDialog*>();ASSERT_NE(dialog,nullptr);
    auto* exposure=dialog->findChild<QDoubleSpinBox*>("cameraExposureValue");
    auto* apply=dialog->findChild<QPushButton*>("applyCameraSettingsButton");
    ASSERT_NE(exposure,nullptr);ASSERT_NE(apply,nullptr);exposure->setValue(175);
    ASSERT_TRUE(apply->isEnabled());EXPECT_FALSE(f.panel.presentation().preferencesLoadCompleted);
    EXPECT_EQ(f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,100);
    gate.release();ASSERT_TRUE(f.wait([&]{return f.panel.presentation().preferencesLoadCompleted;}));
    EXPECT_DOUBLE_EQ(exposure->value(),175);
    EXPECT_EQ(f.panel.presentation().requestedConfiguration->exposure.requestedMicroseconds,100);
    ASSERT_TRUE(apply->isEnabled());apply->click();
    ASSERT_TRUE(f.wait([&]{return !f.panel.presentation().ordinaryOperationPending;}));
    EXPECT_EQ(f.pipeline.snapshot().camera->requestedConfiguration->exposure.requestedMicroseconds,175);
    EXPECT_FALSE(f.pipeline.snapshot().camera->confirmedRevision);
}
}
