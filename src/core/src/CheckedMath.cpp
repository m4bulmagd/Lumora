#include <lumora/core/CheckedMath.hpp>

#include <lumora/core/Error.hpp>

#include <limits>
#include <string>
#include <utility>

namespace lumora::core {
namespace {

[[nodiscard]] Error arithmeticError(std::string code, std::string detail) {
    return Error{
        ErrorCategory::ResourceExhaustion,
        std::move(code),
        "The requested memory size is too large.",
        std::move(detail),
        true,
    };
}

}  // namespace

Result<std::size_t> checkedMultiply(std::size_t left, std::size_t right) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (left != 0U && right > maximum / left) {
        return Result<std::size_t>::failure(arithmeticError(
            "size_multiplication_overflow",
            "Multiplying the requested size values would overflow size_t."));
    }
    return Result<std::size_t>::success(left * right);
}

Result<std::size_t> checkedAdd(std::size_t left, std::size_t right) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (right > maximum - left) {
        return Result<std::size_t>::failure(arithmeticError(
            "size_addition_overflow",
            "Adding the requested size values would overflow size_t."));
    }
    return Result<std::size_t>::success(left + right);
}

}  // namespace lumora::core
