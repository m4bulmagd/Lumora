#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/configuration/PresetCodec.hpp>
#include <lumora/configuration/UiPreferencesCodec.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

    struct SelectionSubmission final {
        std::uint64_t revision;
        camera::CameraId cameraId;
        std::optional<core::CameraIdentity> requiredProfileIdentity;
    };

    struct PresetSaveSubmission final {
        std::uint64_t revision;
        application::PresetState presets;
    };

    struct UiSaveSubmission final {
        std::uint64_t revision;
        application::UiPreferencesUpdate update;
    };

    static void mergeUiUpdate(application::UiPreferencesUpdate& target,
        const application::UiPreferencesUpdate& update) {
        if (update.normalGeometry) target.normalGeometry = update.normalGeometry;
        if (update.panelsCollapsed) target.panelsCollapsed = update.panelsCollapsed;
        if (update.maximized) target.maximized = update.maximized;
        if (update.fullscreen) target.fullscreen = update.fullscreen;
        if (update.diagnosticsVisible) target.diagnosticsVisible = update.diagnosticsVisible;
    }

    static application::UiPreferences applyUiUpdate(application::UiPreferences preferences,
        const application::UiPreferencesUpdate& update) {
        if (update.normalGeometry) preferences.normalGeometry = *update.normalGeometry;
        if (update.panelsCollapsed) preferences.panelsCollapsed = *update.panelsCollapsed;
        if (update.maximized) preferences.maximized = *update.maximized;
        if (update.fullscreen) preferences.fullscreen = *update.fullscreen;
        if (update.diagnosticsVisible) preferences.diagnosticsVisible = *update.diagnosticsVisible;
        return preferences;
    }

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
        application::StartupPreferences preferences,
        bool selectCamera) {
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
        const auto available = checkSaveAvailability();
        if (!available.hasValue()) { return available; }
        if (revision == 0U || revision <= latestAcceptedRevision_) {
            return core::Result<void>::failure(serviceError(
                "startup_save_revision_not_increasing",
                "Startup preferences were not saved.",
                "Save submission revisions must increase monotonically."));
        }
        const auto existingPending = std::find_if(pending_.begin(), pending_.end(),
            [&](const auto& submission) {
                return application::cameraIdentityKeysEqual(
                    submission.preferences.identity, preferences.identity);
            });
        if (existingPending == pending_.end()
            && !profileIdentityKnown(preferences.identity)
            && knownProfileCount() >= application::CameraPreferences::MaximumProfiles) {
            return core::Result<void>::failure(serviceError(
                "startup_save_capacity_reached",
                "Startup preferences were not saved.",
                "The maximum of 64 distinct camera identities has been reached."));
        }
        latestAcceptedRevision_ = revision;
        const auto selectedId = preferences.cameraId;
        const auto selectedIdentity = preferences.identity;
        if (existingPending == pending_.end()) {
            pending_.push_back(SaveSubmission{revision, std::move(preferences)});
        } else {
            pending_.erase(existingPending);
            pending_.push_back(SaveSubmission{revision, std::move(preferences)});
        }
        if (selectCamera) {
            pendingSelection_ = SelectionSubmission{
                revision, selectedId, selectedIdentity};
        }
        changed_.notify_all();
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> postSelection(
        std::uint64_t revision, camera::CameraId cameraId) {
        if (cameraId.value.empty()) {
            return core::Result<void>::failure(serviceError(
                "startup_selection_invalid", "Camera selection was not saved.",
                "The selected logical camera ID must not be empty."));
        }
        std::lock_guard lock(mutex_);
        const auto available = checkSaveAvailability();
        if (!available.hasValue()) return available;
        if (revision == 0U || revision <= latestAcceptedRevision_) {
            return core::Result<void>::failure(serviceError(
                "startup_save_revision_not_increasing",
                "Camera selection was not saved.",
                "Camera profile and selection revisions must increase monotonically."));
        }
        latestAcceptedRevision_ = revision;
        pendingSelection_ = SelectionSubmission{
            revision, std::move(cameraId), std::nullopt};
        changed_.notify_all();
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> postPresetSave(
        std::uint64_t revision, application::PresetState presets) {
        auto repository = PresetCodec::loadDefaultRepository();
        if (!repository.hasValue()) {
            return core::Result<void>::failure(repository.error());
        }
        const auto restored = repository.value().restore(presets);
        if (!restored.hasValue()) {
            return core::Result<void>::failure(restored.error());
        }

        std::lock_guard lock(mutex_);
        const auto available = checkSaveAvailability();
        if (!available.hasValue()) { return available; }
        if (revision == 0U || revision <= latestAcceptedPresetRevision_) {
            return core::Result<void>::failure(serviceError(
                "preset_save_revision_not_increasing", "Presets were not saved.",
                "Preset save submission revisions must increase monotonically."));
        }
        pendingPresets_ = PresetSaveSubmission{revision, std::move(presets)};
        latestAcceptedPresetRevision_ = revision;
        changed_.notify_all();
        return core::Result<void>::success();
    }

    [[nodiscard]] core::Result<void> postUiSave(
        std::uint64_t revision, application::UiPreferences preferences) {
        application::UiPreferencesUpdate update;
        update.normalGeometry = preferences.normalGeometry;
        update.panelsCollapsed = preferences.panelsCollapsed;
        update.maximized = preferences.maximized;
        update.fullscreen = preferences.fullscreen;
        update.diagnosticsVisible = preferences.diagnosticsVisible;
        return postUiUpdate(revision, std::move(update));
    }

    [[nodiscard]] core::Result<void> postUiUpdate(
        std::uint64_t revision, application::UiPreferencesUpdate update) {
        if (!update.normalGeometry && !update.panelsCollapsed && !update.maximized
            && !update.fullscreen && !update.diagnosticsVisible) {
            return core::Result<void>::failure(serviceError(
                "ui_update_empty", "Window preferences were not saved.",
                "A partial UI update must contain at least one changed field."));
        }
        const auto valid = UiPreferencesCodec::validate(applyUiUpdate({}, update));
        if (!valid.hasValue()) return valid;
        std::lock_guard lock(mutex_);
        const auto available = checkSaveAvailability();
        if (!available.hasValue()) return available;
        if (status_->loadCompleted && !status_->uiPreferencesWritable) {
            return core::Result<void>::failure(status_->uiWarning.value_or(serviceError(
                "ui_preferences_unavailable", "Window preferences cannot be saved.",
                "The loaded UI preference section is not writable.")));
        }
        if (revision == 0 || revision <= latestAcceptedUiRevision_) {
            return core::Result<void>::failure(serviceError(
                "ui_save_revision_not_increasing", "Window preferences were not saved.",
                "UI save revisions must increase independently of other preference sections."));
        }
        if (pendingUi_) {
            mergeUiUpdate(pendingUi_->update, update);
            pendingUi_->revision = revision;
        } else {
            pendingUi_ = UiSaveSubmission{revision, std::move(update)};
        }
        latestAcceptedUiRevision_ = revision;
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
    [[nodiscard]] bool profileIdentityKnown(
        const core::CameraIdentity& identity) const {
        if (std::any_of(knownIdentities_.begin(), knownIdentities_.end(),
                [&](const auto& known) {
                    return application::cameraIdentityKeysEqual(known, identity);
                })) {
            return true;
        }
        if (std::any_of(inFlightIdentities_.begin(), inFlightIdentities_.end(),
                [&](const auto& admitted) {
                    return application::cameraIdentityKeysEqual(admitted, identity);
                })) {
            return true;
        }
        return std::any_of(pending_.begin(), pending_.end(), [&](const auto& submission) {
            return application::cameraIdentityKeysEqual(
                submission.preferences.identity, identity);
        });
    }

    [[nodiscard]] std::size_t knownProfileCount() const {
        std::vector<core::CameraIdentity> identities;
        identities = knownIdentities_;
        for (const auto& admitted : inFlightIdentities_) {
            if (std::none_of(identities.begin(), identities.end(),
                    [&](const auto& identity) {
                        return application::cameraIdentityKeysEqual(
                            identity, admitted);
                    })) {
                identities.push_back(admitted);
            }
        }
        for (const auto& submission : pending_) {
            if (std::none_of(identities.begin(), identities.end(), [&](const auto& identity) {
                    return application::cameraIdentityKeysEqual(
                        identity, submission.preferences.identity);
                })) {
                identities.push_back(submission.preferences.identity);
            }
        }
        return identities.size();
    }

    static void normalizeCameraPreferences(ApplicationConfiguration& document) {
        if (document.cameraProfiles.profiles.empty() && document.startup) {
            document.cameraProfiles.profiles.push_back(*document.startup);
            if (!document.cameraProfiles.lastSelectedCameraId) {
                document.cameraProfiles.lastSelectedCameraId = document.startup->cameraId;
            }
        }
        document.startup.reset();
        if (!document.cameraProfiles.lastSelectedCameraId) return;
        const auto selected = std::find_if(document.cameraProfiles.profiles.rbegin(),
            document.cameraProfiles.profiles.rend(), [&](const auto& profile) {
                return profile.cameraId == *document.cameraProfiles.lastSelectedCameraId;
            });
        if (selected != document.cameraProfiles.profiles.rend()) {
            document.startup = *selected;
        }
    }

    // Caller holds mutex_; both sections share admission and source safety.
    [[nodiscard]] core::Result<void> checkSaveAvailability() const {
        if (!started_) {
            return core::Result<void>::failure(serviceError(
                "startup_service_not_started", "Startup preferences are not ready.",
                "A save cannot be submitted before the service starts."));
        }
        if (!accepting_ || stopSource_.stop_requested()) {
            return core::Result<void>::failure(serviceError(
                "startup_save_stopping", "Startup preferences were not saved.",
                "The startup preference worker is stopping."));
        }
        if (status_->loadCompleted && !safeToSave_) {
            return core::Result<void>::failure(serviceError(
                "startup_save_source_unsafe", "Startup preferences were not saved.",
                "The source configuration could not be read or preserved safely."));
        }
        return core::Result<void>::success();
    }

    void publishLoadResult(core::Result<ApplicationConfiguration> loaded) {
        auto next = application::StartupPreferencesStatus{};
        next.loadCompleted = true;
        if (loaded.hasValue()) {
            auto document = std::move(loaded).value();
            normalizeCameraPreferences(document);
            std::vector<core::CameraIdentity> knownIdentities;
            knownIdentities.reserve(document.cameraProfiles.profiles.size());
            for (const auto& profile : document.cameraProfiles.profiles) {
                knownIdentities.push_back(profile.identity);
            }
            next.loadedPreferences = document.startup;
            next.loadedCameraPreferences = document.cameraProfiles;
            next.loadedPresets = document.presets;
            const auto ui = UiPreferencesCodec::decode(document.ui);
            next.loadedUiPreferences = ui.preferences;
            next.uiPreferencesWritable = ui.writable;
            next.uiWarning = ui.warning;
            next.warning = document.loadWarning;
            const auto initialWarning = document.loadWarning;
            const auto safeToSave = !(document.usedDefaults && document.loadWarning
                && !document.preservedInvalidFile.has_value());
            next.uiPreferencesWritable = safeToSave && ui.writable;
            if (!safeToSave) next.uiWarning = document.loadWarning;
            std::lock_guard lock(mutex_);
            initialUiWarning_ = next.uiWarning;
            document_ = std::move(document);
            knownIdentities_ = std::move(knownIdentities);
            initialWarning_ = initialWarning;
            safeToSave_ = safeToSave;
            status_ = std::make_shared<const application::StartupPreferencesStatus>(
                std::move(next));
            return;
        } else {
            next.warning = loaded.error();
            next.uiWarning = loaded.error();
        }
        std::lock_guard lock(mutex_);
        safeToSave_ = false;
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
            if (!pending_.empty() || pendingSelection_) {
                std::uint64_t revision = pendingSelection_
                    ? pendingSelection_->revision : 0U;
                for (const auto& submission : pending_) {
                    revision = std::max(revision, submission.revision);
                }
                next.latestAttemptedSaveRevision = revision;
                pending_.clear();
                pendingSelection_.reset();
            }
            inFlightIdentities_.clear();
            if (pendingPresets_) {
                next.latestAttemptedPresetSaveRevision = pendingPresets_->revision;
                pendingPresets_.reset();
            }
            if (pendingUi_) {
                next.latestAttemptedUiSaveRevision = pendingUi_->revision;
                pendingUi_.reset();
            }
            next.uiPreferencesWritable = false;
            next.uiWarning = next.warning;
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
                std::vector<SaveSubmission> submissions;
                std::optional<SelectionSubmission> selection;
                std::optional<PresetSaveSubmission> presetSubmission;
                std::optional<UiSaveSubmission> uiSubmission;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, stopSource_.get_token(), [&] {
                        return !pending_.empty() || pendingSelection_.has_value()
                            || pendingPresets_.has_value() || pendingUi_.has_value();
                    });
                    if (pending_.empty() && !pendingSelection_ && !pendingPresets_ && !pendingUi_
                        && stopSource_.stop_requested()) {
                        break;
                    }
                    submissions = std::move(pending_);
                    inFlightIdentities_.clear();
                    inFlightIdentities_.reserve(submissions.size());
                    for (const auto& submission : submissions) {
                        inFlightIdentities_.push_back(
                            submission.preferences.identity);
                    }
                    selection = std::move(pendingSelection_);
                    presetSubmission = std::move(pendingPresets_);
                    uiSubmission = std::move(pendingUi_);
                    pending_.clear();
                    pendingSelection_.reset();
                    pendingPresets_.reset();
                    pendingUi_.reset();
                }
                if (!submissions.empty() || selection || presetSubmission || uiSubmission) {
                    save(std::move(submissions), std::move(selection),
                        std::move(presetSubmission), std::move(uiSubmission));
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

    void save(std::vector<SaveSubmission> submissions,
        std::optional<SelectionSubmission> selection,
        std::optional<PresetSaveSubmission> presetSubmission,
        std::optional<UiSaveSubmission> uiSubmission) {
        std::uint64_t cameraRevision = selection ? selection->revision : 0U;
        for (const auto& submission : submissions) {
            cameraRevision = std::max(cameraRevision, submission.revision);
        }
        {
            std::lock_guard lock(mutex_);
            auto next = *status_;
            if (cameraRevision != 0U) {
                next.latestAttemptedSaveRevision = cameraRevision;
            }
            if (presetSubmission) {
                next.latestAttemptedPresetSaveRevision = presetSubmission->revision;
            }
            if (uiSubmission) next.latestAttemptedUiSaveRevision = uiSubmission->revision;
            if (!safeToSave_) {
                next.uiPreferencesWritable = false;
                next.uiWarning = serviceError("startup_save_source_unsafe",
                    "Window preferences were not saved.",
                    "The source configuration could not be read or preserved safely.");
                next.warning = serviceError(
                    "startup_save_source_unsafe", "Startup preferences were not saved.",
                    "The source configuration could not be read or preserved safely.");
                status_ = std::make_shared<const application::StartupPreferencesStatus>(
                    std::move(next));
                inFlightIdentities_.clear();
                return;
            }
            status_ = std::make_shared<const application::StartupPreferencesStatus>(
                std::move(next));
        }

        std::sort(submissions.begin(), submissions.end(),
            [](const auto& left, const auto& right) {
                return left.revision < right.revision;
            });
        bool cameraChanged = false;
        std::vector<std::uint64_t> appliedCameraRevisions;
        std::vector<core::CameraIdentity> failedIdentities;
        std::optional<core::Error> mergeWarning;
        std::optional<std::uint64_t> mergeFailureRevision;
        for (auto& submission : submissions) {
            auto existing = std::find_if(document_->cameraProfiles.profiles.begin(),
                document_->cameraProfiles.profiles.end(), [&](const auto& profile) {
                    return application::cameraIdentityKeysEqual(
                        profile.identity, submission.preferences.identity);
                });
            if (existing != document_->cameraProfiles.profiles.end()) {
                document_->cameraProfiles.profiles.erase(existing);
                document_->cameraProfiles.profiles.push_back(
                    std::move(submission.preferences));
                cameraChanged = true;
                appliedCameraRevisions.push_back(submission.revision);
                continue;
            }
            if (document_->cameraProfiles.profiles.size()
                >= application::CameraPreferences::MaximumProfiles) {
                mergeWarning = serviceError("startup_save_capacity_reached",
                    "Some startup preferences were not saved.",
                    "The maximum of 64 distinct camera identities was reached after loading the source document.");
                failedIdentities.push_back(submission.preferences.identity);
                mergeFailureRevision = mergeFailureRevision
                    ? std::min(*mergeFailureRevision, submission.revision)
                    : std::optional<std::uint64_t>{submission.revision};
                continue;
            }
            document_->cameraProfiles.profiles.push_back(
                std::move(submission.preferences));
            cameraChanged = true;
            appliedCameraRevisions.push_back(submission.revision);
        }
        const auto selectionDependsOnFailedProfile = selection
            && selection->requiredProfileIdentity
            && std::any_of(failedIdentities.begin(), failedIdentities.end(),
                [&](const auto& failed) {
                    return application::cameraIdentityKeysEqual(failed,
                        *selection->requiredProfileIdentity);
                });
        if (selectionDependsOnFailedProfile) {
            mergeFailureRevision = mergeFailureRevision
                ? std::min(*mergeFailureRevision, selection->revision)
                : std::optional<std::uint64_t>{selection->revision};
        }
        if (selection && !selectionDependsOnFailedProfile) {
            document_->cameraProfiles.lastSelectedCameraId =
                std::move(selection->cameraId);
            appliedCameraRevisions.push_back(selection->revision);
            cameraChanged = true;
        }
        if (mergeFailureRevision
            && (!unresolvedCameraFailureRevision_
                || *mergeFailureRevision < *unresolvedCameraFailureRevision_)) {
            unresolvedCameraFailureRevision_ = mergeFailureRevision;
            unresolvedCameraFailure_ = mergeWarning;
        }
        for (const auto revision : appliedCameraRevisions) {
            if (!unresolvedCameraFailureRevision_
                || revision < *unresolvedCameraFailureRevision_) {
                documentCameraRevision_ = documentCameraRevision_
                    ? std::max(*documentCameraRevision_, revision)
                    : std::optional<std::uint64_t>{revision};
            }
        }
        if (cameraChanged) {
            normalizeCameraPreferences(*document_);
        }
        if (presetSubmission) {
            document_->presets = std::move(presetSubmission->presets);
            documentPresetRevision_ = presetSubmission->revision;
        }
        bool uiChanged = false;
        if (uiSubmission) {
            // Decode on the sole document worker after loading (and after any
            // earlier write), so absent update fields retain the current values.
            const auto preferences = applyUiUpdate(
                UiPreferencesCodec::decode(document_->ui).preferences, uiSubmission->update);
            auto merged = UiPreferencesCodec::merge(document_->ui, preferences);
            if (merged.hasValue()) {
                document_->ui = std::move(merged).value();
                documentUiRevision_ = uiSubmission->revision;
                initialUiWarning_.reset();
                uiChanged = true;
            }
        }
        if (!cameraChanged && !presetSubmission && !uiChanged) {
            std::lock_guard lock(mutex_);
            auto next = *status_;
            next.warning = unresolvedCameraFailure_
                ? unresolvedCameraFailure_ : initialWarning_;
            status_ = std::make_shared<const application::StartupPreferencesStatus>(
                std::move(next));
            inFlightIdentities_.clear();
            return;
        }
        std::vector<core::CameraIdentity> knownIdentities;
        knownIdentities.reserve(document_->cameraProfiles.profiles.size());
        for (const auto& profile : document_->cameraProfiles.profiles) {
            knownIdentities.push_back(profile.identity);
        }
        const auto saved = io_->save(*document_);
        std::lock_guard lock(mutex_);
        inFlightIdentities_.clear();
        knownIdentities_ = std::move(knownIdentities);
        auto next = *status_;
        if (saved.hasValue()) {
            // A later whole-document write can also persist a section whose
            // earlier write failed. Report exactly the revisions now on disk.
            next.latestSavedRevision = documentCameraRevision_;
            next.latestSavedPresetRevision = documentPresetRevision_;
            next.latestSavedUiRevision = documentUiRevision_;
            next.uiWarning = initialUiWarning_;
            next.warning = unresolvedCameraFailure_
                ? unresolvedCameraFailure_ : initialWarning_;
        } else {
            next.warning = saved.error();
            if (documentUiRevision_ != next.latestSavedUiRevision)
                next.uiWarning = saved.error();
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
    std::vector<core::CameraIdentity> knownIdentities_;
    std::vector<core::CameraIdentity> inFlightIdentities_;
    std::vector<SaveSubmission> pending_;
    std::optional<SelectionSubmission> pendingSelection_;
    std::optional<PresetSaveSubmission> pendingPresets_;
    std::optional<UiSaveSubmission> pendingUi_;
    std::optional<std::uint64_t> documentCameraRevision_;
    std::optional<std::uint64_t> documentPresetRevision_;
    std::optional<std::uint64_t> documentUiRevision_;
    std::optional<std::uint64_t> unresolvedCameraFailureRevision_;
    std::optional<core::Error> unresolvedCameraFailure_;
    std::optional<core::Error> initialWarning_;
    std::optional<core::Error> initialUiWarning_;
    std::uint64_t latestAcceptedRevision_{0U};
    std::uint64_t latestAcceptedPresetRevision_{0U};
    std::uint64_t latestAcceptedUiRevision_{0U};
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
    application::StartupPreferences preferences,
    bool selectCamera) {
    return impl_->postSave(revision, std::move(preferences), selectCamera);
}

core::Result<void> StartupPreferencesService::postSelection(
    std::uint64_t revision,
    camera::CameraId cameraId) {
    return impl_->postSelection(revision, std::move(cameraId));
}

core::Result<void> StartupPreferencesService::postPresetSave(
    std::uint64_t revision, application::PresetState presets) {
    return impl_->postPresetSave(revision, std::move(presets));
}

core::Result<void> StartupPreferencesService::postUiSave(
    std::uint64_t revision, application::UiPreferences preferences) {
    return impl_->postUiSave(revision, std::move(preferences));
}

core::Result<void> StartupPreferencesService::postUiUpdate(
    std::uint64_t revision, application::UiPreferencesUpdate update) {
    return impl_->postUiUpdate(revision, std::move(update));
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
