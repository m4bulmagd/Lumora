#pragma once

#include <lumora/processing/IProcessingStage.hpp>

#include <array>
#include <cstdint>

namespace lumora::processing {

class BrightnessContrastStage final : public IProcessingStage {
public:
    explicit BrightnessContrastStage(BrightnessContrastParameters parameters = {});

    [[nodiscard]] StageId id() const noexcept override;
    [[nodiscard]] const StageTraits& traits() const noexcept override;
    [[nodiscard]] core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    const BrightnessContrastParameters parameters_;
};

class GammaStage final : public IProcessingStage {
public:
    explicit GammaStage(GammaParameters parameters = {});

    [[nodiscard]] StageId id() const noexcept override;
    [[nodiscard]] const StageTraits& traits() const noexcept override;
    [[nodiscard]] core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    const GammaParameters parameters_;
    const std::array<std::uint16_t, 65536U> lut_;
};

class InvertStage final : public IProcessingStage {
public:
    explicit InvertStage(InvertParameters parameters = {});

    [[nodiscard]] StageId id() const noexcept override;
    [[nodiscard]] const StageTraits& traits() const noexcept override;
    [[nodiscard]] core::Result<void> process(
        const ImageView& source,
        MutableImageView destination,
        const core::SourcePixelFormat& sourceFormat) const override;

private:
    const InvertParameters parameters_;
};

}  // namespace lumora::processing
