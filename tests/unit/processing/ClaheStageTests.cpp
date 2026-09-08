#include <lumora/processing/ClaheStage.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using lumora::core::BitAlignment;
using lumora::core::ImageLayout;
using lumora::core::SourcePacking;
using lumora::core::SourcePixelFormat;
using lumora::core::StorageType;
using lumora::processing::ClaheParameters;
using lumora::processing::ClaheStage;
using lumora::processing::ImageDomain;
using lumora::processing::ImageView;
using lumora::processing::MutableImageView;
using lumora::processing::StageId;

[[nodiscard]] SourcePixelFormat canonicalSourceFormat(
    std::uint8_t validBits = 16U,
    std::uint16_t sampleMaximum = 65535U) {
    return {"CanonicalU16", 0U, validBits, sampleMaximum, SourcePacking::Unpacked,
        BitAlignment::LeastSignificant, StorageType::UInt16};
}

[[nodiscard]] ImageLayout tightLayout(
    std::uint32_t width,
    std::uint32_t height,
    StorageType storage = StorageType::UInt16) {
    const auto bytesPerPixel = storage == StorageType::UInt16 ? 2U : 1U;
    const auto stride = static_cast<std::size_t>(width) * bytesPerPixel;
    return ImageLayout::create(width, height, stride, storage,
        stride * static_cast<std::size_t>(height)).value();
}

[[nodiscard]] std::unique_ptr<ClaheStage> createStage(
    std::uint32_t width,
    std::uint32_t height,
    ClaheParameters parameters = {}) {
    auto result = ClaheStage::create(parameters, tightLayout(width, height));
    EXPECT_TRUE(result.hasValue())
        << (result.hasValue() ? "" : result.error().code + ": " + result.error().diagnosticDetail);
    return result.hasValue() ? std::move(result).value() : nullptr;
}

[[nodiscard]] std::vector<std::uint16_t> runTight(
    ClaheStage& stage,
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint16_t> input,
    const SourcePixelFormat& format = canonicalSourceFormat()) {
    const auto layout = tightLayout(width, height);
    std::vector<std::byte> sourceBytes(input.size_bytes());
    std::memcpy(sourceBytes.data(), input.data(), input.size_bytes());
    const auto sourceBefore = sourceBytes;
    std::vector<std::byte> destinationBytes(input.size_bytes(), std::byte{0xA5});
    const auto source = ImageView::create(
        layout, sourceBytes, ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(
        layout, destinationBytes, ImageDomain::CanonicalU16).value();

    const auto result = stage.process(source, destination, format);

    EXPECT_TRUE(result.hasValue())
        << (result.hasValue() ? "" : result.error().code + ": " + result.error().diagnosticDetail);
    EXPECT_EQ(sourceBytes, sourceBefore);
    std::vector<std::uint16_t> output(input.size());
    if (result.hasValue()) {
        std::memcpy(output.data(), destinationBytes.data(), destinationBytes.size());
    }
    return output;
}

#if !defined(_WIN32)
[[nodiscard]] std::vector<std::uint16_t> fixtureArray(
    std::string_view document,
    std::string_view key) {
    const auto label = std::string{"\""} + std::string(key) + "\"";
    const auto labelPosition = document.find(label);
    if (labelPosition == std::string_view::npos) return {};
    const auto begin = document.find('[', labelPosition + label.size());
    const auto end = document.find(']', begin);
    if (begin == std::string_view::npos || end == std::string_view::npos) return {};

    std::vector<std::uint16_t> values;
    std::uint32_t value = 0U;
    bool reading = false;
    for (std::size_t index = begin + 1U; index < end; ++index) {
        const auto character = static_cast<unsigned char>(document[index]);
        if (std::isdigit(character) != 0) {
            reading = true;
            value = value * 10U + static_cast<std::uint32_t>(character - '0');
        } else if (reading) {
            if (value > std::numeric_limits<std::uint16_t>::max()) return {};
            values.push_back(static_cast<std::uint16_t>(value));
            value = 0U;
            reading = false;
        }
    }
    if (reading) {
        if (value > std::numeric_limits<std::uint16_t>::max()) return {};
        values.push_back(static_cast<std::uint16_t>(value));
    }
    return values;
}
#endif

// Quantizing input to U8 or omitting the U16 CLAHE calculation changes these literals.
TEST(ClaheStage, PreservesLowU16BitsInHandCalculatedIdenticalTiles) {
    constexpr std::array<std::uint16_t, 16> input{
        0U, 1U, 0U, 1U,
        2U, 3U, 2U, 3U,
        0U, 1U, 0U, 1U,
        2U, 3U, 2U, 3U,
    };
    constexpr std::array<std::uint16_t, 16> expected{
        16384U, 32768U, 16384U, 32768U,
        49151U, 65535U, 49151U, 65535U,
        16384U, 32768U, 16384U, 32768U,
        49151U, 65535U, 49151U, 65535U,
    };
    auto stage = createStage(4U, 4U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);

    EXPECT_EQ(runTight(*stage, 4U, 4U, input),
        (std::vector<std::uint16_t>(expected.begin(), expected.end())));
}

// Selecting one tile instead of bilinearly combining neighboring LUTs breaks 24576.
TEST(ClaheStage, InterpolatesDifferentTileLutsAtHorizontalAndVerticalBoundaries) {
    constexpr std::array<std::uint16_t, 16> input{
        65535U, 65535U, 0U, 0U,
        65535U, 65535U, 0U, 0U,
        0U, 0U, 65535U, 65535U,
        0U, 0U, 65535U, 65535U,
    };
    constexpr std::array<std::uint16_t, 16> expected{
        65535U, 65535U, 24576U, 32768U,
        65535U, 65535U, 24576U, 32768U,
        24576U, 24576U, 65535U, 65535U,
        32768U, 32768U, 65535U, 65535U,
    };
    auto stage = createStage(4U, 4U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);

    EXPECT_EQ(runTight(*stage, 4U, 4U, input),
        (std::vector<std::uint16_t>(expected.begin(), expected.end())));
}

// Weakening any parameter/layout bound admits an unsafe OpenCV configuration.
TEST(ClaheStage, FactoryAcceptsBoundariesAndRejectsInvalidConfiguration) {
    const auto layout = tightLayout(32U, 32U);
    for (const auto parameters : {
             ClaheParameters{0.1, 2U},
             ClaheParameters{40.0, 32U},
         }) {
        EXPECT_TRUE(ClaheStage::create(parameters, layout).hasValue());
    }
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto clip : {0.0999999, 40.0000001, infinity, -infinity, nan}) {
        const auto result = ClaheStage::create({clip, 2U}, layout);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "clahe_invalid_clip_limit");
    }
    for (const auto grid : {
             0U, 1U, 33U, std::numeric_limits<std::uint32_t>::max()}) {
        const auto result = ClaheStage::create({2.0, grid}, layout);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "clahe_invalid_tile_grid");
    }
    auto result = ClaheStage::create({2.0, 2U},
        tightLayout(32U, 32U, StorageType::UInt8));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "clahe_layout_storage_mismatch");
    for (const auto& [width, height] : {
             std::pair{7U, 8U}, std::pair{8U, 7U}}) {
        result = ClaheStage::create({2.0, 8U}, tightLayout(width, height));
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "clahe_image_too_small");
    }
}

// Removing a preflight lets signed OpenCV dimensions or internal arithmetic overflow.
TEST(ClaheStage, FactoryRejectsUnsafeOpenCvArithmeticBeforeAllocation) {
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        GTEST_SKIP() << "The 64-bit preflight fixtures cannot be represented by size_t.";
    }
    const auto makeLargeLayout = [](std::uint32_t width, std::uint32_t height) {
        const auto stride = static_cast<std::size_t>(width) * 2U;
        return ImageLayout::create(width, height, stride, StorageType::UInt16,
            stride * static_cast<std::size_t>(height)).value();
    };

    auto result = ClaheStage::create({2.0, 2U},
        makeLargeLayout(std::numeric_limits<std::uint32_t>::max(), 2U));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "clahe_dimension_out_of_range");

    result = ClaheStage::create({2.0, 2U},
        makeLargeLayout(static_cast<std::uint32_t>(INT_MAX), 2U));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "clahe_reflected_extent_overflow");

    result = ClaheStage::create({2.0, 2U},
        makeLargeLayout(static_cast<std::uint32_t>(INT_MAX - 1),
            static_cast<std::uint32_t>(INT_MAX - 1)));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "clahe_tile_area_overflow");

    result = ClaheStage::create({2.0, 2U},
        makeLargeLayout(536870912U, 2U));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "clahe_interpolation_size_overflow");
}

// Changing the declared scratch count lets preparation under-budget the stage.
TEST(ClaheStage, ReportsCanonicalTraitsAndTwoPreparedBridgeImages) {
    auto stage = createStage(4U, 4U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->id(), StageId::Clahe);
    EXPECT_EQ(stage->traits().id, StageId::Clahe);
    EXPECT_EQ(stage->traits().inputDomain, ImageDomain::CanonicalU16);
    EXPECT_EQ(stage->traits().outputDomain, ImageDomain::CanonicalU16);
    EXPECT_FALSE(stage->traits().changesDimensions);
    EXPECT_EQ(stage->traits().scratchImages, 2U);
    EXPECT_EQ(stage->traits().historyFrames, 0U);
    EXPECT_FALSE(stage->traits().requiresCalibrationAsset);

    const auto registry = lumora::processing::stageRegistry();
    const auto entry = std::find_if(registry.begin(), registry.end(), [](const auto& traits) {
        return traits.id == StageId::Clahe;
    });
    ASSERT_NE(entry, registry.end());
    EXPECT_EQ(entry->scratchImages, 2U);
}

// Dropping a view guard permits reinterpretation, stale preparation, or aliased writes.
TEST(ClaheStage, RejectsIncompatibleViewsAndEveryPayloadOverlapBeforeWriting) {
    auto stage = createStage(4U, 4U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);
    std::array<std::byte, 128> sourceStorage{};
    std::array<std::byte, 128> destinationStorage{};
    destinationStorage.fill(std::byte{0x6D});
    const auto destinationBefore = destinationStorage;
    const auto goodLayout = tightLayout(4U, 4U);
    const auto goodSource = ImageView::create(goodLayout, sourceStorage,
        ImageDomain::CanonicalU16).value();
    const auto goodDestination = MutableImageView::create(goodLayout, destinationStorage,
        ImageDomain::CanonicalU16).value();
    const auto u8Layout = tightLayout(4U, 4U, StorageType::UInt8);

    struct Case final {
        ImageView source;
        MutableImageView destination;
        const char* code;
    };
    const std::array cases{
        Case{ImageView::create(u8Layout, sourceStorage, ImageDomain::SensorNative).value(),
            goodDestination, "clahe_source_storage_mismatch"},
        Case{goodSource, MutableImageView::create(u8Layout, destinationStorage,
            ImageDomain::SensorNative).value(), "clahe_destination_storage_mismatch"},
        Case{ImageView::create(goodLayout, sourceStorage, ImageDomain::SensorNative).value(),
            goodDestination, "clahe_image_domain_mismatch"},
        Case{goodSource, MutableImageView::create(goodLayout, destinationStorage,
            ImageDomain::SensorNative).value(), "clahe_image_domain_mismatch"},
        Case{goodSource, MutableImageView::create(tightLayout(3U, 4U), destinationStorage,
            ImageDomain::CanonicalU16).value(), "clahe_image_extent_mismatch"},
        Case{ImageView::create(tightLayout(5U, 4U), sourceStorage,
                 ImageDomain::CanonicalU16).value(),
            MutableImageView::create(tightLayout(5U, 4U), destinationStorage,
                ImageDomain::CanonicalU16).value(),
            "clahe_prepared_extent_mismatch"},
    };
    for (const auto& testCase : cases) {
        const auto result = stage->process(
            testCase.source, testCase.destination, canonicalSourceFormat());
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, testCase.code);
        EXPECT_EQ(destinationStorage, destinationBefore);
    }

    const auto paddedLayout = ImageLayout::create(
        4U, 4U, 10U, StorageType::UInt16, 40U).value();
    for (const auto& [sourceOffset, destinationOffset] : {
             std::pair{8U, 8U}, std::pair{8U, 9U}, std::pair{8U, 16U}}) {
        std::array<std::byte, 96> shared{};
        shared.fill(std::byte{0x4B});
        const auto before = shared;
        const auto source = ImageView::create(paddedLayout,
            std::span(shared).subspan(sourceOffset), ImageDomain::CanonicalU16).value();
        const auto destination = MutableImageView::create(paddedLayout,
            std::span(shared).subspan(destinationOffset), ImageDomain::CanonicalU16).value();
        const auto result = stage->process(source, destination, canonicalSourceFormat());
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "clahe_image_views_overlap");
        EXPECT_EQ(shared, before);
    }
}

// Checking active rows alone misses overlap confined to declared row padding.
TEST(ClaheStage, RejectsPaddingOnlyPayloadOverlapBeforeWriting) {
    auto stage = createStage(2U, 2U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);
    constexpr std::size_t stride = 16U;
    constexpr std::size_t destinationOffset = 8U;
    const auto layout = ImageLayout::create(
        2U, 2U, stride, StorageType::UInt16, stride * 2U).value();
    std::array<std::byte, 48> shared{};
    shared.fill(std::byte{0x71});
    const auto before = shared;
    const auto source = ImageView::create(
        layout, shared, ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(layout,
        std::span(shared).subspan(destinationOffset),
        ImageDomain::CanonicalU16).value();

    // Source active bytes are [0,4) and [16,20); destination active bytes are
    // [8,12) and [24,28). Only their complete 32-byte payload spans overlap.
    for (std::size_t sourceRow = 0U; sourceRow < 2U; ++sourceRow) {
        const auto sourceBegin = sourceRow * stride;
        const auto sourceEnd = sourceBegin + 4U;
        for (std::size_t destinationRow = 0U; destinationRow < 2U; ++destinationRow) {
            const auto destinationBegin = destinationOffset + destinationRow * stride;
            const auto destinationEnd = destinationBegin + 4U;
            ASSERT_TRUE(sourceEnd <= destinationBegin || destinationEnd <= sourceBegin);
        }
    }

    const auto result = stage->process(source, destination, canonicalSourceFormat());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "clahe_image_views_overlap");
    EXPECT_EQ(shared, before);
}

// Direct cv::Mat wrapping that assumes a tight row corrupts padding and samples.
TEST(ClaheStage, HandlesDistinctEvenPaddedStridesAndPreservesCanaries) {
    constexpr std::array<std::uint16_t, 16> input{
        0U, 1U, 0U, 1U, 2U, 3U, 2U, 3U,
        0U, 1U, 0U, 1U, 2U, 3U, 2U, 3U};
    constexpr std::array<std::uint16_t, 16> expected{
        16384U, 32768U, 16384U, 32768U,
        49151U, 65535U, 49151U, 65535U,
        16384U, 32768U, 16384U, 32768U,
        49151U, 65535U, 49151U, 65535U};
    constexpr std::size_t sourceStride = 12U;
    constexpr std::size_t destinationStride = 14U;
    std::array<std::uint16_t, sourceStride / 2U * 4U> sourceStorage{};
    std::array<std::uint16_t, destinationStride / 2U * 4U> destinationStorage{};
    std::fill(sourceStorage.begin(), sourceStorage.end(), 0xC3C3U);
    std::fill(destinationStorage.begin(), destinationStorage.end(), 0x5A5AU);
    for (std::size_t row = 0U; row < 4U; ++row) {
        std::memcpy(reinterpret_cast<std::byte*>(sourceStorage.data()) + row * sourceStride,
            input.data() + row * 4U, 8U);
    }
    const auto sourceBefore = sourceStorage;
    const auto destinationBefore = destinationStorage;
    const auto sourceLayout = ImageLayout::create(
        4U, 4U, sourceStride, StorageType::UInt16, sourceStride * 4U).value();
    const auto destinationLayout = ImageLayout::create(
        4U, 4U, destinationStride, StorageType::UInt16, destinationStride * 4U).value();
    const auto source = ImageView::create(sourceLayout,
        std::as_bytes(std::span(sourceStorage)), ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(destinationLayout,
        std::as_writable_bytes(std::span(destinationStorage)),
        ImageDomain::CanonicalU16).value();
    auto stage = createStage(4U, 4U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);

    const auto result = stage->process(source, destination, canonicalSourceFormat());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(sourceStorage, sourceBefore);
    for (std::size_t row = 0U; row < 4U; ++row) {
        std::array<std::uint16_t, 4> actual{};
        std::memcpy(actual.data(), reinterpret_cast<const std::byte*>(destinationStorage.data())
                + row * destinationStride, 8U);
        EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin() + row * 4U));
        for (std::size_t byte = 8U; byte < destinationStride; ++byte) {
            const auto index = row * destinationStride + byte;
            EXPECT_EQ(reinterpret_cast<const std::byte*>(destinationStorage.data())[index],
                reinterpret_cast<const std::byte*>(destinationBefore.data())[index]);
        }
    }
}

// Passing odd steps or unaligned U16 pointers to cv::Mat rejects valid Lumora views.
TEST(ClaheStage, BridgesOddStridesAndUnalignedStartsWithoutTouchingPadding) {
    constexpr std::array<std::uint16_t, 16> input{
        0U, 1U, 0U, 1U, 2U, 3U, 2U, 3U,
        0U, 1U, 0U, 1U, 2U, 3U, 2U, 3U};
    constexpr std::array<std::uint16_t, 16> expected{
        16384U, 32768U, 16384U, 32768U,
        49151U, 65535U, 49151U, 65535U,
        16384U, 32768U, 16384U, 32768U,
        49151U, 65535U, 49151U, 65535U};
    constexpr std::size_t sourceStride = 11U;
    constexpr std::size_t destinationStride = 13U;
    std::array<std::byte, 1U + sourceStride * 4U + 3U> sourceStorage{};
    std::array<std::byte, 1U + destinationStride * 4U + 3U> destinationStorage{};
    sourceStorage.fill(std::byte{0xC3});
    destinationStorage.fill(std::byte{0x5A});
    for (std::size_t row = 0U; row < 4U; ++row) {
        std::memcpy(sourceStorage.data() + 1U + row * sourceStride,
            input.data() + row * 4U, 8U);
    }
    const auto sourceBefore = sourceStorage;
    const auto destinationBefore = destinationStorage;
    const auto sourceLayout = ImageLayout::create(
        4U, 4U, sourceStride, StorageType::UInt16, sourceStride * 4U).value();
    const auto destinationLayout = ImageLayout::create(
        4U, 4U, destinationStride, StorageType::UInt16, destinationStride * 4U).value();
    const auto source = ImageView::create(sourceLayout,
        std::span(sourceStorage).subspan(1U), ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(destinationLayout,
        std::span(destinationStorage).subspan(1U), ImageDomain::CanonicalU16).value();
    auto stage = createStage(4U, 4U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);

    const auto result = stage->process(source, destination, canonicalSourceFormat());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(sourceStorage, sourceBefore);
    for (std::size_t row = 0U; row < 4U; ++row) {
        std::array<std::uint16_t, 4> actual{};
        std::memcpy(actual.data(), destinationStorage.data() + 1U + row * destinationStride, 8U);
        EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin() + row * 4U));
    }
    for (std::size_t index = 0U; index < destinationStorage.size(); ++index) {
        const auto relative = index == 0U ? destinationStride : (index - 1U) % destinationStride;
        if (index == 0U || relative >= 8U || index >= 1U + destinationStride * 4U) {
            EXPECT_EQ(destinationStorage[index], destinationBefore[index]) << index;
        }
    }
}

// Assuming divisible dimensions or a fixed grid fails one of these prepared shapes.
TEST(ClaheStage, SupportsGridBoundariesDivisibleAndReflectedUniformImages) {
    struct Case final {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t grid;
        std::uint16_t value;
    };
    constexpr std::array cases{
        Case{5U, 6U, 2U, 0U},
        Case{9U, 10U, 8U, 32768U},
        Case{32U, 33U, 32U, 65535U},
    };
    for (const auto& testCase : cases) {
        auto stage = createStage(testCase.width, testCase.height,
            {.clipLimit = 2.0, .tileGridSize = testCase.grid});
        ASSERT_NE(stage, nullptr);
        const std::vector<std::uint16_t> input(
            static_cast<std::size_t>(testCase.width) * testCase.height, testCase.value);
        const auto output = runTight(
            *stage, testCase.width, testCase.height, input);
        ASSERT_FALSE(output.empty());
        EXPECT_TRUE(std::all_of(output.begin(), output.end(), [&](std::uint16_t value) {
            return value == output.front();
        }));
        if (testCase.value == 65535U) {
            EXPECT_EQ(output.front(), 65535U);
        }
    }
}

// Source-dependent contamination or using provenance bits to rescale canonical samples breaks A-B-A.
TEST(ClaheStage, ReusesOneConfiguredObjectAcrossDifferentImagesWithoutTruncatingCanonicalData) {
    constexpr std::uint32_t width = 8U;
    constexpr std::uint32_t height = 8U;
    std::array<std::uint16_t, width * height> first{};
    std::array<std::uint16_t, width * height> second{};
    for (std::size_t index = 0U; index < first.size(); ++index) {
        first[index] = static_cast<std::uint16_t>(
            (index * 65535U) / (first.size() - 1U));
        second[index] = index % 2U == 0U ? 0U : 65535U;
    }
    auto stage = createStage(width, height, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);

    const auto firstOutput = runTight(*stage, width, height, first);
    const auto secondOutput = runTight(*stage, width, height, second);
    const auto repeatedOutput = runTight(
        *stage, width, height, first, canonicalSourceFormat(10U, 1023U));

    EXPECT_EQ(repeatedOutput, firstOutput);
    EXPECT_NE(secondOutput, firstOutput);
    EXPECT_EQ(first.front(), 0U);
    EXPECT_EQ(first.back(), 65535U);
    EXPECT_EQ(firstOutput.back(), 65535U);
}

// This is characterization of the pinned Linux build, not designated-Windows acceptance.
TEST(ClaheStage, ProvisionalLinuxGradientBaseline) {
#if defined(_WIN32)
    GTEST_SKIP()
        << "Linux-provenance CLAHE fixture is not designated-Windows output; "
           "Windows reference and reviewed tolerances remain pending.";
#else
    std::ifstream fixture(LUMORA_CLAHE_REFERENCE_FIXTURE);
    ASSERT_TRUE(fixture.is_open()) << LUMORA_CLAHE_REFERENCE_FIXTURE;
    const std::string document{
        std::istreambuf_iterator<char>(fixture), std::istreambuf_iterator<char>()};
    const auto input = fixtureArray(document, "input");
    const auto expected = fixtureArray(document, "expected");
    ASSERT_EQ(input.size(), 72U);
    ASSERT_EQ(expected.size(), input.size());
    auto stage = createStage(9U, 8U, {.clipLimit = 2.0, .tileGridSize = 2U});
    ASSERT_NE(stage, nullptr);

    EXPECT_EQ(runTight(*stage, 9U, 8U, input), expected);
#endif
}

}  // namespace
