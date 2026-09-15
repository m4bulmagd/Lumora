#pragma once

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/configuration/ApplicationConfiguration.hpp>
#include <lumora/configuration/ConfigurationStore.hpp>
#include <lumora/core/Result.hpp>

#include <cstdint>
#include <memory>

namespace lumora::configuration {

class IStartupPreferencesIo {
public:
    virtual ~IStartupPreferencesIo() = default;
    [[nodiscard]] virtual core::Result<ApplicationConfiguration> load() = 0;
    [[nodiscard]] virtual core::Result<void> save(
        const ApplicationConfiguration& configuration) = 0;
};

class StartupPreferencesService final {
public:
    explicit StartupPreferencesService(
        std::unique_ptr<IStartupPreferencesIo> io);
    explicit StartupPreferencesService(ConfigurationStore store);
    ~StartupPreferencesService();

    StartupPreferencesService(const StartupPreferencesService&) = delete;
    StartupPreferencesService& operator=(const StartupPreferencesService&) = delete;

    // The owner serializes start()/join(). Both post methods, latestStatus(), and
    // requestStop() may be called concurrently after a successful start().
    [[nodiscard]] core::Result<void> start();
    // After start, admission coalesces one latest valid confirmed value even
    // while loading. I/O waits for the whole document; an unsafe load settles
    // the accepted revision as an attempted failure without writing defaults.
    // Success here means admission, not durable persistence. Stop rejects new
    // submissions and drains the newest accepted value after load completes.
    [[nodiscard]] core::Result<void> postSave(
        std::uint64_t revision,
        application::StartupPreferences preferences,
        bool selectCamera = true);
    [[nodiscard]] core::Result<void> postSelection(
        std::uint64_t revision,
        camera::CameraId cameraId);
    // Preset revisions increase independently from camera revisions. Each
    // section coalesces separately; one worker merges both into one document.
    // The same admission, source safety and shutdown rules apply.
    [[nodiscard]] core::Result<void> postPresetSave(
        std::uint64_t revision, application::PresetState presets);
    // UI revisions and coalescing are independent; the same worker preserves
    // the loaded whole document and reports admission separately from durability.
    [[nodiscard]] core::Result<void> postUiSave(
        std::uint64_t revision, application::UiPreferences preferences);
    // Partial edits made before load merge with the loaded UI values on the
    // same worker. Pending edits coalesce field by field in revision order.
    [[nodiscard]] core::Result<void> postUiUpdate(
        std::uint64_t revision, application::UiPreferencesUpdate update);
    [[nodiscard]] std::shared_ptr<const application::StartupPreferencesStatus>
        latestStatus() const;
    void requestStop() noexcept;
    void join() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::configuration
