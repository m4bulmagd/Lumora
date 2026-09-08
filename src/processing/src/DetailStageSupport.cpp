#include "DetailStageSupport.hpp"

#include <lumora/core/CheckedMath.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace lumora::processing::detail {

core::Error stageError(
    std::string_view stage,
    core::ErrorCategory category,
    std::string code,
    std::string detail,
    bool recoverable) {
    return {category, std::move(code),
        std::string(stage) + " processing failed.", std::move(detail), recoverable};
}

core::Result<void> validateProcessViews(
    std::string_view stage,
    const ImageView& source,
    const MutableImageView& destination,
    std::uint32_t preparedWidth,
    std::uint32_t preparedHeight) {
    const auto fail = [&](std::string suffix, std::string detail) {
        return core::Result<void>::failure(stageError(stage,
            core::ErrorCategory::Processing,
            std::string(stage) + "_" + std::move(suffix), std::move(detail)));
    };
    if (source.layout().storage() != core::StorageType::UInt16) {
        return fail("source_storage_mismatch",
            "The source image must use unsigned 16-bit storage.");
    }
    if (destination.layout().storage() != core::StorageType::UInt16) {
        return fail("destination_storage_mismatch",
            "The destination image must use unsigned 16-bit storage.");
    }
    if (source.domain() != ImageDomain::CanonicalU16
        || destination.domain() != ImageDomain::CanonicalU16) {
        return fail("image_domain_mismatch",
            "The source and destination must use the CanonicalU16 domain.");
    }
    if (source.layout().width() != destination.layout().width()
        || source.layout().height() != destination.layout().height()) {
        return fail("image_extent_mismatch",
            "The source and destination extents must match.");
    }
    if (source.layout().width() != preparedWidth
        || source.layout().height() != preparedHeight) {
        return fail("prepared_extent_mismatch",
            "The image extents must match the prepared stage extents.");
    }
    if (overlaps(source, destination)) {
        return fail("image_views_overlap",
            "The complete source and destination payloads must not overlap.");
    }
    return core::Result<void>::success();
}

core::Result<std::size_t> gaussianScratchBytes(
    std::string_view stage,
    const core::ImageLayout& layout,
    std::size_t kernelSize) {
    const auto fail = [&] {
        return core::Result<std::size_t>::failure(stageError(stage,
            core::ErrorCategory::ResourceExhaustion,
            std::string(stage) + "_scratch_size_overflow",
            "Computing the fixed Gaussian scratch storage overflowed size_t.", true));
    };
    const auto sampleCount = core::checkedMultiply(
        static_cast<std::size_t>(layout.width()),
        static_cast<std::size_t>(layout.height()));
    if (!sampleCount.hasValue()) return fail();
    const auto intermediateBytes = core::checkedMultiply(
        sampleCount.value(), sizeof(double));
    if (!intermediateBytes.hasValue()) return fail();
    const auto coefficientBytes = core::checkedMultiply(kernelSize, sizeof(double));
    if (!coefficientBytes.hasValue()) return fail();
    const auto total = core::checkedAdd(
        intermediateBytes.value(), coefficientBytes.value());
    if (!total.hasValue()) return fail();
    return core::Result<std::size_t>::success(total.value());
}

std::size_t gaussianKernelSize(double sigma) noexcept {
    const auto halfWidth = static_cast<std::size_t>(std::ceil(3.0 * sigma));
    return halfWidth * 2U + 1U;
}

void prepareGaussianKernel(
    std::size_t kernelSize,
    double sigma,
    double* coefficients) {
    const auto kernel = cv::getGaussianKernel(
        static_cast<int>(kernelSize), sigma, CV_64F);
    for (std::size_t index = 0U; index < kernelSize; ++index) {
        coefficients[index] = kernel.at<double>(static_cast<int>(index), 0);
    }
}

std::size_t reflect101(std::int64_t coordinate, std::size_t extent) noexcept {
    if (extent == 1U) return 0U;
    const auto period = static_cast<std::int64_t>(2U * (extent - 1U));
    auto reflected = coordinate % period;
    if (reflected < 0) reflected += period;
    if (reflected >= static_cast<std::int64_t>(extent)) {
        reflected = period - reflected;
    }
    return static_cast<std::size_t>(reflected);
}

std::uint16_t loadU16(
    const ImageView& image,
    std::size_t x,
    std::size_t y) noexcept {
    return loadU16(image.row(static_cast<std::uint32_t>(y)), x);
}

std::uint16_t loadU16(
    std::span<const std::byte> row,
    std::size_t x) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, row.data() + x * sizeof(std::uint16_t), sizeof(value));
    return value;
}

void storeU16(
    MutableImageView image,
    std::size_t x,
    std::size_t y,
    std::uint16_t value) noexcept {
    storeU16(image.row(static_cast<std::uint32_t>(y)), x, value);
}

void storeU16(
    std::span<std::byte> row,
    std::size_t x,
    std::uint16_t value) noexcept {
    std::memcpy(row.data() + x * sizeof(std::uint16_t), &value, sizeof(value));
}

void copyActivePixels(
    const ImageView& source,
    MutableImageView destination) noexcept {
    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        std::memcpy(destination.row(y).data(), source.row(y).data(),
            source.layout().rowBytes());
    }
}

void horizontalGaussian(
    const ImageView& source,
    double* intermediate,
    std::size_t width,
    std::size_t height,
    const double* coefficients,
    std::size_t kernelSize) noexcept {
    const auto radius = kernelSize / 2U;
    for (std::size_t y = 0U; y < height; ++y) {
        const auto sourceRow = source.row(static_cast<std::uint32_t>(y));
        for (std::size_t x = 0U; x < width; ++x) {
            double sum = 0.0;
            const bool interior = x >= radius && radius <= width - 1U - x;
            if (interior) {
                const auto firstX = x - radius;
                for (std::size_t index = 0U; index < kernelSize; ++index) {
                    sum += coefficients[index] * static_cast<double>(
                        loadU16(sourceRow, firstX + index));
                }
            } else {
                for (std::size_t index = 0U; index < kernelSize; ++index) {
                    const auto offset = static_cast<std::int64_t>(index)
                        - static_cast<std::int64_t>(radius);
                    const auto reflectedX = reflect101(
                        static_cast<std::int64_t>(x) + offset, width);
                    sum += coefficients[index] * static_cast<double>(
                        loadU16(sourceRow, reflectedX));
                }
            }
            intermediate[y * width + x] = sum;
        }
    }
}

void prepareVerticalGaussianRows(
    const double* intermediate,
    std::size_t width,
    std::size_t height,
    std::size_t y,
    std::size_t kernelSize,
    ReflectedGaussianRows& rows) noexcept {
    const auto radius = static_cast<std::int64_t>(kernelSize / 2U);
    for (std::size_t index = 0U; index < kernelSize; ++index) {
        const auto offset = static_cast<std::int64_t>(index) - radius;
        const auto reflectedY = reflect101(
            static_cast<std::int64_t>(y) + offset, height);
        rows[index] = intermediate + reflectedY * width;
    }
}

double verticalGaussianAt(
    const ReflectedGaussianRows& rows,
    std::size_t x,
    const double* coefficients,
    std::size_t kernelSize) noexcept {
    double sum = 0.0;
    for (std::size_t index = 0U; index < kernelSize; ++index) {
        sum += coefficients[index] * rows[index][x];
    }
    return sum;
}

std::uint16_t roundU16(double value) noexcept {
    const auto clamped = std::clamp(value, 0.0, 65535.0);
    return static_cast<std::uint16_t>(std::floor(clamped + 0.5));
}

}  // namespace lumora::processing::detail
