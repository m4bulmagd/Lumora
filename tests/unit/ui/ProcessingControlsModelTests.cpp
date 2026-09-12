#include <lumora/ui/ProcessingControlsModel.hpp>
#include <lumora/configuration/PresetCodec.hpp>
#include <gtest/gtest.h>

namespace {
using namespace std::chrono_literals;
using lumora::ui::ProcessingControlsModel;
using lumora::ui::ProcessingEditPhase;
using lumora::processing::GammaParameters;

struct Controls : testing::Test {
    lumora::core::ManualClock clock;
    ProcessingControlsModel model{lumora::configuration::PresetCodec::loadDefaultRepository().value(),clock};
    auto gamma(double value) {
        auto definition=model.draft().activePipeline;
        definition.stages[3].enabled=true;
        std::get<GammaParameters>(definition.stages[3].parameters).gamma=value;
        return definition;
    }
    double draftGamma() { return std::get<GammaParameters>(model.draft().activePipeline.stages[3].parameters).gamma; }
    auto take() { return model.takeSubmission(); }
    bool accept(const lumora::ui::ProcessingSubmission& submission) {
        return model.complete(submission.sessionGeneration,submission.state.activePipeline.version.configurationRevision);
    }
    void SetUp() override {
        model.bindSession(1);
        auto initial=take();
        ASSERT_TRUE(initial); ASSERT_TRUE(accept(*initial));
        clock.advance(1s);
    }
};
TEST_F(Controls, DragCoalescesAtThirtyHzAndReleaseFlushesExactValues) {
    ASSERT_TRUE(model.edit(gamma(1.1),ProcessingEditPhase::Drag).hasValue());
    auto first=take(); ASSERT_TRUE(first); ASSERT_TRUE(accept(*first));
    ASSERT_TRUE(model.edit(gamma(1.2),ProcessingEditPhase::Drag).hasValue());
    clock.advance(33ms); EXPECT_FALSE(take()); EXPECT_DOUBLE_EQ(draftGamma(),1.2);
    clock.advance(1ms); auto second=take(); ASSERT_TRUE(second); ASSERT_TRUE(accept(*second));
    ASSERT_TRUE(model.edit(gamma(1.23456789),ProcessingEditPhase::Release).hasValue());
    auto final=take(); ASSERT_TRUE(final);
    EXPECT_DOUBLE_EQ(std::get<GammaParameters>(final->state.activePipeline.stages[3].parameters).gamma,1.23456789);
    ASSERT_TRUE(accept(*final));
    ASSERT_TRUE(model.edit(gamma(1.23456789),ProcessingEditPhase::Release).hasValue());
    EXPECT_FALSE(take());
}
TEST_F(Controls, PresetCancelsDragAndManualEditsSelectCustom) {
    ASSERT_TRUE(model.edit(gamma(2),ProcessingEditPhase::Drag).hasValue());
    EXPECT_EQ(model.draft().selectedId.value,"custom");
    ASSERT_TRUE(model.selectPreset({"standard"}).hasValue());
    ASSERT_TRUE(model.edit(model.draft().activePipeline,ProcessingEditPhase::Release).hasValue());
    EXPECT_EQ(model.draft().selectedId.value,"standard");
    auto selected=take(); ASSERT_TRUE(selected); EXPECT_EQ(selected->state.selectedId.value,"standard");
    ASSERT_TRUE(accept(*selected)); clock.advance(1s); EXPECT_FALSE(take());
    ASSERT_TRUE(model.edit(model.draft().activePipeline).hasValue());
    EXPECT_EQ(model.draft().selectedId.value,"custom");
}
TEST_F(Controls, OlderCompletionCannotOverwriteNewerDraftAndLastRejectionRestoresAccepted) {
    ASSERT_TRUE(model.edit(gamma(2)).hasValue()); auto first=take(); ASSERT_TRUE(first);
    ASSERT_TRUE(model.edit(gamma(3)).hasValue()); EXPECT_FALSE(take());
    ASSERT_TRUE(accept(*first)); EXPECT_DOUBLE_EQ(draftGamma(),3);
    auto second=take(); ASSERT_TRUE(second);
    lumora::core::Error error{lumora::core::ErrorCategory::Configuration,"rejected","Could not apply","fixture",true};
    EXPECT_FALSE(model.complete(1,second->state.activePipeline.version.configurationRevision,error));
    EXPECT_DOUBLE_EQ(draftGamma(),2); ASSERT_TRUE(model.error()); EXPECT_FALSE(take());
}
TEST_F(Controls, EarlierRejectionDoesNotReplaceNewerDraftOrItsStatus) {
    ASSERT_TRUE(model.edit(gamma(2)).hasValue()); auto first=take(); ASSERT_TRUE(first);
    ASSERT_TRUE(model.edit(gamma(3)).hasValue());
    lumora::core::Error error{lumora::core::ErrorCategory::Configuration,"rejected","Earlier edit rejected","fixture",true};
    EXPECT_FALSE(model.complete(1,first->state.activePipeline.version.configurationRevision,error));
    EXPECT_DOUBLE_EQ(draftGamma(),3); EXPECT_FALSE(model.error()); EXPECT_TRUE(model.pending());
    auto next=take(); ASSERT_TRUE(next); ASSERT_TRUE(accept(*next)); EXPECT_DOUBLE_EQ(draftGamma(),3);
}
TEST_F(Controls, LostSessionAdmissionRetainsDesiredStateForReplacement) {
    ASSERT_TRUE(model.edit(gamma(2)).hasValue()); auto submission=take(); ASSERT_TRUE(submission);
    model.rejectAdmission(1,submission->state.activePipeline.version.configurationRevision,
        {lumora::core::ErrorCategory::Processing,"stale_processing_session","Session was replaced","fixture",true});
    EXPECT_DOUBLE_EQ(draftGamma(),2); EXPECT_FALSE(model.error()); EXPECT_FALSE(take());
    model.bindSession(2); auto fresh=take(); ASSERT_TRUE(fresh);
    EXPECT_EQ(fresh->sessionGeneration,2U);
    EXPECT_GT(fresh->state.activePipeline.version.configurationRevision,submission->state.activePipeline.version.configurationRevision);
    EXPECT_DOUBLE_EQ(std::get<GammaParameters>(fresh->state.activePipeline.stages[3].parameters).gamma,2);
}
TEST_F(Controls, ReplacementRejectsOldOutcomeAndResubmitsDesiredWithFreshRevision) {
    ASSERT_TRUE(model.edit(gamma(2)).hasValue()); auto old=take(); ASSERT_TRUE(old);
    model.bindSession(2); EXPECT_FALSE(accept(*old));
    auto fresh=take(); ASSERT_TRUE(fresh); EXPECT_EQ(fresh->sessionGeneration,2U);
    EXPECT_GT(fresh->state.activePipeline.version.configurationRevision,old->state.activePipeline.version.configurationRevision);
    EXPECT_DOUBLE_EQ(std::get<GammaParameters>(fresh->state.activePipeline.stages[3].parameters).gamma,2);
}
TEST_F(Controls, InvalidWholeRevisionPreservesDraftAndResetUsesOriginal) {
    ASSERT_TRUE(model.selectPreset({"standard"}).hasValue());
    auto invalid=gamma(0); EXPECT_FALSE(model.edit(invalid).hasValue());
    EXPECT_EQ(model.draft().selectedId.value,"standard");
    ASSERT_TRUE(model.reset().hasValue()); EXPECT_EQ(model.draft().selectedId.value,"original");
    auto reset=take(); ASSERT_TRUE(reset); EXPECT_FALSE(reset->state.activePipeline.stages[3].enabled);
}
}
