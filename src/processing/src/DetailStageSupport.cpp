#include "DetailStageSupport.hpp"

#include <lumora/core/CheckedMath.hpp>

#include <opencv2/imgproc.hpp>

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace lumora::processing::detail {
namespace {

constexpr std::size_t laneCount = 8U;

[[nodiscard]] double reflectedHorizontalAt(
    std::span<const std::byte> sourceRow,
    std::size_t x,
    std::size_t width,
    const double* coefficients,
    std::size_t kernelSize) noexcept {
    double sum = 0.0;
    const auto radius = static_cast<std::int64_t>(kernelSize / 2U);
    for (std::size_t tap = 0U; tap < kernelSize; ++tap) {
        const auto offset = static_cast<std::int64_t>(tap) - radius;
        const auto reflectedX = reflect101(
            static_cast<std::int64_t>(x) + offset, width);
        sum += coefficients[tap] * static_cast<double>(
            loadU16(sourceRow, reflectedX));
    }
    return sum;
}

[[nodiscard]] double interiorHorizontalAt(
    std::span<const std::byte> sourceRow,
    std::size_t firstX,
    const double* coefficients,
    std::size_t kernelSize) noexcept {
    double sum = 0.0;
    for (std::size_t tap = 0U; tap < kernelSize; ++tap) {
        sum += coefficients[tap] * static_cast<double>(
            loadU16(sourceRow, firstX + tap));
    }
    return sum;
}

template <typename WritePixel>
void writeVerticalRow(
    const ReflectedGaussianRows& rows,
    const double* coefficients,
    std::size_t kernelSize,
    std::size_t width,
    WritePixel&& writePixel) noexcept {
    std::size_t x = 0U;
    for (; width - x >= laneCount; x += laneCount) {
        std::array<double, laneCount> sums{};
        for (std::size_t tap = 0U; tap < kernelSize; ++tap) {
            const auto coefficient = coefficients[tap];
            const auto* input = rows[tap] + x;
            for (std::size_t lane = 0U; lane < laneCount; ++lane) {
                sums[lane] += coefficient * input[lane];
            }
        }
        for (std::size_t lane = 0U; lane < laneCount; ++lane) {
            writePixel(x + lane, sums[lane]);
        }
    }
    for (; x < width; ++x) {
        writePixel(x, verticalGaussianAt(rows, x, coefficients, kernelSize));
    }
}

#if defined(__x86_64__) || defined(_M_X64)
struct PackedGaussianBlock final {
    __m128d pairs[4U];
};

[[nodiscard]] PackedGaussianBlock accumulatePackedBlock(
    const ReflectedGaussianRows& rows, const double* coefficients,
    std::size_t kernelSize, std::size_t firstX) noexcept {
    PackedGaussianBlock block{{_mm_setzero_pd(), _mm_setzero_pd(),
        _mm_setzero_pd(), _mm_setzero_pd()}};
    for (std::size_t tap = 0U; tap < kernelSize; ++tap) {
        const auto coefficient = _mm_set1_pd(coefficients[tap]);
        for (std::size_t pair = 0U; pair < 4U; ++pair) {
            const auto input = _mm_loadu_pd(rows[tap] + firstX + pair * 2U);
            block.pairs[pair] = _mm_add_pd(block.pairs[pair],
                _mm_mul_pd(coefficient, input));
        }
    }
    return block;
}

[[nodiscard]] __m128d selectPair(__m128d mask, __m128d yes, __m128d no) noexcept {
    return _mm_or_pd(_mm_and_pd(mask, yes), _mm_andnot_pd(mask, no));
}

[[nodiscard]] __m128i roundPairU16(__m128d value) noexcept {
    const auto zero = _mm_setzero_pd();
    const auto maximum = _mm_set1_pd(65535.0);
    // Ordered comparisons preserve std::clamp's operand selection, including -0.
    const auto lowClamped = selectPair(_mm_cmplt_pd(value, zero), zero, value);
    const auto clamped = selectPair(_mm_cmplt_pd(maximum, lowClamped), maximum, lowClamped);
    return _mm_cvttpd_epi32(_mm_add_pd(clamped, _mm_set1_pd(0.5)));
}

[[nodiscard]] __m128i finishSharpenPair(
    __m128d blurred, __m128d original, __m128d amount, __m128d threshold) noexcept {
    const auto detail = _mm_sub_pd(original, _mm_cvtepi32_pd(roundPairU16(blurred)));
    const auto magnitude = _mm_andnot_pd(_mm_set1_pd(-0.0), detail);
    const auto unchanged = _mm_cmple_pd(magnitude, threshold);
    const auto candidate = _mm_add_pd(original, _mm_mul_pd(amount, detail));
    return roundPairU16(selectPair(unchanged, original, candidate));
}

void storeSharpenBlock(const PackedGaussianBlock& blurred,
    std::span<const std::byte> sourceRow, std::span<std::byte> destinationRow,
    std::size_t x, __m128d amount, __m128d threshold) noexcept {
    __m128i input;
    std::memcpy(&input, sourceRow.data() + x * sizeof(std::uint16_t), sizeof(input));
    const auto low = _mm_unpacklo_epi16(input, _mm_setzero_si128());
    const auto high = _mm_unpackhi_epi16(input, _mm_setzero_si128());
    const auto pair0 = finishSharpenPair(blurred.pairs[0], _mm_cvtepi32_pd(low), amount, threshold);
    const auto pair1 = finishSharpenPair(blurred.pairs[1],
        _mm_cvtepi32_pd(_mm_srli_si128(low, 8)), amount, threshold);
    const auto pair2 = finishSharpenPair(blurred.pairs[2], _mm_cvtepi32_pd(high), amount, threshold);
    const auto pair3 = finishSharpenPair(blurred.pairs[3],
        _mm_cvtepi32_pd(_mm_srli_si128(high, 8)), amount, threshold);
    // Bias proven [0,65535] integers into signed i16 range for SSE2 packing.
    const auto bias = _mm_set1_epi32(32768);
    const auto output = _mm_xor_si128(_mm_packs_epi32(
        _mm_sub_epi32(_mm_unpacklo_epi64(pair0, pair1), bias),
        _mm_sub_epi32(_mm_unpacklo_epi64(pair2, pair3), bias)), _mm_set1_epi16(-32768));
    std::memcpy(destinationRow.data() + x * sizeof(std::uint16_t), &output, sizeof(output));
}
#endif

}  // namespace

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
    for (std::size_t y = 0U; y < height; ++y) {
        const auto sourceRow = source.row(static_cast<std::uint32_t>(y));
        auto* destinationRow = intermediate + y * width;
        horizontalGaussianRow(sourceRow, destinationRow, width,
            coefficients, kernelSize);
    }
}

void horizontalGaussianRow(
    std::span<const std::byte> sourceRow,
    double* destinationRow,
    std::size_t width,
    const double* coefficients,
    std::size_t kernelSize) noexcept {
    const auto radius = kernelSize / 2U;
    if (width < kernelSize) {
        for (std::size_t x = 0U; x < width; ++x) {
            destinationRow[x] = reflectedHorizontalAt(
                sourceRow, x, width, coefficients, kernelSize);
        }
        return;
    }

    for (std::size_t x = 0U; x < radius; ++x) {
        destinationRow[x] = reflectedHorizontalAt(
            sourceRow, x, width, coefficients, kernelSize);
    }

    const auto interiorEnd = width - radius;
    std::size_t x = radius;
    for (; interiorEnd - x >= laneCount; x += laneCount) {
        std::array<double, laneCount> sums{};
        const auto firstX = x - radius;
        for (std::size_t tap = 0U; tap < kernelSize; ++tap) {
            const auto coefficient = coefficients[tap];
            for (std::size_t lane = 0U; lane < laneCount; ++lane) {
                sums[lane] += coefficient * static_cast<double>(
                    loadU16(sourceRow, firstX + tap + lane));
            }
        }
        for (std::size_t lane = 0U; lane < laneCount; ++lane) {
            destinationRow[x + lane] = sums[lane];
        }
    }
    for (; x < interiorEnd; ++x) {
        destinationRow[x] = interiorHorizontalAt(
            sourceRow, x - radius, coefficients, kernelSize);
    }
    for (; x < width; ++x) {
        destinationRow[x] = reflectedHorizontalAt(
            sourceRow, x, width, coefficients, kernelSize);
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

void writeGaussianRow(
    const ReflectedGaussianRows& rows,
    const double* coefficients,
    std::size_t kernelSize,
    std::size_t width,
    std::span<std::byte> destinationRow) noexcept {
    writeVerticalRow(rows, coefficients, kernelSize, width,
        [&](std::size_t x, double blurred) {
            storeU16(destinationRow, x, roundU16(blurred));
        });
}

static void writeSharpenScalarRow(
    const ReflectedGaussianRows& rows,
    const double* coefficients,
    std::size_t kernelSize,
    std::size_t width,
    std::span<const std::byte> sourceRow,
    std::span<std::byte> destinationRow,
    double amount,
    double threshold) noexcept {
    writeVerticalRow(rows, coefficients, kernelSize, width,
        [&](std::size_t x, double blurredValue) {
            const auto blurred = roundU16(blurredValue);
            const auto original = loadU16(sourceRow, x);
            const auto signedDetail = static_cast<std::int32_t>(original)
                - static_cast<std::int32_t>(blurred);
            if (std::abs(static_cast<double>(signedDetail)) <= threshold) {
                storeU16(destinationRow, x, original);
                return;
            }
            const auto candidate = static_cast<double>(original)
                + amount * static_cast<double>(signedDetail);
            storeU16(destinationRow, x, roundU16(candidate));
        });
}

void accumulateVerticalBlock8(
    const ReflectedGaussianRows& rows, const double* coefficients,
    std::size_t kernelSize, std::size_t firstX, GaussianBlock8& output,
    DetailRowBackend backend) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    if (backend == DetailRowBackend::BaselineSse2) {
        const auto block = accumulatePackedBlock(rows, coefficients, kernelSize, firstX);
        for (std::size_t pair = 0U; pair < 4U; ++pair) {
            _mm_storeu_pd(output.data() + pair * 2U, block.pairs[pair]);
        }
        return;
    }
#else
    (void)backend;
#endif
    for (std::size_t lane = 0U; lane < laneCount; ++lane) {
        output[lane] = verticalGaussianAt(rows, firstX + lane, coefficients, kernelSize);
    }
}

std::size_t writeSharpenRow(
    const ReflectedGaussianRows& rows, const double* coefficients,
    std::size_t kernelSize, std::size_t width,
    std::span<const std::byte> sourceRow, std::span<std::byte> destinationRow,
    double amount, double threshold, DetailRowBackend backend) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    if (backend == DetailRowBackend::BaselineSse2) {
        const auto packedAmount = _mm_set1_pd(amount);
        const auto packedThreshold = _mm_set1_pd(threshold);
        std::size_t x = 0U;
        for (; width - x >= laneCount; x += laneCount) {
            const auto block = accumulatePackedBlock(rows, coefficients, kernelSize, x);
            storeSharpenBlock(block, sourceRow, destinationRow, x, packedAmount, packedThreshold);
        }
        const auto blocks = x / laneCount;
        for (; x < width; ++x) {
            const auto blurred = roundU16(verticalGaussianAt(rows, x, coefficients, kernelSize));
            const auto original = loadU16(sourceRow, x);
            const auto detail = static_cast<std::int32_t>(original) - static_cast<std::int32_t>(blurred);
            const auto result = std::abs(static_cast<double>(detail)) <= threshold ? original
                : roundU16(static_cast<double>(original) + amount * static_cast<double>(detail));
            storeU16(destinationRow, x, result);
        }
        return blocks;
    }
#else
    (void)backend;
#endif
    writeSharpenScalarRow(rows, coefficients, kernelSize, width,
        sourceRow, destinationRow, amount, threshold);
    return 0U;
}

void writeSharpenRow(
    const ReflectedGaussianRows& rows, const double* coefficients,
    std::size_t kernelSize, std::size_t width,
    std::span<const std::byte> sourceRow, std::span<std::byte> destinationRow,
    double amount, double threshold) noexcept {
    writeSharpenRow(rows, coefficients, kernelSize, width,
        sourceRow, destinationRow, amount, threshold, DetailRowBackend::BaselineSse2);
}

std::uint16_t roundU16(double value) noexcept {
    const auto clamped = std::clamp(value, 0.0, 65535.0);
    return static_cast<std::uint16_t>(clamped + 0.5);
}

}  // namespace lumora::processing::detail
