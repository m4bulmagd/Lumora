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

    // The owner serializes start()/join(). postSave(), latestStatus(), and
    // requestStop() may be called concurrently after a successful start().
    [[nodiscard]] core::Result<void> start();
    [[nodiscard]] core::Result<void> postSave(
        std::uint64_t revision,
        application::StartupPreferences preferences);
    [[nodiscard]] std::shared_ptr<const application::StartupPreferencesStatus>
        latestStatus() const;
    void requestStop() noexcept;
    void join() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::configuration
