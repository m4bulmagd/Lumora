#pragma once

#include <lumora/core/Result.hpp>

#include <filesystem>

namespace lumora::diagnostics {

class Logging final {
public:
    [[nodiscard]] static core::Result<void> start(
        const std::filesystem::path& directory);
    static void shutdown() noexcept;

    Logging() = delete;
};

}  // namespace lumora::diagnostics
