#include <lumora/presentation/CameraSettingsDraft.hpp>
#include <lumora/application/StartupPreferences.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace lumora::presentation {
namespace {
WorkstationState state() {
    const core::SourcePixelFormat format{"Mono8", 0x01080001U, 8U, 255U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
    camera::CameraConfiguration request{format, {0, 0, 640, 480}, 30.0,
        {camera::ExposureMode::Manual, 1000.1234567890123},
        {camera::GainMode::Manual, 2.123456789012345}, camera::AcquisitionMode::Continuous};
    auto camera = std::make_shared<application::CameraStatusSnapshot>();
    camera->state = application::CameraSessionState::ConnectedIdle;
    camera->actualIdentity = lumora::camera::CameraId{"draft-camera"};
    camera->sessionGeneration = 17;
    camera->requestedRevision = camera->appliedRevision = 3;
    camera->requestedConfiguration = request;
    auto actual = request;
    actual.exposure.requestedMicroseconds = 999.5;
    camera->currentConfiguration = actual;
    camera->appliedConfiguration = lumora::camera::AppliedCameraConfiguration{request, actual};
    camera->capabilities = lumora::camera::CameraCapabilities{{format},
        {{0, 0, 16, 16},{100, 100, 1920, 1080},{2, 2, 2, 2}},
        {1, 60, 1, lumora::camera::ControlAccess::WritableStopped},
        {10, 10000, .125, lumora::camera::ControlAccess::WritableStopped},
        {lumora::camera::ExposureMode::Manual, lumora::camera::ExposureMode::Auto},
        {-6, 24, .25, lumora::camera::ControlAccess::WritableStopped},
        {lumora::camera::GainMode::Manual, lumora::camera::GainMode::Auto}};
    WorkstationState result;
    result.cameraStatus = camera;
    result.requestedConfiguration = request;
    result.selectedCameraId = camera->actualIdentity;
    return result;
}
auto mutableCamera(WorkstationState& presentation) {
    auto camera = std::make_shared<application::CameraStatusSnapshot>(*presentation.cameraStatus);
    presentation.cameraStatus = camera;
    return camera;
}

// Replacing a local draft with actual readback during polling loses operator intent.
TEST(CameraSettingsDraft, RetainsExactCompleteRequestAndSeparatesActualReadback) {
    auto presentation = state();
    CameraSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.configuration());
    ASSERT_TRUE(draft.readback());
    ASSERT_TRUE(draft.editable());
    EXPECT_DOUBLE_EQ(*draft.configuration()->exposure.requestedMicroseconds, 1000.1234567890123);
    EXPECT_DOUBLE_EQ(*draft.readback()->exposure.requestedMicroseconds, 999.5);
    auto edited = *draft.configuration();
    edited.gain.requestedDb = -1.375;
    ASSERT_TRUE(draft.setConfiguration(edited));
    draft.update(presentation);
    auto request = draft.prepareApply();
    ASSERT_TRUE(request);
    EXPECT_EQ(request->source.sessionGeneration, 17);
    EXPECT_EQ(request->source.cameraId.value,"draft-camera");
    EXPECT_TRUE(application::cameraConfigurationsEqual(request->requested, edited));
    EXPECT_DOUBLE_EQ(*draft.readback()->exposure.requestedMicroseconds, 999.5);
}

// Edit intent must protect unfinished text once, and stopped-only admission is live.
TEST(CameraSettingsDraft, TagsUncommittedEditingAndRejectsStreamingOrPendingChanges) {
    auto presentation = state();
    CameraSettingsDraft draft;
    draft.update(presentation);
    const auto intent = draft.beginEditing();
    ASSERT_TRUE(intent);
    EXPECT_EQ(intent->sessionGeneration, 17);
    EXPECT_EQ(intent->cameraId.value,"draft-camera");
    EXPECT_FALSE(draft.beginEditing());
    auto edited = *draft.configuration();
    edited.gain.requestedDb = 4;
    auto camera = mutableCamera(presentation);
    camera->state = application::CameraSessionState::Streaming;
    draft.update(presentation);
    EXPECT_FALSE(draft.editable());
    EXPECT_FALSE(draft.setConfiguration(edited));
    EXPECT_FALSE(draft.prepareApply());
    EXPECT_TRUE(draft.readback());
    camera = mutableCamera(presentation);
    camera->state = application::CameraSessionState::ConnectedIdle;
    presentation.ordinaryOperationPending = true;
    draft.update(presentation);
    EXPECT_FALSE(draft.setConfiguration(edited));
    EXPECT_FALSE(draft.prepareApply());
    presentation.ordinaryOperationPending = false;
    draft.update(presentation);
    EXPECT_TRUE(draft.editable());
}

// Numeric invalidity must fail Apply without clamping the retained candidate.
TEST(CameraSettingsDraft, ValidatesWholeCandidateAndDoesNotInventIncrementAlignment) {
    auto presentation = state();
    CameraSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.configuration());
    auto edited = *draft.configuration();
    edited.exposure.requestedMicroseconds = 1234.56789012345;
    ASSERT_TRUE(draft.setConfiguration(edited));
    EXPECT_TRUE(draft.canApply());
    EXPECT_TRUE(draft.prepareApply());
    edited.gain.requestedDb = std::numeric_limits<double>::quiet_NaN();
    ASSERT_TRUE(draft.setConfiguration(edited));
    EXPECT_FALSE(draft.canApply());
    EXPECT_FALSE(draft.prepareApply());
    edited.gain.requestedDb = 25;
    ASSERT_TRUE(draft.setConfiguration(edited));
    EXPECT_FALSE(draft.canApply());
    EXPECT_FALSE(draft.prepareApply());
    EXPECT_DOUBLE_EQ(*draft.configuration()->gain.requestedDb, 25);
}

// Fixed fields reconcile initially; later fixed readback drift makes the draft stale.
TEST(CameraSettingsDraft, NormalizesFixedFieldsAndInvalidatesDriftWithoutReplacingDraft) {
    auto presentation = state();
    auto camera = mutableCamera(presentation);
    camera->capabilities->exposure.access = camera::ControlAccess::ReadOnly;
    CameraSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.configuration());
    EXPECT_DOUBLE_EQ(*draft.configuration()->exposure.requestedMicroseconds, 999.5);
    auto edited = *draft.configuration();
    edited.gain.requestedDb = 4.123456789;
    ASSERT_TRUE(draft.setConfiguration(edited));
    camera = mutableCamera(presentation);
    camera->currentConfiguration->exposure.requestedMicroseconds = 800;
    draft.update(presentation);
    EXPECT_TRUE(draft.invalidated());
    EXPECT_FALSE(draft.prepareApply());
    EXPECT_TRUE(application::cameraConfigurationsEqual(*draft.configuration(), edited));
}

// A same-ID replacement session must not inherit this editor or show unrelated readback.
TEST(CameraSettingsDraft, SourceReplacementPermanentlyInvalidatesWithoutLeakingReadback) {
    auto presentation = state();
    CameraSettingsDraft draft;
    draft.update(presentation);
    auto replacement = presentation;
    auto camera = mutableCamera(replacement);
    ++camera->sessionGeneration;
    draft.update(replacement);
    EXPECT_TRUE(draft.invalidated());
    EXPECT_FALSE(draft.readback());
    draft.update(presentation);
    EXPECT_TRUE(draft.invalidated());
    EXPECT_FALSE(draft.editable());
}

// An external request that happens to match a rejected Apply cannot authorize rebind.
TEST(CameraSettingsDraft, RejectedSubmissionCannotAuthorizeLaterExternalRequest) {
    auto presentation = state();
    CameraSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.configuration());
    auto edited = *draft.configuration();
    edited.roi.width = 320;
    ASSERT_TRUE(draft.setConfiguration(edited));
    ASSERT_TRUE(draft.prepareApply());
    draft.update(presentation); // No pending admission: submission was rejected.
    auto camera = mutableCamera(presentation);
    camera->requestedRevision = 4;
    camera->requestedConfiguration = edited;
    presentation.requestedConfiguration = edited;
    draft.update(presentation);
    EXPECT_TRUE(draft.invalidated());
    EXPECT_FALSE(draft.rebindCompleted());
}

// Only an admitted, exact immediate source rebind may expose the new actual readback.
TEST(CameraSettingsDraft, OwnAdmittedImmediateRebindShowsReviewOnlyThenRejectsAnotherGeneration) {
    auto presentation = state();
    CameraSettingsDraft draft;
    draft.update(presentation);
    ASSERT_TRUE(draft.configuration());
    auto edited = *draft.configuration();
    edited.roi.width = 320;
    ASSERT_TRUE(draft.setConfiguration(edited));
    ASSERT_TRUE(draft.prepareApply());
    auto camera = mutableCamera(presentation);
    presentation.requestedConfiguration = edited;
    presentation.ordinaryOperationPending = true;
    draft.update(presentation);
    camera = mutableCamera(presentation);
    ++camera->sessionGeneration;
    camera->requestedRevision = camera->appliedRevision = 4;
    camera->requestedConfiguration = edited;
    camera->currentConfiguration = edited;
    camera->appliedConfiguration = camera::AppliedCameraConfiguration{edited, edited};
    presentation.ordinaryOperationPending = false;
    draft.update(presentation);
    EXPECT_TRUE(draft.rebindCompleted());
    EXPECT_FALSE(draft.invalidated());
    EXPECT_FALSE(draft.editable());
    ASSERT_TRUE(draft.readback());
    EXPECT_EQ(draft.readback()->roi.width, 320);
    EXPECT_FALSE(draft.prepareApply());
    camera = mutableCamera(presentation);
    ++camera->sessionGeneration;
    draft.update(presentation);
    EXPECT_FALSE(draft.rebindCompleted());
    EXPECT_TRUE(draft.invalidated());
    EXPECT_FALSE(draft.readback());
}

}  // namespace
}  // namespace lumora::presentation
