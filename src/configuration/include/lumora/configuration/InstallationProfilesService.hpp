#pragma once

#include <lumora/configuration/InstallationProfileStore.hpp>
#include <memory>

namespace lumora::configuration {

struct InstallationProfilesOptions final {
    bool deliberateAdministratorLaunch{false};
    application::InstallationProfilePolicy policy{application::InstallationProfilePolicy::Required};
};

class InstallationProfilesService final : public application::IInstallationProfiles {
public:
    // Production path and actual OS authority are resolved internally; no environment override.
    explicit InstallationProfilesService(InstallationProfilesOptions options = {});
    InstallationProfilesService(std::unique_ptr<IInstallationProfilesIo> io,
        bool administratorMode, application::InstallationProfilePolicy policy);
    ~InstallationProfilesService() override;
    InstallationProfilesService(const InstallationProfilesService&) = delete;
    InstallationProfilesService& operator=(const InstallationProfilesService&) = delete;
    [[nodiscard]] core::Result<void> start();
    [[nodiscard]] core::Result<void> postSave(std::uint64_t requestId,
        application::InstallationCameraProfile profile, bool repairInvalid = false) override;
    [[nodiscard]] std::shared_ptr<const application::InstallationProfilesSnapshot> latestStatus() const override;
    void requestStop() noexcept;
    void join() noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lumora::configuration
