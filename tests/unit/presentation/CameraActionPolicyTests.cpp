#include <lumora/presentation/CameraActionPolicy.hpp>

#include <gtest/gtest.h>

namespace lumora::presentation {
namespace {

struct CameraStateFixture final {
    std::shared_ptr<application::CameraStatusSnapshot> camera =
        std::make_shared<application::CameraStatusSnapshot>();
    WorkstationState state;

    CameraStateFixture() {
        camera->state = application::CameraSessionState::ConnectedIdle;
        camera->actualIdentity = camera::CameraId{"SIM-POLICY"};
        camera->desiredIdentity = camera->actualIdentity;
        camera->discoveredDescriptors = {{*camera->actualIdentity,
            {"Lumora", "Simulator", "SIM-POLICY", "Simulator", std::nullopt}, true}};
        const camera::CameraConfiguration request{
            {"Mono8", 0x01080001U, 8U, 255U, core::SourcePacking::Unpacked,
                core::BitAlignment::LeastSignificant, core::StorageType::UInt8},
            {0, 0, 8, 6}, 30.0, {camera::ExposureMode::Manual, 100.0},
            {camera::GainMode::Manual, 0.0}, camera::AcquisitionMode::Continuous};
        camera->requestedConfiguration = request;
        camera->currentConfiguration = request;
        camera->appliedConfiguration = camera::AppliedCameraConfiguration{request, request};
        camera->requestedRevision = camera->appliedRevision = 7;
        camera->confirmedRevision = 7;
        state.cameraStatus = camera;
        state.selectedCameraId = camera->actualIdentity;
        state.requestedConfiguration = request;
        state.contextBound = true;
        state.preferencesLoadCompleted = true;
    }
};

TEST(CameraActionPolicy, StartRequiresMatchingRequestConfirmationAndBoundContext) {
    CameraStateFixture f;
    EXPECT_TRUE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Start));
    f.state.contextBound = false;
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Start));
    f.state.contextBound = true;
    f.camera->confirmedRevision.reset();
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Start));
    EXPECT_TRUE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Confirm));
    f.camera->confirmedRevision = 7;
    f.camera->requestedRevision = 8;
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Start));
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Confirm));
}

TEST(CameraActionPolicy, SelectedSourceMismatchCannotApplyConfirmOrStart) {
    CameraStateFixture f;
    f.state.selectedCameraId = camera::CameraId{"OTHER"};
    const auto policy = CameraActionPolicy::evaluate(f.state);
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Apply));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Confirm));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Start));
    EXPECT_FALSE(policy.settings.visible);
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Disconnect));
}

TEST(CameraActionPolicy, MissingDesiredRequestCannotUseBackendReadbackAsIntent) {
    CameraStateFixture f;
    f.state.requestedConfiguration.reset();
    auto policy = CameraActionPolicy::evaluate(f.state);
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Apply));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Start));
    f.camera->confirmedRevision.reset();
    policy = CameraActionPolicy::evaluate(f.state);
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Confirm));
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Disconnect));
}

TEST(CameraActionPolicy, PendingResumeCanBeCancelledWhileStopIsHidden) {
    CameraStateFixture f;
    f.state.ordinaryOperationPending = true;
    const auto policy = CameraActionPolicy::evaluate(f.state);
    EXPECT_FALSE(policy.stop.visible);
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Stop));
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Disconnect));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Apply));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Start));
    EXPECT_FALSE(policy.selectionEnabled);
}

TEST(CameraActionPolicy, PendingDiscoveryCanBeDisconnectedBeforeStatusCatchesUp) {
    WorkstationState state;
    state.ordinaryOperationPending = true;
    const auto policy = CameraActionPolicy::evaluate(state);
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Disconnect));
    EXPECT_FALSE(policy.disconnect.visible);
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Refresh));
}

TEST(CameraActionPolicy, InstallationChangesBlockOrdinaryActionsButPermitCancellation) {
    CameraStateFixture f;
    f.camera->state = application::CameraSessionState::Streaming;
    f.state.installationProfilePending = true;
    auto policy = CameraActionPolicy::evaluate(f.state);
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Stop));
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Disconnect));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Apply));
    f.state.installationProfilePending = false;
    f.state.installationBindingCurrent = false;
    f.camera->state = application::CameraSessionState::ConnectedIdle;
    f.camera->confirmedRevision.reset();
    policy = CameraActionPolicy::evaluate(f.state);
    EXPECT_TRUE(policy.allows(CameraStartupIntent::Apply));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Confirm));
    EXPECT_FALSE(policy.allows(CameraStartupIntent::Start));
}

TEST(CameraActionPolicy, DiscoveryAndRetryFollowStateAndRetainedIdentity) {
    CameraStateFixture f;
    f.camera->state = application::CameraSessionState::Disconnected;
    EXPECT_TRUE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Connect));
    f.camera->discoveredDescriptors.front().available = false;
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Connect));
    f.camera->state = application::CameraSessionState::Error;
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Refresh));
    EXPECT_TRUE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Retry));
    f.camera->desiredIdentity.reset();
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::Retry));
}

TEST(CameraActionPolicy, ResumeRequiresLoadedEligibilityAndNeverFollowsSelectionAlone) {
    CameraStateFixture f;
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::ResumeLive));
    f.state.resumeLiveAvailable = true;
    EXPECT_TRUE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::ResumeLive));
    f.state.preferencesLoadCompleted = false;
    EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(CameraStartupIntent::ResumeLive));
}

TEST(CameraActionPolicy, DisabledWorkstationRejectsEveryCommand) {
    CameraStateFixture f;
    f.state.controlsEnabled = false;
    f.state.ordinaryOperationPending = true;
    for (const auto intent : {CameraStartupIntent::Refresh, CameraStartupIntent::Connect,
             CameraStartupIntent::Apply, CameraStartupIntent::Confirm, CameraStartupIntent::Start,
             CameraStartupIntent::Stop, CameraStartupIntent::Disconnect, CameraStartupIntent::Retry,
             CameraStartupIntent::ResumeLive}) {
        EXPECT_FALSE(CameraActionPolicy::evaluate(f.state).allows(intent));
    }
}

}  // namespace
}  // namespace lumora::presentation
