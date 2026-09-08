#include "DetailStageSupport.hpp"
#include "PreparedCpuExecutor.hpp"
#include "StageStorage.hpp"
#include <atomic>
#include <cfenv>
#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

#include <lumora/processing/DenoiseStage.hpp>
#include <lumora/processing/SharpenStage.hpp>

#include <gtest/gtest.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {

using namespace lumora;
using processing::DenoiseMode;
using processing::DenoiseParameters;
using processing::DenoiseStage;
using processing::IProcessingStage;
using processing::ImageDomain;
using processing::ImageView;
using processing::MutableImageView;
using processing::SharpenParameters;
using processing::SharpenStage;

[[nodiscard]] core::ImageLayout layout(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t stride) {
    return core::ImageLayout::create(width, height, stride,
        core::StorageType::UInt16, stride * static_cast<std::size_t>(height)).value();
}

[[nodiscard]] core::SourcePixelFormat canonicalFormat() {
    return {"CanonicalU16", 0U, 16U, 65535U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt16};
}

[[nodiscard]] std::size_t reflect101(
    std::int64_t coordinate,
    std::size_t extent) noexcept {
    if (extent == 1U) return 0U;
    const auto period = static_cast<std::int64_t>(2U * (extent - 1U));
    auto reflected = coordinate % period;
    if (reflected < 0) reflected += period;
    if (reflected >= static_cast<std::int64_t>(extent)) reflected = period - reflected;
    return static_cast<std::size_t>(reflected);
}

[[nodiscard]] std::uint16_t roundU16(double value) noexcept {
    return static_cast<std::uint16_t>(
        std::floor(std::clamp(value, 0.0, 65535.0) + 0.5));
}

[[nodiscard]] std::vector<double> coefficients(
    std::size_t kernelSize,
    double sigma) {
    const auto kernel = cv::getGaussianKernel(
        static_cast<int>(kernelSize), sigma, CV_64F);
    std::vector<double> result(kernelSize);
    for (std::size_t tap = 0U; tap < kernelSize; ++tap) {
        result[tap] = kernel.at<double>(static_cast<int>(tap), 0);
    }
    return result;
}

[[nodiscard]] std::vector<double> horizontalReference(
    std::span<const std::uint16_t> input,
    std::size_t width,
    std::size_t height,
    std::span<const double> kernel) {
    std::vector<double> result(width * height);
    const auto radius = static_cast<std::int64_t>(kernel.size() / 2U);
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            double sum = 0.0;
            for (std::size_t tap = 0U; tap < kernel.size(); ++tap) {
                const auto offset = static_cast<std::int64_t>(tap) - radius;
                const auto sourceX = reflect101(
                    static_cast<std::int64_t>(x) + offset, width);
                sum += kernel[tap] * static_cast<double>(input[y * width + sourceX]);
            }
            result[y * width + x] = sum;
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::uint16_t> gaussianReference(
    std::span<const std::uint16_t> input,
    std::size_t width,
    std::size_t height,
    std::span<const double> kernel) {
    const auto horizontal = horizontalReference(input, width, height, kernel);
    std::vector<std::uint16_t> result(width * height);
    const auto radius = static_cast<std::int64_t>(kernel.size() / 2U);
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            double sum = 0.0;
            for (std::size_t tap = 0U; tap < kernel.size(); ++tap) {
                const auto offset = static_cast<std::int64_t>(tap) - radius;
                const auto sourceY = reflect101(
                    static_cast<std::int64_t>(y) + offset, height);
                sum += kernel[tap] * horizontal[sourceY * width + x];
            }
            result[y * width + x] = roundU16(sum);
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::uint16_t> sharpenReference(
    std::span<const std::uint16_t> input,
    std::size_t width,
    std::size_t height,
    std::span<const double> kernel,
    double amount,
    double threshold) {
    auto result = gaussianReference(input, width, height, kernel);
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto original = input[index];
        const auto detail = static_cast<std::int32_t>(original)
            - static_cast<std::int32_t>(result[index]);
        if (std::abs(static_cast<double>(detail)) <= threshold) {
            result[index] = original;
        } else {
            result[index] = roundU16(static_cast<double>(original)
                + amount * static_cast<double>(detail));
        }
    }
    return result;
}

enum class Pattern {
    Ramp,
    Step,
    BoundaryImpulse,
    TopBottomImpulse,
    Alternating,
    Constant,
    Noise,
};

[[nodiscard]] std::vector<std::uint16_t> makeInput(
    std::size_t width,
    std::size_t height,
    Pattern pattern) {
    std::vector<std::uint16_t> result(width * height);
    std::uint32_t state = 0xC001D00DU;
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto x = index % width;
        switch (pattern) {
        case Pattern::Ramp:
            result[index] = static_cast<std::uint16_t>(
                (index * 997U + x * 37U + 11U) % 65536U);
            break;
        case Pattern::Step:
            result[index] = x < width / 2U ? 10000U : 51000U;
            break;
        case Pattern::BoundaryImpulse:
            result[index] = (x == 0U || x + 1U == width || index == result.size() / 2U)
                ? 65535U : 0U;
            break;
        case Pattern::TopBottomImpulse: {
            const auto y = index / width;
            result[index] = y == 0U || y + 1U == height ? 65535U : 0U;
            break;
        }
        case Pattern::Alternating:
            result[index] = index % 2U == 0U ? 0U : 65535U;
            break;
        case Pattern::Constant:
            result[index] = 1001U;
            break;
        case Pattern::Noise:
            state = state * 1664525U + 1013904223U;
            result[index] = static_cast<std::uint16_t>(state >> 16U);
            break;
        }
    }
    return result;
}

struct BufferedImage final {
    BufferedImage(
        std::size_t widthValue,
        std::size_t heightValue,
        std::span<const std::uint16_t> input,
        std::size_t sourcePadding = 3U,
        std::size_t destinationPadding = 5U,
        std::size_t sourceStartValue = 1U,
        std::size_t destinationStartValue = 1U)
        : width(widthValue),
          height(heightValue),
          sourceStride(width * sizeof(std::uint16_t) + sourcePadding),
          destinationStride(width * sizeof(std::uint16_t) + destinationPadding),
          sourceStart(sourceStartValue),
          destinationStart(destinationStartValue),
          source(sourceStart + sourceStride * height + 3U, std::byte{0xC3}),
          destination(destinationStart + destinationStride * height + 3U,
              std::byte{0x5A}) {
        for (std::size_t y = 0U; y < height; ++y) {
            std::memcpy(source.data() + sourceStart + y * sourceStride,
                input.data() + y * width, width * sizeof(std::uint16_t));
        }
    }

    [[nodiscard]] ImageView sourceView() const {
        const auto imageLayout = layout(static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), sourceStride);
        return ImageView::create(imageLayout,
            std::span(source).subspan(sourceStart, sourceStride * height),
            ImageDomain::CanonicalU16).value();
    }

    [[nodiscard]] MutableImageView destinationView() {
        const auto imageLayout = layout(static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), destinationStride);
        return MutableImageView::create(imageLayout,
            std::span(destination).subspan(destinationStart, destinationStride * height),
            ImageDomain::CanonicalU16).value();
    }

    [[nodiscard]] std::vector<std::uint16_t> activeDestination() const {
        std::vector<std::uint16_t> result(width * height);
        for (std::size_t y = 0U; y < height; ++y) {
            std::memcpy(result.data() + y * width,
                destination.data() + destinationStart + y * destinationStride,
                width * sizeof(std::uint16_t));
        }
        return result;
    }

    [[nodiscard]] bool destinationCanariesIntact() const {
        for (std::size_t index = 0U; index < destination.size(); ++index) {
            const auto relative = index < destinationStart
                ? destinationStride : (index - destinationStart) % destinationStride;
            const bool active = index >= destinationStart
                && index < destinationStart + destinationStride * height
                && relative < width * sizeof(std::uint16_t);
            if (!active && destination[index] != std::byte{0x5A}) return false;
        }
        return true;
    }

    std::size_t width;
    std::size_t height;
    std::size_t sourceStride;
    std::size_t destinationStride;
    std::size_t sourceStart;
    std::size_t destinationStart;
    std::vector<std::byte> source;
    std::vector<std::byte> destination;
};

[[nodiscard]] std::vector<std::size_t> boundaryWidths(std::size_t kernelSize) {
    const auto radius = kernelSize / 2U;
    std::vector<std::size_t> widths{1U, 2U, 3U,
        kernelSize - 1U, kernelSize, kernelSize + 1U};
    for (const auto interior : {7U, 8U, 9U, 15U, 16U, 17U}) {
        widths.push_back(radius * 2U + interior);
    }
    std::sort(widths.begin(), widths.end());
    widths.erase(std::unique(widths.begin(), widths.end()), widths.end());
    return widths;
}

void expectProcessMatches(
    IProcessingStage& stage,
    std::span<const std::uint16_t> input,
    std::span<const std::uint16_t> expected,
    std::size_t width,
    std::size_t height) {
    BufferedImage buffers(width, height, input);
    const auto sourceBefore = buffers.source;
    ASSERT_TRUE(stage.process(
        buffers.sourceView(), buffers.destinationView(), canonicalFormat()).hasValue());
    EXPECT_EQ(buffers.source, sourceBefore);
    EXPECT_EQ(buffers.activeDestination(),
        (std::vector<std::uint16_t>(expected.begin(), expected.end())));
    EXPECT_TRUE(buffers.destinationCanariesIntact());
}

constexpr std::array patterns{Pattern::Ramp, Pattern::Step,
    Pattern::BoundaryImpulse, Pattern::TopBottomImpulse, Pattern::Alternating,
    Pattern::Constant, Pattern::Noise};

// Frozen with independent exact-rational product/add rounding, not stage helpers.
TEST(DetailStageBatching, VerticalBlockPreservesFrozenBinary64TapOrder) {
    using namespace processing::detail;
    constexpr std::array<std::array<double, 8U>, 3U> samples{{
        {0x1.0000000000001p+16, 0x1.0000000000001p+17, 0x1.8000000000002p+17, 0x1.0000000000001p+18, 0x1.4000000000001p+18, 0x1.8000000000002p+18, 0x1.c000000000002p+18, 0x1.0000000000001p+19},
        {0x1.0000000000001p-2, 0x1.0000000000001p-1, 0x1.8000000000002p-1, 0x1.0000000000001p+0, 0x1.4000000000001p+0, 0x1.8000000000002p+0, 0x1.c000000000002p+0, 0x1.0000000000001p+1},
        {-0x1p+16, -0x1p+17, -0x1.8p+17, -0x1p+18, -0x1.4p+18, -0x1.8p+18, -0x1.cp+18, -0x1p+19}}};
    constexpr std::array weights{0x1.0000000000001p-3,
        0x1.fffffffffffffp-2, 0x1.0000000000001p-3};
    constexpr std::array<std::array<std::uint64_t, 8U>, 3U> expected{{
        {0x3fc0000000010000ULL, 0x3fd0000000010000ULL, 0x3fd8000000020000ULL, 0x3fe0000000010000ULL, 0x3fe4000000010000ULL, 0x3fe8000000020000ULL, 0x3fec000000020000ULL, 0x3ff0000000010000ULL},
        {0x40c0002000000004ULL, 0x40d0002000000004ULL, 0x40d8003000000008ULL, 0x40e0002000000004ULL, 0x40e4002800000004ULL, 0x40e8003000000008ULL, 0x40ec003800000008ULL, 0x40f0002000000004ULL},
        {0x40c000a00000000cULL, 0x40d000a00000000cULL, 0x40d800f000000018ULL, 0x40e000a00000000cULL, 0x40e400c80000000cULL, 0x40e800f000000018ULL, 0x40ec011800000018ULL, 0x40f000a00000000cULL}}};
    alignas(16) std::array<std::array<double, 10U>, 31U> storage{};
    ReflectedGaussianRows rows{};
    std::array<double, 31U> kernel{};
    for (std::size_t tap = 0U; tap < rows.size(); ++tap) {
        std::copy(samples[tap % 3U].begin(), samples[tap % 3U].end(),
            storage[tap].begin() + 1);
        rows[tap] = storage[tap].data();
        kernel[tap] = weights[tap % 3U];
    }
    constexpr std::array<std::size_t, 3U> sizes{3U, 7U, 31U};
    for (std::size_t fixture = 0U; fixture < sizes.size(); ++fixture) {
        for (const auto backend : {DetailRowBackend::Scalar, DetailRowBackend::BaselineSse2}) {
            GaussianBlock8 actual{};
            accumulateVerticalBlock8(rows, kernel.data(), sizes[fixture], 1U, actual, backend);
            for (std::size_t lane = 0U; lane < 8U; ++lane) {
                EXPECT_EQ(std::bit_cast<std::uint64_t>(actual[lane]), expected[fixture][lane])
                    << "kernel=" << sizes[fixture] << " lane=" << lane;
            }
        }
    }
}

TEST(DetailStageBatching, SharpenRowsPreserveFrozenRoundingThresholdAndSaturation) {
    using namespace processing::detail;
    // First eight lanes expose blur rounding; the next eight expose signed
    // threshold ties, adjacent details and low/high candidate saturation.
    constexpr std::array<double, 17U> blurred{
        -0x1p+0, 0x1.ffffffffffffep-2, 0x1.fffffffffffffp-2, 0x1p-1,
        0x1.0000000000001p-1, 0x1.fffcfffffffffp+15, 0x1.fffdp+15, 0x1p+16,
        97.0, 103.0, 96.0, 104.0, 0.0, 65535.0, 99.0, 101.0,
        0x1.fffd000000001p+15};
    constexpr std::array<std::uint16_t, 17U> originals{
        100, 100, 100, 100, 100, 65500, 65500, 65500,
        100, 100, 100, 100, 65500, 10, 100, 100, 65500};
    constexpr std::array<std::uint16_t, 17U> expected{
        250, 250, 249, 249, 249, 65449, 65448, 65448,
        100, 100, 106, 94, 65535, 0, 100, 100, 65448};
    constexpr double coefficient = 1.0;
    for (const auto width : {1U, 7U, 8U, 9U, 15U, 16U, 17U}) {
        for (const auto offset : {0U, 8U}) {
            std::vector<std::uint16_t> input(width * 3U), want(width * 3U);
            std::vector<double> intermediate(width);
            for (std::size_t x = 0U; x < width; ++x) {
                const auto lane = (x + offset) % originals.size();
                intermediate[x] = blurred[lane];
                for (std::size_t y = 0U; y < 3U; ++y) {
                    input[y * width + x] = originals[lane];
                    want[y * width + x] = expected[lane];
                }
            }
            ReflectedGaussianRows rows{};
            rows[0] = intermediate.data();
            for (const auto backend : {DetailRowBackend::Scalar, DetailRowBackend::BaselineSse2}) {
                BufferedImage buffers(width, 3U, input);
                const auto sourceBefore = buffers.source;
                for (std::uint32_t y = 0U; y < 3U; ++y) {
                    const auto blocks = writeSharpenRow(rows, &coefficient, 1U, width,
                        buffers.sourceView().row(y), buffers.destinationView().row(y), 1.5, 3.0, backend);
#if defined(__x86_64__) || defined(_M_X64)
                    EXPECT_EQ(blocks, backend == DetailRowBackend::BaselineSse2 ? width / 8U : 0U);
#else
                    EXPECT_EQ(blocks, 0U);
#endif
                }
                EXPECT_EQ(buffers.activeDestination(), want) << "width=" << width;
                EXPECT_EQ(buffers.source, sourceBefore);
                EXPECT_TRUE(buffers.destinationCanariesIntact());
            }
        }
    }
}

TEST(DetailStageBatching, SharpenAmountHalfNeighborsHaveFrozenLaneAndTailResults) {
    using namespace processing::detail;
    struct Case final { double amount; std::uint16_t expected; };
    // 3 + amount * 3 lies just below, at, and above 7.5 after two
    // independently rounded operations. These literals are adjacent binary64s.
    for (const auto fixture : {Case{0x1.7ffffffffffffp+0, 7U},
             Case{0x1.8p+0, 8U}, Case{0x1.8000000000001p+0, 8U}}) {
        for (const auto backend : {DetailRowBackend::Scalar, DetailRowBackend::BaselineSse2}) {
            for (const auto width : {7U, 8U, 9U, 17U}) {
                const std::vector<std::uint16_t> input(width, 3U);
                const std::vector<std::uint16_t> expected(width, fixture.expected);
                const std::vector<double> blurred(width, 0.0);
                constexpr double coefficient = 1.0;
                ReflectedGaussianRows rows{};
                rows[0] = blurred.data();
                BufferedImage buffers(width, 1U, input);
                writeSharpenRow(rows, &coefficient, 1U, width,
                    buffers.sourceView().row(0), buffers.destinationView().row(0),
                    fixture.amount, 0.0, backend);
                EXPECT_EQ(buffers.activeDestination(), expected);
                EXPECT_TRUE(buffers.destinationCanariesIntact());
            }
        }
    }
}

TEST(DetailStageBatching, SharpenBackendsPreserveCallerControlsAndExactModeResults) {
    using namespace processing::detail;
    struct EnvironmentGuard final {
        std::fenv_t saved{};
#if defined(__x86_64__) || defined(_M_X64)
        unsigned control = _mm_getcsr();
#endif
        EnvironmentGuard() { std::fegetenv(&saved); }
        ~EnvironmentGuard() {
            std::fesetenv(&saved);
#if defined(__x86_64__) || defined(_M_X64)
            _mm_setcsr(control);
#endif
        }
    } guard;
    constexpr std::size_t width = 17U;
    const auto input = makeInput(width, 1U, Pattern::Noise);
    alignas(16) std::array<std::array<double, width + 1U>, 31U> storage{};
    ReflectedGaussianRows rows{};
    std::array<double, 31U> kernel{};
    for (std::size_t tap = 0U; tap < rows.size(); ++tap) {
        rows[tap] = storage[tap].data() + 1U;
        kernel[tap] = 0x1.0842108421084p-5;
        for (std::size_t x = 0U; x < width; ++x) {
            storage[tap][x + 1U] = x == 0U ? 0x0.0000000000001p-1022
                : static_cast<double>((x * 997U + tap * 431U) % 65536U) + 0x1.1p-2;
        }
    }
    for (const auto mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO, FE_TONEAREST}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        for (const auto flush : {false, true}) {
#if defined(__x86_64__) || defined(_M_X64)
            constexpr unsigned flushMask = 1U << 15U;
            const auto controls = (_mm_getcsr() & ~flushMask) | (flush ? flushMask : 0U);
            _mm_setcsr(controls);
#else
            (void)flush;
#endif
            GaussianBlock8 scalar{}, packed{};
            accumulateVerticalBlock8(rows, kernel.data(), 31U, 0U, scalar, DetailRowBackend::Scalar);
            accumulateVerticalBlock8(rows, kernel.data(), 31U, 0U, packed, DetailRowBackend::BaselineSse2);
            for (std::size_t lane = 0U; lane < scalar.size(); ++lane) {
                EXPECT_EQ(std::bit_cast<std::uint64_t>(scalar[lane]),
                    std::bit_cast<std::uint64_t>(packed[lane]));
            }
            BufferedImage first(width, 1U, input), second(width, 1U, input);
            writeSharpenRow(rows, kernel.data(), 31U, width,
                first.sourceView().row(0), first.destinationView().row(0),
                0x1.8000000000001p+0, 13.0, DetailRowBackend::Scalar);
            writeSharpenRow(rows, kernel.data(), 31U, width,
                second.sourceView().row(0), second.destinationView().row(0),
                0x1.8000000000001p+0, 13.0, DetailRowBackend::BaselineSse2);
            EXPECT_EQ(first.activeDestination(), second.activeDestination());
            EXPECT_TRUE(first.destinationCanariesIntact());
            EXPECT_TRUE(second.destinationCanariesIntact());
            EXPECT_EQ(std::fegetround(), mode);
#if defined(__x86_64__) || defined(_M_X64)
            // Exception status is intentionally outside the executor contract.
            EXPECT_EQ(_mm_getcsr() & ~0x3FU, controls & ~0x3FU);
#endif
        }
    }
}

TEST(DetailStageBatching, RoundU16ClampsAndRoundsHalfNeighbors) {
    struct Case final {
        double value;
        std::uint16_t expected;
    };
    const auto belowHalf = std::nextafter(0.5, 0.0);
    const auto belowRoundingHalf = std::nextafter(belowHalf, 0.0);
    const auto aboveHalf = std::nextafter(0.5, 1.0);
    const auto belowUpperHalf = std::nextafter(65534.5, 65534.0);
    const auto aboveUpperHalf = std::nextafter(65534.5, 65535.0);
    for (const auto testCase : {
             Case{-1.0, 0U},
             Case{0.0, 0U},
             Case{belowRoundingHalf, 0U},
             Case{belowHalf, 1U},
             Case{0.5, 1U},
             Case{aboveHalf, 1U},
             Case{1.499999999999, 1U},
             Case{1.5, 2U},
             Case{belowUpperHalf, 65534U},
             Case{65534.5, 65535U},
             Case{aboveUpperHalf, 65535U},
             Case{65535.0, 65535U},
             Case{65535.5, 65535U},
             Case{1000000.0, 65535U}}) {
        EXPECT_EQ(processing::detail::roundU16(testCase.value), testCase.expected)
            << "value=" << testCase.value;
    }
}

TEST(DetailStageBatching, HorizontalGaussianPreservesScalarTapOrderAtBordersBlocksAndTails) {
    struct Case final { std::size_t kernelSize; double sigma; };
    for (const auto testCase : {
             Case{3U, 0.0}, Case{5U, 5.0}, Case{7U, 1.25}, Case{31U, 5.0}}) {
        const auto kernel = coefficients(testCase.kernelSize, testCase.sigma);
        for (const auto width : boundaryWidths(testCase.kernelSize)) {
            constexpr std::size_t height = 3U;
            for (const auto pattern : patterns) {
                const auto input = makeInput(width, height, pattern);
                BufferedImage buffers(width, height, input);
                std::vector<double> actual(width * height);
                processing::detail::horizontalGaussian(buffers.sourceView(), actual.data(),
                    width, height, kernel.data(), kernel.size());
                const auto expected = horizontalReference(
                    input, width, height, kernel);
                ASSERT_EQ(actual.size(), expected.size());
                for (std::size_t index = 0U; index < actual.size(); ++index) {
                    EXPECT_EQ(std::bit_cast<std::uint64_t>(actual[index]),
                        std::bit_cast<std::uint64_t>(expected[index]))
                        << "kernel=" << testCase.kernelSize
                        << " width=" << width << " index=" << index;
                }
            }
        }
    }
}

TEST(DetailStageBatching, GaussianDenoiseMatchesScalarReferenceAcrossEightLaneBoundaries) {
    for (const auto parameters : {
             DenoiseParameters{DenoiseMode::Gaussian, 3U, 0.0},
             DenoiseParameters{DenoiseMode::Gaussian, 5U, 5.0},
             DenoiseParameters{DenoiseMode::Gaussian, 7U, 1.25}}) {
        const auto kernel = coefficients(parameters.kernelSize, parameters.sigma);
        for (const auto width : boundaryWidths(parameters.kernelSize)) {
            for (const auto height : {1U, 4U}) {
                const auto imageLayout = layout(static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height), width * 2U + 3U);
                auto created = DenoiseStage::create(parameters, imageLayout);
                ASSERT_TRUE(created.hasValue());
                auto stage = std::move(created).value();
                for (const auto pattern : patterns) {
                    const auto input = makeInput(width, height, pattern);
                    const auto expected = gaussianReference(input, width, height, kernel);
                    expectProcessMatches(*stage, input, expected, width, height);
                }
                const auto first = makeInput(width, height, Pattern::Ramp);
                const auto expected = gaussianReference(first, width, height, kernel);
                expectProcessMatches(*stage, first, expected, width, height);
            }
        }
    }
}

TEST(DetailStageBatching, SharpenTallRingReuseMatchesIndependentOracleForABA) {
    constexpr std::size_t width = 17U;
    constexpr std::size_t height = 37U;
    constexpr SharpenParameters parameters{1.5, 2.0, 12.5};
    const auto kernel = coefficients(13U, parameters.radius);
    const auto inputA = makeInput(width, height, Pattern::Noise);
    const auto inputB = makeInput(width, height, Pattern::Alternating);
    const auto expectedA = sharpenReference(
        inputA, width, height, kernel, parameters.amount, parameters.threshold);
    const auto expectedB = sharpenReference(
        inputB, width, height, kernel, parameters.amount, parameters.threshold);
    BufferedImage buffersA(width, height, inputA, 3U, 5U, 1U, 3U);
    BufferedImage buffersB(width, height, inputB, 7U, 9U, 3U, 1U);
    const auto sourceBeforeA = buffersA.source;
    const auto sourceBeforeB = buffersB.source;
    auto created = SharpenStage::create(parameters,
        layout(width, height, buffersA.sourceStride));
    ASSERT_TRUE(created.hasValue());
    auto stage = std::move(created).value();

    const auto expectCall = [&](BufferedImage& buffers,
                                const std::vector<std::uint16_t>& expected) {
        ASSERT_TRUE(stage->process(buffers.sourceView(), buffers.destinationView(),
            canonicalFormat()).hasValue());
        EXPECT_EQ(buffers.activeDestination(), expected);
        EXPECT_TRUE(buffers.destinationCanariesIntact());
    };
    expectCall(buffersA, expectedA);
    expectCall(buffersB, expectedB);
    expectCall(buffersA, expectedA);
    EXPECT_EQ(buffersA.source, sourceBeforeA);
    EXPECT_EQ(buffersB.source, sourceBeforeB);
}

TEST(DetailStageBatching, SharpenMatchesScalarReferenceAcrossEightLaneBoundaries) {
    const auto transition = 2.0 / 3.0;
    for (const auto parameters : {
             SharpenParameters{1.75, 0.5, 13.5},
             SharpenParameters{0.5, 1.0 / std::sqrt(2.0 * std::log(2.0)), 0.0},
             SharpenParameters{2.25, std::nextafter(transition, 0.5), 42.25},
             SharpenParameters{2.25, std::nextafter(transition, 1.0), 42.25},
             SharpenParameters{5.0, 5.0, 0.0}}) {
        const auto kernelSize = static_cast<std::size_t>(
            2.0 * std::ceil(3.0 * parameters.radius) + 1.0);
        const auto kernel = coefficients(kernelSize, parameters.radius);
        for (const auto width : boundaryWidths(kernelSize)) {
            for (const auto height : {1U, 2U,
                     static_cast<unsigned>(kernelSize - 1U),
                     static_cast<unsigned>(kernelSize),
                     static_cast<unsigned>(kernelSize + 1U),
                     static_cast<unsigned>(2U * kernelSize + 3U)}) {
                const auto imageLayout = layout(static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height), width * 2U + 3U);
                auto created = SharpenStage::create(parameters, imageLayout);
                ASSERT_TRUE(created.hasValue());
                auto stage = std::move(created).value();
                for (const auto pattern : patterns) {
                    const auto input = makeInput(width, height, pattern);
                    const auto expected = sharpenReference(input, width, height,
                        kernel, parameters.amount, parameters.threshold);
                    expectProcessMatches(*stage, input, expected, width, height);
                }
                const auto first = makeInput(width, height, Pattern::Noise);
                const auto expected = sharpenReference(first, width, height,
                    kernel, parameters.amount, parameters.threshold);
                expectProcessMatches(*stage, first, expected, width, height);
            }
        }
    }
}


TEST(DetailStageBatching, PreparedParallelFiltersMatchIndependentOracleAndDispatch) {
    using namespace processing::detail;
    for (const auto slots : {1U,2U,4U}) {
        PreparedCpuExecutor executor(slots);
        std::atomic<unsigned> observed{};
        executor.setWorkObserver(&observed, [](void* c,CpuJobKind,std::size_t slot,std::size_t,std::size_t) noexcept {
            if(slot) static_cast<std::atomic<unsigned>*>(c)->fetch_or(1U<<slot);
        });
        for (const auto& [width,height] : {std::pair{1U,1U}, {17U,37U}, {257U,257U}, {3U,21847U}, {21847U,3U}, {4097U,17U}}) {
            const auto imageLayout=layout(width,height,width*2U+3U);
            constexpr DenoiseParameters denoise{DenoiseMode::Gaussian,7U,1.25};
            constexpr SharpenParameters sharpen{1.75,5.0,12.5};
            auto gaussian=StageStorage::createDenoise(denoise,imageLayout,64U*1024U*1024U,&executor);
            auto sharp=StageStorage::createSharpen(sharpen,imageLayout,64U*1024U*1024U,&executor);
            ASSERT_TRUE(gaussian.hasValue()); ASSERT_TRUE(sharp.hasValue());
            for(const auto pattern : {Pattern::Noise,Pattern::TopBottomImpulse,Pattern::Noise}) {
                const auto input=makeInput(width,height,pattern);
                observed=0;
                expectProcessMatches(*gaussian.value(),input,gaussianReference(input,width,height,coefficients(7U,1.25)),width,height);
                EXPECT_EQ(observed.load(),width*height>=65536U ? (1U<<std::min(slots,height))-2U : 0U);
                observed=0;
                expectProcessMatches(*sharp.value(),input,sharpenReference(input,width,height,coefficients(31U,5.0),1.75,12.5),width,height);
                // Short images can have fewer nonempty stripes than slots.
                EXPECT_EQ(observed.load(),width*height>=65536U ? (1U<<std::min(slots,height))-2U : 0U);
            }
        }
        executor.setWorkObserver(nullptr,nullptr);
    }
}


TEST(DetailStageBatching, ParallelFilterVariantsAndEnvironmentFallbackPreserveCompletePixels) {
    using namespace processing::detail;
    constexpr unsigned width=263U,height=251U;
    const auto imageLayout=layout(width,height,width*2U+3U);
    const auto input=makeInput(width,height,Pattern::Noise);
    for(const bool fallback : {false,true}) {
        CpuExecutorTestHooks hooks;
        if(fallback) hooks.failEnvironment=[](void*,CpuEnvironmentOperation op,std::size_t) noexcept {return op==CpuEnvironmentOperation::InstallHelper;};
        PreparedCpuExecutor executor(4,hooks);
        for(const auto parameters : {DenoiseParameters{DenoiseMode::Gaussian,3,0},DenoiseParameters{DenoiseMode::Gaussian,5,5}}) {
            auto stage=StageStorage::createDenoise(parameters,imageLayout,16U*1024U*1024U,&executor);
            ASSERT_TRUE(stage.hasValue());
            expectProcessMatches(*stage.value(),input,gaussianReference(input,width,height,coefficients(parameters.kernelSize,parameters.sigma)),width,height);
        }
        for(const auto parameters : {SharpenParameters{5,0.5,0},SharpenParameters{1.5,2,12.5}}) {
            auto stage=StageStorage::createSharpen(parameters,imageLayout,16U*1024U*1024U,&executor);
            ASSERT_TRUE(stage.hasValue());
            const auto kernel=coefficients(static_cast<std::size_t>(2*std::ceil(3*parameters.radius)+1),parameters.radius);
            expectProcessMatches(*stage.value(),input,sharpenReference(input,width,height,kernel,parameters.amount,parameters.threshold),width,height);
        }
    }
    // Prepare coefficients once; helpers must follow caller modes on each subsequent job.
    PreparedCpuExecutor executor(4);
    const DenoiseParameters parameters{DenoiseMode::Gaussian,7,1.25};
    auto parallel=StageStorage::createDenoise(parameters,imageLayout,16U*1024U*1024U,&executor);
    auto serial=DenoiseStage::create(parameters,imageLayout);
    ASSERT_TRUE(parallel.hasValue()); ASSERT_TRUE(serial.hasValue());
    struct RoundingGuard { int saved=std::fegetround(); ~RoundingGuard() {std::fesetround(saved);} } guard;
    for(const auto mode : {FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO,FE_TONEAREST}) {
        ASSERT_EQ(std::fesetround(mode),0);
        BufferedImage first(width,height,input),second(width,height,input);
        ASSERT_TRUE(serial.value()->process(first.sourceView(),first.destinationView(),canonicalFormat()).hasValue());
        ASSERT_TRUE(parallel.value()->process(second.sourceView(),second.destinationView(),canonicalFormat()).hasValue());
        EXPECT_EQ(first.activeDestination(),second.activeDestination());
        EXPECT_EQ(std::fegetround(),mode);
    }
    std::atomic<unsigned> calls{};
    executor.setWorkObserver(&calls,[](void* p,CpuJobKind,std::size_t,std::size_t,std::size_t) noexcept {++*static_cast<std::atomic<unsigned>*>(p);});
    auto median=StageStorage::createDenoise({DenoiseMode::Median,3,0},imageLayout,0,&executor);
    ASSERT_TRUE(median.hasValue());
    BufferedImage buffer(width,height,input);
    ASSERT_TRUE(median.value()->process(buffer.sourceView(),buffer.destinationView(),canonicalFormat()).hasValue());
    EXPECT_EQ(calls,0U);
    executor.setWorkObserver(nullptr,nullptr);
}

TEST(DetailStageBatching, PreparedPrivateScratchAdmitsEverySlotBeforeAllocation) {
    using namespace processing::detail;
    const auto imageLayout=layout(257U,259U,517U);
    constexpr SharpenParameters parameters{1.75,5.0,12.5};
    const auto serial=SharpenStage::requiredScratchBytes(parameters,imageLayout).value();
    for (const auto slots : {1U,2U,4U}) {
        PreparedCpuExecutor executor(slots);
        const auto expected=slots*257U*31U*sizeof(double)+31U*sizeof(double);
        EXPECT_EQ(StageStorage::sharpenScratchBytes(parameters,imageLayout,slots).value(),expected);
        auto rejected=StageStorage::createSharpen(parameters,imageLayout,expected-1U,&executor);
        EXPECT_FALSE(rejected.hasValue());
        auto made=StageStorage::createSharpen(parameters,imageLayout,expected,&executor);
        ASSERT_TRUE(made.hasValue()); EXPECT_EQ(made.value()->scratchBytes(),expected);
        EXPECT_EQ(SharpenStage::requiredScratchBytes(parameters,imageLayout).value(),serial);
    }
    EXPECT_FALSE(StageStorage::sharpenScratchBytes(parameters,imageLayout,std::numeric_limits<std::size_t>::max()).hasValue());
}

}  // namespace
