# Milestone 9 Presets and Workstation UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the complete English, localization-ready evaluation interface for explicit camera startup, Original/Enhanced/Compare presentation, fixed-order processing controls, installation orientation, presets, fullscreen, and persisted layout preferences.

**Architecture:** Qt views bind to presentation models and publish complete application commands. Presets are versioned pipeline definitions; UI edits are coalesced but final values are exact.

**Tech Stack:** C++20, Qt 6 Quick/QML/Quick Controls, existing application/processing/configuration modules, GoogleTest/CTest. The normal `lumora_app` is QML-only; unique Widgets regression tests remain behind an explicit opt-in build option.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

**Current direction (2026-09-15):** The owner’s 2026-09-13 Qt Quick/QML decision in [ADR 0001](../../adr/0001-qt-quick-qml-frontend.md) has been implemented: the [QML application migration](../../architecture/milestones/qml-only-workstation.md) supplies the normal application and camera/processing parity. Task 5 development follows the [refined QML layout plan](2026-09-15-qml-layout.md) and [layout milestone record](../../architecture/milestones/qml-layout.md); native/staged verification passes and the implementation is integrated locally at `5f6b82f`. The Task 1–4 file lists and original steps below retain their historical implementation context, rather than directing new Widgets work. Formal M9 acceptance remains separate.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- UI code never calls a camera device or processing stage directly.
- Original, Enhanced, and Compare are presentation modes, not pipeline mutations.
- Built-in presets are neutral, immutable, and never claim clinical validation.
- Manual parameter edits select Custom and publish one complete valid pipeline revision.
- Reset changes processing only.
- Record UI is absent from all v1 production and evaluation compositions.

---

### Task 1: Preset model, built-ins, and typed persistence

The owner-authorized [2026-09-09 preset contract](../../architecture/milestones/m09-presets.md) and [bounded implementation plan](2026-09-09-m09-presets.md) refine this task: typed active state, explicit Custom editing identity, resource loading, schema migration and per-entry recovery. Tasks 2–5 remain separate development.

**Files:**
- Create: `src/application/include/lumora/application/Preset.hpp`
- Create: `src/application/include/lumora/application/PresetRepository.hpp`
- Create: `src/application/src/PresetRepository.cpp`
- Create: `config/default-presets.json`
- Create: `tests/unit/application/PresetRepositoryTests.cpp`
- Modify: `src/configuration/include/lumora/configuration/ApplicationConfiguration.hpp`
- Modify: `src/configuration/src/ConfigurationCodec.cpp`

**Interfaces:**
- Consumes: `PipelineDefinition`, `processing::standardPipeline()`, configuration schema, and built-in JSON resource.
- Produces: `PresetId`, versioned `Preset`, typed `PresetState`, and `PresetRepository::list/find/apply/edit/classify/saveCustom/deleteCustom/restore/snapshot`. JSON resource loading and persistence belong to configuration; the application model is Qt-free.

- [x] **Step 1: Write failing built-in and Custom-transition tests**

```cpp
TEST(PresetRepository, BuiltInsAreImmutableAndEditsBecomeCustom) {
    auto repository = makeRepository(); // Test-local shipped recipe fixture.
    auto standard = repository.find(PresetId{"standard"}).value();
    EXPECT_TRUE(standard.builtIn);
    EXPECT_FALSE(repository.deleteCustom(standard.id).hasValue());
    auto edited = standard.pipeline;
    setGamma(edited, 1.4);
    ASSERT_TRUE(repository.edit(edited).hasValue());
    EXPECT_EQ(repository.snapshot().selectedId, PresetId{"custom"});
}
```

- [x] **Step 2: Verify missing repository fails**

Build `lumora_application_tests`; expect failure.

- [x] **Step 3: Define complete built-in presets**

Create Original, Standard, High Contrast, Soft Detail, and Custom. Original disables all optional enhancement stages while retaining Normalize and WindowLevel. Standard must normalize equal to `processing::standardPipeline()`. Every file entry includes schema version, order version, stable ID, neutral description, fixed canonical stage order, enabled flags, and all parameter values. Reordered definitions are invalid.

- [x] **Step 4: Implement parsing and classification**

Validate built-ins through `PipelineCompiler`. Compare normalized pipeline values, not display labels, when classifying a definition. User presets use distinct IDs and may duplicate parameters but not IDs.

- [x] **Step 5: Test round-trip and invalid preset isolation**

Cover missing stage, out-of-range value, unknown future stage, duplicate ID, built-in deletion/overwrite, custom save/delete, and schema migration. Invalid custom entries are reported and skipped without losing valid entries.

- [x] **Step 6: Commit presets**

```powershell
git add src/application src/configuration config/default-presets.json tests/unit/application/PresetRepositoryTests.cpp
git commit -m "feat(presets): add validated processing bundles"
```

### Task 2: Processing controls and coalesced configuration publication

Owner-authorized continuation: use the [2026-09-09 refined plan](2026-09-09-m09-processing-controls.md) and [Task 2 milestone record](../../architecture/milestones/m09-processing-controls.md). The application had no SetPipelineDefinition facade; the refinement supplies asynchronous generation/revision activation outcomes, extends the single settings writer and adds the minimal single Enhanced preview required to see control effects. Compare remains Task 3.

**Files:**
- Create: `src/ui/include/lumora/ui/ProcessingControlsModel.hpp`
- Create: `src/ui/include/lumora/ui/ProcessingPanel.hpp`
- Create: `src/ui/src/ProcessingControlsModel.cpp`
- Create: `src/ui/src/ProcessingPanel.cpp`
- Create: `tests/unit/ui/ProcessingControlsModelTests.cpp`
- Create: `tests/unit/ui/ProcessingPanelTests.cpp`
- Modify: `src/ui/src/WorkstationView.cpp`

**Interfaces:**
- Consumes: active `PipelineDefinition`, `PresetRepository`, and controller command `SetPipelineDefinition`.
- Produces: numeric window/level, brightness, contrast, gamma, denoise, sharpen, and inversion controls in the canonical order; preset selector; Reset; and at-most-30-Hz drag publication. Flip/rotation are not processing controls.

- [ ] **Step 1: Write failing atomic-update/coalescing tests**

```cpp
TEST(ProcessingControlsModel, SliderDragPublishesAtMostThirtyPerSecondAndReleaseIsExact) {
    ManualClock clock;
    RecordingPipelineSink sink;
    ProcessingControlsModel model(defaultPipeline(), sink, clock);
    for (int value = 1; value <= 100; ++value) model.dragGamma(value / 50.0);
    EXPECT_LE(sink.revisions().size(), 1U);
    clock.advance(34ms);
    model.tick();
    model.releaseGamma(2.0);
    EXPECT_DOUBLE_EQ(readGamma(sink.last()), 2.0);
}
```

- [ ] **Step 2: Verify model/panel are missing**

Build `lumora_ui_tests`; expect failure.

- [ ] **Step 3: Implement presentation model**

The model owns a draft pipeline, validates each complete revision, updates numeric labels immediately, coalesces drag events by monotonic clock, emits the exact release revision, applies presets in one call, and changes preset identity to Custom after a manual edit.

- [ ] **Step 4: Implement clean panel sections**

Display mode, Image Controls, Preset, Capture area, and Reset are visually separated. Controls retain the fixed order and cannot be dragged/reordered. Denoise exposes mode and strength/kernel consistently; sharpening exposes amount with radius/threshold under Advanced; inversion may also be Advanced. Flip and rotation appear only in the administrator-managed installation camera-profile workflow.

- [ ] **Step 5: Test keyboard/accessibility and Reset isolation**

Assert labels, value text, tab order, accessible names, range endpoints, invalid draft rejection, and that Reset emits no camera/view/connection command.

- [ ] **Step 6: Commit processing UI**

```powershell
git add src/ui tests/unit/ui/ProcessingControlsModelTests.cpp tests/unit/ui/ProcessingPanelTests.cpp
git commit -m "feat(ui): add coalesced enhancement controls"
```

### Task 3: Original, Enhanced, and synchronized Compare presentation

The owner-authorized [2026-09-10 refined plan](2026-09-10-m09-compare.md) and [Task 3 milestone record](../../architecture/milestones/m09-compare.md) refine this task against the integrated viewer. They extend the existing ImageViewport into one atomic canvas with a shared transform instead of introducing a second widget, preserving its standalone API. The refined plan specifies token/mode/owner paint receipts, frozen-bundle switching and fallback/recovery semantics; its verification checklist records completion.

**Files:**
- Create: `src/ui/include/lumora/ui/DisplayMode.hpp`
- Create: `src/ui/include/lumora/ui/ComparisonViewport.hpp`
- Create: `src/ui/src/ComparisonViewport.cpp`
- Create: `tests/unit/ui/ComparisonViewportTests.cpp`
- Modify: `src/ui/include/lumora/ui/FramePresenter.hpp`
- Modify: `src/ui/src/FramePresenter.cpp`

**Interfaces:**
- Consumes: `FrameBundle` original/enhanced displays and `DisplayMode { Original, Enhanced, Compare }`.
- Produces: atomic mode switching and side-by-side viewports sharing one `ViewportTransform`.

- [ ] **Step 1: Write failing frame-ID and synchronized-transform tests**

```cpp
TEST(ComparisonViewport, RejectsMismatchedSourceFrames) {
    auto result = comparison.present(makeOriginalDisplay(10), makeEnhancedDisplay(11));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "comparison_frame_mismatch");
}
```

- [ ] **Step 2: Verify Compare implementation is missing**

Build `lumora_ui_tests`; expect failure.

- [ ] **Step 3: Implement one transform authority**

Comparison viewport owns a single transform calculated against each half's available bounds. Wheel, drag, Fit, and 100% update both images. Divider remains centered initially and is not draggable in v1.

- [ ] **Step 4: Update presenter mode logic**

Original uses `bundle.originalDisplay`, Enhanced uses `bundle.enhancedDisplay`, and Compare requires both. If enhancement failed/unavailable, Enhanced/Compare are disabled with an operator-safe reason and Original remains active.

- [ ] **Step 5: Run reference rendering tests**

Use a bundle with visibly distinct left/right ramps, verify same frame ID labels in diagnostic test mode, synchronized transforms and shared installation orientation, no aspect distortion, and paused mode holding both images with persistent PAUSED/timestamp/age indication.

- [ ] **Step 6: Commit comparison workflow**

```powershell
git add src/ui tests/unit/ui/ComparisonViewportTests.cpp
git commit -m "feat(ui): add synchronized original enhanced comparison"
```

### Task 4: Camera selection and safe settings dialog

The owner-authorized [Task 4A refined plan](2026-09-10-m09-camera-settings.md) and [camera-settings record](../../architecture/milestones/m09-camera-settings.md) implement stopped exposure/gain editing first, preserving the existing startup panel and preferences policy. The subsequent [Task 4B frame-rate continuation](../../architecture/milestones/m09-frame-rate.md#pr-21-integration) implements low-FPS watchdog correction and stopped numeric FPS editing, merged through PR #21 after Linux/Windows Debug/Release CI passed. The local [Task 4C ROI/format continuation](../../architecture/milestones/m09-format-roi.md) implements same-device resource rebinding, followed by [Task 4D camera profiles and installation orientation](../../architecture/milestones/m09-camera-profiles.md) at `10e1018`. Task 4D passes Linux Debug/Release 66/66, targeted sanitizers and supplemental native checks; Windows execution remains separate. The local [Task 4E camera controls continuation](../../architecture/milestones/m09-camera-controls.md) completes the compact panel and explicit capability access at `2be3a85`, including absent gain/read-only FPS, authoritative current readback and fingerprint-2 migration. The combined Task 4 implementation is locally verified on Linux; Windows, physical-camera and formal milestone acceptance remain separate. These Task 4A–4E records describe the historical Widgets parity baseline. As of 2026-09-15, the [QML completion record](../../architecture/milestones/qml-only-workstation.md) documents that workflow in the normal QML application; Task 5 is implemented, locally verified and integrated in QML as recorded below.

The historical Task 4 implementation extended the [M5 startup contract](../../architecture/milestones/m05-preflight.md#3-minimal-startup-ui-and-persistence): startup controls, typed saved preferences, capability comparison, confirmation/revision guards and asynchronous persistence. It expanded `CameraStartupPanel` without duplicating controller policy or stored camera records. The QML adapters now preserve those shared C++ contracts.

**Files:**
- Create: `src/ui/include/lumora/ui/CameraSettingsDialog.hpp`
- Create: `src/ui/src/CameraSettingsDialog.cpp`
- Create: `tests/unit/ui/CameraSettingsDialogTests.cpp`
- Modify: `src/ui/src/WorkstationController.cpp`
- Modify: `src/ui/include/lumora/ui/CameraStartupPanel.hpp` and `src/ui/src/CameraStartupPanel.cpp` (evolved in place by the authorized Task 4E refinement)
- Modify: `src/application/include/lumora/application/StartupPreferences.hpp`
- Modify: `src/application/include/lumora/application/AcquisitionWorker.hpp`
- Modify: `src/application/src/AcquisitionWorker.cpp`
- Modify: `src/application/src/LivePipeline.cpp`
- Modify: `src/configuration/src/StartupPreferencesService.cpp`
- Modify: `src/configuration/include/lumora/configuration/ApplicationConfiguration.hpp`
- Modify: `src/configuration/src/ConfigurationCodec.cpp`
- Modify: `tests/unit/configuration/ConfigurationStoreTests.cpp`
- Modify: `tests/unit/application/AcquisitionWorkerTests.cpp`
- Modify: `tests/integration/LivePipelineTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CameraDescriptor`, `CameraCapabilities`, `CameraConfiguration`, `AppliedCameraConfiguration`, and application commands.
- Produces: Discover/Refresh, Connect/Disconnect, Start/Stop, selected camera, capability-driven safe settings editing, and an administrator-managed stopped-state installation-orientation profile.

- [x] **Step 1: Write failing capability-driven control tests** (Task 4E retained behavioral RED)

Provide capabilities without gain and read-only FPS; assert gain controls are absent/disabled with explanation and FPS is shown read-only. Supply ROI increment 8 and assert width stepping follows 8.

- [x] **Step 2: Verify the missing behavior before implementation**

Task 4A introduced the dialog. Task 4E retained failing capability, controller and compact-layout tests against the existing types before implementation.

- [x] **Step 3: Implement compact main status and separate dialog** (Task 4E, Linux local verification)

Main view shows selected camera identity, active installation orientation, connection state, Live/Paused, and Connect/Disconnect/Start/Stop appropriate to state. Dialog edits format, ROI, FPS, exposure, and gain only when capability exists. First run requires explicit selection, configuration confirmation, and Start. Later runs may offer one-click Resume Live only for the unchanged stored identity/capabilities and never stream silently.

- [x] **Step 4: Implement stopped-state apply UX** (Task 4A–4E, Linux local verification)

The authorized Task 4A–4E refinements use explicit stopped editing: when acquisition is running, use Stop before changing settings; Apply submits the complete draft, then review actual readback, Confirm and Start. There is no automatic stop or restart. One Apply command carries the entire requested configuration; UI does not issue individual node writes. Flip/rotation changes require the application to be deliberately launched by an administrator, a stopped stream, explicit confirmation, and a preview showing that both Original and Enhanced will use the same orientation; they never transform native stored Original. The ordinary operator UI is read-only for this setting, and Lumora never silently self-elevates.

Extend M5's fixed-mode contract with a stopped-state resource-rebinding operation on the camera-owning worker for resolution changes. Quiesce old processing, provision checked replacement pools/exchanges, reset/rebind presentation and acknowledge the new context before restart; keep the same device instance and its source-ID sequence. Test a rejected request/allocation failure without partial activation, successful rebind, stale acknowledgement, and same-device ID continuity. A configuration or pool change must not quietly become a Disconnect/Connect that resets IDs.

- [x] **Step 5: Test errors and applied-value feedback** (automated simulator/policy coverage; native Windows and hardware acceptance remain open)

Cover no cameras, first-run confirmation, later Resume Live, identity/capability change requiring review, manual Disconnect suppressing reconnect, camera disappearance, unsupported saved profile, absent/invalid/mismatched Basler installation profile blocking Start, simulator identity-orientation fallback, orientation confirmation/stopped-state/admin enforcement, validation errors, quantized applied value, apply rollback, connection failure, and UI responsiveness using asynchronous command results.

- [x] **Step 6: Persist selected camera and per-serial profiles** (implemented and locally verified in Task 4D; Windows execution and milestone acceptance remain separate)

Extend the M5 schema-2 typed startup record into per-camera preferences keyed by vendor/model/serial; retain its last selected `CameraId`, requested/last-applied settings, and versioned canonical capability fingerprint. Supply sequential migration from schema 2 for any schema change and keep M5 first-run/Resume/drift/save-failure tests passing. Reuse the background persistence adapter; do not add file I/O to the UI or camera worker. Store the confirmed installation identity/orientation in a separate machine-profile schema/path adapter: `%PROGRAMDATA%\Lumora\Config` on Windows with admin-write/operator-read ACLs and an injected system root on Linux tests. On discovery, validate the saved request and installation identity/capability fingerprint; require operator Apply/review when either is no longer valid.

- [x] **Step 7: Commit camera UI** (local source `2be3a85`; no publication or merge)

```powershell
git add src/ui src/application src/configuration tests/unit/ui/CameraSettingsDialogTests.cpp tests/unit/configuration/ConfigurationStoreTests.cpp tests/unit/application/AcquisitionWorkerTests.cpp tests/integration/LivePipelineTests.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): add capability-driven camera controls"
```

### Task 5: Fullscreen, collapsible sidebar, and persisted UI preferences

**Implemented, locally verified and integrated in QML (2026-09-15).** The [refined implementation plan](2026-09-15-qml-layout.md) replaces the original Widgets file list and mechanics. The [milestone record](../../architecture/milestones/qml-layout.md) tracks source-bound evidence, review and remaining gates.

**Implemented files:**

- `src/application/include/lumora/application/UiPreferences.hpp` and UI status additions in `StartupPreferences.hpp`
- `src/configuration/include/lumora/configuration/UiPreferencesCodec.hpp`, `src/configuration/src/UiPreferencesCodec.cpp`, and the existing `StartupPreferencesService.hpp/.cpp`
- `src/qml/LayoutAdapter.hpp/.cpp` and `QmlWorkstation.hpp/.cpp`
- `src/qml/qml/Main.qml`, `StatusStrip.qml`, `CameraStartup.qml` and processing editor input-cancellation bindings
- `tests/unit/configuration/UiPreferencesTests.cpp`, `StartupPreferencesTests.cpp`, `tests/integration/QmlLayoutAdapterTests.cpp` and `QmlWorkstationTests.cpp`

**Interfaces:**

- `LayoutAdapter` observes the existing QQuickWindow, exposes panel/fullscreen/diagnostics state and warnings, handles F11/Escape, and emits `layoutChanging` before focus or visibility transitions.
- `UiPreferences` stores ordinary geometry, `panelsCollapsed`, maximized/fullscreen preferences and diagnostics visibility. `postUiSave` and dirty-field `postUiUpdate` use the existing asynchronous document writer with independent UI revisions.
- One ApplicationWindow and ViewerSurface retain image ownership and transforms. Mandatory evaluation, acquisition/error, paused/stale/timestamp/age, orientation and processing status remain visible alongside priority Stop/Disconnect controls.

- [x] **Step 1: Write failing fullscreen safety tests**

Behavioral RED tests cover panel/fullscreen transitions, same-surface paused Compare identity and transform, required status, Escape with an open popup, fallback/disconnection, and close/reopen persistence. Layout transitions cancel uncommitted editor input without implying processing edits, Apply, Confirm or Start.

- [x] **Step 2: Implement UI state transitions**

One remembered panel choice covers camera and processing panels. Fullscreen temporarily hides both without changing that choice; exit restores the previous ordinary/maximized state. F11 and a visible action toggle fullscreen; Escape exits it, while viewer-local F remains Fit. The viewport is neither replaced nor reparented.

- [x] **Step 3: Persist and recover preferences**

The version-1 `ui.layout` codec preserves unrelated keys and configuration sections. Partial updates merge after loading, retaining untouched saved fields even when shutdown precedes load completion. Unsafe documents prevent writes; future layout versions block UI writes. Ordinary geometry is saved only outside fullscreen/minimized states and validated against available screens. Invalid/inaccessible geometry falls back to centered 1280×800 bounded by available space and the window minimum. Debounced updates flush before the existing worker drain.

- [x] **Step 4: Exclude unready recording UI**

No Record action is introduced. Diagnostics hides only optional detail; required status remains visible in ordinary, collapsed and fullscreen presentation.

- [ ] **Step 5: Run full UI/integration suite and manual resolutions**

Final Linux Debug and Release CTest each pass **66/66** on the source snapshot beginning `0c40`. All six native software/threaded OpenGL/DPR 2 scene configurations pass 28/28. The installed application passes normal close and second-process layout restoration. Windows execution and the manual Windows 1280×720, 1920×1080, 2560×1440, 100/125/150% scaling matrix remain pending. Translation readiness and critical text independent of color remain verification requirements; Linux results do not close the Windows or milestone gates.

- [x] **Step 6: Commit workstation UI completion**

Local implementation `5f6b82f` is integrated into main. The linked milestone binds source, tests and reviews. No push or formal M9 acceptance is claimed here.

## Milestone 9 acceptance gate

- [ ] Presets validate, apply atomically, persist, and use neutral non-clinical descriptions.
- [ ] Manual control changes choose Custom; release value is exact and drag updates are bounded.
- [ ] Original/Enhanced/Compare use one matching frame ID and preserve viewport state.
- [ ] Camera settings are capability-driven and contain no pylon node names.
- [ ] Fullscreen retains essential acquisition/error status and Escape exit.
- [ ] Evaluation, paused/stale, and orientation indications remain visible in normal, Compare, and fullscreen presentation.
- [ ] First/later-run startup and manual-disconnect behavior match the approved explicit workflow.
- [ ] No v1 production or evaluation UI exposes an unimplemented Record action.
