#include <lumora/core/Clock.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace {

using namespace std::chrono_literals;
using lumora::core::ManualClock;
using lumora::core::SystemClock;

TEST(Clock, SystemClockExposesSeparateSteadyAndUtcDomains) {
    const SystemClock clock;

    const auto steady = clock.steadyNow();
    const auto utc = clock.utcNow();

    EXPECT_NE(steady.time_since_epoch(), std::chrono::steady_clock::duration::zero());
    EXPECT_NE(utc.time_since_epoch(), std::chrono::system_clock::duration::zero());
}

TEST(ManualClock, AdvancesOnlyWhenExplicitlyRequested) {
    const auto initialSteady =
        std::chrono::steady_clock::time_point{std::chrono::seconds{10}};
    const auto initialUtc =
        std::chrono::system_clock::time_point{std::chrono::seconds{20}};
    ManualClock clock(initialSteady, initialUtc);

    EXPECT_EQ(clock.steadyNow(), initialSteady);
    EXPECT_EQ(clock.steadyNow(), initialSteady);
    EXPECT_EQ(clock.utcNow(), initialUtc);

    clock.advance(250ms);
    EXPECT_EQ(clock.steadyNow(), initialSteady + 250ms);
    EXPECT_EQ(clock.utcNow(), initialUtc + 250ms);
}

TEST(ManualClock, UtcCorrectionDoesNotChangeMonotonicTime) {
    const auto initialSteady =
        std::chrono::steady_clock::time_point{std::chrono::seconds{10}};
    const auto initialUtc =
        std::chrono::system_clock::time_point{std::chrono::seconds{20}};
    ManualClock clock(initialSteady, initialUtc);

    clock.setUtc(std::chrono::system_clock::time_point{std::chrono::seconds{5}});

    EXPECT_EQ(clock.steadyNow(), initialSteady);
    EXPECT_EQ(clock.utcNow(),
              std::chrono::system_clock::time_point{std::chrono::seconds{5}});
}

}  // namespace
