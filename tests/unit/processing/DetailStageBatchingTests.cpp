#include "DetailStageSupport.hpp"
#include "PreparedCpuExecutor.hpp"
#include "StageStorage.hpp"
#include <atomic>
#include <cfenv>

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
        for (const auto [width,height] : {std::pair{1U,1U}, {17U,37U}, {257U,257U}, {3U,21847U}, {21847U,3U}, {4097U,17U}}) {
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
