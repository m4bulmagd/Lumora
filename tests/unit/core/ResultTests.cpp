#include <lumora/core/Error.hpp>
#include <lumora/core/Result.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <string>
#include <type_traits>
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

struct FailureConstructionTrace final {
    int copies{};
    int moves{};
    int commits{};
    bool throwOnCopy{};
    bool constructedAfterCommit{};
    const void* copiedAddress{};
};

struct ObservedFailure final {
    FailureConstructionTrace* trace;

    explicit ObservedFailure(FailureConstructionTrace& observed) : trace(&observed) {}
    ObservedFailure(const ObservedFailure& other) : trace(other.trace) {
        ++trace->copies;
        trace->constructedAfterCommit |= trace->commits != 0;
        trace->copiedAddress = this;
        if (trace->throwOnCopy) {
            throw std::bad_alloc{};
        }
    }
    ObservedFailure(ObservedFailure&& other) noexcept : trace(other.trace) {
        ++trace->moves;
        trace->constructedAfterCommit |= trace->commits != 0;
    }
};

template<typename Value>
Result<Value, ObservedFailure> committedFailureThroughHelper(
    const ObservedFailure& error, FailureConstructionTrace& trace) {
    return Result<Value, ObservedFailure>::failureAndCommit(error, [&trace]() noexcept {
        ++trace.commits;
    });
}

template<typename Value>
void verifyFailureIsCopiedIntoFinalStorageBeforeCommit() {
    FailureConstructionTrace trace;
    const ObservedFailure error{trace};
    const auto result = committedFailureThroughHelper<Value>(error, trace);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(trace.copies, 1);
    EXPECT_EQ(trace.moves, 0);
    EXPECT_EQ(trace.commits, 1);
    EXPECT_FALSE(trace.constructedAfterCommit);
    EXPECT_EQ(trace.copiedAddress, &result.error());
}

TEST(Result, FailureAndCommitBuildsFinalValueResultBeforeCommitWithoutErrorMoves) {
    verifyFailureIsCopiedIntoFinalStorageBeforeCommit<int>();
}

TEST(Result, FailureAndCommitBuildsFinalVoidResultBeforeCommitWithoutErrorMoves) {
    verifyFailureIsCopiedIntoFinalStorageBeforeCommit<void>();
}

TEST(Result, FailureAndCommitDoesNotCommitWhenFinalErrorCopyThrows) {
    FailureConstructionTrace trace;
    trace.throwOnCopy = true;
    const ObservedFailure error{trace};
    EXPECT_THROW(static_cast<void>(committedFailureThroughHelper<int>(error, trace)), std::bad_alloc);
    EXPECT_THROW(static_cast<void>(committedFailureThroughHelper<void>(error, trace)), std::bad_alloc);
    EXPECT_EQ(trace.copies, 2);
    EXPECT_EQ(trace.moves, 0);
    EXPECT_EQ(trace.commits, 0);
}

struct CopyOnlyFailure final {
    explicit CopyOnlyFailure(int value) : code(value) {}
    CopyOnlyFailure(const CopyOnlyFailure&) = default;
    CopyOnlyFailure(CopyOnlyFailure&&) = delete;
    int code;
};

using ImmobileFailureResult = Result<std::unique_ptr<int>, CopyOnlyFailure>;
static_assert(!std::is_copy_constructible_v<ImmobileFailureResult>);
static_assert(!std::is_move_constructible_v<ImmobileFailureResult>);

ImmobileFailureResult committedImmobileFailure(const CopyOnlyFailure& error, bool& committed) {
    return ImmobileFailureResult::failureAndCommit(error, [&committed]() noexcept { committed = true; });
}

TEST(Result, FailureAndCommitSupportsAnImmobileReturnThroughPrvalueExpressions) {
    const CopyOnlyFailure error{42};
    bool committed = false;
    const auto result = committedImmobileFailure(error, committed);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, 42);
    EXPECT_TRUE(committed);
}

}  // namespace
