#include <lumora/core/LatestValueSlot.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>

namespace {

using namespace std::chrono_literals;
using lumora::core::LatestValueSlot;

TEST(LatestValueSlot, ConsumerReceivesNewestAndCountsReplacement) {
    LatestValueSlot<int> slot;

    const auto first = slot.publish(std::make_shared<const int>(1));
    const auto second = slot.publish(std::make_shared<const int>(2));
    const auto consumed = slot.consumeAfter(0U);
    const auto third = slot.publish(std::make_shared<const int>(3));

    EXPECT_EQ(first.revision, 1U);
    EXPECT_FALSE(first.replacedUnconsumed);
    EXPECT_EQ(second.revision, 2U);
    EXPECT_TRUE(second.replacedUnconsumed);
    ASSERT_TRUE(consumed.has_value());
    EXPECT_EQ(consumed->revision, 2U);
    EXPECT_EQ(*consumed->value, 2);
    EXPECT_FALSE(third.replacedUnconsumed);
}

TEST(LatestValueSlot, ConsumeAfterNeverReturnsAnOldRevision) {
    LatestValueSlot<int> slot;
    static_cast<void>(slot.publish(std::make_shared<const int>(10)));

    const auto first = slot.consumeAfter(0U);
    const auto duplicate = slot.consumeAfter(first->revision);
    static_cast<void>(slot.publish(std::make_shared<const int>(20)));
    const auto next = slot.consumeAfter(first->revision);

    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(duplicate.has_value());
    ASSERT_TRUE(next.has_value());
    EXPECT_GT(next->revision, first->revision);
    EXPECT_EQ(*next->value, 20);
}

TEST(LatestValueSlot, SlowConsumerObservesMonotonicValuesAndFinalPublication) {
    constexpr int publicationCount = 100'000;
    LatestValueSlot<int> slot;
    std::atomic<bool> sequenceIsMonotonic{true};
    std::atomic<std::uint64_t> finalRevision{0U};
    std::promise<void> observedFinalPromise;
    auto observedFinal = observedFinalPromise.get_future();

    static_cast<void>(slot.publish(std::make_shared<const int>(1)));
    const auto forcedReplacement = slot.publish(std::make_shared<const int>(2));
    ASSERT_TRUE(forcedReplacement.replacedUnconsumed);

    std::jthread consumer([&](std::stop_token stopToken) {
        std::uint64_t lastRevision = 0U;
        int lastValue = 0;
        while (const auto item = slot.waitForNewer(lastRevision, stopToken)) {
            if (item->revision <= lastRevision || *item->value <= lastValue) {
                sequenceIsMonotonic.store(false, std::memory_order_relaxed);
            }
            lastRevision = item->revision;
            lastValue = *item->value;
            if (lastValue == publicationCount) {
                finalRevision.store(lastRevision, std::memory_order_relaxed);
                observedFinalPromise.set_value();
                return;
            }
            std::this_thread::yield();
        }
    });

    std::size_t replacements = 1U;
    for (int value = 3; value <= publicationCount; ++value) {
        const auto published = slot.publish(std::make_shared<const int>(value));
        if (published.replacedUnconsumed) {
            ++replacements;
        }
    }

    ASSERT_EQ(observedFinal.wait_for(2s), std::future_status::ready);
    consumer.join();
    EXPECT_TRUE(sequenceIsMonotonic.load(std::memory_order_relaxed));
    EXPECT_EQ(finalRevision.load(std::memory_order_relaxed),
              static_cast<std::uint64_t>(publicationCount));
    EXPECT_GT(replacements, 0U);
}

TEST(LatestValueSlot, CloseWakesBlockedConsumerWithinBound) {
    LatestValueSlot<int> slot;
    std::promise<void> enteredPromise;
    auto entered = enteredPromise.get_future();
    std::promise<bool> resultPromise;
    auto result = resultPromise.get_future();

    std::jthread consumer([&](std::stop_token stopToken) {
        enteredPromise.set_value();
        resultPromise.set_value(slot.waitForNewer(0U, stopToken).has_value());
    });
    entered.wait();
    const auto started = std::chrono::steady_clock::now();
    slot.close();

    ASSERT_EQ(result.wait_for(250ms), std::future_status::ready);
    EXPECT_FALSE(result.get());
    EXPECT_LT(std::chrono::steady_clock::now() - started, 250ms);
}

TEST(LatestValueSlot, StopRequestWakesBlockedConsumerWithinBound) {
    LatestValueSlot<int> slot;
    std::promise<void> enteredPromise;
    auto entered = enteredPromise.get_future();
    std::promise<bool> resultPromise;
    auto result = resultPromise.get_future();

    std::jthread consumer([&](std::stop_token stopToken) {
        enteredPromise.set_value();
        resultPromise.set_value(slot.waitForNewer(0U, stopToken).has_value());
    });
    entered.wait();
    const auto started = std::chrono::steady_clock::now();
    consumer.request_stop();

    ASSERT_EQ(result.wait_for(250ms), std::future_status::ready);
    EXPECT_FALSE(result.get());
    EXPECT_LT(std::chrono::steady_clock::now() - started, 250ms);
}

}  // namespace
