#include "AllocationTracker.hpp"

#include <lumora/processing/ClaheStage.hpp>
#include <lumora/processing/DenoiseStage.hpp>
#include <lumora/processing/SharpenStage.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace {

using namespace lumora;

[[nodiscard]] core::ImageLayout layout(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t stride) {
    return core::ImageLayout::create(width, height, stride,
        core::StorageType::UInt16,
        stride * static_cast<std::size_t>(height)).value();
}

[[nodiscard]] core::SourcePixelFormat canonicalFormat() {
    return {"CanonicalU16", 0U, 16U, 65535U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        core::StorageType::UInt16};
}

[[nodiscard]] bool positiveControlsPass() {
    test::beginAllocationTracking();
    auto* ordinary = ::operator new(7U);
    auto* array = ::operator new[](11U);
    auto* noThrow = ::operator new(13U, std::nothrow);
    auto* arrayNoThrow = ::operator new[](17U, std::nothrow);
    auto* aligned = ::operator new(19U, std::align_val_t{64U});
    auto* alignedArray = ::operator new[](23U, std::align_val_t{64U});
    auto* alignedNoThrow = ::operator new(
        29U, std::align_val_t{64U}, std::nothrow);
    auto* alignedArrayNoThrow = ::operator new[](
        31U, std::align_val_t{64U}, std::nothrow);
    ::operator delete(ordinary);
    ::operator delete[](array);
    ::operator delete(noThrow, std::nothrow);
    ::operator delete[](arrayNoThrow, std::nothrow);
    ::operator delete(aligned, std::align_val_t{64U});
    ::operator delete[](alignedArray, std::align_val_t{64U});
    ::operator delete(alignedNoThrow, std::align_val_t{64U}, std::nothrow);
    ::operator delete[](alignedArrayNoThrow, std::align_val_t{64U}, std::nothrow);
    const auto observed = test::endAllocationMeasurement();
    return observed.allocations == 8U && observed.allocatedBytes == 150U && observed.deallocations == 8U;
}

struct PreparedMeasurement final {
    std::size_t successfulCalls;
    test::AllocationCounts counts;
    std::uint64_t checksumA;
    std::uint64_t checksumB;
};

void accumulateChecksum(
    std::uint64_t& checksum,
    processing::MutableImageView view) noexcept {
    for (std::uint32_t y = 0U; y < view.layout().height(); ++y) {
        const auto row = view.row(y);
        for (std::uint32_t x = 0U; x < view.layout().width(); ++x) {
            std::uint16_t value = 0U;
            std::memcpy(&value, row.data() + static_cast<std::size_t>(x) * 2U,
                sizeof(value));
            checksum ^= value;
            checksum *= 1099511628211ULL;
        }
    }
}

template<typename Stage>
[[nodiscard]] PreparedMeasurement measurePreparedStage(
    Stage& stage,
    const processing::ImageView& sourceA,
    processing::MutableImageView destinationA,
    const processing::ImageView& sourceB,
    processing::MutableImageView destinationB) {
    const auto format = canonicalFormat();
    std::size_t successfulCalls = 0U;
    std::uint64_t checksumA = 1469598103934665603ULL;
    std::uint64_t checksumB = 1469598103934665603ULL;
    test::beginAllocationTracking();
    for (std::size_t call = 0U; call < 1000U; ++call) {
        const bool alternate = call % 2U != 0U;
        const auto result = alternate
            ? stage.process(sourceB, destinationB, format)
            : stage.process(sourceA, destinationA, format);
        successfulCalls += result.hasValue() ? 1U : 0U;
        if (result.hasValue()) {
            accumulateChecksum(alternate ? checksumB : checksumA,
                alternate ? destinationB : destinationA);
        }
    }
    return {successfulCalls, test::endAllocationMeasurement(), checksumA, checksumB};
}

[[nodiscard]] bool passed(const PreparedMeasurement& measurement) {
    return measurement.successfulCalls == 1000U
        && measurement.counts.allocations == 0U
        && measurement.checksumA != measurement.checksumB;
}

[[nodiscard]] std::size_t recoverableClahePreparationAllocationCount(
    const core::ImageLayout& preparedLayout) {
    test::beginAllocationTracking();
    auto baseline = processing::ClaheStage::create({2.0, 2U}, preparedLayout);
    const auto counts = test::endAllocationMeasurement();
    if (!baseline.hasValue() || counts.allocations == 0U) return 0U;

    for (std::size_t failure = 0U; failure < counts.allocations; ++failure) {
        test::failOneAllocationAfter(failure);
        const auto rejected = processing::ClaheStage::create(
            {2.0, 2U}, preparedLayout);
        test::cancelAllocationFailure();
        if (rejected.hasValue()
            || rejected.error().code != "clahe_allocation_failed"
            || rejected.error().category != core::ErrorCategory::ResourceExhaustion
            || !rejected.error().recoverable) {
            return 0U;
        }
    }
    return counts.allocations;
}

template<std::size_t Size>
[[nodiscard]] bool canariesIntact(const std::array<std::byte, Size>& storage,
    std::size_t stride, std::uint32_t width, std::uint32_t height,
    std::byte sentinel) {
    if (storage.front() != sentinel) return false;
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::size_t byte = static_cast<std::size_t>(width) * 2U;
             byte < stride; ++byte) {
            if (storage[1U + static_cast<std::size_t>(y) * stride + byte] != sentinel)
                return false;
        }
    }
    for (std::size_t index = 1U + stride * height; index < storage.size(); ++index) {
        if (storage[index] != sentinel) return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!positiveControlsPass()) {
        std::fputs("Allocation tracker did not observe all eight C++ new forms.\n", stderr);
        return 1;
    }

    constexpr std::uint32_t width = 11U;
    constexpr std::uint32_t height = 7U;
    constexpr std::size_t strideA = width * 2U + 1U;
    constexpr std::size_t strideB = width * 2U + 3U;
    std::array<std::byte, 1U + strideA * height + 3U> sourceStorageA{};
    std::array<std::byte, 1U + strideA * height + 3U> destinationStorageA{};
    std::array<std::byte, 1U + strideB * height + 3U> sourceStorageB{};
    std::array<std::byte, 1U + strideB * height + 3U> destinationStorageB{};
    sourceStorageA.fill(std::byte{0xA1});
    destinationStorageA.fill(std::byte{0xB2});
    sourceStorageB.fill(std::byte{0xC3});
    destinationStorageB.fill(std::byte{0xD4});
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            const auto first = static_cast<std::uint16_t>((x * 1001U + y * 313U) % 65536U);
            const auto second = static_cast<std::uint16_t>(
                65535U - ((x * 997U + y * 271U) % 65536U));
            std::memcpy(sourceStorageA.data() + 1U + y * strideA + x * 2U,
                &first, sizeof(first));
            std::memcpy(sourceStorageB.data() + 1U + y * strideB + x * 2U,
                &second, sizeof(second));
        }
    }
    const auto layoutA = layout(width, height, strideA);
    const auto layoutB = layout(width, height, strideB);
    const auto sourceA = processing::ImageView::create(layoutA,
        std::span(sourceStorageA).subspan(1U), processing::ImageDomain::CanonicalU16).value();
    const auto destinationA = processing::MutableImageView::create(layoutA,
        std::span(destinationStorageA).subspan(1U),
        processing::ImageDomain::CanonicalU16).value();
    const auto sourceB = processing::ImageView::create(layoutB,
        std::span(sourceStorageB).subspan(1U), processing::ImageDomain::CanonicalU16).value();
    const auto destinationB = processing::MutableImageView::create(layoutB,
        std::span(destinationStorageB).subspan(1U),
        processing::ImageDomain::CanonicalU16).value();
    const auto sourceBeforeA = sourceStorageA;
    const auto sourceBeforeB = sourceStorageB;

    const auto clahePreparationAllocations =
        recoverableClahePreparationAllocationCount(layoutA);
    if (clahePreparationAllocations == 0U) {
        std::fputs("CLAHE preparation allocation injection was not recoverable.\n", stderr);
        return 2;
    }

    auto gaussianResult = processing::DenoiseStage::create(
        {processing::DenoiseMode::Gaussian, 7U, 1.25}, layoutA);
    auto medianResult = processing::DenoiseStage::create(
        {processing::DenoiseMode::Median, 5U, 0.0}, layoutA);
    auto sharpenResult = processing::SharpenStage::create(
        {1.5, 2.0, 12.5}, layoutA);
    auto claheResult = processing::ClaheStage::create(
        {2.0, 2U}, layoutA);
    if (!gaussianResult.hasValue()
        || !medianResult.hasValue()
        || !sharpenResult.hasValue()
        || !claheResult.hasValue()) {
        std::fputs("Could not prepare detail stages for allocation measurement.\n", stderr);
        return 2;
    }
    auto gaussian = std::move(gaussianResult).value();
    auto median = std::move(medianResult).value();
    auto sharpen = std::move(sharpenResult).value();
    auto clahe = std::move(claheResult).value();

    if (!passed(measurePreparedStage(
            *gaussian, sourceA, destinationA, sourceB, destinationB))) {
        std::fputs("Gaussian denoise allocated or failed during 1,000 calls.\n", stderr);
        return 3;
    }
    if (!passed(measurePreparedStage(
            *median, sourceA, destinationA, sourceB, destinationB))) {
        std::fputs("Median denoise allocated or failed during 1,000 calls.\n", stderr);
        return 4;
    }
    if (!passed(measurePreparedStage(
            *sharpen, sourceA, destinationA, sourceB, destinationB))) {
        std::fputs("Sharpen allocated or failed during 1,000 calls.\n", stderr);
        return 5;
    }
    const auto claheMeasurement = measurePreparedStage(
        *clahe, sourceA, destinationA, sourceB, destinationB);
    if (!passed(claheMeasurement)) {
        std::fprintf(stderr,
            "CLAHE prepared calls: success=%zu/1000 allocations=%zu bytes=%zu "
            "deallocations=%zu.\n",
            claheMeasurement.successfulCalls,
            claheMeasurement.counts.allocations,
            claheMeasurement.counts.allocatedBytes,
            claheMeasurement.counts.deallocations);
        return 6;
    }
    if (sourceStorageA != sourceBeforeA || sourceStorageB != sourceBeforeB
        || !canariesIntact(destinationStorageA, strideA, width, height, std::byte{0xB2})
        || !canariesIntact(destinationStorageB, strideB, width, height, std::byte{0xD4})) {
        std::fputs("Prepared stage measurement changed source or destination canaries.\n",
            stderr);
        return 7;
    }

    std::printf(
        "Allocation tracker positive controls: 8/8; "
        "Gaussian, median, sharpen, and CLAHE: 0 allocations over 1,000 calls each; "
        "CLAHE preparation failures=%zu; A/B checksums=%llu/%llu; canaries intact.\n",
        clahePreparationAllocations,
        static_cast<unsigned long long>(claheMeasurement.checksumA),
        static_cast<unsigned long long>(claheMeasurement.checksumB));
    return 0;
}
