#include <lumora/processing/FrameProcessingEngine.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <barrier>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

namespace lumora::processing {
namespace {
using namespace lumora;

core::SourcePixelFormat format(unsigned bits) {
    return {"Mono" + std::to_string(bits), bits == 8 ? 0x01080001U : 0x01100005U,
        static_cast<std::uint8_t>(bits), static_cast<std::uint16_t>((1U << bits) - 1U),
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
        bits == 8 ? core::StorageType::UInt8 : core::StorageType::UInt16};
}
core::ImageLayout layout(unsigned bits, std::uint32_t width = 4, std::uint32_t height = 1,
                         std::size_t padding = 0) {
    const auto stride = width * (bits == 8 ? 1U : 2U) + padding;
    return core::ImageLayout::create(width, height, stride, format(bits).applicationStorage,
        stride * height).value();
}
std::shared_ptr<const core::RawFrame> rawFrame(unsigned bits,
    const std::vector<std::uint16_t>& samples, std::uint64_t id = 1,
    std::size_t padding = 0, std::uint32_t height = 1, double fps = 30.0) {
    const auto width = static_cast<std::uint32_t>(samples.size() / height);
    const auto sourceLayout = layout(bits, width, height, padding);
    auto pool = core::BufferPool::create(1, sourceLayout.payloadBytes()).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), std::byte{0xAB});
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto sample = samples[y * width + x];
            auto* destination = lease->bytes().data() + y * sourceLayout.strideBytes() + x * (bits == 8 ? 1U : 2U);
            if (bits == 8) *destination = static_cast<std::byte>(sample);
            else std::memcpy(destination, &sample, sizeof(sample));
        }
    }
    auto settings = core::AcquisitionSettingsSnapshot::create(
        {"Test", "Numeric", "1", "virtual", {}}, format(bits), {0, 0, width, height}, 30, 30, {}, {}).value();
    settings.actualFps = fps;
    return core::RawFrame::create(id, sourceLayout, std::move(*lease).seal(),
        {{}, {}, {}, {}, std::move(settings)}).value();
}
std::vector<std::byte> copyBytes(const core::SharedBuffer& buffer) {
    return {buffer.bytes().begin(), buffer.bytes().end()};
}
std::uint64_t digest(const std::vector<std::byte>& bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (auto byte : bytes) { value ^= std::to_integer<unsigned>(byte); value *= 1099511628211ULL; }
    return value;
}
std::vector<std::uint16_t> samples(const core::ProcessedFrame& frame) {
    std::vector<std::uint16_t> values;
    for (std::uint32_t y = 0; y < frame.layout.height(); ++y) {
        for (std::uint32_t x = 0; x < frame.layout.width(); ++x) {
            std::uint16_t value;
            std::memcpy(&value, frame.pixels.bytes().data() + y * frame.layout.strideBytes() + x * 2U, 2);
            values.push_back(value);
        }
    }
    return values;
}
struct Fixture {
    std::shared_ptr<core::BufferPool> u16;
    std::shared_ptr<core::BufferPool> gray;
    std::unique_ptr<FrameProcessingEngine> engine;
    explicit Fixture(unsigned bits = 12, std::uint32_t width = 4, std::uint32_t height = 1,
                     std::size_t u16Count = 9, std::size_t grayCount = 16)
        : u16(core::BufferPool::create(u16Count, width * height * 2U).value()),
          gray(core::BufferPool::create(grayCount, width * height).value()),
          engine(FrameProcessingEngine::create(*u16, *gray, layout(bits, width, height)).value()) {}
};
PipelineDefinition windowDefinition(bool enabled, std::uint64_t revision = 4) {
    auto definition = defaultPipeline();
    definition.version.configurationRevision = revision;
    definition.stages[1].enabled = enabled;
    definition.stages[1].parameters = WindowLevelParameters{32768, 16384};
    return definition;
}

TEST(FrameProcessingEngine, PreservesRawAndPairsExactCanonicalAndGrayAtAllDepths) {
    for (unsigned bits : {8U, 10U, 12U, 16U}) {
        SCOPED_TRACE(bits);
        Fixture fixture(bits);
        const auto maximum = format(bits).sampleMaximum;
        const std::vector<std::uint16_t> input{0, 1, static_cast<std::uint16_t>(maximum / 2), maximum};
        auto raw = rawFrame(bits, input);
        const auto before = copyBytes(raw->pixels);
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        const auto& bundle = result.value();
        ASSERT_NE(bundle->enhanced, nullptr);
        ASSERT_NE(bundle->enhancedDisplay, nullptr);
        EXPECT_EQ(bundle->raw, raw);
        EXPECT_EQ(bundle->originalDisplay->sourceFrameId, raw->frameId);
        EXPECT_EQ(bundle->enhanced->sourceFrameId, raw->frameId);
        EXPECT_EQ(bundle->enhancedDisplay->sourceFrameId, raw->frameId);
        EXPECT_EQ(bundle->enhanced->layout.storage(), core::StorageType::UInt16);
        EXPECT_EQ(bundle->originalDisplay->storage, core::DisplayStorage::Gray8);
        EXPECT_EQ(bundle->enhancedDisplay->storage, core::DisplayStorage::Gray8);
        std::vector<std::uint16_t> expected;
        std::vector<std::byte> display;
        for (auto value : input) {
            const auto canonical = (static_cast<std::uint64_t>(value) * 65535U + maximum / 2U) / maximum;
            expected.push_back(static_cast<std::uint16_t>(canonical));
            display.push_back(static_cast<std::byte>((canonical + 128U) / 257U));
        }
        EXPECT_EQ(samples(*bundle->enhanced), expected);
        EXPECT_EQ(copyBytes(bundle->originalDisplay->pixels), display);
        EXPECT_EQ(copyBytes(bundle->enhancedDisplay->pixels), display);
        EXPECT_EQ(bundle->originalDisplay->mapping, bundle->enhancedDisplay->mapping);
        EXPECT_EQ(bundle->originalDisplay->mapping.configurationRevision, 0U);
        EXPECT_EQ(bundle->enhanced->pipelineVersion.configurationRevision, 0U);
        EXPECT_EQ(copyBytes(raw->pixels), before);
        EXPECT_EQ(digest(copyBytes(raw->pixels)), digest(before));
    }
}
TEST(FrameProcessingEngine, PreparesExactlyTwoCanonicalAndTwoDisplayLeases) {
    Fixture fixture;
    EXPECT_EQ(fixture.u16->stats().inUse, 2U);
    EXPECT_EQ(fixture.gray->stats().inUse, 2U);
    fixture.engine.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, DisabledWindowStillMapsOriginalAndOmittedWindowIsIdentity) {
    Fixture fixture(16);
    const auto raw = rawFrame(16, {0, 16384, 32768, 65535});
    ASSERT_TRUE(fixture.engine->activate(windowDefinition(false)).hasValue());
    auto result = fixture.engine->process(raw);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(samples(*result.value()->enhanced), (std::vector<std::uint16_t>{0,16384,32768,65535}));
    ASSERT_EQ(result.value()->enhanced->timings.stages.size(), 4U);
    EXPECT_EQ(result.value()->enhanced->timings.stages[1].stageId, "original_window_level");
    EXPECT_EQ(copyBytes(result.value()->originalDisplay->pixels),
        (std::vector<std::byte>{std::byte{0},std::byte{128},std::byte{255},std::byte{255}}));
    EXPECT_EQ(copyBytes(result.value()->enhancedDisplay->pixels),
        (std::vector<std::byte>{std::byte{0},std::byte{64},std::byte{128},std::byte{255}}));
    ASSERT_TRUE(fixture.engine->activate(windowDefinition(true, 5)).hasValue());
    result = fixture.engine->process(raw);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(samples(*result.value()->enhanced), (std::vector<std::uint16_t>{0,32768,65535,65535}));
    EXPECT_EQ(result.value()->enhanced->pipelineVersion.configurationRevision, 5U);
    EXPECT_EQ(result.value()->enhanced->timings.stages[1].stageId, "shared_window_level");
    EXPECT_GE(result.value()->enhanced->timings.total, std::chrono::nanoseconds::zero());
    auto omitted = defaultPipeline(); omitted.stages.erase(omitted.stages.begin() + 1);
    ASSERT_TRUE(fixture.engine->activate(omitted).hasValue());
    result = fixture.engine->process(raw);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(copyBytes(result.value()->originalDisplay->pixels), copyBytes(result.value()->enhancedDisplay->pixels));
    EXPECT_EQ(samples(*result.value()->enhanced), (std::vector<std::uint16_t>{0,16384,32768,65535}));
}
TEST(FrameProcessingEngine, ActivationOwnsSnapshotsAndRejectsUnavailableOrInvalidDefinitions) {
    ProcessingPipeline pipeline;
    auto definition = windowDefinition(true);
    ASSERT_TRUE(pipeline.activate(definition).hasValue());
    auto previous = pipeline.snapshot();
    ASSERT_NE(previous, nullptr);
    definition.version.configurationRevision = 99;
    EXPECT_EQ(previous->definition().version.configurationRevision, 4U);
    for (std::size_t index = 2; index < definition.stages.size(); ++index) {
        auto invalid = definition; invalid.stages[index].enabled = true;
        auto result = pipeline.activate(invalid);
        ASSERT_FALSE(result.hasValue());
        EXPECT_EQ(result.error().code, "processing_stage_unavailable");
        ASSERT_EQ(result.error().violations.size(), 1U);
        EXPECT_EQ(result.error().violations[0].stageIndex, index);
        EXPECT_EQ(pipeline.snapshot(), previous);
    }
    auto allUnavailable = definition;
    for (std::size_t index = 2; index < allUnavailable.stages.size(); ++index) allUnavailable.stages[index].enabled = true;
    auto rejected = pipeline.activate(allUnavailable);
    ASSERT_FALSE(rejected.hasValue());
    ASSERT_EQ(rejected.error().violations.size(), allUnavailable.stages.size() - 2);
    for (std::size_t index = 0; index < rejected.error().violations.size(); ++index)
        EXPECT_EQ(rejected.error().violations[index].stageIndex, index + 2);
    auto invalid = definition; std::swap(invalid.stages[0], invalid.stages[1]);
    EXPECT_FALSE(pipeline.activate(invalid).hasValue());
    invalid = definition; invalid.stages[1].parameters = WindowLevelParameters{0, 1};
    EXPECT_FALSE(pipeline.activate(invalid).hasValue());
    EXPECT_EQ(pipeline.snapshot(), previous);
    ASSERT_TRUE(pipeline.activate(definition).hasValue());
    EXPECT_EQ(pipeline.snapshot()->definition().version.configurationRevision, 99U);
    EXPECT_EQ(previous->definition().version.configurationRevision, 4U);
}
TEST(FrameProcessingEngine, RetainedRawCanonicalAndDisplaysNeverChangeAcrossFrames) {
    Fixture fixture(12, 2, 2);
    auto raw = rawFrame(12, {0,100,2048,4095}, 1, 1, 2);
    const auto before = copyBytes(raw->pixels);
    auto first = fixture.engine->process(raw);
    ASSERT_TRUE(first.hasValue());
    const auto canonical = copyBytes(first.value()->enhanced->pixels);
    const auto original = copyBytes(first.value()->originalDisplay->pixels);
    const auto enhanced = copyBytes(first.value()->enhancedDisplay->pixels);
    for (std::uint64_t id = 2; id < 12; ++id) {
        auto next = fixture.engine->process(rawFrame(12, {4095,2048,100,0}, id, 3, 2));
        ASSERT_TRUE(next.hasValue());
        EXPECT_EQ(copyBytes(raw->pixels), before);
        EXPECT_EQ(digest(copyBytes(raw->pixels)), digest(before));
        EXPECT_EQ(copyBytes(first.value()->enhanced->pixels), canonical);
        EXPECT_EQ(copyBytes(first.value()->originalDisplay->pixels), original);
        EXPECT_EQ(copyBytes(first.value()->enhancedDisplay->pixels), enhanced);
    }
    first.value().reset(); fixture.engine.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, ReplenishmentIsBoundedTypedAndRecoversAfterRetainedOwnersRelease) {
    for (bool exhaustU16 : {true, false}) {
        Fixture fixture(12, 4, 1, exhaustU16 ? 2 : 9, exhaustU16 ? 16 : 2);
        auto first = fixture.engine->process(rawFrame(12, {0,100,2048,4095}));
        ASSERT_TRUE(first.hasValue());
        auto next = fixture.engine->process(rawFrame(12, {1,2,3,4}, 2));
        ASSERT_FALSE(next.hasValue());
        EXPECT_EQ(next.error().category, core::ErrorCategory::ResourceExhaustion);
        EXPECT_EQ(next.error().code, exhaustU16 ? processingBufferPoolExhaustedCode : displayBufferPoolExhaustedCode);
        // A failed Gray8 replenishment must not retain its new U16 acquisition.
        EXPECT_EQ(fixture.u16->stats().inUse, 2U);
        EXPECT_EQ(fixture.gray->stats().inUse, 2U);
        first.value().reset();
        ASSERT_TRUE(fixture.engine->process(rawFrame(12, {1,2,3,4}, 3)).hasValue());
        fixture.engine.reset();
        EXPECT_EQ(fixture.u16->stats().inUse, 0U);
        EXPECT_EQ(fixture.gray->stats().inUse, 0U);
    }
}
TEST(FrameProcessingEngine, RejectsInvalidInputWithoutPublishingPartiallyWrittenSamples) {
    Fixture fixture;
    EXPECT_FALSE(fixture.engine->process(nullptr).hasValue());
    EXPECT_FALSE(fixture.engine->process(rawFrame(8, {0,1,2,3})).hasValue());
    EXPECT_FALSE(fixture.engine->process(rawFrame(12, {0,1})).hasValue());
    for (double fps : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        EXPECT_FALSE(fixture.engine->process(rawFrame(12, {0,1,2,3}, 1, 0, 1, fps)).hasValue());
    EXPECT_FALSE(fixture.engine->process(rawFrame(12, {0,100,4096,2})).hasValue());
    auto valid = fixture.engine->process(rawFrame(12, {0,0,0,0}));
    ASSERT_TRUE(valid.hasValue());
    EXPECT_EQ(samples(*valid.value()->enhanced), (std::vector<std::uint16_t>{0,0,0,0}));
}
TEST(FrameProcessingEngine, PreparationFailsForSmallPoolsWithoutLeakingPartialAcquisitions) {
    auto u16 = core::BufferPool::create(2, 8).value();
    auto gray = core::BufferPool::create(1, 4).value();
    auto created = FrameProcessingEngine::create(*u16, *gray, layout(12));
    EXPECT_FALSE(created.hasValue());
    EXPECT_EQ(u16->stats().inUse, 0U); EXPECT_EQ(gray->stats().inUse, 0U);
    auto tiny = core::BufferPool::create(4, 1).value();
    EXPECT_FALSE(FrameProcessingEngine::create(*tiny, *gray, layout(12)).hasValue());
    EXPECT_FALSE(FrameProcessingEngine::create(*u16, *tiny, layout(12)).hasValue());
    auto bad = defaultPipeline(); bad.stages[2].enabled = true;
    EXPECT_FALSE(FrameProcessingEngine::create(*u16, *gray, layout(12), bad).hasValue());
    EXPECT_EQ(u16->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, ConcurrentActivationKeepsEveryBundleOnOneConfiguration) {
    Fixture fixture(16, 257, 63);
    auto raw = rawFrame(16, std::vector<std::uint16_t>(257 * 63, 16384), 1, 0, 63);
    std::barrier start{2};
    std::jthread writer([&] {
        start.arrive_and_wait();
        for (std::uint64_t revision = 1; revision <= 64; ++revision)
            EXPECT_TRUE(fixture.engine->activate(windowDefinition(revision % 2 != 0, revision)).hasValue());
    });
    start.arrive_and_wait();
    for (int frame = 0; frame < 32; ++frame) {
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        const auto& bundle = result.value();
        const auto revision = bundle->enhanced->pipelineVersion.configurationRevision;
        EXPECT_EQ(bundle->originalDisplay->mapping.configurationRevision, revision);
        EXPECT_EQ(bundle->enhancedDisplay->mapping.configurationRevision, revision);
        const std::uint16_t expected = revision % 2 ? 32768 : 16384;
        EXPECT_TRUE(std::ranges::all_of(samples(*bundle->enhanced), [&](auto value) { return value == expected; }));
        EXPECT_TRUE(std::ranges::all_of(bundle->originalDisplay->pixels.bytes(), [&](auto value) {
            return value == (revision ? std::byte{128} : std::byte{64});
        }));
    }
    writer.join();
    auto final = fixture.engine->process(raw);
    ASSERT_TRUE(final.hasValue());
    EXPECT_EQ(final.value()->enhanced->pipelineVersion.configurationRevision, 64U);
}
TEST(FrameProcessingEngine, ProductionEnvelopeRetainsEightBundlesAndDetachedChildrenAfterEngineDestruction) {
    Fixture fixture;
    auto raw = rawFrame(12, {0,100,2048,4095});
    std::array<std::shared_ptr<const core::FrameBundle>, 8> retained{};
    for (auto& output : retained) {
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        output = std::move(result).value();
    }
    EXPECT_EQ(fixture.u16->stats().inUse, 9U);
    EXPECT_EQ(fixture.gray->stats().inUse, 16U);
    EXPECT_FALSE(fixture.engine->process(raw).hasValue());
    auto child = retained[0]->enhanced;
    const auto expected = copyBytes(child->pixels);
    retained[1].reset();
    ASSERT_TRUE(fixture.engine->process(raw).hasValue());
    fixture.engine.reset();
    for (const auto& output : retained) {
        if (!output) continue;
        EXPECT_EQ(output->raw, raw);
        EXPECT_EQ(copyBytes(output->enhanced->pixels), expected);
        const auto& timings = output->enhanced->timings.stages;
        ASSERT_EQ(timings.size(), 4U);
        EXPECT_EQ(timings[0].stageId, "normalize");
        EXPECT_EQ(timings[1].stageId, "shared_window_level");
        EXPECT_EQ(timings[2].stageId, "original_display_map");
        EXPECT_EQ(timings[3].stageId, "enhanced_display_map");
    }
    std::jthread release([owners = std::move(retained)]() mutable { for (auto& owner : owners) owner.reset(); });
    release.join();
    EXPECT_EQ(fixture.u16->stats().inUse, 1U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
    EXPECT_EQ(copyBytes(child->pixels), expected);
    child.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
}
TEST(FrameProcessingEngine, ExpiredWeakOwnersExhaustFinalBundleControlAndRecoveryPreservesPublishedOutput) {
    Fixture fixture;
    auto raw = rawFrame(12, {0,100,2048,4095});
    auto published = fixture.engine->process(raw).value();
    const auto expected = copyBytes(published->enhanced->pixels);
    std::array<std::weak_ptr<const core::FrameBundle>, 27> weak{};
    for (auto& retained : weak) {
        auto result = fixture.engine->process(raw);
        ASSERT_TRUE(result.hasValue());
        retained = result.value();
    }
    // 27 expired controls + 4 published controls leave exactly 3: all children
    // construct, then final bundle control acquisition fails in the 34-slot pool.
    for (int retry = 0; retry < 10; ++retry) {
        auto failed = fixture.engine->process(raw);
        ASSERT_FALSE(failed.hasValue());
        EXPECT_EQ(failed.error().code, "frame_control_pool_exhausted");
        EXPECT_EQ(copyBytes(published->enhanced->pixels), expected);
        EXPECT_EQ(published->raw, raw);
        EXPECT_EQ(fixture.u16->stats().inUse, 2U);
        EXPECT_EQ(fixture.gray->stats().inUse, 2U);
    }
    weak[0].reset();
    ASSERT_TRUE(fixture.engine->process(raw).hasValue());
    published.reset();
    fixture.engine.reset();
    EXPECT_EQ(fixture.u16->stats().inUse, 0U);
    EXPECT_EQ(fixture.gray->stats().inUse, 0U);
    for (auto& retained : weak) { EXPECT_TRUE(retained.expired()); retained.reset(); }
}
}  // namespace
}  // namespace lumora::processing
