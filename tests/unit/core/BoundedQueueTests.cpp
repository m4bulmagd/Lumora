#include <lumora/core/BoundedQueue.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <optional>
#include <stop_token>
#include <thread>

namespace {

using namespace std::chrono_literals;
using lumora::core::BoundedQueue;

TEST(BoundedQueue, RejectsPushAtCapacityAndPreservesFifoOrdering) {
    BoundedQueue<int> queue(2U);

    EXPECT_EQ(queue.capacity(), 2U);
    EXPECT_TRUE(queue.tryPush(1));
    EXPECT_TRUE(queue.tryPush(2));
    EXPECT_FALSE(queue.tryPush(3));
    EXPECT_EQ(queue.size(), 2U);

    const auto first = queue.waitPop({});
    const auto second = queue.waitPop({});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, 1);
    EXPECT_EQ(*second, 2);
    EXPECT_EQ(queue.size(), 0U);
}

TEST(BoundedQueue, ReusesConstructorFixedStorageAfterPop) {
    BoundedQueue<int> queue(1U);

    ASSERT_TRUE(queue.tryPush(7));
    ASSERT_TRUE(queue.waitPop({}).has_value());
    EXPECT_TRUE(queue.tryPush(8));
    EXPECT_EQ(queue.capacity(), 1U);
}

TEST(BoundedQueue, ZeroCapacityRemainsSafelyBounded) {
    BoundedQueue<int> queue(0U);

    EXPECT_EQ(queue.capacity(), 0U);
    EXPECT_FALSE(queue.tryPush(1));
    EXPECT_EQ(queue.size(), 0U);
}

TEST(BoundedQueue, CloseRejectsPushAndWakesBlockedConsumerWithinBound) {
    BoundedQueue<int> queue(2U);
    std::promise<void> enteredPromise;
    auto entered = enteredPromise.get_future();
    std::promise<bool> resultPromise;
    auto result = resultPromise.get_future();

    std::jthread consumer([&](std::stop_token stopToken) {
        enteredPromise.set_value();
        resultPromise.set_value(queue.waitPop(stopToken).has_value());
    });
    entered.wait();
    const auto started = std::chrono::steady_clock::now();
    queue.close();

    ASSERT_EQ(result.wait_for(250ms), std::future_status::ready);
    EXPECT_FALSE(result.get());
    EXPECT_FALSE(queue.tryPush(9));
    EXPECT_TRUE(queue.closed());
    EXPECT_LT(std::chrono::steady_clock::now() - started, 250ms);
}

TEST(BoundedQueue, StopRequestWakesBlockedConsumerWithinBound) {
    BoundedQueue<int> queue(2U);
    std::promise<void> enteredPromise;
    auto entered = enteredPromise.get_future();
    std::promise<bool> resultPromise;
    auto result = resultPromise.get_future();

    std::jthread consumer([&](std::stop_token stopToken) {
        enteredPromise.set_value();
        resultPromise.set_value(queue.waitPop(stopToken).has_value());
    });
    entered.wait();
    const auto started = std::chrono::steady_clock::now();
    consumer.request_stop();

    ASSERT_EQ(result.wait_for(250ms), std::future_status::ready);
    EXPECT_FALSE(result.get());
    EXPECT_LT(std::chrono::steady_clock::now() - started, 250ms);
}

TEST(BoundedQueue, CloseAllowsAlreadyQueuedValuesToDrain) {
    BoundedQueue<int> queue(2U);
    ASSERT_TRUE(queue.tryPush(1));
    ASSERT_TRUE(queue.tryPush(2));

    queue.close();
    const auto first = queue.waitPop({});
    const auto second = queue.waitPop({});
    const auto empty = queue.waitPop({});

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, 1);
    EXPECT_EQ(*second, 2);
    EXPECT_FALSE(empty.has_value());
}

}  // namespace
