#include <lumora/core/Error.hpp>
#include <lumora/core/Result.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using lumora::core::Error;
using lumora::core::ErrorCategory;
using lumora::core::Result;

TEST(ResultSmoke, PreservesTypedErrorDetails) {
    Error error{
        ErrorCategory::InvalidFrame,
        "payload_small",
        "Invalid frame",
        "Payload is shorter than the declared image layout.",
        false,
    };

    auto result = Result<int>::failure(error);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, ErrorCategory::InvalidFrame);
    EXPECT_EQ(result.error().code, "payload_small");
    EXPECT_EQ(result.error().operatorSummary, "Invalid frame");
    EXPECT_FALSE(result.error().recoverable);
}

TEST(ResultSmoke, CarriesMoveOnlyValues) {
    auto result = Result<std::unique_ptr<int>>::success(std::make_unique<int>(42));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.value(), 42);
}

TEST(ResultSmoke, RepresentsVoidSuccessAndFailure) {
    auto success = Result<void>::success();
    auto failure = Result<void>::failure(Error{
        ErrorCategory::Internal,
        "test_failure",
        "Operation failed",
        "Expected failure used by the result contract test.",
        false,
    });

    EXPECT_TRUE(success.hasValue());
    ASSERT_FALSE(failure.hasValue());
    EXPECT_EQ(failure.error().code, "test_failure");
}

}  // namespace
