#pragma once

#include <lumora/core/Frame.hpp>
#include <lumora/core/LatestValueSlot.hpp>

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
    ~FramePresenter();
    FramePresenter(const FramePresenter&) = delete;
    FramePresenter& operator=(const FramePresenter&) = delete;
    void start();
    void stop();
    void refresh();
    void pause();
    void resume();
    void resetSource(core::LatestValueSlot<core::FrameBundle>& freshSlot);
    [[nodiscard]] std::uint64_t displayedFrameCount() const noexcept;
    [[nodiscard]] std::shared_ptr<const core::FrameBundle> presentedBundle()
        const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::ui
