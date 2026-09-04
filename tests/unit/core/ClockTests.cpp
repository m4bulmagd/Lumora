#include <lumora/core/Clock.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stop_token>
#include <thread>

namespace {

using namespace std::chrono_literals;
using lumora::core::ClockWaitOutcome;
using lumora::core::ManualClock;
using lumora::core::SystemClock;

TEST(Clock, SystemClockExposesSeparateSteadyAndUtcDomains) {
    const SystemClock clock;

    const auto steady = clock.steadyNow();
    const auto utc = clock.utcNow();

    EXPECT_NE(steady.time_since_epoch(), std::chrono::steady_clock::duration::zero());
    EXPECT_NE(utc.time_since_epoch(), std::chrono::system_clock::duration::zero());
}

TEST(Clock, SystemClockWaitUntilReturnsTrueAtDeadline) {
    const SystemClock clock;

    EXPECT_TRUE(clock.waitUntil(std::chrono::steady_clock::now() + 1ms, {}));
}

TEST(Clock, SystemClockWaitUntilReturnsFalseWhenCancelled) {
    const SystemClock clock;
    std::stop_source stopSource;
    std::promise<void> waiterStarted;
    auto started = waiterStarted.get_future();
    auto reached = std::async(std::launch::async, [&] {
        waiterStarted.set_value();
        return clock.waitUntil(
            std::chrono::steady_clock::now() + 250ms,
            stopSource.get_token());
    });

    started.wait();
    stopSource.request_stop();
    ASSERT_EQ(reached.wait_for(100ms), std::future_status::ready);
    EXPECT_FALSE(reached.get());
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

TEST(ManualClock, WaitUntilIsReleasedByAdvanceAtTheDeadline) {
    ManualClock clock;
    std::promise<void> waiterStarted;
    auto started = waiterStarted.get_future();

    auto reached = std::async(std::launch::async, [&] {
        waiterStarted.set_value();
        return clock.waitUntil(
            std::chrono::steady_clock::time_point{100ms}, {});
    });

    started.wait();
    clock.advance(99ms);
    EXPECT_EQ(reached.wait_for(0ms), std::future_status::timeout);

    clock.advance(1ms);
    ASSERT_EQ(reached.wait_for(100ms), std::future_status::ready);
    EXPECT_TRUE(reached.get());
}

TEST(ManualClock, WaitUntilReturnsFalseWhenCancelled) {
    ManualClock clock;
    std::stop_source stopSource;
    std::promise<void> waiterStarted;
    auto started = waiterStarted.get_future();

    auto reached = std::async(std::launch::async, [&] {
        waiterStarted.set_value();
        return clock.waitUntil(
            std::chrono::steady_clock::time_point{100ms},
            stopSource.get_token());
    });

    started.wait();
    stopSource.request_stop();
    ASSERT_EQ(reached.wait_for(100ms), std::future_status::ready);
    EXPECT_FALSE(reached.get());
}

TEST(ManualClock, WaitUntilHasAnIndependentRealElapsedBound) {
    ManualClock clock;
    const auto before = std::chrono::steady_clock::now();

    const auto outcome = clock.waitUntil(
        std::chrono::steady_clock::time_point{100ms}, {}, 20ms);
    const auto elapsed = std::chrono::steady_clock::now() - before;

    EXPECT_EQ(outcome, ClockWaitOutcome::MaximumWaitElapsed);
    EXPECT_GE(elapsed, 10ms);
    EXPECT_LT(elapsed, 200ms);
    EXPECT_EQ(clock.steadyNow(), std::chrono::steady_clock::time_point{});
}

TEST(ManualClock, ExtremeFiniteRealElapsedBoundRemainsCancellable) {
    ManualClock clock;
    std::stop_source stopSource;
    stopSource.request_stop();
    const auto before = std::chrono::steady_clock::now();

    const auto outcome = clock.waitUntil(
        std::chrono::steady_clock::time_point{100ms},
        stopSource.get_token(),
        std::chrono::milliseconds::max() - 1ms);

    EXPECT_EQ(outcome, ClockWaitOutcome::Cancelled);
    EXPECT_LT(std::chrono::steady_clock::now() - before, 100ms);
}

}  // namespace
