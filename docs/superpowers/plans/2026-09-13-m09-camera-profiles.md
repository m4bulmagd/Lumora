# M9 camera profiles implementation plan

Spec: `docs/superpowers/specs/2026-09-13-m09-camera-profiles-design.md`.
Baseline: `f393f3377e1a92c31f77c34ef713c97d56da2117`, branch `codex/m09-camera-profiles`.

## Global constraints

- Plain C++ application domain, Qt adapters; no I/O on UI/acquisition/control threads.
- Explicit stopped Save installation → Apply → review → Confirm → Start; no automatic restart/elevation.
- Preserve ROI transaction rollback, same device and IDs, processing definition, resource budget and presenter acknowledgement.
- User schema 4, machine schema 1; 64 identities, no silent eviction; canonical structural fingerprint version 1.
- Missing simulator profile alone allows identity fallback; required-profile absence, invalid data, identity/capability mismatch and stale active binding block Start.
- Root owns CMake, builds/tests, docs and Git. Workers own disjoint files. Write tests first, request root RED, then implement. Do not commit or run concurrent shared builds.

## Shared contracts

Task 1 owns `application/CameraProfile.hpp` with `InstallationProfileReference { uint32_t recordVersion{1}; uint64_t revision; core::Orientation orientation; }`, equality, `cameraIdentityKeysEqual(left,right)`, and `validateCameraCapabilities(capabilities)` returning Result<void>. StartupPreferences appends `capabilityFingerprintVersion{1}` and optional `installationProfile`. `CameraPreferences { static constexpr size_t MaximumProfiles=64; optional<CameraId> lastSelectedCameraId; vector<StartupPreferences> profiles; }`. Status adds optional `loadedCameraPreferences`, retaining initial `loadedPreferences` as a derived compatibility value. ApplicationConfiguration replaces opaque cameraProfiles with typed CameraPreferences, adds legacyCameraProfiles, and retains startup only as an in-memory legacy compatibility field derived from/normalized into the typed collection (never serialize a second authoritative record). Service `postSave(revision, preferences, bool selectCamera=true)` and `postSelection(revision, CameraId)` share monotonically increasing camera revisions; the controller uses false plus explicit selection.

Task 1 exposes `configuration/CameraProfileCodec.hpp`, namespace functions `encodeCameraIdentity(const core::CameraIdentity&) -> QJsonObject`, `decodeCameraIdentity(const QJsonObject&) -> Result<CameraIdentity>`, `encodeCameraCapabilities(CameraCapabilities) -> QJsonObject`, `decodeCameraCapabilities(const QJsonObject&) -> Result<CameraCapabilities>`, extracting current helpers without duplication.

Task 2 owns `application/InstallationProfiles.hpp` and implementation with:
`InstallationCameraProfile { uint32_t recordVersion{1}; uint64_t revision; CameraIdentity identity; uint32_t capabilityFingerprintVersion{1}; CameraCapabilities capabilities; Orientation orientation; bool confirmed{false}; }`;
`InstallationProfilePolicy { Required, SimulatorIdentityFallback }`;
`InstallationSaveOutcome { uint64_t requestId; optional<InstallationCameraProfile> savedProfile; optional<Error> error; }`;
`InstallationProfilesSnapshot { bool loadCompleted; bool administratorMode; InstallationProfilePolicy policy{Required}; vector<InstallationCameraProfile> profiles; optional<Error> loadError; bool savePending; optional<InstallationSaveOutcome> latestSaveOutcome; }`;
`IInstallationProfiles` virtual destructor, `latestStatus() const -> shared_ptr<const InstallationProfilesSnapshot>`, `postSave(uint64_t requestId, InstallationCameraProfile profile, bool repairInvalid=false) -> Result<void>`.
Validation, profile lookup/resolution, and reference helpers belong to Task 2; communicate exact function signatures promptly to Task 3. Expected revision is new revision minus one; reject overflow, missing identity and invalid/duplicate capabilities. A repaired invalid file starts a new revision 1 collection only after preserved backup.

Task 3 owns LivePipeline integration. Constructor appends `IInstallationProfiles* installationProfiles=nullptr` after existing preparation options (borrowed lifetime). Add `InstallationProfileCommand { uint64_t requestId; uint64_t sessionGeneration; camera::CameraId cameraId; core::Orientation orientation; bool confirmed; bool repairInvalid{false}; }` and `saveInstallationProfile(command) -> Result<void>`. Snapshot adds repository snapshot, optional active reference, active orientation, and optional installation error/pending state as needed. Commands are capacity-one and control-thread validated. The save changes only durable data; later ordinary Apply activates. Publish outcome and guidance via snapshot.

## Task 1: Per-camera domain, user codec and writer

Own application CameraProfile header/source, StartupPreferences header/source, configuration ApplicationConfiguration, ConfigurationCodec, new CameraProfileCodec header/source, StartupPreferencesService header/source, and corresponding application/configuration unit tests. No UI, LivePipeline, machine service, or CMake edits.

Implement the shared contract. Test old schemas 1/2/3 migration, schema 4 roundtrip and future 5 rejection; preserve opaque legacy cameraProfiles and legacy presets. Avoid ambiguity between compatibility startup field and typed records: typed collection authoritative whenever populated, normalize old fixture/programmatic startup only when collection empty. Service stores each identity while loading/saving; selected identity is independently revisioned within camera section. Preserve unsafe-load behavior, failed-section retry through later whole-document saves, preset coalescing, drain-on-stop and immutable initial status. Capacity rejection must not discard accepted existing updates. Validate duplicates, finite capability fields, enum/ROI ranges and reference orientation/version. Add behavioral RED proving A/B saves both persist (existing codec/service API can establish loss), then implement and report required source/test registrations.

## Task 2: Machine profile storage and administrator policy

Own new application InstallationProfiles header/source; new configuration InstallationProfilesService, InstallationProfileStore and codec files; new tests for these files. Do not edit Task 1 files, CMake, app, UI or LivePipeline. Depend only on frozen Task 1 shared contracts. Use fake repository/I/O for deterministic worker and policy tests; platform storage tests use injected temp directories.

Implement domain validation/resolution, immutable snapshots, bounded asynchronous service and injected I/O. Provide a production constructor/options API with explicit deliberate administrator launch plus actual OS authority; production path cannot be redirected through environment-controlled user settings. Windows ProgramData known-folder API, actual elevated token, admin/System write + Users read ACLs (use official Microsoft documentation where needed), Linux system default with injected test roots. Only the explicit administrator workflow can write. Read-only load never mutates invalid source. Save lock/re-read/expected-revision/atomic-replace, explicit backup-before-repair, retain all other profiles and reject capacity overflow. Do not silently overwrite unreadable files. Report production composition exact signature to Task 4. Tests first; ask root for RED and registrations, then implement. Platform-specific Windows behavior must be compilable by inspection and documented as awaiting Windows execution, never claim native validation here.

## Task 3: Installation binding in LivePipeline

Own LivePipeline header/source, LiveResourcePreparation only if needed, new integration installation tests and existing LivePipelineTests if required. No CameraCommand/AcquisitionWorker changes unless root coordinates a compelling need. No UI/configuration files or CMake.

Implement shared pipeline API. One pending install-save side channel, control-thread authority/stopped/current-generation/current-camera/explicit-confirmation gates, repository mutation only via async postSave. Start/Confirm/Apply fenced while outcome unknown; priorities responsive. Reject stale commands and never autoactivate after durable success, cancellation, or disconnect. Current durable profile resolution gates ordinary Apply/Confirm/Start; simulator absence fallback identity, required absence/mismatch/invalid blocking. Prepare orientation using existing engine preparation; rebind when source OR orientation differs, same-device transaction, exact readback/rollback, resource budget, accepted processing, presenter acknowledgement. Active binding only changes with successful matching Apply, reset on disconnect; Start matches durable binding. Custom factory must reject nonidentity. Test real frames/raw invariant, frame-ID continuity, profile mismatch, save/activation distinction, allocation failure, stale/priorities and profile changes while saved Resume would otherwise qualify. Use deterministic fake repository and existing provider utilities. Tests first and root records RED. Report exact snapshot/API to Task 4.

## Task 4: Operator/controller integration and installation dialog

After shared runtime/storage contracts settle, own app/main.cpp, WorkstationController, CameraStartupPanel, new InstallationSettingsDialog, relevant WorkstationView/Status/FramePresenter display metadata integration and UI tests. Root may dispatch this to a fresh worker while reviewing completed lower-layer tasks. Restore preferences from initial plus current-run per-identity cache; persist independent selection; never clobber user choice on late load or stale confirmation; reference equality gates Resume. Produce readonly operator profile/status, separate deliberate administrator editor with flips/rotation and two asymmetric previews, stopped/current-session checks, explicit confirmation reset on edits, explicit invalid repair consent, clear saved-versus-active feedback and normal Apply/review. Status must describe retained image using immutable display metadata. Compose machine service for production SIM-LIVE with explicit fallback, lifetime-safe startup/shutdown, CLI installation flag and OS authority. No native/preset pixel orientation changes. Add behavioral UI tests, read-only/admin/streaming/stale/save/error cases and capture inspectable layouts.

## Task 5: Review, verification and delivery

Root registers sources/tests serially and records all RED/GREEN evidence. Review each completed task for spec and quality; fix substantive findings. Run complete Debug and Release headless suites, targeted ASan/UBSan with leaks, supplemental native Linux smoke and visual dialog QA. Strongest-model whole-branch review includes concurrency/storage security/lifecycle and saved versus active state. Fix and re-review. Update M9 plan/QA evidence and remaining milestones accurately, commit local branch only, preserve ledger and all QA artifacts. No push, main merge, hardware acceptance or Windows execution claim.
