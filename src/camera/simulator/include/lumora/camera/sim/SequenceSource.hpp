#pragma once

#include <lumora/core/PixelFormat.hpp>
#include <lumora/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace lumora::camera::sim {

enum class SequenceEnd { Loop, Stop, Error };

struct SequenceReplayOptions final {
    std::filesystem::path directory;
    SequenceEnd end{SequenceEnd::Loop};
};

struct SequenceFrame final {
    std::uint32_t width;
    std::uint32_t height;
    core::SourcePixelFormat format;
    // Unpacked native-endian application samples, never normalized or scaled.
    std::vector<std::byte> pixels;
};

// Synthetic/evaluation files only. Directory contents are validated and loaded
// once in lexical filename order. Copies share immutable pixels, not cursors.
// Like ICameraDevice, cursor operations require externally serialized access.
// After maxval, consume one whitespace separator, treating CRLF as one pair.
// Raster bytes (including whitespace and '#') are never tokenized or skipped.
class SequenceSource final {
public:
    using Frame = std::shared_ptr<const SequenceFrame>;
    using NextResult = core::Result<std::optional<Frame>>;

    [[nodiscard]] static core::Result<SequenceSource> openDirectory(
        const std::filesystem::path& directory, SequenceEnd end);

    [[nodiscard]] const SequenceFrame& descriptor() const noexcept;
    // Stop is successful empty; Error is sequence_exhausted; Loop wraps.
    [[nodiscard]] NextResult next();
    // Device adapters prepare pixels using peek, then commit only publication.
    [[nodiscard]] NextResult peek() const;
    void commit() noexcept;

private:
    SequenceSource(std::vector<Frame> frames, SequenceEnd end);
    std::vector<Frame> frames_;
    SequenceEnd end_;
    std::size_t position_{0U};
};

}  // namespace lumora::camera::sim
