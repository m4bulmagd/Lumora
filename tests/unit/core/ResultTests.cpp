#include <lumora/core/Error.hpp>
#include <lumora/core/Result.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace {

using lumora::core::Error;
using lumora::core::ErrorCategory;
using lumora::core::Result;

TEST(Result, PreservesTypedError) {
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
    EXPECT_EQ(result.error().diagnosticDetail,
              "Payload is shorter than the declared image layout.");
    EXPECT_FALSE(result.error().recoverable);
    EXPECT_FALSE(result.error().nativeCode.has_value());
}

TEST(Result, WrongAlternativeCannotBeExposed) {
    auto failed = Result<int>::failure(Error{
        ErrorCategory::Internal,
        "failed",
        "Operation failed",
        "Test failure",
        false,
    });
    auto succeeded = Result<int>::success(7);

    EXPECT_THROW(static_cast<void>(failed.value()), std::bad_variant_access);
    EXPECT_THROW(static_cast<void>(succeeded.error()), std::bad_variant_access);
}

TEST(Result, SupportsMoveOnlyValues) {
    auto result = Result<std::unique_ptr<int>>::success(std::make_unique<int>(42));

    ASSERT_TRUE(result.hasValue());
    auto value = std::move(result).value();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);
}

TEST(Result, VoidSuccessAndFailureRemainDistinct) {
    auto success = Result<void>::success();
    auto failure = Result<void>::failure(Error{
        ErrorCategory::Cancelled,
        "cancelled",
        "Operation cancelled",
        "A stop request interrupted the operation.",
        true,
    });

    EXPECT_TRUE(success.hasValue());
    ASSERT_FALSE(failure.hasValue());
    EXPECT_EQ(failure.error().code, "cancelled");
}

}  // namespace
