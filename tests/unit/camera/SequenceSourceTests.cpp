#include <lumora/camera/sim/SequenceSource.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace lumora::camera::sim {
namespace {

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{LUMORA_SEQUENCE_FIXTURES} / name;
}

class TemporarySequence {
public:
    TemporarySequence() {
        static std::atomic_uint64_t counter{0U};
        path = std::filesystem::temp_directory_path() /
               ("lumora-pgm-" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(counter.fetch_add(1U)));
        std::filesystem::create_directory(path);
    }
    ~TemporarySequence() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    void write(const std::string& name, const std::string& data) const {
        std::ofstream file(path / name, std::ios::binary);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        ASSERT_TRUE(file.good());
    }
    std::filesystem::path path;
};

std::uint16_t sample(const SequenceFrame& frame, std::size_t index = 0U) {
    if (frame.format.applicationStorage == core::StorageType::UInt8) {
        return std::to_integer<std::uint8_t>(frame.pixels[index]);
    }
    std::uint16_t value{};
    std::memcpy(&value, frame.pixels.data() + index * sizeof(value), sizeof(value));
    return value;
}

TEST(SequenceSource, ReadsSixteenBitPgmInLexicalOrderAndLoops) {
    auto opened = SequenceSource::openDirectory(fixture("ramp16"), SequenceEnd::Loop);
    ASSERT_TRUE(opened.hasValue());
    auto source = std::move(opened).value();
    const auto first = source.next();
    const auto second = source.next();
    const auto third = source.next();
    ASSERT_TRUE(first.hasValue() && first.value().has_value());
    ASSERT_TRUE(second.hasValue() && second.value().has_value());
    ASSERT_TRUE(third.hasValue() && third.value().has_value());
    EXPECT_EQ(sample(**first.value()), 0x0123U);
    EXPECT_EQ(sample(**second.value()), 0x0456U);
    EXPECT_EQ(sample(**third.value()), 0x0123U);
    EXPECT_EQ(sample(**first.value(), 1U), 0x0ABCU);
    EXPECT_EQ(sample(**first.value(), 2U), 0x0FFFU);
}

TEST(SequenceSource, UsesFilenameLexicalRatherThanNumericOrInsertionOrder) {
    TemporarySequence sequence;
    sequence.write("2.pgm", "P5\n1 1\n255\nB");
    sequence.write("10.pgm", "P5\n1 1\n255\nA");
    auto opened = SequenceSource::openDirectory(sequence.path, SequenceEnd::Stop);
    ASSERT_TRUE(opened.hasValue());
    EXPECT_EQ(sample(**opened.value().next().value()), 65U);
    EXPECT_EQ(sample(**opened.value().next().value()), 66U);
}

TEST(SequenceSource, DerivesStorageBitsAndDeclaredMaximumWithoutScalingSamples) {
    struct Case { std::uint16_t maximum; std::uint8_t bits; core::StorageType storage; };
    for (const auto& test : std::vector<Case>{
             {255U, 8U, core::StorageType::UInt8},
             {1023U, 10U, core::StorageType::UInt16},
             {4095U, 12U, core::StorageType::UInt16},
             {65535U, 16U, core::StorageType::UInt16},
             {1000U, 10U, core::StorageType::UInt16}}) {
        SCOPED_TRACE(test.maximum);
        auto opened = SequenceSource::openDirectory(
            fixture("max" + std::to_string(test.maximum)), SequenceEnd::Stop);
        ASSERT_TRUE(opened.hasValue());
        auto frame = opened.value().next();
        ASSERT_TRUE(frame.hasValue() && frame.value().has_value());
        const auto& value = **frame.value();
        EXPECT_EQ(value.format.sampleMaximum, test.maximum);
        EXPECT_EQ(value.format.validBits, test.bits);
        EXPECT_EQ(value.format.applicationStorage, test.storage);
        EXPECT_EQ(value.format.packing, core::SourcePacking::Unpacked);
        EXPECT_EQ(value.format.alignment, core::BitAlignment::LeastSignificant);
        EXPECT_TRUE(core::validateSourcePixelFormat(value.format).hasValue());
        EXPECT_EQ(sample(value), 0U);
        EXPECT_EQ(sample(value, 1U), 35U);
        EXPECT_EQ(sample(value, 2U), test.maximum);
    }
}

TEST(SequenceSource, StopReturnsEmptyAndErrorReturnsStableAcquisitionError) {
    auto stopped = SequenceSource::openDirectory(fixture("max255"), SequenceEnd::Stop).value();
    auto error = SequenceSource::openDirectory(fixture("max255"), SequenceEnd::Error).value();
    ASSERT_TRUE(stopped.next().hasValue());
    ASSERT_TRUE(error.next().hasValue());
    for (int repeat = 0; repeat != 2; ++repeat) {
        const auto end = stopped.next();
        ASSERT_TRUE(end.hasValue());
        EXPECT_FALSE(end.value().has_value());
        const auto exhausted = error.next();
        ASSERT_FALSE(exhausted.hasValue());
        EXPECT_EQ(exhausted.error().category, core::ErrorCategory::Acquisition);
        EXPECT_EQ(exhausted.error().code, "sequence_exhausted");
    }
}

TEST(SequenceSource, PeekDoesNotConsumeAndOnlyCommitAdvancesPosition) {
    auto source = SequenceSource::openDirectory(fixture("ramp16"), SequenceEnd::Stop).value();
    EXPECT_EQ(sample(**source.peek().value()), 0x0123U);
    EXPECT_EQ(sample(**source.peek().value()), 0x0123U);
    source.commit();
    EXPECT_EQ(sample(**source.peek().value()), 0x0456U);
}

TEST(SequenceSource, HeaderCommentsAndCrLfNeverConsumeFirstRasterByte) {
    for (const char raster : std::string{" \t\n\r#"}) {
        for (const auto& separator : {std::string{"\n"}, std::string{"\r\n"}}) {
            TemporarySequence sequence;
            sequence.write("frame.pgm", "P5\r\n# comment\r\n1 # width\n1\n255" + separator + raster);
            auto opened = SequenceSource::openDirectory(sequence.path, SequenceEnd::Stop);
            ASSERT_TRUE(opened.hasValue());
            EXPECT_EQ(sample(**opened.value().next().value()), static_cast<unsigned char>(raster));
        }
    }
}

TEST(SequenceSource, RejectsMalformedHeadersOverflowAndInexactOrOutOfRangeRaster) {
    const std::vector<std::string> invalid{
        "", "P2\n1 1\n255\nA", "P51 1 255\nA", "P5\n0 1\n255\n",
        "P5\n1 0\n255\n", "P5\n-1 1\n255\nA", "P5\n+1 1\n255\nA",
        "P5\n1x 1\n255\nA", "P5\n1 1\n0\nA", "P5\n1 1\n65536\nAA",
        "P5\n1 1\n255", "P5\n1 1\n255A", "P5\n1 1\n255#no separator\nA",
        "P5\n1 1\n255\n", "P5\n1 1\n255\nAB", "P5\n1 1\n255\nA\n",
        "P5\n1 1\n10\nA", "P5\n1 1\n1023\n\x04\x01", "P5\n1 1\n1023\nA",
        "P5\n4294967296 1\n255\nA", "P5\n4294967295 4294967295\n65535\nAA",
        "P5\n999999999999999999999999999999 1\n255\nA", "P5\n#unterminated",
        "P5\n1 1\n255\r\nAB", "P5\v1\f1\n255\nAB"};
    for (const auto& bytes : invalid) {
        SCOPED_TRACE(bytes);
        TemporarySequence sequence;
        sequence.write("frame.pgm", bytes);
        const auto opened = SequenceSource::openDirectory(sequence.path, SequenceEnd::Loop);
        ASSERT_FALSE(opened.hasValue());
        EXPECT_EQ(opened.error().category, core::ErrorCategory::InvalidFrame);
    }
}

TEST(SequenceSource, AcceptsLeadingZerosMinimumMaximumAndEightBitStorageBoundary) {
    struct Case { std::string maximum; std::string raster; std::uint16_t value; std::uint8_t bits; };
    for (const auto& test : std::vector<Case>{
             {"0000000000000000000000000000001", std::string(1U, '\x01'), 1U, 1U},
             {"256", std::string{"\x01\x00", 2U}, 256U, 9U},
             {"257", std::string{"\x01\x01", 2U}, 257U, 9U}}) {
        TemporarySequence sequence;
        sequence.write("frame.pgm", "P5\n1 1\n" + test.maximum + "\n" + test.raster);
        auto opened = SequenceSource::openDirectory(sequence.path, SequenceEnd::Stop);
        ASSERT_TRUE(opened.hasValue());
        const auto frame = opened.value().next();
        ASSERT_TRUE(frame.hasValue() && frame.value().has_value());
        EXPECT_EQ(sample(**frame.value()), test.value);
        EXPECT_EQ((*frame.value())->format.validBits, test.bits);
    }
}

TEST(SequenceSource, CrLfIsOneHeaderSeparatorAndMissingRasterIsRejected) {
    TemporarySequence sequence;
    sequence.write("frame.pgm", "P5\n1 1\n255\r\n");
    const auto opened = SequenceSource::openDirectory(sequence.path, SequenceEnd::Stop);
    ASSERT_FALSE(opened.hasValue());
    EXPECT_EQ(opened.error().code, "invalid_pgm_payload");
}

TEST(SequenceSource, RejectsMissingEmptyNonFileAndMixedSequences) {
    TemporarySequence sequence;
    EXPECT_FALSE(SequenceSource::openDirectory(sequence.path / "missing", SequenceEnd::Loop).hasValue());
    EXPECT_FALSE(SequenceSource::openDirectory(sequence.path, SequenceEnd::Loop).hasValue());
    std::filesystem::create_directory(sequence.path / "nested");
    EXPECT_FALSE(SequenceSource::openDirectory(sequence.path, SequenceEnd::Loop).hasValue());
    std::filesystem::remove(sequence.path / "nested");
    sequence.write("a.pgm", "P5\n1 1\n255\nA");
    EXPECT_FALSE(SequenceSource::openDirectory(sequence.path / "a.pgm", SequenceEnd::Loop).hasValue());
    for (const auto& other : {"P5\n2 1\n255\nAB", "P5\n1 1\n254\nA", "P5\n1 1\n1023\n\x01\x41"}) {
        sequence.write("b.pgm", other);
        const auto opened = SequenceSource::openDirectory(sequence.path, SequenceEnd::Loop);
        ASSERT_FALSE(opened.hasValue());
        EXPECT_EQ(opened.error().code, "sequence_format_mismatch");
    }
}

}  // namespace
}  // namespace lumora::camera::sim
