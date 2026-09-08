#pragma once
#include <cstddef>
namespace lumora::processing::detail {
struct StageStorage final {
    static std::size_t denoiseOwnerBytes() noexcept;
    static std::size_t sharpenOwnerBytes() noexcept;
    static std::size_t claheOwnerBytes() noexcept;
};
}
