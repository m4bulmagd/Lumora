# M9 Task 4A: stopped camera settings dialog implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement and independently review each task. Root owns Git and all shared builds.

**Goal:** Provide a useful camera settings dialog for stopped-state exposure/gain edits with authoritative readback, explicit confirmation and persisted same-mode Resume.

**Architecture:** Retain CameraStartupPanel and WorkstationController as the existing presentation/command owners. Add one modeless dialog owning an unsubmitted draft and one application policy for edits compatible with prepared source resources. Keep the existing asynchronous Apply/Confirm/Start and preferences writer.

**Tech Stack:** C++20, Qt6 Widgets, CMake, pinned vcpkg, GoogleTest.

**Spec:** Original M9 Task4 in `docs/superpowers/plans/2026-04-25-m09-presets-workstation-ui.md`, design sections7/11/13, and the M5 startup contract `docs/architecture/milestones/m05-preflight.md`.

## Global constraints and scope rulings

- This is the first independent Task4 slice; remaining Task4 is explicitly unfinished. User approved Compare publication/CI/merge followed by camera settings development. Preserve unrelated root-checkout documentation edits.
- Linux is the development host; Windows11 remains official installation/hardware acceptance. Evaluation banner, no clinical use and existing gates remain unchanged.
- Exposure/gain modes and numeric values follow current capabilities and existing validator. No direct camera writes or file I/O in UI. No implicit Start or stream restart. Editing requires ConnectedIdle, not viewer Pause.
- FPS, complete ROI (x/y/width/height), source format and acquisition mode remain the prepared request. FPS editing is deferred because advertised1FPS can exceed the existing three250ms timeout policy. ROI/format require real same-device resource rebinding. Do not alter watchdog behavior in this slice.
- NumericCapability lacks availability/general-writability, and empty gainModes is invalid in the domain. Do not interpret writableWhileStreaming=false as absent/read-only. Absent-gain/read-only-FPS model/schema extensions are later Task4 work. Invalid capability/configuration combinations disable Apply with an explanation.
- No persistence schema change. Restore only saved requests compatible with prepared fixed fields; retain explicit identity, complete capability and readback comparisons. Keep M5 slow-load, priority cancellation and save-failure regressions.
- Installation orientation, per-camera records, compact-panel replacement, automatic stop/apply/restart and fullscreen remain separate planned steps. Dialog accurately shows this slice's readonly fields without exposing internal implementation jargon.

## Shared interfaces

Task A adds `src/application/include/lumora/application/CameraSettingsPolicy.hpp` and `.cpp`:
```cpp
[[nodiscard]] bool isCameraSettingsCompatible(
    const camera::CameraConfiguration& requested,
    const camera::CameraConfiguration& prepared);
```
It compares every field except exposure and gain (including optional FPS, complete source descriptor, full ROI and acquisitionMode). It is a compatibility predicate, not capability validation. LivePipeline::post uses it instead of full configuration equality, then worker validation remains authoritative. Controller uses the same predicate for edited requests and saved-request restoration.

Task B adds `CameraSettingsDialog` (QDialog with Q_OBJECT) and extends CameraStartupPanel:
```cpp
// Presentation field renamed from fixedRequestedConfiguration:
std::optional<camera::CameraConfiguration> requestedConfiguration;
// Dialog consumes the existing CameraStartupPanelPresentation:
void CameraSettingsDialog::setPresentation(CameraStartupPanelPresentation presentation);
// Both dialog and panel expose the same intent:
void settingsApplyRequested(std::uint64_t sessionGeneration,
    camera::CameraId cameraId, camera::CameraConfiguration requested);
```
Panel opens at most one dialog via `cameraSettingsButton`, maintains its lifetime, forwards intent and each presentation update. Keep panel Apply/Confirm/Start controls and requested/actual text. Keep Stop/Disconnect accessible while ordinary commands are pending. No CameraPanel rename. Correct stale warning that incorrectly claims all modes require Mono8.

Task C adds controller API:
```cpp
[[nodiscard]] core::Result<void> applyCameraSettings(
    std::uint64_t sessionGeneration, camera::CameraId cameraId,
    camera::CameraConfiguration requested);
```
The controller keeps immutable prepared request and mutable desired request. Validate fresh status state, generation, actual identity, capabilities, no pending/barrier and settings compatibility before posting one full ApplyConfiguration with increasing revision. Validate full camera configuration. Change desired/presentation only after admission; admission failure retains old desired and reports warning. Ordinary Apply uses current desired. Worker failure retains requested-versus-actual distinction and blocks Confirm/Start for an unapplied revision. No optimistic success or confirmation.

Initial saved settings are adopted only once, if valid/confirmed/compatible and no manual settings submission or explicit selection already owns the request. Saved probing and Resume require mutable desired exactly equal to loaded.requested, in addition to the same compatibility rule and existing exact identity/capabilities/readback checks. A previous saved Resume cannot override newer unconfirmed edits. Slow load cannot overwrite already submitted manual settings. On reconnect preserve desired only for the same identity; explicit different selection restores prepared defaults and invalidates dialog's old source. An open dialog never transfers a draft across identity, generation or capability replacement. Retained panel Apply/Confirm/Start also require selected identity to match actual identity. Confirm/Start require requestedRevision==appliedRevision!=0, and Start additionally requires confirmedRevision==appliedRevision, even for direct controller dispatch after a failed Apply. Confirm/Start additionally require the displayed desired request to exactly match the worker requested configuration; selecting another identity and returning cannot reuse an older confirmation for newly displayed defaults.

## Task A: application admission policy

Files: CameraSettingsPolicy.hpp/.cpp, LivePipeline.cpp/.hpp comments, tests/unit/application/CameraSettingsPolicyTests.cpp. Root registers CMake and owns all LivePipelineTests.cpp integration additions under Task C.

- [x] Write failing tests for compatible exposure/gain modes/values and incompatible every fixed-field variant, including same storage with altered source metadata and absent/different FPS.
- [x] Specify integration expectations for root Task C: stopped compatible request admitted/applied, readback recorded, old confirmation invalidated, explicit Confirm/Start required, same context/resources/source sequence retained. Invalid values still fail worker validation. Geometry/FPS changes rejected at admission.
- [x] Root records RED; implement predicate and minimal facade relaxation. Do not change worker, capabilities or settings schema.
- [x] Root runs focused application/pipeline tests; independent task review and fixes.

## Task B: capability-driven stopped settings dialog

Files: CameraSettingsDialog.hpp/.cpp, CameraStartupPanel.hpp/.cpp and tests CameraSettingsDialogTests.cpp/CameraStartupPanelTests.cpp. Root registers CMake; root owns any controller test field rename.

- [x] Write tests against real widgets: manual/auto mode choices; ranges/increments; untouched fractional values round-trip; auto omits optional manual value; draft edits produce no intent; Apply emits one complete configuration tagged with old source ID/generation. Literal expected values must differ from initial values.
- [x] Show readonly FPS, ROI including offsets, pixel format and continuous mode; source camera identity; actual readback separately from draft. Use translated plain-text labels, accessible names/buddies and locale-aware full-precision numeric display. Invalid capability/configuration disables Apply with reason, no silent clamping submitted values.
- [x] Repeated polling never overwrites draft. Streaming or viewer-paused-but-streaming disables edits with explicit Stop instruction. Pending operation disables Apply; close/cancel never sends a command. Replaced identity/generation/capabilities invalidates draft and requires close/reopen. External requested revision changes cannot silently apply an older draft; successful own Apply retains actual readback without implying confirmation. Panel source selection must also invalidate draft if it differs from actual identity.
- [x] Root records RED then author implements; preserve panel controls and persistent warning/priority behavior. Focused UI tests plus native dialog/widget captures at supported window sizes.

## Task C: controller binding and persistence

Files: WorkstationController.hpp/.cpp, tests/integration/LivePipelineTests.cpp (existing controller fixture), tests/unit/ui/WorkstationControllerTests.cpp as necessary. Root owns.

- [x] Behavioral RED: complete stopped edits apply asynchronously and require fresh explicit confirmation; mismatched generation/ID, viewer Pause while streaming, pending requests, invalid numbers and incompatible fixed fields emit no application command and preserve requested/readback.
- [x] Use shared policy and dialog intent; verify real Apply/readback/Confirm/Start end-to-end, updated desired values survive subsequent ordinary Apply, priority Disconnect prevents late Start, and stale dialog signals cannot apply to replacement sessions.
- [x] Persist confirmed edited values through existing writer and resume on next launch only when identity/capabilities/readback match. Verify late preferences load cannot overwrite manual submission and different-camera selection resets desired defaults.
- [x] Root records GREEN, independent task review and fixes; no duplicated startup policy, new worker or file writer.

## Task D: completion and evidence

- [x] Independent whole-branch source/spec review; preserve task review records and close actionable findings.
- [x] Full Linux Debug/Release build/headless suites, native X11 in both, actual camera dialog screenshot inspection. Commit reviewed source/tests and source-bound clean checks under out/qa/m09-camera-settings.
- [x] Update README, PROGRESS, original/refined plans, traceability, launch guide and dedicated milestone with Compare integration and Task4A local scope/results. Keep all remaining Task4 work and capability/FPS limitations explicit. Commit docs; Task4A remains local for subsequent publication decision.


Final source `0b6f8f4`: full Debug/Release62/62 in65.95/26.67s, native X11each1/1 in0.12/0.06s; native dialog1/1 in0.087s at560×560 and720×640 with both captures inspected. Seven clean command records and two BMP hashes are verified. The [milestone record](../../architecture/milestones/m09-camera-settings.md#final-source-verification) preserves review corrections, compiler diagnostic and evidence limits. Completion documentation and the independent evidence audit are complete; no Task4A publication or full Task4 acceptance is claimed.
