# QML fullscreen, panels and window preferences

> **For agentic workers:** Use superpowers:subagent-driven-development. Root alone runs serial builds, tests and apps, with at most three compiler jobs.

**Goal:** Complete M9 Task 5 in QML after integrating the verified migration into local main.

**Authority:** The owner approved the recommended main integration followed by fullscreen/sidebar work. PRD sections 19.11–19.12 require remembered panel state and continuously visible acquisition/error/evaluation indications. The original M9 Task 5 also specifies valid ordinary geometry, maximized/fullscreen preferences and diagnostics visibility. Windows execution remains a separate environment-dependent gate; no push is authorized.

**Architecture:** Keep one ApplicationWindow and ViewerSurface attached to the existing C++ sink. A workstation-owned LayoutAdapter handles actual window state, keyboard transitions, geometry validation and deferred persistence. Typed UI preferences join the existing single asynchronous document writer; never introduce QSettings or a competing writer. Main.qml binds panel visibility and retains a compact persistent command/status area.

**Spec:** `prd.md` sections 19.11–19.12 and `docs/superpowers/plans/2026-04-25-m09-presets-workstation-ui.md` Task 5.

**Development status:** Implementation and focused development checks are recorded below. Final source-bound builds, suites, native scenes and staged execution pass. Local implementation `5f6b82f` is integrated into main; Task 4 and the [milestone record](../../architecture/milestones/qml-layout.md) preserve evidence and external gates. The preceding QML migration integration was verified separately at `3cca2c0`; that result is not feature verification.

## Constraints and decisions

- Existing camera admission, processing pipeline, pixel ownership and renderer retirement remain unchanged.
- One panelsCollapsed choice covers the camera and processing panels. Fullscreen temporarily hides both without altering that saved choice. Exiting restores prior windowed/maximized state.
- F11 toggles fullscreen; Escape exits it even with a popup open; an explicit visible Exit action is also provided. F remains Fit when the viewer is focused.
- Keep evaluation, acquisition/disconnection, paused/stale, frozen timestamp/age, active orientation, critical errors, processing fallback, Stop and Disconnect visible. Auto-hide is optional and omitted.
- Layout transitions cancel uncommitted numeric editor input before hiding controls/moving focus. No Apply, Confirm, Start or processing edit is implied by layout changes.
- Persist normal geometry only while windowed, debounce resize/move saves, flush before existing preference-worker drain, validate against available screens, and fall back to centered 1280×800 bounded by available geometry. Preserve negative monitor coordinates. Never store fullscreen/minimized transient bounds as ordinary geometry.
- Preserve unrelated JSON UI keys, camera profiles, presets and machine profiles. Unsafe whole-document loads prevent all writes; unknown future layout versions prevent UI writes. Missing/current-version malformed layout data may recover to defaults with a visible warning, and only deliberate subsequent UI changes replace known fields.
- Diagnostics visibility controls only optional detail; required status is never hidden. No Record action is introduced.
- Reuse the retained QML worktree for feature implementation after the main fast-forward. Preserve all older QA and the differing untracked main plan (backed up under `out/qa/qml-main-integration`).

## Task 1 — Typed UI preferences and the existing writer

Owned files: new `src/application/include/lumora/application/UiPreferences.hpp`, existing application StartupPreferences status, configuration service/codec files and configuration tests. Build registration remains root-owned.

Interface: `application::WindowGeometry {int x,y,width,height;}`; `application::UiPreferences { optional<WindowGeometry> normalGeometry; bool panelsCollapsed, maximized, fullscreen, diagnosticsVisible; }`, with default false flags and structural equality. Service adds `postUiSave(uint64_t, UiPreferences)`. Status adds initial `loadedUiPreferences`, `uiPreferencesWritable`, `uiWarning`, and independently tracked `latestAttemptedUiSaveRevision` / `latestSavedUiRevision`.

The implemented service also accepts `postUiUpdate(uint64_t, UiPreferencesUpdate)`. Each update field is optional: absent fields preserve loaded values; a present empty geometry explicitly clears the stored rectangle. The adapter submits only dirty fields. This prevents edits flushed before a delayed initial load from replacing untouched preferences with adapter defaults.

- [x] Add compile-ready stubs and failing behavioral tests for codec recovery/unknown keys, interleaved camera/preset/UI writes, independent coalescing/revisions, failed/unsafe loads and shutdown drain.
- [x] Root runs focused RED and confirms failures reach the new behavior.
- [x] Implement a bounded third pending section in the same worker; merge known `ui.layout` version-1 fields into the loaded document and track durable whole-document saves accurately.
- [x] Run existing plus new configuration tests and review preservation/failure semantics during development; add dirty-field updates for delayed-load preservation. Final corrected-source reruns pass and are recorded in Task 4.

## Task 2 — Window layout adapter

Owned files: `src/qml/LayoutAdapter.hpp/.cpp`, `tests/integration/QmlLayoutAdapterTests.cpp`.

Consumes the exact Task 1 types/status/service. Exposes properties `panelsCollapsed`, `fullscreen`, `diagnosticsVisible`, `ready`, `warning`; methods `attachWindow(QQuickWindow*)`, `setPanelsCollapsed(bool)`, `toggleFullscreen()`, `exitFullscreen()`, `setDiagnosticsVisible(bool)`, `refresh()`, `prepareShutdown()`. Emits `stateChanged` and `layoutChanging` before transitions. The adapter borrows the existing preferences service and owns no pixels.

- [x] Add compile-ready stubs and failing tests using real QQuickWindow plus temporary/controlled service: delayed-load edits, safe screen placement, negative coordinates, fullscreen/maximized restore, ordinary geometry, debounce/close drain, F11/Escape and rejected saves.
- [x] Root verifies RED; implement minimal state restoration, event filtering and service submission. No filesystem work on the GUI thread.
- [x] Complete final corrected-source adapter verification and review closure for lifecycle, screen geometry and save authority.

## Task 3 — QML controls and whole-scene behavior

Root owns `QmlWorkstation`, Main.qml, StatusStrip/CameraStartup/processing input cancellation, CMake and scene tests. Register the adapter, refresh it from existing polling and prepare its save before coordinator shutdown. QML attaches the existing root window during completion.

- [x] Add failing real-scene tests for collapsed/fullscreen transitions, retained paused Compare source and transform, essential status and priority commands, cancellation with numeric focus/popup, fallback/disconnection visibility, minimum geometry and reopening saved layout.
- [x] Implement header panel/fullscreen actions and persistent priority controls without replacing/reparenting the image surface. Close editor popups and clear unfinished processing input on `layoutChanging`, then focus the retained viewer. Keep processing errors/status visible when both panels are hidden, while preserving ordinary editor viewport geometry.
- [x] Exercise the focused new scenes during development, including managed-window fullscreen behavior; add no Record UI.
- [x] Complete the final native matrix and all existing scene/runtime regressions in Task 4.

## Task 4 — Verification and integration record

- [x] Fresh main Debug/Release normal builds, lint and full suites after migration fast-forward, verified separately at `3cca2c0`. Evidence remains under `out/qa/qml-main-integration`; it does not verify this feature.
- [x] Final feature Debug/Release builds and full suites; native software/threaded OpenGL/DPR 2 scene checks, inspected minimum-size/fullscreen/collapsed/paused/fallback states.
- [x] Fresh isolated staged application: actual keyboard/pointer layout changes, normal close and second-process restored preferences, no automatic acquisition, unchanged camera/preset sections, loader/import isolation.
- [x] Independent source and evidence reviews, source-bound completion documentation and local commits. Preserve worktrees; no push or Windows/hardware claim.
