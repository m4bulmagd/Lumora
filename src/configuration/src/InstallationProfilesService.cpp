#include <lumora/configuration/InstallationProfilesService.hpp>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace lumora::configuration {
namespace {
core::Error serviceError(std::string code, std::string summary, std::string detail = {}) {
    return {core::ErrorCategory::Storage, std::move(code), std::move(summary), std::move(detail), true};
}
// Resolve the machine path on the worker, including platform known-folder calls.
class ProductionIo final : public IInstallationProfilesIo {
public:
    explicit ProductionIo(bool administratorMode) : administratorMode_(administratorMode) {}
    core::Result<std::vector<application::InstallationCameraProfile>> load() override {
        auto path = machineInstallationProfilesPath();
        if (!path.hasValue()) return core::Result<std::vector<application::InstallationCameraProfile>>::failure(path.error());
        return InstallationProfileStore(std::move(path).value(), administratorMode_, true).load();
    }
    core::Result<application::InstallationCameraProfile> save(
        application::InstallationCameraProfile profile, bool repairInvalid) override {
        auto path = machineInstallationProfilesPath();
        if (!path.hasValue()) return core::Result<application::InstallationCameraProfile>::failure(path.error());
        // Recheck current OS authority at the actual write boundary as well as launch.
        auto protectedRoot = path.value().parent_path();
#ifdef _WIN32
        protectedRoot = protectedRoot.parent_path();  // ProgramData/Lumora owns Config too.
#endif
        return InstallationProfileStore(std::move(path).value(),
            administratorMode_ && hasInstallationAdministratorAuthority(), true,
            std::move(protectedRoot)).save(std::move(profile), repairInvalid);
    }
private:
    bool administratorMode_;
};
}

class InstallationProfilesService::Impl final {
public:
    struct Request final {
        std::uint64_t id;
        application::InstallationCameraProfile profile;
        bool repairInvalid;
    };
    Impl(std::unique_ptr<IInstallationProfilesIo> io, bool administratorMode,
        application::InstallationProfilePolicy policy) : io_(std::move(io)) {
        auto status = std::make_shared<application::InstallationProfilesSnapshot>();
        status->administratorMode = administratorMode;
        status->policy = policy;
        status_ = std::move(status);
    }
    core::Result<void> start() {
        std::lock_guard lock(mutex_);
        if (started_ || stopping_ || !io_) return core::Result<void>::failure(serviceError(
            "installation_service_unavailable", "The installation profile service cannot be started."));
        try {
            worker_ = std::thread([this] { run(); });
            started_ = true;
        } catch (const std::exception& exception) {
            return core::Result<void>::failure(serviceError("installation_worker_failed",
                "The installation profile worker could not be started.", exception.what()));
        }
        return core::Result<void>::success();
    }
    core::Result<void> postSave(std::uint64_t id, application::InstallationCameraProfile profile, bool repairInvalid) {
        const auto valid = application::validateInstallationProfile(profile);
        if (!valid.hasValue()) return valid;
        std::lock_guard lock(mutex_);
        if (!started_ || stopping_) return core::Result<void>::failure(serviceError(
            "installation_service_unavailable", "The installation profile service is not accepting saves."));
        if (!status_->administratorMode) return core::Result<void>::failure(serviceError(
            "installation_administrator_required", "Installation saves require deliberate administrator mode."));
        if (status_->savePending) return core::Result<void>::failure(serviceError(
            "installation_save_pending", "Wait for the outstanding installation save to finish."));
        if (id == 0 || id <= lastRequestId_) return core::Result<void>::failure(serviceError(
            "installation_request_stale", "The installation save request is stale."));
        auto next = std::make_shared<application::InstallationProfilesSnapshot>(*status_);
        next->savePending = true;
        request_ = Request{id, std::move(profile), repairInvalid};
        lastRequestId_ = id;
        status_ = std::move(next);
        cv_.notify_one();
        return core::Result<void>::success();
    }
    std::shared_ptr<const application::InstallationProfilesSnapshot> latestStatus() const {
        std::lock_guard lock(mutex_);
        return status_;
    }
    void requestStop() noexcept {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        cv_.notify_one();
    }
    void join() noexcept { if (worker_.joinable()) worker_.join(); }
private:
    core::Result<std::vector<application::InstallationCameraProfile>> load() {
        try {
            auto loaded = io_->load();
            if (loaded.hasValue()) {
                const auto valid = application::validateInstallationProfiles(loaded.value());
                if (!valid.hasValue()) return core::Result<std::vector<application::InstallationCameraProfile>>::failure(valid.error());
            }
            return loaded;
        } catch (const std::exception& exception) {
            return core::Result<std::vector<application::InstallationCameraProfile>>::failure(serviceError(
                "installation_read_failed", "The installation profile load failed.", exception.what()));
        } catch (...) {
            return core::Result<std::vector<application::InstallationCameraProfile>>::failure(serviceError(
                "installation_read_failed", "The installation profile load failed."));
        }
    }
    core::Result<application::InstallationCameraProfile> save(Request request) {
        try {
            return io_->save(std::move(request.profile), request.repairInvalid);
        } catch (const std::exception& exception) {
            return core::Result<application::InstallationCameraProfile>::failure(serviceError(
                "installation_save_failed", "The installation save failed.", exception.what()));
        } catch (...) {
            return core::Result<application::InstallationCameraProfile>::failure(serviceError(
                "installation_save_failed", "The installation save failed."));
        }
    }
    void run() {
        auto loaded = load();
        {
            std::lock_guard lock(mutex_);
            auto next = std::make_shared<application::InstallationProfilesSnapshot>(*status_);
            next->loadCompleted = true;
            if (loaded.hasValue()) next->profiles = std::move(loaded).value();
            else next->loadError = loaded.error();
            status_ = std::move(next);
        }
        while (true) {
            std::optional<Request> request;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return stopping_ || request_.has_value(); });
                if (!request_) return;
                request = std::move(request_);
                request_.reset();
            }
            const auto requestId = request->id;
            auto saved = save(std::move(*request));
            // A failed write can reveal a newer revision or unsafe source. Keep
            // admission fenced until the current disk facts are published too.
            auto refreshed = load();
            {
                std::lock_guard lock(mutex_);
                auto next = std::make_shared<application::InstallationProfilesSnapshot>(*status_);
                next->savePending = false;
                application::InstallationSaveOutcome outcome{requestId, {}, {}};
                if (saved.hasValue()) {
                    outcome.savedProfile = std::move(saved).value();
                } else outcome.error = saved.error();
                if (refreshed.hasValue()) {
                    next->profiles = std::move(refreshed).value();
                    next->loadError.reset();
                } else next->loadError = refreshed.error();
                next->latestSaveOutcome = std::move(outcome);
                status_ = std::move(next);
            }
        }
    }
    std::unique_ptr<IInstallationProfilesIo> io_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::shared_ptr<const application::InstallationProfilesSnapshot> status_;
    std::optional<Request> request_;
    std::thread worker_;
    std::uint64_t lastRequestId_{0};
    bool started_{false};
    bool stopping_{false};
};

InstallationProfilesService::InstallationProfilesService(InstallationProfilesOptions options) {
    const bool administratorMode = options.deliberateAdministratorLaunch && hasInstallationAdministratorAuthority();
    impl_ = std::make_unique<Impl>(std::make_unique<ProductionIo>(administratorMode), administratorMode, options.policy);
}
InstallationProfilesService::InstallationProfilesService(std::unique_ptr<IInstallationProfilesIo> io,
    bool administratorMode, application::InstallationProfilePolicy policy)
    : impl_(std::make_unique<Impl>(std::move(io), administratorMode, policy)) {}
InstallationProfilesService::~InstallationProfilesService() { requestStop(); join(); }
core::Result<void> InstallationProfilesService::start() { return impl_->start(); }
core::Result<void> InstallationProfilesService::postSave(std::uint64_t requestId,
    application::InstallationCameraProfile profile, bool repairInvalid) {
    return impl_->postSave(requestId, std::move(profile), repairInvalid);
}
std::shared_ptr<const application::InstallationProfilesSnapshot> InstallationProfilesService::latestStatus() const {
    return impl_->latestStatus();
}
void InstallationProfilesService::requestStop() noexcept { impl_->requestStop(); }
void InstallationProfilesService::join() noexcept { impl_->join(); }
}  // namespace lumora::configuration
