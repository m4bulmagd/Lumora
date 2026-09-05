#include <lumora/camera/sim/SequenceSource.hpp>

#include <lumora/core/CheckedMath.hpp>
#include <lumora/core/Error.hpp>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace lumora::camera::sim {
namespace {

core::Error sequenceError(
    std::string code, std::string detail,
    core::ErrorCategory category = core::ErrorCategory::InvalidFrame) {
    return {category, std::move(code), "The evaluation sequence could not be read.",
            std::move(detail), true};
}

bool whitespace(int byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' ||
           byte == '\v' || byte == '\f';
}

// Token scanning is confined to the header. Never call this after maxval.
bool skipHeaderSeparators(std::istream& stream) {
    for (;;) {
        const auto byte = stream.peek();
        if (whitespace(byte)) {
            stream.get();
        } else if (byte == '#') {
            int current{};
            do {
                current = stream.get();
            } while (current != '\r' && current != '\n' && current != EOF);
            if (current == EOF) {
                return false;
            }
        } else {
            return byte != EOF;
        }
    }
}

std::optional<std::uint32_t> readNumber(std::istream& stream) {
    if (!skipHeaderSeparators(stream)) {
        return std::nullopt;
    }
    // Accumulate without token allocation. Leading zeros are legal; reject
    // overflow before multiplication rather than bounding the token length.
    std::uint32_t result{};
    bool hasDigit = false;
    for (auto byte = stream.peek(); byte != EOF && !whitespace(byte) && byte != '#';
         byte = stream.peek()) {
        if (byte < '0' || byte > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint32_t>(stream.get() - '0');
        if (result > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        result = result * 10U + digit;
        hasDigit = true;
    }
    if (!hasDigit || result == 0U) {
        return std::nullopt;
    }
    return result;
}

core::Result<SequenceSource::Frame> readPgm(const std::filesystem::path& path) {
    using ReadResult = core::Result<SequenceSource::Frame>;
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return ReadResult::failure(sequenceError("sequence_io_error", "Opening a PGM file failed.", core::ErrorCategory::Storage));
    }
    const auto end = stream.tellg();
    if (end < std::streampos{0}) {
        return ReadResult::failure(sequenceError("sequence_io_error", "Reading PGM file length failed.", core::ErrorCategory::Storage));
    }
    stream.seekg(0);
    if (stream.get() != 'P' || stream.get() != '5' || !whitespace(stream.peek())) {
        return ReadResult::failure(sequenceError("invalid_pgm_header", "Expected binary P5 followed by whitespace."));
    }
    const auto width = readNumber(stream);
    const auto height = readNumber(stream);
    const auto maximum = readNumber(stream);
    if (!width || !height || !maximum || *maximum > 65535U || !whitespace(stream.peek())) {
        return ReadResult::failure(sequenceError("invalid_pgm_header", "Invalid dimensions, maxval, or required raster separator."));
    }

    const std::size_t sampleBytes = *maximum <= 255U ? 1U : 2U;
    const auto samples = core::checkedMultiply(*width, *height);
    if (!samples.hasValue()) {
        return ReadResult::failure(sequenceError("pgm_size_overflow", "PGM sample count overflows size_t."));
    }
    const auto payload = core::checkedMultiply(samples.value(), sampleBytes);
    if (!payload.hasValue() || payload.value() > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return ReadResult::failure(sequenceError("pgm_size_overflow", "PGM byte count cannot be represented."));
    }
    const auto separator = stream.get();
    auto rasterStart = stream.tellg();
    if (rasterStart < std::streampos{0} || end < rasterStart) {
        return ReadResult::failure(sequenceError("invalid_pgm_payload", "Missing PGM raster."));
    }
    // CRLF always means one Windows header separator (including for short
    // payloads). There is no general whitespace/comment skipping after maxval.
    if (separator == '\r' && stream.peek() == '\n') {
        stream.get();
        rasterStart = stream.tellg();
    }
    if (static_cast<std::uintmax_t>(end - rasterStart) != payload.value()) {
        return ReadResult::failure(sequenceError("invalid_pgm_payload", "PGM raster length must exactly match dimensions and storage."));
    }

    std::vector<std::byte> pixels(payload.value());
    stream.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(pixels.size()) || stream.peek() != EOF) {
        return ReadResult::failure(sequenceError("invalid_pgm_payload", "PGM raster is short, trailing, or unreadable."));
    }
    for (std::size_t offset = 0U; offset < pixels.size(); offset += sampleBytes) {
        std::uint16_t value = std::to_integer<std::uint8_t>(pixels[offset]);
        if (sampleBytes == 2U) {
            value = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(value) * 256U +
                std::to_integer<std::uint8_t>(pixels[offset + 1U]));
            std::memcpy(pixels.data() + offset, &value, sizeof(value));
        }
        if (value > *maximum) {
            return ReadResult::failure(sequenceError("pgm_sample_above_maximum", "A PGM sample exceeds its declared maxval."));
        }
    }
    return ReadResult::success(std::make_shared<const SequenceFrame>(SequenceFrame{
        *width, *height,
        {"PGM.P5.Max" + std::to_string(*maximum),
         0x80000000U | *maximum, // Simulator-private stable encoding, not PFNC.
         static_cast<std::uint8_t>(std::bit_width(*maximum)),
         static_cast<std::uint16_t>(*maximum),
         core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
         sampleBytes == 1U ? core::StorageType::UInt8 : core::StorageType::UInt16},
        std::move(pixels)}));
}

}  // namespace

core::Result<SequenceSource> SequenceSource::openDirectory(
    const std::filesystem::path& directory, SequenceEnd end) {
    using OpenResult = core::Result<SequenceSource>;
    if (end != SequenceEnd::Loop && end != SequenceEnd::Stop && end != SequenceEnd::Error) {
        return OpenResult::failure(sequenceError("invalid_sequence_policy", "Unknown sequence end policy.", core::ErrorCategory::CameraConfiguration));
    }
    try {
        if (!std::filesystem::is_directory(directory)) {
            return OpenResult::failure(sequenceError("sequence_not_directory", "The sequence path must identify a directory.", core::ErrorCategory::Storage));
        }
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!std::filesystem::is_regular_file(entry.symlink_status())) {
                return OpenResult::failure(sequenceError("sequence_entry_not_file", "Sequence directories must contain regular PGM files only.", core::ErrorCategory::Storage));
            }
            files.push_back(entry.path());
        }
        if (files.empty()) {
            return OpenResult::failure(sequenceError("sequence_empty", "The sequence directory contains no frames."));
        }
        std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
            return left.filename() < right.filename();
        });
        std::vector<Frame> frames;
        for (const auto& file : files) {
            auto frame = readPgm(file);
            if (!frame.hasValue()) {
                return OpenResult::failure(frame.error());
            }
            if (!frames.empty()) {
                const auto& first = *frames.front();
                const auto& current = *frame.value();
                // Storage, valid bits, canonical descriptor are functions of maxval.
                if (first.width != current.width || first.height != current.height ||
                    first.format.sampleMaximum != current.format.sampleMaximum) {
                    return OpenResult::failure(sequenceError("sequence_format_mismatch", "All sequence frames must have identical geometry and sample format."));
                }
            }
            frames.push_back(std::move(frame).value());
        }
        return OpenResult::success(SequenceSource{std::move(frames), end});
    } catch (const std::filesystem::filesystem_error& error) {
        return OpenResult::failure(sequenceError("sequence_io_error", error.what(), core::ErrorCategory::Storage));
    } catch (const std::ios_base::failure& error) {
        return OpenResult::failure(sequenceError("sequence_io_error", error.what(), core::ErrorCategory::Storage));
    } catch (const std::bad_alloc&) {
        return OpenResult::failure(sequenceError("sequence_allocation_failed", "Allocating sequence frames failed.", core::ErrorCategory::ResourceExhaustion));
    } catch (const std::length_error&) {
        return OpenResult::failure(sequenceError("sequence_allocation_failed", "Sequence storage exceeds container limits.", core::ErrorCategory::ResourceExhaustion));
    }
}

SequenceSource::SequenceSource(std::vector<Frame> frames, SequenceEnd end)
    : frames_(std::move(frames)), end_(end) {}

const SequenceFrame& SequenceSource::descriptor() const noexcept {
    return *frames_.front();
}

SequenceSource::NextResult SequenceSource::peek() const {
    if (position_ < frames_.size()) {
        return NextResult::success(frames_[position_]);
    }
    if (end_ == SequenceEnd::Stop) {
        return NextResult::success(std::nullopt);
    }
    return NextResult::failure(sequenceError("sequence_exhausted", "The last sequence frame has been published.", core::ErrorCategory::Acquisition));
}

void SequenceSource::commit() noexcept {
    if (position_ < frames_.size()) {
        ++position_;
        if (position_ == frames_.size() && end_ == SequenceEnd::Loop) {
            position_ = 0U;
        }
    }
}

SequenceSource::NextResult SequenceSource::next() {
    auto frame = peek();
    if (frame.hasValue() && frame.value().has_value()) {
        commit();
    }
    return frame;
}

}  // namespace lumora::camera::sim
