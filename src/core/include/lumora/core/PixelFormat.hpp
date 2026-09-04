#pragma once

#include <lumora/core/Result.hpp>

#include <cstdint>
#include <string>

namespace lumora::core {

enum class StorageType {
    UInt8,
    UInt16,
};

enum class DisplayStorage {
    Gray8,
    Gray16,
};

enum class SourcePacking {
    Unpacked,
    Packed,
};

enum class BitAlignment {
    LeastSignificant,
    MostSignificant,
};

struct SourcePixelFormat final {
    std::string canonicalName;
    std::uint32_t canonicalEncoding;
    std::uint8_t validBits;
    std::uint16_t sampleMaximum;
    SourcePacking packing;
    BitAlignment alignment;
    StorageType applicationStorage;
};

[[nodiscard]] Result<void> validateSourcePixelFormat(
    const SourcePixelFormat& format);

}  // namespace lumora::core
