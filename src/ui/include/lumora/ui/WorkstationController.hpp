#pragma once
#include <lumora/application/LivePipeline.hpp>
#include <QObject>
#include <memory>
namespace lumora::configuration { class StartupPreferencesService; }
namespace lumora::ui {
class CameraStartupPanel;
class FramePresenter;
class WorkstationView;
enum class CameraStartupIntent { Refresh, Connect, Apply, Confirm, Start, Stop, Disconnect, Retry, ResumeLive };
// UI-thread owner of presentation and startup continuations. Composition starts
// the pipeline/preferences service before start(), and keeps them and widgets
// alive through shutdown(). It then finishes/join preferences persistence.
class WorkstationController final : public QObject {
public:
    WorkstationController(application::LivePipeline& pipeline,
        configuration::StartupPreferencesService& preferences, WorkstationView& view,
        CameraStartupPanel& panel, core::IClock& clock, camera::CameraConfiguration fixedRequest);
    ~WorkstationController() override;
    [[nodiscard]] core::Result<void> start();
    void poll();
    void selectCamera(camera::CameraId id);
    [[nodiscard]] core::Result<void> dispatch(CameraStartupIntent intent);
    [[nodiscard]] FramePresenter* presenter() const noexcept;
    void shutdown() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
