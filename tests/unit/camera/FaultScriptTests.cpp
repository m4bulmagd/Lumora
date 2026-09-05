#include <lumora/camera/sim/FaultScript.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace lumora::camera::sim {
namespace {
using namespace std::chrono_literals;

TEST(FaultScript, RejectsInvalidRepeatTriggerAndUnknownFault) {
    for (const auto& event : std::vector<FaultEvent>{
             {1U, SimulatedFault::Timeout, 0U},
             {0U, SimulatedFault::Timeout, 1U},
             {1U, SimulatedFault::Timeout, 1U, 1ms},
             {0U, SimulatedFault::Timeout, 1U, -1ms},
             {1U, static_cast<SimulatedFault>(99), 1U}}) {
        const auto script = FaultScript::create({event});
        ASSERT_FALSE(script.hasValue());
        EXPECT_EQ(script.error().category, core::ErrorCategory::CameraConfiguration);
        EXPECT_EQ(script.error().code, "invalid_fault_script");
    }
}

TEST(FaultScript, SortsExactFramesAndPreservesInputOrderAtSameTrigger) {
    auto script = FaultScript::create({
        {3U, SimulatedFault::Timeout, 1U},
        {1U, SimulatedFault::MalformedFrame, 2U},
        {1U, SimulatedFault::Timeout, 1U}}).value();
    EXPECT_FALSE(script->match(2U, 0ms, FaultPoint::Retrieval).has_value());
    for (int repeat = 0; repeat != 2; ++repeat) {
        const auto event = script->match(1U, 0ms, FaultPoint::Retrieval);
        ASSERT_TRUE(event.has_value());
        EXPECT_EQ(event->fault, SimulatedFault::MalformedFrame);
        script->consume(*event);
    }
    auto timeout = script->match(1U, 0ms, FaultPoint::Retrieval);
    ASSERT_TRUE(timeout.has_value());
    EXPECT_EQ(timeout->fault, SimulatedFault::Timeout);
    script->consume(*timeout);
    EXPECT_FALSE(script->match(1U, 0ms, FaultPoint::Retrieval).has_value());
    EXPECT_TRUE(script->match(3U, 0ms, FaultPoint::Retrieval).has_value());
}

TEST(FaultScript, ElapsedEventsAreDueAfterThresholdAndConfigurationIsSeparate) {
    auto script = FaultScript::create({
        {0U, SimulatedFault::Timeout, 1U, 100ms},
        {1U, SimulatedFault::ConfigurationFailure, 1U}}).value();
    EXPECT_FALSE(script->match(1U, std::nullopt, FaultPoint::Retrieval).has_value());
    EXPECT_FALSE(script->match(1U, 99ms, FaultPoint::Retrieval).has_value());
    EXPECT_TRUE(script->match(1U, 100ms, FaultPoint::Retrieval).has_value());
    EXPECT_TRUE(script->match(2U, 150ms, FaultPoint::Retrieval).has_value());
    const auto configuration = script->match(1U, 0ms, FaultPoint::Configuration);
    ASSERT_TRUE(configuration.has_value());
    EXPECT_EQ(configuration->fault, SimulatedFault::ConfigurationFailure);
}

TEST(FaultScript, SelectionDoesNotConsumeAndDisconnectRequiresExplicitRestore) {
    auto script = FaultScript::create({{1U, SimulatedFault::Disconnect, 1U}}).value();
    const auto event = script->match(1U, 0ms, FaultPoint::Retrieval);
    ASSERT_TRUE(event.has_value());
    EXPECT_TRUE(script->match(1U, 0ms, FaultPoint::Retrieval).has_value());
    EXPECT_FALSE(script->disconnected());
    script->consume(*event);
    EXPECT_TRUE(script->disconnected());
    EXPECT_FALSE(script->match(1U, 0ms, FaultPoint::Retrieval).has_value());
    script->restoreConnection();
    EXPECT_FALSE(script->disconnected());
}

}  // namespace
}  // namespace lumora::camera::sim
