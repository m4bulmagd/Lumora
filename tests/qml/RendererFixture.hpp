#pragma once
#include <lumora/core/BufferPool.hpp>
#include <lumora/core/Clock.hpp>
#include <lumora/core/Frame.hpp>
#include <algorithm>
#include <vector>

namespace lumora::qml::test {
// Real sealed pool storage, padded with a sentinel excluded from active pixels.
struct Frames {
    std::vector<std::shared_ptr<core::BufferPool>> pools;
    std::shared_ptr<const core::FrameBundle> make(std::uint64_t id, core::IClock& clock,
        unsigned width = 256, unsigned height = 3, bool enhanced = true) {
        const std::size_t stride = width + 7U;
        auto l8 = core::ImageLayout::create(width, height, stride, core::StorageType::UInt8, stride * height).value();
        auto l16 = core::ImageLayout::create(width, height, width * 2U, core::StorageType::UInt16, width * height * 2U).value();
        auto buffer = [&](std::size_t bytes, bool inverse, bool ramp) {
            auto pool = core::BufferPool::create(1, bytes).value();
            auto lease = pool->tryAcquire();
            std::ranges::fill(lease->bytes(), std::byte{0xAD});
            if (ramp) for (unsigned y=0; y<height; ++y) for (unsigned x=0; x<width; ++x) {
                const auto v = static_cast<unsigned char>((x + 53U*y) % 256U);
                lease->bytes()[y*stride+x] = std::byte{static_cast<unsigned char>(inverse ? 255U-v : v)};
            }
            pools.push_back(pool);
            return std::move(*lease).seal();
        };
        auto settings = core::AcquisitionSettingsSnapshot::create(
            {"Lumora", "Renderer fixture", "SIM", "Simulator", std::nullopt},
            {"Mono8", 0x01080001U, 8, 255, core::SourcePacking::Unpacked,
             core::BitAlignment::LeastSignificant, core::StorageType::UInt8},
            {0,0,width,height},30,30,std::nullopt,std::nullopt).value();
        auto raw = core::RawFrame::create(id,l8,buffer(stride*height,false,true),
            {std::nullopt,clock.steadyNow(),clock.utcNow(),std::nullopt,std::move(settings)}).value();
        const core::Orientation oriented{true,false,core::Rotation::Degrees0};
        auto original = core::DisplayFrame::create(id,l8,buffer(stride*height,false,true),
            core::DisplayStorage::Gray8,{0,255,255,1},oriented).value();
        auto processed = enhanced ? core::ProcessedFrame::create(id,l16,buffer(width*height*2U,false,false),{1,1,1},{}).value() : nullptr;
        auto second = enhanced ? core::DisplayFrame::create(id,l8,buffer(stride*height,true,true),
            core::DisplayStorage::Gray8,{0,255,255,1},oriented).value() : nullptr;
        return core::FrameBundle::create(raw,original,processed,second).value();
    }
    std::size_t leases() const {
        std::size_t count=0;
        for (const auto& pool:pools) count += pool->stats().inUse;
        return count;
    }
};
}
