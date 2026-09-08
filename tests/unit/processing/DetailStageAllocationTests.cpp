#include "AllocationTracker.hpp"

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

template<typename Stage>
[[nodiscard]] bool measurePreparedStage(
    Stage& stage,
    const processing::ImageView& sourceA,
    processing::MutableImageView destinationA,
    const processing::ImageView& sourceB,
    processing::MutableImageView destinationB) {
    const auto format = canonicalFormat();
    bool successful = true;
    test::beginAllocationTracking();
    for (std::size_t call = 0U; call < 1000U; ++call) {
        const bool alternate = call % 2U != 0U;
        const auto result = alternate
            ? stage.process(sourceB, destinationB, format)
            : stage.process(sourceA, destinationA, format);
        successful = successful && result.hasValue();
    }
    const auto observed = test::endAllocationTracking();
    return successful && observed == 0U;
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
    std::array<std::byte, 1U + strideA * height> sourceStorageA{};
    std::array<std::byte, 1U + strideA * height> destinationStorageA{};
    std::array<std::byte, 1U + strideB * height> sourceStorageB{};
    std::array<std::byte, 1U + strideB * height> destinationStorageB{};
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

    auto gaussianResult = processing::DenoiseStage::create(
        {processing::DenoiseMode::Gaussian, 7U, 1.25}, layoutA);
    auto medianResult = processing::DenoiseStage::create(
        {processing::DenoiseMode::Median, 5U, 0.0}, layoutA);
    auto sharpenResult = processing::SharpenStage::create(
        {1.5, 2.0, 12.5}, layoutA);
    if (!gaussianResult.hasValue()
        || !medianResult.hasValue()
        || !sharpenResult.hasValue()) {
        std::fputs("Could not prepare detail stages for allocation measurement.\n", stderr);
        return 2;
    }
    auto gaussian = std::move(gaussianResult).value();
    auto median = std::move(medianResult).value();
    auto sharpen = std::move(sharpenResult).value();

    if (!measurePreparedStage(
            *gaussian, sourceA, destinationA, sourceB, destinationB)) {
        std::fputs("Gaussian denoise allocated or failed during 1,000 calls.\n", stderr);
        return 3;
    }
    if (!measurePreparedStage(
            *median, sourceA, destinationA, sourceB, destinationB)) {
        std::fputs("Median denoise allocated or failed during 1,000 calls.\n", stderr);
        return 4;
    }
    if (!measurePreparedStage(
            *sharpen, sourceA, destinationA, sourceB, destinationB)) {
        std::fputs("Sharpen allocated or failed during 1,000 calls.\n", stderr);
        return 5;
    }

    std::fputs(
        "Allocation tracker positive controls: 8/8; "
        "Gaussian, median, and sharpen: 0 allocations over 1,000 calls each.\n",
        stdout);
    return 0;
}
