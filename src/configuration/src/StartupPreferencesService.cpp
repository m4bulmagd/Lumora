#include <lumora/configuration/StartupPreferencesService.hpp>

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace lumora::configuration {
namespace {

[[nodiscard]] core::Error serviceError(
    std::string code,
    std::string summary,
    std::string detail) {
    return {core::ErrorCategory::Configuration, std::move(code), std::move(summary),
        std::move(detail), true};
}

class ConfigurationStoreIo final : public IStartupPreferencesIo {
public:
    explicit ConfigurationStoreIo(ConfigurationStore store)
        : store_(std::move(store)) {}

    core::Result<ApplicationConfiguration> load() override {
        return store_.load();
    }

    core::Result<void> save(const ApplicationConfiguration& configuration) override {
        return store_.save(configuration);
    }

private:
    ConfigurationStore store_;
};

}  // namespace

class StartupPreferencesService::Impl final {
public:
    struct SaveSubmission final {
        std::uint64_t revision;
        application::StartupPreferences preferences;
    };

    explicit Impl(std::unique_ptr<IStartupPreferencesIo> io)
        : io_(std::move(io)),
          status_(std::make_shared<const application::StartupPreferencesStatus>()) {}

    ~Impl() {
        requestStop();
        join();
    }

    [[nodiscard]] core::Result<void> start() {
        std::lock_guard lock(mutex_);
        if (started_) {
            return core::Result<void>::failure(serviceError(
                "startup_service_already_started",
                "Startup preferences are already loading.",
                "The startup preferences worker can be started only once."));
        }
        if (!io_) {
            return core::Result<void>::failure(serviceError(
                "startup_service_io_missing",
                "Startup preferences are unavailable.",
                "No startup preference I/O implementation was provided."));
        }
        try {
            started_ = true;
            accepting_ = true;
            worker_ = std::jthread([this] { workerEntry(); });
        } catch (const std::exception& exception) {
            started_ = false;
            accepting_ = false;
            return core::Result<void>::failure(serviceError(
                "startup_service_start_failed",
                "Startup preferences could not be started.", exception.what()));
        } catch (...) {
            started_ = false;
            accepting_ = false;
            return core::Result<void>::failure(serviceError(
                "startup_service_start_failed",
                "Startup preferences could not be started.",
                "An unknown exception occurred while starting the worker."));
        }
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> postSave(
        std::uint64_t revision,
        application::StartupPreferences preferences) {
        const auto validated = application::validateStartupPreferences(preferences);
        if (!validated.hasValue() || !preferences.confirmed) {
            return core::Result<void>::failure(serviceError(
                !preferences.confirmed ? "startup_save_unconfirmed"
                                       : "startup_save_invalid",
                "Startup preferences were not saved.",
                !preferences.confirmed
                    ? "Only an explicitly confirmed configuration may be saved."
                    : validated.error().diagnosticDetail));
        }

        std::lock_guard lock(mutex_);
        if (!started_ || !status_->loadCompleted) {
            return core::Result<void>::failure(serviceError(
                "startup_save_load_pending", "Startup preferences are not ready.",
                "A save cannot be submitted before the initial load completes."));
        }
        if (!accepting_ || stopSource_.stop_requested()) {
            return core::Result<void>::failure(serviceError(
                "startup_save_stopping", "Startup preferences were not saved.",
                "The startup preference worker is stopping."));
        }
        if (!safeToSave_) {
            return core::Result<void>::failure(serviceError(
                "startup_save_source_unsafe", "Startup preferences were not saved.",
                "The source configuration could not be read or preserved safely."));
        }
        if (revision == 0U || revision <= latestAcceptedRevision_) {
            return core::Result<void>::failure(serviceError(
                "startup_save_revision_not_increasing",
                "Startup preferences were not saved.",
                "Save submission revisions must increase monotonically."));
        }
        latestAcceptedRevision_ = revision;
        pending_ = SaveSubmission{revision, std::move(preferences)};
        changed_.notify_all();
        return core::Result<void>::success();
    }

    [[nodiscard]] std::shared_ptr<const application::StartupPreferencesStatus>
    latestStatus() const {
        std::lock_guard lock(mutex_);
        return status_;
    }

    void requestStop() noexcept {
        try {
            {
                std::lock_guard lock(mutex_);
                accepting_ = false;
            }
            stopSource_.request_stop();
            changed_.notify_all();
        } catch (...) {
        }
    }

    void join() noexcept {
        try {
            if (worker_.joinable()) {
                worker_.join();
            }
        } catch (...) {
        }
    }

private:
    void publishLoadResult(core::Result<ApplicationConfiguration> loaded) {
        auto next = application::StartupPreferencesStatus{};
        next.loadCompleted = true;
        if (loaded.hasValue()) {
            document_ = std::move(loaded).value();
            next.loadedPreferences = document_->startup;
            next.warning = document_->loadWarning;
            initialWarning_ = document_->loadWarning;
            safeToSave_ = !(document_->usedDefaults && document_->loadWarning
                && !document_->preservedInvalidFile.has_value());
        } else {
            next.warning = loaded.error();
            safeToSave_ = false;
        }
        std::lock_guard lock(mutex_);
        status_ = std::make_shared<const application::StartupPreferencesStatus>(
            std::move(next));
    }

    void publishWorkerException(const char* detail) noexcept {
        try {
            std::lock_guard lock(mutex_);
            auto next = *status_;
            next.loadCompleted = true;
            next.warning = serviceError("startup_service_worker_exception",
                "Startup preferences are unavailable.", detail);
            accepting_ = false;
            safeToSave_ = false;
            status_ = std::make_shared<const application::StartupPreferencesStatus>(
                std::move(next));
        } catch (...) {
        }
    }

    void workerEntry() noexcept {
        try {
            publishLoadResult(io_->load());
            while (true) {
                std::optional<SaveSubmission> submission;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, stopSource_.get_token(), [&] {
                        return pending_.has_value();
                    });
                    if (pending_) {
                        submission = std::move(pending_);
                        pending_.reset();
                    } else if (stopSource_.stop_requested()) {
                        break;
                    }
                }
                if (submission) {
                    save(std::move(*submission));
                }
            }
        } catch (const std::exception&) {
            publishWorkerException(
                "The startup preference worker caught a standard exception.");
        } catch (...) {
            publishWorkerException(
                "The startup preference worker caught an unknown exception.");
        }
    }

    void save(SaveSubmission submission) {
        {
            std::lock_guard lock(mutex_);
            auto next = *status_;
            next.latestAttemptedSaveRevision = submission.revision;
            status_ = std::make_shared<const application::StartupPreferencesStatus>(
                std::move(next));
        }

        document_->startup = std::move(submission.preferences);
        const auto saved = io_->save(*document_);
        std::lock_guard lock(mutex_);
        auto next = *status_;
        if (saved.hasValue()) {
            next.latestSavedRevision = submission.revision;
            next.warning = initialWarning_;
        } else {
            next.warning = saved.error();
        }
        status_ = std::make_shared<const application::StartupPreferencesStatus>(
            std::move(next));
    }

    std::unique_ptr<IStartupPreferencesIo> io_;
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    std::stop_source stopSource_;
    std::jthread worker_;
    std::shared_ptr<const application::StartupPreferencesStatus> status_;
    std::optional<ApplicationConfiguration> document_;
    std::optional<SaveSubmission> pending_;
    std::optional<core::Error> initialWarning_;
    std::uint64_t latestAcceptedRevision_{0U};
    bool started_{false};
    bool accepting_{false};
    bool safeToSave_{false};
};

StartupPreferencesService::StartupPreferencesService(
    std::unique_ptr<IStartupPreferencesIo> io)
    : impl_(std::make_unique<Impl>(std::move(io))) {}

StartupPreferencesService::StartupPreferencesService(ConfigurationStore store)
    : StartupPreferencesService(
          std::make_unique<ConfigurationStoreIo>(std::move(store))) {}

StartupPreferencesService::~StartupPreferencesService() = default;

core::Result<void> StartupPreferencesService::start() {
    return impl_->start();
}

core::Result<void> StartupPreferencesService::postSave(
    std::uint64_t revision,
    application::StartupPreferences preferences) {
    return impl_->postSave(revision, std::move(preferences));
}

std::shared_ptr<const application::StartupPreferencesStatus>
StartupPreferencesService::latestStatus() const {
    return impl_->latestStatus();
}

void StartupPreferencesService::requestStop() noexcept {
    impl_->requestStop();
}

void StartupPreferencesService::join() noexcept {
    impl_->join();
}

}  // namespace lumora::configuration
