/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2013, NVIDIA Corporation, all rights reserved.
// Copyright (C) 2014, Itseez Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the copyright holders or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

// Adapted by Lumora from OpenCV 4.12.0 modules/imgproc/src/clahe.cpp.
// This U16-only implementation owns prepared sequential arrays, reads reflected
// samples directly from byte views, and performs no scheduler or backend dispatch.

#include <lumora/processing/ClaheStage.hpp>
#include "StageStorage.hpp"

#include <lumora/core/CheckedMath.hpp>

#include <opencv2/core/saturate.hpp>

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lumora::processing {
namespace {

constexpr std::size_t histogramBins = 65536U;
constexpr std::size_t defaultScratchBudgetBytes = 256U * 1024U * 1024U;

static_assert(sizeof(std::uint16_t) == 2U);
static_assert(sizeof(std::int32_t) == 4U);
static_assert(sizeof(std::uint32_t) == 4U);
static_assert(sizeof(int) == 4U);
static_assert(sizeof(float) == 4U);

using StageResult = core::Result<std::unique_ptr<ClaheStage>>;

struct PreparedPlan final {
    int width;
    int height;
    int grid;
    int tileWidth;
    int tileHeight;
    int clipCount;
    std::uint32_t reflectedWidth;
    std::uint32_t reflectedHeight;
    float lutScale;
    std::size_t lutElements;
    std::size_t scratchBytes;
};

[[nodiscard]] core::Error claheError(core::ErrorCategory category,
    std::string code, std::string detail, bool recoverable = false) {
    return {category, std::move(code), "Local contrast processing failed.",
        std::move(detail), recoverable};
}

[[nodiscard]] StageResult factoryFailure(std::string code, std::string detail) {
    return StageResult::failure(claheError(
        core::ErrorCategory::Processing, std::move(code), std::move(detail)));
}

[[nodiscard]] core::Result<PreparedPlan> planFailure(std::string code,
    std::string detail,
    core::ErrorCategory category = core::ErrorCategory::Processing,
    bool recoverable = false) {
    return core::Result<PreparedPlan>::failure(claheError(
        category, std::move(code), std::move(detail), recoverable));
}

[[nodiscard]] core::Result<void> processFailure(
    std::string code, std::string detail) {
    return core::Result<void>::failure(claheError(
        core::ErrorCategory::Processing, std::move(code), std::move(detail)));
}

[[nodiscard]] bool checkedAccumulate(std::size_t& total,
    std::size_t count, std::size_t elementSize) {
    const auto bytes = core::checkedMultiply(count, elementSize);
    if (!bytes.hasValue()) return false;
    const auto combined = core::checkedAdd(total, bytes.value());
    if (!combined.hasValue()) return false;
    total = combined.value();
    return true;
}

[[nodiscard]] core::Result<PreparedPlan> makePlan(
    ClaheParameters parameters, const core::ImageLayout& layout) {
    if (!std::isfinite(parameters.clipLimit)
        || parameters.clipLimit < 0.1 || parameters.clipLimit > 40.0) {
        return planFailure("clahe_invalid_clip_limit",
            "CLAHE clip limit must be finite and within [0.1, 40].");
    }
    if (parameters.tileGridSize < 2U || parameters.tileGridSize > 32U) {
        return planFailure("clahe_invalid_tile_grid",
            "CLAHE tile grid size must be an integer within [2, 32].");
    }
    if (layout.storage() != core::StorageType::UInt16) {
        return planFailure("clahe_layout_storage_mismatch",
            "CLAHE preparation requires unsigned 16-bit storage.");
    }
    if (layout.width() < parameters.tileGridSize
        || layout.height() < parameters.tileGridSize) {
        return planFailure("clahe_image_too_small",
            "Prepared width and height must each be at least the tile grid size.");
    }

    constexpr auto maximumSignedDimension =
        static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (layout.width() > maximumSignedDimension
        || layout.height() > maximumSignedDimension) {
        return planFailure("clahe_dimension_out_of_range",
            "Prepared dimensions do not fit the retained signed CLAHE dimensions.");
    }

    const auto grid = static_cast<std::uint64_t>(parameters.tileGridSize);
    std::uint64_t reflectedWidth = layout.width();
    std::uint64_t reflectedHeight = layout.height();
    if (layout.width() % parameters.tileGridSize != 0U
        || layout.height() % parameters.tileGridSize != 0U) {
        reflectedWidth += grid - layout.width() % parameters.tileGridSize;
        reflectedHeight += grid - layout.height() % parameters.tileGridSize;
        if (reflectedWidth > maximumSignedDimension
            || reflectedHeight > maximumSignedDimension) {
            return planFailure("clahe_reflected_extent_overflow",
                "The reflected CLAHE extent does not fit signed dimensions.");
        }
    }

    const auto tileWidth = reflectedWidth / grid;
    const auto tileHeight = reflectedHeight / grid;
    if (tileWidth != 0U
        && tileHeight > static_cast<std::uint64_t>(
               std::numeric_limits<int>::max()) / tileWidth) {
        return planFailure("clahe_tile_area_overflow",
            "CLAHE tile area does not fit the retained signed accumulator.");
    }
    if (layout.width() > maximumSignedDimension / 4U) {
        return planFailure("clahe_interpolation_size_overflow",
            "CLAHE interpolation width-times-four does not fit a signed integer.");
    }

    // Historical bridge-named errors remain stable public validation codes even
    // though the prepared implementation no longer owns full-image bridges.
    const auto sampleCount = core::checkedMultiply(
        static_cast<std::size_t>(layout.width()),
        static_cast<std::size_t>(layout.height()));
    if (!sampleCount.hasValue()) {
        return planFailure("clahe_bridge_size_overflow",
            "Computing the historical prepared U16 sample count overflowed size_t.");
    }
    const auto rowBytes = core::checkedMultiply(
        static_cast<std::size_t>(layout.width()), sizeof(std::uint16_t));
    if (!rowBytes.hasValue()) {
        return planFailure("clahe_bridge_size_overflow",
            "Computing the historical prepared U16 row size overflowed size_t.");
    }

    const auto tileCount = core::checkedMultiply(
        static_cast<std::size_t>(parameters.tileGridSize),
        static_cast<std::size_t>(parameters.tileGridSize));
    if (!tileCount.hasValue()) {
        return planFailure("clahe_scratch_size_overflow",
            "Computing the CLAHE tile count overflowed size_t.",
            core::ErrorCategory::ResourceExhaustion, true);
    }
    const auto lutElements = core::checkedMultiply(tileCount.value(), histogramBins);
    if (!lutElements.hasValue()) {
        return planFailure("clahe_scratch_size_overflow",
            "Computing the CLAHE LUT length overflowed size_t.",
            core::ErrorCategory::ResourceExhaustion, true);
    }
    std::size_t scratchBytes = 0U;
    const bool scratchValid =
        checkedAccumulate(scratchBytes, lutElements.value(), sizeof(std::uint16_t))
        && checkedAccumulate(scratchBytes, histogramBins, sizeof(std::int32_t))
        && checkedAccumulate(scratchBytes, static_cast<std::size_t>(layout.width()),
            2U * sizeof(std::int32_t))
        && checkedAccumulate(scratchBytes, static_cast<std::size_t>(layout.width()),
            2U * sizeof(float))
        && checkedAccumulate(scratchBytes, static_cast<std::size_t>(reflectedWidth),
            sizeof(std::uint32_t))
        && checkedAccumulate(scratchBytes, static_cast<std::size_t>(reflectedHeight),
            sizeof(std::uint32_t));
    if (!scratchValid) {
        return planFailure("clahe_scratch_size_overflow",
            "Computing the retained CLAHE array bytes overflowed size_t.",
            core::ErrorCategory::ResourceExhaustion, true);
    }

    const auto area = static_cast<int>(tileWidth * tileHeight);
    const auto clipCount = std::max(static_cast<int>(parameters.clipLimit
        * static_cast<double>(area) / static_cast<double>(histogramBins)), 1);
    return core::Result<PreparedPlan>::success({
        static_cast<int>(layout.width()), static_cast<int>(layout.height()),
        static_cast<int>(parameters.tileGridSize), static_cast<int>(tileWidth),
        static_cast<int>(tileHeight), clipCount,
        static_cast<std::uint32_t>(reflectedWidth),
        static_cast<std::uint32_t>(reflectedHeight),
        static_cast<float>(histogramBins - 1U) / static_cast<float>(area),
        lutElements.value(), scratchBytes});
}

[[nodiscard]] std::uint32_t reflectedIndex(
    std::uint64_t position, std::uint32_t extent) noexcept {
    const auto period = 2U * (static_cast<std::uint64_t>(extent) - 1U);
    const auto folded = position % period;
    return static_cast<std::uint32_t>(
        folded < extent ? folded : period - folded);
}

[[nodiscard]] std::uint16_t loadU16(const std::byte* source) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

void storeU16(std::byte* destination, std::uint16_t value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

}  // namespace

struct ClaheStage::Impl final {
    explicit Impl(const PreparedPlan& plan)
        : width(plan.width), height(plan.height), grid(plan.grid),
          tileWidth(plan.tileWidth), tileHeight(plan.tileHeight),
          clipCount(plan.clipCount), lutScale(plan.lutScale),
          retainedBytes(plan.scratchBytes), lut(plan.lutElements),
          histogram(histogramBins), xIndex1(static_cast<std::size_t>(plan.width)),
          xIndex2(static_cast<std::size_t>(plan.width)),
          xWeight(static_cast<std::size_t>(plan.width)),
          xWeight1(static_cast<std::size_t>(plan.width)),
          reflectedX(plan.reflectedWidth), reflectedY(plan.reflectedHeight) {
        for (std::uint32_t x = 0U; x < plan.reflectedWidth; ++x)
            reflectedX[x] = reflectedIndex(x, static_cast<std::uint32_t>(plan.width));
        for (std::uint32_t y = 0U; y < plan.reflectedHeight; ++y)
            reflectedY[y] = reflectedIndex(y, static_cast<std::uint32_t>(plan.height));

        const float inverseTileWidth = 1.0F / static_cast<float>(tileWidth);
        for (int x = 0; x < width; ++x) {
            const float tileX = static_cast<float>(x) * inverseTileWidth - 0.5F;
            int first = static_cast<int>(std::floor(tileX));
            int second = first + 1;
            const auto index = static_cast<std::size_t>(x);
            xWeight[index] = tileX - static_cast<float>(first);
            xWeight1[index] = 1.0F - xWeight[index];
            first = std::max(first, 0);
            second = std::min(second, grid - 1);
            xIndex1[index] = static_cast<std::int32_t>(
                first * static_cast<int>(histogramBins));
            xIndex2[index] = static_cast<std::int32_t>(
                second * static_cast<int>(histogramBins));
        }
    }

    int width;
    int height;
    int grid;
    int tileWidth;
    int tileHeight;
    int clipCount;
    float lutScale;
    std::size_t retainedBytes;
    std::vector<std::uint16_t> lut;
    std::vector<std::int32_t> histogram;
    std::vector<std::int32_t> xIndex1;
    std::vector<std::int32_t> xIndex2;
    std::vector<float> xWeight;
    std::vector<float> xWeight1;
    std::vector<std::uint32_t> reflectedX;
    std::vector<std::uint32_t> reflectedY;
};

std::size_t detail::StageStorage::claheOwnerBytes() noexcept {
    return sizeof(ClaheStage) + sizeof(ClaheStage::Impl);
}

ClaheStage::ClaheStage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ClaheStage::~ClaheStage() = default;

core::Result<std::size_t> ClaheStage::requiredScratchBytes(
    ClaheParameters parameters, const core::ImageLayout& layout) {
    const auto plan = makePlan(parameters, layout);
    if (!plan.hasValue()) return core::Result<std::size_t>::failure(plan.error());
    return core::Result<std::size_t>::success(plan.value().scratchBytes);
}

core::Result<std::unique_ptr<ClaheStage>> ClaheStage::create(
    ClaheParameters parameters, const core::ImageLayout& layout) {
    return create(parameters, layout, defaultScratchBudgetBytes);
}

core::Result<std::unique_ptr<ClaheStage>> ClaheStage::create(
    ClaheParameters parameters, const core::ImageLayout& layout,
    std::size_t scratchBudgetBytes) {
    const auto plan = makePlan(parameters, layout);
    if (!plan.hasValue()) return StageResult::failure(plan.error());
    if (std::fegetround() != FE_TONEAREST) {
        return factoryFailure("clahe_rounding_mode_unsupported",
            "Prepared CLAHE requires the FE_TONEAREST floating-point rounding mode.");
    }
    if (plan.value().scratchBytes > scratchBudgetBytes) {
        return StageResult::failure(claheError(core::ErrorCategory::ResourceExhaustion,
            "clahe_scratch_budget_exceeded",
            "The retained CLAHE arrays exceed the supplied scratch byte budget.", true));
    }
    try {
        auto impl = std::make_unique<Impl>(plan.value());
        return StageResult::success(
            std::unique_ptr<ClaheStage>(new ClaheStage(std::move(impl))));
    } catch (const std::bad_alloc&) {
        return StageResult::failure(claheError(core::ErrorCategory::ResourceExhaustion,
            "clahe_allocation_failed",
            "CLAHE preparation could not allocate its fixed retained arrays.", true));
    } catch (const std::length_error&) {
        return StageResult::failure(claheError(core::ErrorCategory::ResourceExhaustion,
            "clahe_allocation_failed",
            "CLAHE preparation could not represent its fixed retained arrays.", true));
    }
}

std::size_t ClaheStage::scratchBytes() const noexcept { return impl_->retainedBytes; }
StageId ClaheStage::id() const noexcept { return StageId::Clahe; }

const StageTraits& ClaheStage::traits() const noexcept {
    static constexpr StageTraits traits{StageId::Clahe,
        ImageDomain::CanonicalU16, ImageDomain::CanonicalU16, false, 0U, 0U, false,
        ExecutionBackend::Cpu};
    return traits;
}

core::Result<void> ClaheStage::process(const ImageView& source,
    MutableImageView destination, const core::SourcePixelFormat&) const {
    if (source.layout().storage() != core::StorageType::UInt16)
        return processFailure("clahe_source_storage_mismatch", "CLAHE input must use unsigned 16-bit storage.");
    if (destination.layout().storage() != core::StorageType::UInt16)
        return processFailure("clahe_destination_storage_mismatch", "CLAHE output must use unsigned 16-bit storage.");
    if (source.domain() != ImageDomain::CanonicalU16
        || destination.domain() != ImageDomain::CanonicalU16)
        return processFailure("clahe_image_domain_mismatch", "CLAHE requires CanonicalU16 input and output.");
    if (source.layout().width() != destination.layout().width()
        || source.layout().height() != destination.layout().height())
        return processFailure("clahe_image_extent_mismatch", "CLAHE input and output extents must match.");
    if (source.layout().width() != static_cast<std::uint32_t>(impl_->width)
        || source.layout().height() != static_cast<std::uint32_t>(impl_->height))
        return processFailure("clahe_prepared_extent_mismatch", "CLAHE image extents must match the prepared stage extents.");
    if (overlaps(source, destination))
        return processFailure("clahe_image_views_overlap", "CLAHE input and output payload storage must not overlap.");
    if (std::fegetround() != FE_TONEAREST)
        return processFailure("clahe_rounding_mode_unsupported", "Prepared CLAHE requires the FE_TONEAREST floating-point rounding mode.");

    for (int tileY = 0; tileY < impl_->grid; ++tileY) {
        for (int tileX = 0; tileX < impl_->grid; ++tileX) {
            for (int localY = 0; localY < impl_->tileHeight; ++localY) {
                const auto reflectedY = impl_->reflectedY[static_cast<std::size_t>(
                    tileY * impl_->tileHeight + localY)];
                const auto sourceRow = source.row(reflectedY);
                for (int localX = 0; localX < impl_->tileWidth; ++localX) {
                    const auto reflectedX = impl_->reflectedX[static_cast<std::size_t>(
                        tileX * impl_->tileWidth + localX)];
                    const auto sample = loadU16(sourceRow.data()
                        + static_cast<std::size_t>(reflectedX) * sizeof(std::uint16_t));
                    ++impl_->histogram[sample];
                }
            }
            int clipped = 0;
            for (auto& count : impl_->histogram) {
                if (count > impl_->clipCount) {
                    clipped += count - impl_->clipCount;
                    count = impl_->clipCount;
                }
            }
            const int binCount = static_cast<int>(histogramBins);
            const int batch = clipped / binCount;
            const int residualCount = clipped - batch * binCount;
            const int residualStep = residualCount == 0
                ? 0 : std::max(binCount / residualCount, 1);
            int residualRemaining = residualCount;
            int nextResidualBin = 0;
            int cumulative = 0;
            const auto lutOffset = static_cast<std::size_t>(
                tileY * impl_->grid + tileX) * histogramBins;
            for (int bin = 0; bin < binCount; ++bin) {
                const auto index = static_cast<std::size_t>(bin);
                int redistributed = impl_->histogram[index] + batch;
                if (residualRemaining > 0 && bin == nextResidualBin) {
                    ++redistributed;
                    --residualRemaining;
                    nextResidualBin += residualStep;
                }
                cumulative += redistributed;
                impl_->histogram[index] = 0;
                impl_->lut[lutOffset + index] = cv::saturate_cast<std::uint16_t>(
                    static_cast<float>(cumulative) * impl_->lutScale);
            }
        }
    }

    const float inverseTileHeight = 1.0F / static_cast<float>(impl_->tileHeight);
    for (int y = 0; y < impl_->height; ++y) {
        const float tileY = static_cast<float>(y) * inverseTileHeight - 0.5F;
        int firstTileY = static_cast<int>(std::floor(tileY));
        int secondTileY = firstTileY + 1;
        const float yWeight = tileY - static_cast<float>(firstTileY);
        const float yWeight1 = 1.0F - yWeight;
        firstTileY = std::max(firstTileY, 0);
        secondTileY = std::min(secondTileY, impl_->grid - 1);
        const auto firstRowOffset = static_cast<std::size_t>(
            firstTileY * impl_->grid) * histogramBins;
        const auto secondRowOffset = static_cast<std::size_t>(
            secondTileY * impl_->grid) * histogramBins;
        const auto sourceRow = source.row(static_cast<std::uint32_t>(y));
        const auto destinationRow = destination.row(static_cast<std::uint32_t>(y));
        for (int x = 0; x < impl_->width; ++x) {
            const auto index = static_cast<std::size_t>(x);
            const auto sample = loadU16(sourceRow.data() + index * sizeof(std::uint16_t));
            const auto firstIndex = static_cast<std::size_t>(impl_->xIndex1[index]) + sample;
            const auto secondIndex = static_cast<std::size_t>(impl_->xIndex2[index]) + sample;
            const float result =
                (impl_->lut[firstRowOffset + firstIndex] * impl_->xWeight1[index]
                    + impl_->lut[firstRowOffset + secondIndex] * impl_->xWeight[index]) * yWeight1
                + (impl_->lut[secondRowOffset + firstIndex] * impl_->xWeight1[index]
                    + impl_->lut[secondRowOffset + secondIndex] * impl_->xWeight[index]) * yWeight;
            storeU16(destinationRow.data() + index * sizeof(std::uint16_t),
                cv::saturate_cast<std::uint16_t>(result));
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
