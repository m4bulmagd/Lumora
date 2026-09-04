#include <lumora/core/BufferPool.hpp>
#include <lumora/core/SharedBuffer.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using lumora::core::BufferPool;
using lumora::core::ErrorCategory;
using lumora::core::SharedBuffer;
using lumora::core::WritableBufferLease;

static_assert(!std::is_copy_constructible_v<WritableBufferLease>);
static_assert(!std::is_copy_assignable_v<WritableBufferLease>);
static_assert(std::is_move_constructible_v<WritableBufferLease>);
static_assert(std::is_same_v<decltype(std::declval<SharedBuffer>().bytes()),
                             std::span<const std::byte>>);

TEST(BufferPool, RejectsInvalidAndOverflowingConfiguration) {
    const auto zeroCapacity = BufferPool::create(0U, 4096U);
    const auto zeroBytes = BufferPool::create(2U, 0U);
    const auto overflow = BufferPool::create(
        std::numeric_limits<std::size_t>::max(), 2U);

    ASSERT_FALSE(zeroCapacity.hasValue());
    EXPECT_EQ(zeroCapacity.error().category, ErrorCategory::ResourceExhaustion);
    EXPECT_EQ(zeroCapacity.error().code, "buffer_pool_zero_capacity");
    ASSERT_FALSE(zeroBytes.hasValue());
    EXPECT_EQ(zeroBytes.error().code, "buffer_pool_zero_block_size");
    ASSERT_FALSE(overflow.hasValue());
    EXPECT_EQ(overflow.error().code, "buffer_pool_size_overflow");
}

TEST(BufferPool, NeverGrowsPastConfiguredCapacity) {
    auto result = BufferPool::create(2U, 4096U);
    ASSERT_TRUE(result.hasValue());
    auto pool = std::move(result).value();

    auto first = pool->tryAcquire();
    auto second = pool->tryAcquire();
    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
    EXPECT_FALSE(pool->tryAcquire().has_value());

    const auto exhaustedStats = pool->stats();
    EXPECT_EQ(exhaustedStats.capacity, 2U);
    EXPECT_EQ(exhaustedStats.bytesPerBuffer, 4096U);
    EXPECT_EQ(exhaustedStats.inUse, 2U);
    EXPECT_EQ(exhaustedStats.available, 0U);
    EXPECT_EQ(exhaustedStats.highWaterMark, 2U);
    EXPECT_EQ(exhaustedStats.acquisitionFailures, 1U);

    first.reset();
    EXPECT_TRUE(pool->tryAcquire().has_value());
}

TEST(BufferPool, UnsealedLeaseReturnsItsBlock) {
    auto result = BufferPool::create(1U, 16U);
    ASSERT_TRUE(result.hasValue());
    auto pool = std::move(result).value();

    {
        auto lease = pool->tryAcquire();
        ASSERT_TRUE(lease.has_value());
        EXPECT_EQ(lease->bytes().size(), 16U);
        std::ranges::fill(lease->bytes(), std::byte{0x2A});
    }

    EXPECT_EQ(pool->stats().inUse, 0U);
    EXPECT_TRUE(pool->tryAcquire().has_value());
}

TEST(SharedBuffer, SealingPublishesConstBytesUntilFinalCopyIsReleased) {
    auto result = BufferPool::create(1U, 4U);
    ASSERT_TRUE(result.hasValue());
    auto pool = std::move(result).value();
    auto lease = pool->tryAcquire();
    ASSERT_TRUE(lease.has_value());
    lease->bytes()[0] = std::byte{0x11};
    lease->bytes()[3] = std::byte{0x44};

    SharedBuffer sealed = std::move(*lease).seal();
    lease.reset();
    EXPECT_EQ(sealed.bytes()[0], std::byte{0x11});
    EXPECT_EQ(sealed.bytes()[3], std::byte{0x44});
    EXPECT_EQ(pool->stats().inUse, 1U);
    EXPECT_FALSE(pool->tryAcquire().has_value());

    {
        const SharedBuffer copy = sealed;
        sealed = SharedBuffer{};
        EXPECT_EQ(copy.bytes().size(), 4U);
        EXPECT_EQ(pool->stats().inUse, 1U);
    }

    EXPECT_EQ(pool->stats().inUse, 0U);
    EXPECT_TRUE(pool->tryAcquire().has_value());
}

TEST(BufferPool, SealedBufferSafelyOutlivesPublicPoolHandle) {
    auto result = BufferPool::create(1U, 8U);
    ASSERT_TRUE(result.hasValue());
    std::weak_ptr<BufferPool> publicPool = result.value();
    auto lease = result.value()->tryAcquire();
    ASSERT_TRUE(lease.has_value());
    lease->bytes()[2] = std::byte{0x7F};
    SharedBuffer sealed = std::move(*lease).seal();
    lease.reset();

    result.value().reset();
    EXPECT_TRUE(publicPool.expired());
    ASSERT_EQ(sealed.bytes().size(), 8U);
    EXPECT_EQ(sealed.bytes()[2], std::byte{0x7F});

    sealed = SharedBuffer{};
    SUCCEED();
}

TEST(BufferPool, ConcurrentReuseRemainsBoundedForOneHundredThousandAcquisitions) {
    constexpr std::size_t capacity = 4U;
    constexpr std::size_t threadCount = 4U;
    constexpr std::size_t acquisitionsPerThread = 25'000U;

    auto result = BufferPool::create(capacity, 64U);
    ASSERT_TRUE(result.hasValue());
    auto pool = std::move(result).value();

    std::vector<WritableBufferLease> saturation;
    saturation.reserve(capacity);
    for (std::size_t index = 0U; index < capacity; ++index) {
        auto lease = pool->tryAcquire();
        ASSERT_TRUE(lease.has_value());
        saturation.push_back(std::move(*lease));
    }
    saturation.clear();

    std::atomic<std::size_t> successfulAcquisitions{0U};
    std::atomic<std::size_t> greatestObservedInUse{0U};
    std::vector<std::thread> producers;
    producers.reserve(threadCount);

    for (std::size_t threadIndex = 0U; threadIndex < threadCount; ++threadIndex) {
        producers.emplace_back([pool, &successfulAcquisitions, &greatestObservedInUse] {
            for (std::size_t count = 0U; count < acquisitionsPerThread;) {
                auto lease = pool->tryAcquire();
                if (!lease.has_value()) {
                    std::this_thread::yield();
                    continue;
                }

                lease->bytes()[0] = std::byte{0x5A};
                ++count;
                successfulAcquisitions.fetch_add(1U, std::memory_order_relaxed);

                const auto observed = pool->stats().inUse;
                auto previous = greatestObservedInUse.load(std::memory_order_relaxed);
                while (previous < observed &&
                       !greatestObservedInUse.compare_exchange_weak(
                           previous, observed, std::memory_order_relaxed)) {
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    const auto finalStats = pool->stats();
    EXPECT_EQ(successfulAcquisitions.load(std::memory_order_relaxed), 100'000U);
    EXPECT_LE(greatestObservedInUse.load(std::memory_order_relaxed), capacity);
    EXPECT_EQ(finalStats.inUse, 0U);
    EXPECT_EQ(finalStats.available, capacity);
    EXPECT_EQ(finalStats.highWaterMark, capacity);
}

}  // namespace
