#include <lumora/processing/WindowLevelStage.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace {
using lumora::core::BitAlignment;
using lumora::core::ImageLayout;
using lumora::core::SourcePacking;
using lumora::core::SourcePixelFormat;
using lumora::core::StorageType;
using lumora::processing::ImageDomain;
using lumora::processing::ImageView;
using lumora::processing::MutableImageView;
using lumora::processing::WindowLevelParameters;
using lumora::processing::WindowLevelStage;

[[nodiscard]] SourcePixelFormat canonicalSourceFormat() {
    return {"CanonicalU16", 0U, 16U, 65535U, SourcePacking::Unpacked,
        BitAlignment::LeastSignificant, StorageType::UInt16};
}

[[nodiscard]] std::vector<std::uint16_t> runWindowLevel(
    std::span<const std::uint16_t> input,
    WindowLevelParameters parameters) {
    std::vector<std::byte> sourceBytes(input.size_bytes());
    std::vector<std::byte> destinationBytes(input.size_bytes(), std::byte{0xA5});
    std::memcpy(sourceBytes.data(), input.data(), input.size_bytes());
    const auto layout = ImageLayout::create(static_cast<std::uint32_t>(input.size()),
        1U, input.size_bytes(), StorageType::UInt16, input.size_bytes()).value();
    const auto source = ImageView::create(
        layout, sourceBytes, ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(
        layout, destinationBytes, ImageDomain::CanonicalU16).value();
    const auto result = WindowLevelStage(parameters).process(
        source, destination, canonicalSourceFormat());
    if (!result.hasValue()) {
        ADD_FAILURE() << result.error().code << ": " << result.error().diagnosticDetail;
        return {};
    }

    std::vector<std::uint16_t> output(input.size());
    std::memcpy(output.data(), destinationBytes.data(), destinationBytes.size());
    return output;
}

[[nodiscard]] std::uint16_t exactHalfSampleOracle(
    std::uint16_t input,
    std::int64_t window,
    std::int64_t doubledLevel) {
    constexpr std::int64_t doubledMaximum = 2 * 65535;
    const auto lower = std::max<std::int64_t>(0, doubledLevel - window);
    const auto upper = std::min(doubledMaximum, doubledLevel + window);
    const auto doubledInput = 2 * static_cast<std::int64_t>(input);
    if (doubledInput <= lower) return 0U;
    if (doubledInput >= upper) return 65535U;

    const auto distance = static_cast<std::uint64_t>(doubledInput - lower);
    const auto range = static_cast<std::uint64_t>(upper - lower);
    const auto numerator = distance * 65535U;
    return static_cast<std::uint16_t>((numerator + range / 2U) / range);
}

TEST(WindowLevelStage, MapsBelowInsideAndAboveWindow) {
    constexpr std::array<std::uint16_t, 5> input{1999U, 2000U, 3000U, 4000U, 4001U};
    const auto output = runWindowLevel(input, {.window = 2000.0, .level = 3000.0});
    EXPECT_EQ(output, (std::vector<std::uint16_t>{0U, 0U, 32768U, 65535U, 65535U}));
}

TEST(WindowLevelStage, SupportsFractionalParametersWithoutQuantization) {
    constexpr std::array<std::uint16_t, 7> input{0U, 5U, 6U, 7U, 8U, 9U, 65535U};
    const auto output = runWindowLevel(input, {.window = 3.75, .level = 7.125});
    EXPECT_EQ(output,
        (std::vector<std::uint16_t>{0U, 0U, 13107U, 30583U, 48059U, 65535U, 65535U}));
}

TEST(WindowLevelStage, ClipsBoundsBeforeInterpolation) {
    constexpr std::array<std::uint16_t, 4> lowInput{0U, 500U, 1000U, 1500U};
    EXPECT_EQ(runWindowLevel(lowInput, {.window = 2000.0, .level = 500.0}),
        (std::vector<std::uint16_t>{0U, 21845U, 43690U, 65535U}));

    constexpr std::array<std::uint16_t, 4> highInput{64000U, 64535U, 65534U, 65535U};
    EXPECT_EQ(runWindowLevel(highInput, {.window = 2000.0, .level = 65000.0}),
        (std::vector<std::uint16_t>{0U, 22841U, 65492U, 65535U}));
}

TEST(WindowLevelStage, MatchesExactIndependentOracleForEveryCanonicalValue) {
    std::vector<std::uint16_t> input(65536U);
    for (std::uint32_t value = 0U; value <= 65535U; ++value) {
        input[value] = static_cast<std::uint16_t>(value);
    }

    constexpr std::array<std::int64_t, 4> windows{1, 2, 4096, 65535};
    constexpr std::array<std::int64_t, 3> doubledLevels{0, 65535, 131070};
    for (const auto window : windows) {
        for (const auto doubledLevel : doubledLevels) {
            const auto output = runWindowLevel(input,
                {.window = static_cast<double>(window),
                    .level = static_cast<double>(doubledLevel) / 2.0});
            ASSERT_EQ(output.size(), input.size());
            for (std::uint32_t value = 0U; value <= 65535U; ++value) {
                EXPECT_EQ(output[value], exactHalfSampleOracle(
                    static_cast<std::uint16_t>(value), window, doubledLevel))
                    << "window=" << window << ", doubledLevel=" << doubledLevel
                    << ", input=" << value;
            }
        }
    }
}

TEST(WindowLevelStage, DefaultMappingIsExactIdentityForEveryCanonicalValue) {
    std::vector<std::uint16_t> input(65536U);
    for (std::uint32_t value = 0U; value <= 65535U; ++value) {
        input[value] = static_cast<std::uint16_t>(value);
    }
    EXPECT_EQ(runWindowLevel(input, {}), input);
}

TEST(WindowLevelStage, CopiesDefaultIdentityAcrossUnalignedRowsWithUnequalStrides) {
    constexpr std::size_t sourceStride = 9U;
    constexpr std::size_t destinationStride = 11U;
    std::array<std::byte, 1U + sourceStride * 2U + 1U> sourceStorage{};
    std::array<std::byte, 1U + destinationStride * 2U + 1U> destinationStorage{};
    sourceStorage.fill(std::byte{0xC3});
    destinationStorage.fill(std::byte{0x5A});
    constexpr std::array<std::uint16_t, 6> samples{
        0U, 1U, 32768U, 65534U, 65535U, 12345U};
    std::memcpy(sourceStorage.data() + 1U, samples.data(), 6U);
    std::memcpy(sourceStorage.data() + 1U + sourceStride, samples.data() + 3U, 6U);
    const auto sourceBefore = sourceStorage;
    auto expectedDestination = destinationStorage;
    std::memcpy(expectedDestination.data() + 1U, samples.data(), 6U);
    std::memcpy(expectedDestination.data() + 1U + destinationStride,
        samples.data() + 3U, 6U);
    const auto sourceLayout = ImageLayout::create(
        3U, 2U, sourceStride, StorageType::UInt16, sourceStride * 2U).value();
    const auto destinationLayout = ImageLayout::create(
        3U, 2U, destinationStride, StorageType::UInt16,
        destinationStride * 2U).value();
    const auto source = ImageView::create(sourceLayout,
        std::span(sourceStorage).subspan(1U), ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(destinationLayout,
        std::span(destinationStorage).subspan(1U), ImageDomain::CanonicalU16).value();

    const auto result = WindowLevelStage{}.process(
        source, destination, canonicalSourceFormat());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(sourceStorage, sourceBefore);
    EXPECT_EQ(destinationStorage, expectedDestination);
}

TEST(WindowLevelStage, CopiesClippedWideIdentityForUnalignedSingletonColumn) {
    constexpr std::size_t sourceStride = 5U;
    constexpr std::size_t destinationStride = 7U;
    std::array<std::byte, 1U + sourceStride * 3U + 1U> sourceStorage{};
    std::array<std::byte, 1U + destinationStride * 3U + 1U> destinationStorage{};
    sourceStorage.fill(std::byte{0xD4});
    destinationStorage.fill(std::byte{0x6B});
    constexpr std::array<std::uint16_t, 3> samples{0U, 32768U, 65535U};
    for (std::size_t row = 0U; row < samples.size(); ++row) {
        std::memcpy(sourceStorage.data() + 1U + row * sourceStride,
            samples.data() + row, sizeof(std::uint16_t));
    }
    const auto sourceBefore = sourceStorage;
    auto expectedDestination = destinationStorage;
    for (std::size_t row = 0U; row < samples.size(); ++row) {
        std::memcpy(expectedDestination.data() + 1U + row * destinationStride,
            samples.data() + row, sizeof(std::uint16_t));
    }
    const auto sourceLayout = ImageLayout::create(
        1U, 3U, sourceStride, StorageType::UInt16, sourceStride * 3U).value();
    const auto destinationLayout = ImageLayout::create(
        1U, 3U, destinationStride, StorageType::UInt16,
        destinationStride * 3U).value();
    const auto source = ImageView::create(sourceLayout,
        std::span(sourceStorage).subspan(1U), ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(destinationLayout,
        std::span(destinationStorage).subspan(1U), ImageDomain::CanonicalU16).value();

    const auto result = WindowLevelStage({.window = 65536.0, .level = 32767.5})
                            .process(source, destination, canonicalSourceFormat());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(sourceStorage, sourceBefore);
    EXPECT_EQ(destinationStorage, expectedDestination);
}

TEST(WindowLevelStage, HandlesUnalignedPaddedRowsAndPreservesPaddingAndSource) {
    constexpr std::size_t stride = 7U;
    std::array<std::byte, 1U + stride * 2U> sourceStorage{};
    std::array<std::byte, 1U + stride * 2U> destinationStorage{};
    sourceStorage.fill(std::byte{0xC3});
    destinationStorage.fill(std::byte{0x5A});
    constexpr std::array<std::uint16_t, 4> samples{0U, 1000U, 2000U, 3000U};
    std::memcpy(sourceStorage.data() + 1U, samples.data(), 4U);
    std::memcpy(sourceStorage.data() + 1U + stride, samples.data() + 2U, 4U);
    const auto sourceBefore = sourceStorage;
    const auto layout = ImageLayout::create(
        2U, 2U, stride, StorageType::UInt16, stride * 2U).value();
    const auto source = ImageView::create(layout, std::span(sourceStorage).subspan(1U),
        ImageDomain::CanonicalU16).value();
    const auto destination = MutableImageView::create(layout,
        std::span(destinationStorage).subspan(1U), ImageDomain::CanonicalU16).value();

    const auto result = WindowLevelStage({.window = 2000.0, .level = 2000.0})
                            .process(source, destination, canonicalSourceFormat());

    ASSERT_TRUE(result.hasValue());
    std::array<std::uint16_t, 4> actual{};
    std::memcpy(actual.data(), destinationStorage.data() + 1U, 4U);
    std::memcpy(actual.data() + 2U, destinationStorage.data() + 1U + stride, 4U);
    EXPECT_EQ(actual, (std::array<std::uint16_t, 4>{0U, 0U, 32768U, 65535U}));
    EXPECT_EQ(sourceStorage, sourceBefore);
    EXPECT_EQ(destinationStorage[0], std::byte{0x5A});
    for (const auto index : {5U, 6U, 7U, 12U, 13U, 14U}) {
        EXPECT_EQ(destinationStorage[index], std::byte{0x5A}) << index;
    }
}

TEST(WindowLevelStage, RejectsInvalidParametersBeforeWriting) {
    constexpr std::array<std::uint16_t, 2> samples{100U, 200U};
    const auto layout = ImageLayout::create(
        2U, 1U, 4U, StorageType::UInt16, 4U).value();
    const auto source = ImageView::create(layout, std::as_bytes(std::span(samples)),
        ImageDomain::CanonicalU16).value();
    constexpr std::array<WindowLevelParameters, 5> invalidParameters{
        WindowLevelParameters{0.0, 100.0},
        WindowLevelParameters{-1.0, 100.0},
        WindowLevelParameters{std::numeric_limits<double>::infinity(), 100.0},
        WindowLevelParameters{100.0, std::numeric_limits<double>::infinity()},
        WindowLevelParameters{100.0, std::numeric_limits<double>::quiet_NaN()},
    };
    for (const auto parameters : invalidParameters) {
        std::array<std::byte, 4> destinationBytes{};
        destinationBytes.fill(std::byte{0x7E});
        const auto destination = MutableImageView::create(layout, destinationBytes,
            ImageDomain::CanonicalU16).value();
        const auto result = WindowLevelStage(parameters).process(
            source, destination, canonicalSourceFormat());
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "invalid_window_level_parameters");
        EXPECT_EQ(destinationBytes,
            (std::array<std::byte, 4>{std::byte{0x7E}, std::byte{0x7E},
                std::byte{0x7E}, std::byte{0x7E}}));
    }
}

TEST(WindowLevelStage, RejectsStorageDomainExtentAndOverlapBeforeWriting) {
    std::array<std::byte, 24> sourceStorage{};
    std::array<std::byte, 8> separateDestination{};
    separateDestination.fill(std::byte{0x4D});
    const auto separateDestinationBefore = separateDestination;
    const auto sourceLayout = ImageLayout::create(
        2U, 2U, 4U, StorageType::UInt16, 8U).value();
    const auto source = ImageView::create(sourceLayout, sourceStorage,
        ImageDomain::CanonicalU16).value();

    const auto u8Layout = ImageLayout::create(
        2U, 2U, 2U, StorageType::UInt8, 4U).value();
    const auto u8Destination = MutableImageView::create(
        u8Layout, separateDestination, ImageDomain::SensorNative).value();
    auto result = WindowLevelStage{}.process(
        source, u8Destination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "destination_storage_mismatch");
    EXPECT_EQ(separateDestination, separateDestinationBefore);

    const auto u8Source = ImageView::create(
        u8Layout, sourceStorage, ImageDomain::SensorNative).value();
    const auto goodDestination = MutableImageView::create(sourceLayout,
        separateDestination, ImageDomain::CanonicalU16).value();
    result = WindowLevelStage{}.process(
        u8Source, goodDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "source_storage_mismatch");
    EXPECT_EQ(separateDestination, separateDestinationBefore);

    const auto sensorSource = ImageView::create(
        sourceLayout, sourceStorage, ImageDomain::SensorNative).value();
    result = WindowLevelStage{}.process(
        sensorSource, goodDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "image_domain_mismatch");
    EXPECT_EQ(separateDestination, separateDestinationBefore);

    const auto narrowLayout = ImageLayout::create(
        1U, 2U, 2U, StorageType::UInt16, 4U).value();
    const auto narrowDestination = MutableImageView::create(
        narrowLayout, separateDestination, ImageDomain::CanonicalU16).value();
    result = WindowLevelStage{}.process(
        source, narrowDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "image_extent_mismatch");
    EXPECT_EQ(separateDestination, separateDestinationBefore);

    const auto overlappingDestination = MutableImageView::create(sourceLayout,
        std::span(sourceStorage).subspan(1U), ImageDomain::CanonicalU16).value();
    const auto overlapBefore = sourceStorage;
    result = WindowLevelStage{}.process(
        source, overlappingDestination, canonicalSourceFormat());
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "image_views_overlap");
    EXPECT_EQ(sourceStorage, overlapBefore);
}

TEST(WindowLevelStage, IdentifiesItsFixedDomainContract) {
    const WindowLevelStage stage;
    EXPECT_EQ(stage.id(), lumora::processing::StageId::WindowLevel);
    EXPECT_EQ(stage.traits().id, lumora::processing::StageId::WindowLevel);
    EXPECT_EQ(stage.traits().inputDomain, ImageDomain::CanonicalU16);
    EXPECT_EQ(stage.traits().outputDomain, ImageDomain::CanonicalU16);
    EXPECT_EQ(stage.traits().scratchImages, 0U);
}

}  // namespace
