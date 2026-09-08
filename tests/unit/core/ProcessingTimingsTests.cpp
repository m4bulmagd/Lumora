#include <lumora/core/ProcessingTimings.hpp>
#include <gtest/gtest.h>
#include <string>
#include <utility>

namespace lumora::core {
namespace {
using namespace std::chrono_literals;
TEST(ProcessingTimings, OwnsDynamicNamesAcrossCopiesAndMoves) {
    ProcessingTimings timings;
    {
        std::string name = "a dynamically allocated diagnostic stage identifier";
        ASSERT_TRUE(timings.stages.append(name, 17ns).hasValue());
        name.assign(100, 'x');
    }
    auto copied = timings;
    auto moved = std::move(timings);
    for (const auto* record : {&copied, &moved}) {
        ASSERT_EQ(record->stages.size(), 1U);
        EXPECT_EQ(record->stages[0].stageId.view(), "a dynamically allocated diagnostic stage identifier");
        EXPECT_EQ(record->stages[0].elapsed, 17ns);
        EXPECT_FALSE(record->stages[0].stageId.empty());
    }
}
TEST(ProcessingTimings, RejectsInvalidAppendAtomicallyWithConsistentPriority) {
    StageTimings timings;
    EXPECT_TRUE(timings.empty());
    EXPECT_EQ(timings.begin(), timings.end());
    EXPECT_EQ(timings.append("", -1ns).error(), TimingAppendError::EmptyStageId);
    EXPECT_EQ(timings.append(std::string(64, 'x'), -1ns).error(), TimingAppendError::StageIdTooLong);
    EXPECT_EQ(timings.append("stage", -1ns).error(), TimingAppendError::NegativeElapsed);
    EXPECT_TRUE(timings.empty());
    ASSERT_TRUE(timings.append(std::string(63, 'a'), 0ns).hasValue());
    for (std::size_t index = 1; index < StageTimings::capacity; ++index)
        ASSERT_TRUE(timings.append("normalize", 1ns).hasValue());
    EXPECT_EQ(timings.append("", -1ns).error(), TimingAppendError::CapacityExceeded);
    EXPECT_EQ(timings.append("overflow", 1ns).error(), TimingAppendError::CapacityExceeded);
    ASSERT_EQ(timings.size(), 16U);
    EXPECT_EQ(timings[0].stageId.view(), std::string(63, 'a'));
    EXPECT_EQ(timings[0].elapsed, 0ns);
    std::size_t index = 0;
    for (const auto& timing : timings) {
        EXPECT_EQ(&timing, &timings[index]);
        if (index) { EXPECT_EQ(timing.stageId, "normalize"); EXPECT_EQ(timing.elapsed, 1ns); }
        ++index;
    }
    EXPECT_EQ(index, 16U);
}
}  // namespace
}  // namespace lumora::core
