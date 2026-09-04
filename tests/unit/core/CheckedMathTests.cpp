#include <lumora/core/CheckedMath.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

namespace {

using lumora::core::checkedAdd;
using lumora::core::checkedMultiply;

TEST(CheckedMath, MultipliesZeroWithoutOverflow) {
    const auto result = checkedMultiply(std::numeric_limits<std::size_t>::max(), 0U);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 0U);
}

TEST(CheckedMath, MultipliesAtMaximumBoundary) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    const auto result = checkedMultiply(maximum / 2U, 2U);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), maximum - 1U);
}

TEST(CheckedMath, RejectsMultiplicationOverflow) {
    const auto result = checkedMultiply(std::numeric_limits<std::size_t>::max(), 2U);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "size_multiplication_overflow");
}

TEST(CheckedMath, AddsAtMaximumBoundary) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    const auto result = checkedAdd(maximum - 1U, 1U);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), maximum);
}

TEST(CheckedMath, RejectsAdditionOverflow) {
    const auto result = checkedAdd(std::numeric_limits<std::size_t>::max(), 1U);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "size_addition_overflow");
}

}  // namespace
