#include <lumora/camera/sim/IPatternGenerator.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <new>
#include <stop_token>
#include <utility>

static_assert(!noexcept(std::declval<const lumora::camera::sim::IPatternGenerator&>().fill(
    {}, 1U, 1U, 1U, std::declval<const lumora::core::SourcePixelFormat&>(), 1U)));

// This executable alone replaces allocation. The one-shot flag is armed only
// after fixture setup and automatically disarmed before throwing.
namespace {
thread_local bool failNextAllocation = false;
}

void* operator new(std::size_t size) {
    if (failNextAllocation) {
        failNextAllocation = false;
        throw std::bad_alloc{};
    }
    if (auto* memory = std::malloc(size == 0U ? 1U : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    std::set_terminate([] {
        std::fputs("Pattern error allocation terminated instead of propagating.\n", stderr);
        std::_Exit(99);
    });
    using namespace lumora;
    const auto generator = camera::sim::makePatternGenerator(camera::sim::SimulationPattern::Ramp, 1U);
    const core::SourcePixelFormat format{"Mono8", 0x01080001U, 8U, 255U,
        core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant, core::StorageType::UInt8};
    std::byte pixels[1]{};
    for (const bool cancel : {false, true}) {
        std::stop_source stop;
        if (cancel) {
            stop.request_stop();
        }
        bool propagated = false;
        failNextAllocation = true;
        try {
            static_cast<void>(generator->fill(pixels, cancel ? 1U : 0U, 1U, 1U, format, 1U, stop.get_token()));
        } catch (const std::bad_alloc&) {
            propagated = true;
        }
        failNextAllocation = false;
        if (!propagated) {
            std::fputs("Pattern error allocation did not propagate bad_alloc.\n", stderr);
            return 1;
        }
    }
    return 0;
}
