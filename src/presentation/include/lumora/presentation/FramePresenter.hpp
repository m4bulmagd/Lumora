#pragma once

#include <lumora/core/Clock.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/presentation/PresentationProtocol.hpp>
#include <lumora/presentation/WorkstationStatus.hpp>

namespace lumora::presentation {

// All operations are frontend-thread only. The caller keeps source contexts,
// clock and sink alive. Before destroying/replacing a context, call retire or
// resetSource and refresh until retirementComplete. Destruction neither starts
// asynchronous retirement nor acknowledges release of rendering owners.
class FramePresenter final {
public:
    FramePresenter(core::LatestValueSlot<core::FrameBundle>&, IPresentationSink&,
        core::IClock&, std::uint64_t sessionGeneration);
    ~FramePresenter();
    FramePresenter(const FramePresenter&) = delete;
    FramePresenter& operator=(const FramePresenter&) = delete;
    // Drain events before admission. Call again after synchronous sink completion
    // to publish its receipt; this class owns no GUI timer or event receiver.
    void refresh();
    void pause();
    void resume();
    void setDisplayMode(DisplayMode);
    // A later reset during retirement changes only the eventual source; the
    // outstanding retirement ID and all old owners remain until its receipt.
    void resetSource(core::LatestValueSlot<core::FrameBundle>&, std::uint64_t generation);
    void retire();
    [[nodiscard]] const WorkstationStatus& status() const noexcept;
    [[nodiscard]] DisplayMode displayMode() const noexcept;
    [[nodiscard]] std::uint64_t displayedFrameCount() const noexcept;
    [[nodiscard]] std::shared_ptr<const core::FrameBundle> presentedBundle() const noexcept;
    [[nodiscard]] bool originalAvailable() const noexcept;
    [[nodiscard]] bool enhancedAvailable() const noexcept;
    [[nodiscard]] bool compareAvailable() const noexcept;
    [[nodiscard]] const std::optional<core::Error>& error() const noexcept;
    [[nodiscard]] bool retirementComplete() const noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::presentation
