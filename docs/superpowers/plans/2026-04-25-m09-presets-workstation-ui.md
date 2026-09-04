# Milestone 9 Presets and Workstation UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the complete English, localization-ready evaluation interface for explicit camera startup, Original/Enhanced/Compare presentation, fixed-order processing controls, installation orientation, presets, fullscreen, and persisted layout preferences.

**Architecture:** Qt views bind to presentation models and publish complete application commands. Presets are versioned pipeline definitions; UI edits are coalesced but final values are exact.

**Tech Stack:** C++20, Qt 6 Widgets/Core/Test, existing application/processing/configuration modules, GoogleTest/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

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

**Files:**
- Create: `src/application/include/lumora/application/Preset.hpp`
- Create: `src/application/include/lumora/application/PresetRepository.hpp`
- Create: `src/application/src/PresetRepository.cpp`
- Create: `config/default-presets.json`
- Create: `tests/unit/application/PresetRepositoryTests.cpp`
- Modify: `src/configuration/include/lumora/configuration/ApplicationConfiguration.hpp`
- Modify: `src/configuration/src/ConfigurationCodec.cpp`

**Interfaces:**
- Consumes: `PipelineDefinition`, `makeStandardPipelineDefinition()`, configuration schema, and built-in JSON resource.
- Produces: `PresetId`, `Preset { id, name, description, builtIn, pipeline }`, `PresetRepository::list/find/apply/saveCustom/deleteCustom`, and typed preset persistence.

- [ ] **Step 1: Write failing built-in and Custom-transition tests**

```cpp
TEST(PresetRepository, BuiltInsAreImmutableAndEditsBecomeCustom) {
    auto repository = loadDefaultPresets();
    auto standard = repository.find(PresetId{"standard"}).value();
    EXPECT_TRUE(standard.builtIn);
    EXPECT_FALSE(repository.deleteCustom(standard.id).hasValue());
    auto edited = standard.pipeline;
    setGamma(edited, 1.4);
    EXPECT_EQ(repository.classify(edited), PresetId{"custom"});
}
```

- [ ] **Step 2: Verify missing repository fails**

Build `lumora_application_tests`; expect failure.

- [ ] **Step 3: Define complete built-in presets**

Create Original, Standard, High Contrast, Soft Detail, and Custom. Original disables all optional enhancement stages while retaining Normalize and WindowLevel. Standard must normalize equal to `makeStandardPipelineDefinition()`. Every file entry includes schema version, order version, stable ID, neutral description, fixed canonical stage order, enabled flags, and all parameter values. Reordered definitions are invalid.

- [ ] **Step 4: Implement parsing and classification**

Validate built-ins through `PipelineCompiler`. Compare normalized pipeline values, not display labels, when classifying a definition. User presets use distinct IDs and may duplicate parameters but not IDs.

- [ ] **Step 5: Test round-trip and invalid preset isolation**

Cover missing stage, out-of-range value, unknown future stage, duplicate ID, built-in deletion/overwrite, custom save/delete, and schema migration. Invalid custom entries are reported and skipped without losing valid entries.

- [ ] **Step 6: Commit presets**

```powershell
git add src/application src/configuration config/default-presets.json tests/unit/application/PresetRepositoryTests.cpp
git commit -m "feat(presets): add validated processing bundles"
```

### Task 2: Processing controls and coalesced configuration publication

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

**Files:**
- Create: `src/ui/include/lumora/ui/CameraPanel.hpp`
- Create: `src/ui/include/lumora/ui/CameraSettingsDialog.hpp`
- Create: `src/ui/src/CameraPanel.cpp`
- Create: `src/ui/src/CameraSettingsDialog.cpp`
- Create: `tests/unit/ui/CameraSettingsDialogTests.cpp`
- Modify: `src/ui/src/WorkstationController.cpp`
- Modify: `src/configuration/include/lumora/configuration/ApplicationConfiguration.hpp`
- Modify: `src/configuration/src/ConfigurationCodec.cpp`
- Modify: `tests/unit/configuration/ConfigurationStoreTests.cpp`

**Interfaces:**
- Consumes: `CameraDescriptor`, `CameraCapabilities`, `CameraConfiguration`, `AppliedCameraConfiguration`, and application commands.
- Produces: Discover/Refresh, Connect/Disconnect, Start/Stop, selected camera, capability-driven safe settings editing, and an administrator-managed stopped-state installation-orientation profile.

- [ ] **Step 1: Write failing capability-driven control tests**

Provide capabilities without gain and read-only FPS; assert gain controls are absent/disabled with explanation and FPS is shown read-only. Supply ROI increment 8 and assert width stepping follows 8.

- [ ] **Step 2: Verify dialog types are missing**

Build `lumora_ui_tests`; expect failure.

- [ ] **Step 3: Implement compact main status and separate dialog**

Main view shows selected camera identity, active installation orientation, connection state, Live/Paused, and Connect/Disconnect/Start/Stop appropriate to state. Dialog edits format, ROI, FPS, exposure, and gain only when capability exists. First run requires explicit selection, configuration confirmation, and Start. Later runs may offer one-click Resume Live only for the unchanged stored identity/capabilities and never stream silently.

- [ ] **Step 4: Implement stopped-state apply UX**

If the application reports a setting requires stopping, the dialog clearly states that Apply will perform stop/apply/verify/restart. One Apply command carries the entire requested configuration; UI does not issue individual node writes. Flip/rotation changes require the application to be deliberately launched by an administrator, a stopped stream, explicit confirmation, and a preview showing that both Original and Enhanced will use the same orientation; they never transform native stored Original. The ordinary operator UI is read-only for this setting, and Lumora never silently self-elevates.

- [ ] **Step 5: Test errors and applied-value feedback**

Cover no cameras, first-run confirmation, later Resume Live, identity/capability change requiring review, manual Disconnect suppressing reconnect, camera disappearance, unsupported saved profile, absent/invalid/mismatched Basler installation profile blocking Start, simulator identity-orientation fallback, orientation confirmation/stopped-state/admin enforcement, validation errors, quantized applied value, apply rollback, connection failure, and UI responsiveness using asynchronous command results.

- [ ] **Step 6: Persist selected camera and per-serial profiles**

Extend typed per-user configuration with last selected `CameraId` and preferences keyed by vendor/model/serial. Store requested and last-applied settings there. Store the confirmed installation identity/orientation in a separate machine-profile schema/path adapter: `%PROGRAMDATA%\Lumora\Config` on Windows with admin-write/operator-read ACLs and an injected system root on Linux tests. On discovery, validate the saved request and installation identity/capability fingerprint; require operator Apply/review when either is no longer valid.

- [ ] **Step 7: Commit camera UI**

```powershell
git add src/ui src/configuration tests/unit/ui/CameraSettingsDialogTests.cpp tests/unit/configuration/ConfigurationStoreTests.cpp
git commit -m "feat(ui): add capability-driven camera controls"
```

### Task 5: Fullscreen, collapsible sidebar, and persisted UI preferences

**Files:**
- Create: `src/ui/include/lumora/ui/UiPreferences.hpp`
- Create: `src/ui/src/FullscreenController.cpp`
- Create: `tests/unit/ui/FullscreenControllerTests.cpp`
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `src/configuration/include/lumora/configuration/ApplicationConfiguration.hpp`
- Modify: `src/configuration/src/ConfigurationCodec.cpp`

**Interfaces:**
- Consumes: Qt window state, UI preference section, and camera/error presentation state.
- Produces: sidebar collapse, fullscreen enter/exit, Escape handling, persistent evaluation/paused/stale/orientation/critical-status overlays, and validated window-geometry persistence.

- [ ] **Step 1: Write failing fullscreen safety tests**

Enter fullscreen, assert sidebar/footer hidden, `EVALUATION — NOT FOR CLINICAL USE`, Live/Error, PAUSED timestamp/age, STALE IMAGE / NOT LIVE, and active orientation remain visible when applicable; Escape exits and a camera-disconnected update remains visible.

- [ ] **Step 2: Implement UI state transitions**

Do not reparent/destroy the viewport during fullscreen. Save ordinary window geometry only when valid/on-screen. Collapsing sidebar expands viewer and does not change pipeline/viewport transform.

- [ ] **Step 3: Persist and migrate preferences**

Store sidebar collapsed, window geometry, maximized/fullscreen preference, and diagnostics visibility. On invalid/off-screen geometry, center a default 1280x800 window on the primary screen.

- [ ] **Step 4: Exclude unready recording UI**

Do not include a Record widget/action in v1 production or evaluation compositions. Recording experiments use non-shipping harnesses until a separate specification is approved. Assert UI object discovery and menus contain no Record action.

- [ ] **Step 5: Run full UI/integration suite and manual resolutions**

Test offscreen automation on Linux and Windows CI plus manual Windows 1280x720, 1920x1080, 2560x1440 and scaling 100/125/150%. Verify all English strings use Qt translation facilities and critical text is understandable without color.

- [ ] **Step 6: Commit workstation UI completion**

```powershell
git add src/ui src/configuration tests/unit/ui
git commit -m "feat(ui): complete workstation presentation modes"
```

## Milestone 9 acceptance gate

- [ ] Presets validate, apply atomically, persist, and use neutral non-clinical descriptions.
- [ ] Manual control changes choose Custom; release value is exact and drag updates are bounded.
- [ ] Original/Enhanced/Compare use one matching frame ID and preserve viewport state.
- [ ] Camera settings are capability-driven and contain no pylon node names.
- [ ] Fullscreen retains essential acquisition/error status and Escape exit.
- [ ] Evaluation, paused/stale, and orientation indications remain visible in normal, Compare, and fullscreen presentation.
- [ ] First/later-run startup and manual-disconnect behavior match the approved explicit workflow.
- [ ] No v1 production or evaluation UI exposes an unimplemented Record action.
