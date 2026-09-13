#pragma once

#include <lumora/core/Frame.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/ui/DisplayMode.hpp>

#include <cstdint>
#include <memory>

namespace lumora::core {
class IClock;
}

namespace lumora::ui {

class WorkstationView;

class FramePresenter final {
public:
    FramePresenter(core::LatestValueSlot<core::FrameBundle>& slot,
                   WorkstationView& view, core::IClock& clock);
    FramePresenter(core::LatestValueSlot<core::FrameBundle>& slot,
                   WorkstationView& view, core::IClock& clock,
                   std::uint64_t sessionGeneration);
    ~FramePresenter();
    FramePresenter(const FramePresenter&) = delete;
    FramePresenter& operator=(const FramePresenter&) = delete;
    void start();
    void stop();
    void refresh();
    void pause();
    void resume();
    void resetSource(core::LatestValueSlot<core::FrameBundle>& freshSlot);
    void resetSource(core::LatestValueSlot<core::FrameBundle>& freshSlot,
                     std::uint64_t sessionGeneration);
    void retire();
    void setDisplayMode(DisplayMode mode);
    [[nodiscard]] DisplayMode displayMode() const noexcept;
    [[nodiscard]] std::uint64_t displayedFrameCount() const noexcept;
    [[nodiscard]] std::shared_ptr<const core::FrameBundle> presentedBundle()
        const noexcept;
    [[nodiscard]] bool retirementComplete() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::ui
