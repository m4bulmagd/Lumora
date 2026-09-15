#include <lumora/presentation/InstallationSettingsDraft.hpp>
#include <gtest/gtest.h>

namespace lumora::presentation {
namespace {
WorkstationState state(bool administrator = true) {
    WorkstationState result;
    auto camera = std::make_shared<application::CameraStatusSnapshot>();
    camera->state = application::CameraSessionState::ConnectedIdle;
    camera->sessionGeneration = 42;
    camera->actualIdentity = camera::CameraId{"installation-camera"};
    camera->capabilities = camera::CameraCapabilities{};
    camera->discoveredDescriptors = {{{"installation-camera"},
        {"Lumora", "Camera", "serial", "test", {}}, true}};
    result.cameraStatus = camera;
    result.selectedCameraId = camera->actualIdentity;
    auto repository = std::make_shared<application::InstallationProfilesSnapshot>();
    repository->loadCompleted = true;
    repository->administratorMode = administrator;
    result.installationProfiles = repository;
    result.activeOrientation = core::Orientation{false, false, core::Rotation::Degrees0};
    return result;
}

TEST(InstallationSettingsDraft, FirstCompletedLoadSeedsSavedOrientationWithoutReplacingLaterDraft) {
    auto presentation = state();
    auto repository = std::make_shared<application::InstallationProfilesSnapshot>(*presentation.installationProfiles);
    repository->loadCompleted = false;
    presentation.installationProfiles = repository;
    InstallationSettingsDraft draft;
    draft.update(presentation);
    EXPECT_FALSE(draft.initialized());
    EXPECT_FALSE(draft.editable());
    repository = std::make_shared<application::InstallationProfilesSnapshot>(*repository);
    repository->loadCompleted = true;
    application::InstallationCameraProfile profile;
    profile.identity = presentation.cameraStatus->discoveredDescriptors.front().identity;
    profile.orientation = {true, false, core::Rotation::Degrees90};
    repository->profiles = {profile};
    presentation.installationProfiles = repository;
    draft.update(presentation);
    ASSERT_TRUE(draft.initialized());
    EXPECT_EQ(draft.orientation(), profile.orientation);
    ASSERT_TRUE(draft.setOrientation({true, true, core::Rotation::Degrees270}));
    ASSERT_TRUE(draft.setConfirmation(true));
    draft.update(presentation);
    EXPECT_EQ(draft.orientation(), (core::Orientation{true, true, core::Rotation::Degrees270}));
    EXPECT_TRUE(draft.confirmationChecked());
}

TEST(InstallationSettingsDraft, SourceIdentityCapabilitiesAndGenerationPermanentlyInvalidate) {
    for (int change = 0; change < 5; ++change) {
        SCOPED_TRACE(change);
        auto original = state();
        InstallationSettingsDraft draft;
        draft.update(original);
        ASSERT_TRUE(draft.setConfirmation(true));
        auto changed = original;
        auto camera = std::make_shared<application::CameraStatusSnapshot>(*original.cameraStatus);
        if (change == 0) ++camera->sessionGeneration;
        if (change == 1) changed.selectedCameraId = camera::CameraId{"other"};
        if (change == 2) camera->discoveredDescriptors.front().identity.serial = "replacement";
        if (change == 3) ++camera->capabilities->roi.maximum.width;
        if (change == 4) camera->discoveredDescriptors.clear();
        changed.cameraStatus = camera;
        draft.update(changed);
        EXPECT_TRUE(draft.invalidated());
        EXPECT_FALSE(draft.confirmationChecked());
        EXPECT_FALSE(draft.prepareSave());
        draft.update(original);
        EXPECT_TRUE(draft.invalidated());
        EXPECT_FALSE(draft.editable());
    }
}

TEST(InstallationSettingsDraft, ConfirmationAndRepairAreSeparateAndSaveLatchesBeforeDispatch) {
    auto presentation = state();
    auto repository = std::make_shared<application::InstallationProfilesSnapshot>(*presentation.installationProfiles);
    repository->loadError = core::Error{core::ErrorCategory::Configuration,
        "invalid", "Invalid installation", "", true};
    presentation.installationProfiles = repository;
    InstallationSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.repairVisible());
    ASSERT_TRUE(draft.setConfirmation(true));
    EXPECT_FALSE(draft.saveEnabled());
    ASSERT_TRUE(draft.setRepairConsent(true));
    EXPECT_FALSE(draft.confirmationChecked());
    ASSERT_TRUE(draft.setConfirmation(true));
    const auto request = draft.prepareSave();
    ASSERT_TRUE(request);
    EXPECT_EQ(request->sessionGeneration, 42);
    EXPECT_EQ(request->cameraId.value, "installation-camera");
    EXPECT_TRUE(request->confirmed);
    EXPECT_TRUE(request->repairInvalid);
    EXPECT_TRUE(draft.pending());
    EXPECT_FALSE(draft.confirmationChecked());
    EXPECT_FALSE(draft.prepareSave());
    draft.update(presentation);
    EXPECT_TRUE(draft.pending());
}

TEST(InstallationSettingsDraft, SynchronousRejectionReleasesLatchButRequiresRenewedConfirmation) {
    InstallationSettingsDraft draft;
    const auto presentation = state();
    draft.update(presentation);
    ASSERT_TRUE(draft.setConfirmation(true));
    ASSERT_TRUE(draft.prepareSave());
    ASSERT_TRUE(draft.pending());
    draft.rejectSubmission();
    EXPECT_FALSE(draft.pending());
    EXPECT_TRUE(draft.editable());
    EXPECT_FALSE(draft.confirmationChecked());
    EXPECT_FALSE(draft.saveEnabled());
    ASSERT_TRUE(draft.setConfirmation(true));
    EXPECT_TRUE(draft.prepareSave());
}

TEST(InstallationSettingsDraft, OldOutcomeCannotFinishSubmissionAndFailureRequiresReconfirmation) {
    auto presentation = state();
    presentation.installationProfileOutcome = application::InstallationSaveOutcome{7, {}, {}};
    InstallationSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.setConfirmation(true));
    ASSERT_TRUE(draft.prepareSave());
    draft.update(presentation);
    EXPECT_TRUE(draft.pending());
    presentation.installationProfileOutcome = application::InstallationSaveOutcome{8, {},
        core::Error{core::ErrorCategory::Storage, "save_failed", "Save failed", "", true}};
    draft.update(presentation);
    EXPECT_FALSE(draft.pending());
    EXPECT_FALSE(draft.confirmationChecked());
    EXPECT_FALSE(draft.saveEnabled());
    ASSERT_TRUE(draft.setConfirmation(true));
    EXPECT_TRUE(draft.saveEnabled());
}

TEST(InstallationSettingsDraft, OperatorStreamingPendingAndInvalidRotationCannotMutateDraft) {
    InstallationSettingsDraft operatorDraft;
    operatorDraft.update(state(false));
    EXPECT_TRUE(operatorDraft.initialized());
    EXPECT_FALSE(operatorDraft.editable());
    EXPECT_FALSE(operatorDraft.setConfirmation(true));
    EXPECT_FALSE(operatorDraft.setOrientation({true, false, core::Rotation::Degrees90}));
    auto presentation = state();
    InstallationSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.setConfirmation(true));
    EXPECT_FALSE(draft.setOrientation({false, false, static_cast<core::Rotation>(4)}));
    EXPECT_EQ(draft.orientation(), (core::Orientation{false, false, core::Rotation::Degrees0}));
    EXPECT_TRUE(draft.confirmationChecked());
    for (const bool streaming : {true, false}) {
        auto changed = presentation;
        auto camera = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
        if (streaming) camera->state = application::CameraSessionState::Streaming;
        else changed.installationProfilePending = true;
        changed.cameraStatus = camera;
        draft.update(changed);
        EXPECT_FALSE(draft.editable());
        EXPECT_FALSE(draft.setOrientation({true, false, core::Rotation::Degrees90}));
        EXPECT_FALSE(draft.prepareSave());
    }
}
}  // namespace
}  // namespace lumora::presentation
