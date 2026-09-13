#pragma once
#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/FrameObjectPool.hpp>
#include <algorithm>

namespace lumora::test {
// Real sealed buffers and pooled immutable metadata, with no GUI dependencies.
class PresentationTestFrames {
public:
    std::shared_ptr<core::FrameObjectPool> objects =
        core::FrameObjectPool::create({32, 64, 32, 160}).value();
    std::shared_ptr<const core::FrameBundle> frame(std::uint64_t id,
        core::IClock& clock, bool enhanced = true, double fps = 30.0,
        core::DisplayStorage originalStorage = core::DisplayStorage::Gray8,
        core::DisplayStorage enhancedStorage = core::DisplayStorage::Gray8,
        core::Orientation orientation = {false, false, core::Rotation::Degrees0}) {
        const auto layout8 = core::ImageLayout::create(4, 4, 8,
            core::StorageType::UInt8, 32).value();
        const auto layout16 = core::ImageLayout::create(4, 4, 8,
            core::StorageType::UInt16, 32).value();
        auto buffer = [](std::byte value) {
            auto pool = core::BufferPool::create(1, 32).value();
            auto lease = pool->tryAcquire();
            std::ranges::fill(lease->bytes(), value);
            return std::move(*lease).seal();
        };
        auto display = [&](core::DisplayStorage storage, std::byte value) {
            return core::DisplayFrame::create(id,
                storage == core::DisplayStorage::Gray8 ? layout8 : layout16,
                buffer(value), storage, {0, 255, 255, 1}, orientation, *objects).value();
        };
        auto original = display(originalStorage, std::byte{0x40});
        auto settings = core::AcquisitionSettingsSnapshot::create(
            {"Lumora", "Fixture", "SIM-TEST", "Simulator", std::nullopt},
            {"Mono8", 0x01080001U, 8, 255, core::SourcePacking::Unpacked,
             core::BitAlignment::LeastSignificant, core::StorageType::UInt8},
            {0, 0, 4, 4}, fps, fps, std::nullopt, std::nullopt).value();
        auto raw = core::RawFrame::create(id, layout8, buffer(std::byte{0x40}),
            {std::nullopt, clock.steadyNow(), clock.utcNow(), std::nullopt,
             std::move(settings)}).value();
        auto processed = enhanced ? core::ProcessedFrame::create(id, layout16,
            buffer(std::byte{0x80}), {1, 1, 1}, {}, *objects).value() : nullptr;
        auto second = enhanced ? display(enhancedStorage, std::byte{0xE0}) : nullptr;
        return core::FrameBundle::create(raw, original, processed, second, *objects).value();
    }
};
}  // namespace lumora::test
