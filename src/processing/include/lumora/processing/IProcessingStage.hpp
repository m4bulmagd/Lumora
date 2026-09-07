#pragma once

#include <lumora/processing/ImageView.hpp>

namespace lumora::processing {

// A configured stage owns its parameters. The executor owns frame timing,
// scratch leases, and lifetime of all borrowed views. Source format describes
// this frame's provenance; application samples have already been unpacked.
class IProcessingStage {
public:
    virtual ~IProcessingStage() = default;
    [[nodiscard]] virtual StageId id() const noexcept = 0;
    [[nodiscard]] virtual const StageTraits& traits() const noexcept = 0;
    // Implementations reject incompatible dimensions/domains/storage and any
    // overlapping input/output before writing. RawFrame exposes only const data.
    [[nodiscard]] virtual core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const = 0;
};

}  // namespace lumora::processing
