# Checkpoint 4: integrated QML SIM-LIVE workstation

> Execute in the retained `codex/qml-foundation` worktree. The owner approved the migration design and implementation; this plan refines its already-authorized Checkpoint 4. Use subagent-driven development with focused tests and independent review.

**Baseline:** `916ca7d`, following reviewed renderer source `60b7542`. Main remains `e4520dd`.

**Implementation:** `146f10a`; [verified completion record](../../architecture/milestones/qml-live.md).

**Goal:** Make the opt-in QML application a usable SIM-LIVE Stage One workstation with guarded camera startup, completed-frame viewing and exact window/level editing. Retain shared C++ policy, processing algorithms, pools, schemas and frame ownership.

**Architecture:** Application composition owns the clock, simulator, installation/preferences services and LivePipeline. A frontend QObject runtime owns WorkstationCoordinator and typed Camera/Processing/Viewer adapters. The runtime owns the shared FramePresenter and clock-injected QuickImageItem. A QML-created typed image host attaches that application-owned sink without transferring QObject ownership. Required typed root properties receive the adapters; QML never receives pixels, request/generation/revision authority or session contexts.

## Constraints and rulings

- Reuse `SimulatorComposition`, CameraActionPolicy, WorkstationCoordinator, ProcessingControlsModel, FramePresenter and QuickImageItem. No application/core/processing GUI dependencies, second camera policy or duplicate persistence loop.
- Select → Connect → Apply → review → Confirm → Start remains explicit. No automatic stream. Priority Stop/Disconnect remain accessible during pending ordinary work. Saved Resume is explicit and uses shared eligibility.
- Read-only requested/current camera configuration and orientation now; full camera/preset/installation editors, fullscreen/layout preferences, Capture and default launcher replacement are later stages.
- Isolated stable application identity `LumoraQmlPilot`; tests use explicit temporary ConfigurationStore paths. Reuse existing codecs/schema and installation authority.
- Reserve renderer current/replacement CPU images plus nominal textures for Compare at the fixed simulator maximum 640×480: 9,830,400 bytes. Checked arithmetic; use default processor factory and existing `externalSessionStorageBytes` so complete admission includes raw/processing/display/executor storage. This is requested storage, not measured driver/RSS overhead. Do not generalize this fixed SIM envelope to hardware.
- Exact handoff IDs remain C++. Retire/reset once per handoff, drain events, acknowledge only real retirement. Retain candidates across failed acknowledgments. Closing disables ordinary admission but keeps the event loop/window/sink alive through actual retirement, then completes coordinator shutdown. No timeout masquerading as release.
- Project pre-ticket renderer initialization errors independently. Mandatory evaluation, camera state, waiting/pausing/paused/stale, orientation, completed source and errors remain visible outside the dedicated pixel surface. No image effects, QML image copies or second orientation transform.
- Window [1,65535], level [0,65535], native double precision. Only mutate the existing window/level stage; retain all other pipeline data. Commit/Drag/Release map directly to the model; coordinator owns submission/completion/save. Draft, active acknowledgment and persistence failure are separate facts.
- Qt 6.11.1 matching official SDK is used with non-Qt vcpkg dependencies. Debug app uses Release Qt libraries. Native GL here is llvmpipe. No Windows, physical GPU, hardware or performance acceptance claims.

## Task 1: runtime, camera/viewer adapters and owned image host

**Own:** new `src/qml/{QmlWorkstation,CameraAdapter,ViewerAdapter,ViewerSurface}.{hpp,cpp}`, `tests/integration/QmlRuntimeTests.cpp`. Root owns CMake wiring and app main. No processing algorithms or renderer changes unless a demonstrated defect requires a scoped ruling.

- [x] Write focused runtime tests first using actual simulator/pipeline/preferences and QQuickWindow, temporary settings and injected clock. Prove no automatic start and direct commands cannot skip policy; verify full startup and camera readback.
- [x] Implement typed QObject adapters (QML uncreatable), camera descriptor list/read-only review data, explicit commands and policy-derived action visibility/enabled state. Return command success and expose errors; no JavaScript policy.
- [x] Implement application-owned sink and typed QML `ViewerSurface` attachment with logical geometry. Clock and sink outlive render cleanup and QML host destruction. Publish completed mode, availability, pause/freshness/orientation/renderer error; validate numeric zoom/pan at the boundary.
- [x] Implement runtime `start`, `poll`, `requestShutdown`, `shutdownComplete`; async retirement handoffs and closing must keep draining. Runtime borrows services; executable owns their final stop/join after shutdown signal. Expose coordinator to C++ composition/tests only so ProcessingAdapter can share it.
- [x] Provide checked SIM renderer reserve helper using assessStorage Compare maximum; test real default-factory resource admission including this reserve, and low-budget rejection.
- [x] Verify native startup, same-bundle Compare, Pause, context replacement and visible/hidden/host-deleted/never-shown shutdown. Record exact focused commands and remaining integration cases in report.

## Task 2: processing adapter and exact activation/persistence

**Own:** new `src/qml/ProcessingAdapter.{hpp,cpp}`, `tests/integration/QmlProcessingAdapterTests.cpp`. Borrow `WorkstationCoordinator&`; expose `refresh()` for runtime publication. This task is independent of Task 1's source files.

- [x] Write tests around real coordinator/model with temporary preferences: late loading, exact fractional window/level, invalid/nonfinite input, unchanged release flush, successful activation/acknowledged save/reopen and failed activation never saved.
- [x] Implement QML-uncreatable typed QObject with read-only draft values/text, enabled/pending/load/error/persistence status, acknowledged active summary and fallback/retry state. Exact decimal parsing occurs in C++; text commit must not round the native double.
- [x] Commands edit only existing WindowLevelParameters in current activePipeline and use ProcessingEditPhase. Do not call takeSubmission, pipeline activation or preference saves. Retry delegates to coordinator.
- [x] Preserve non-window/level stages and paused-frame semantics; report tests and integration expectations. Builds are serialized with Task 1/root.

## Task 3: real QML composition and control integration

**Own (root unless delegated):** `src/qml/main.cpp`, `src/qml/CMakeLists.txt`, top-level option wording, `tests/CMakeLists.txt`, QML `Main`, `CameraStartup`, `ViewingToolbar`, `WindowLevelControls`, `StatusStrip`; replace `tests/qml/ThemeSmokeTests.cpp` preview assertions and add `tests/integration/QmlWorkstationTests.cpp` as needed.

- [x] Register types through the static QML module and inject required adapter properties. Compose production simulator/services/pipeline with admitted renderer reserve, isolated pilot identity and async close. Application quit follows retirement completion; handle engine-load failure.
- [x] Build image-dominant responsive shell at 900×600 and1280×800, accessible names/focus and scrolling readback/adjustments. Show actual commands, requested/current review and explicit Confirm. Remove unsupported decorative controls.
- [x] Bind completed Original/Enhanced/Compare and truthful Pause/Resume; Fit/100%/zoom/pan use C++. Scope Space/F/1/+/- shortcuts to viewport, preserving numeric typing/button activation. Keep indications unobscured.
- [x] Bind native double numeric fields and sliders; polling must not overwrite active text input. Drag/release use exact model phases; display acknowledged active processing independently.
- [x] Exercise actual QML controls through Qt Test: guarded startup and direct negative calls, Stop/Disconnect pending continuation, readback, modes/pause/edit/stop/restart, errors/fallback/retry, saved explicit Resume and close with outstanding rendering. Reuse existing lower-layer tests instead of duplicating every policy permutation.

## Task 4: staged runtime, regression evidence and documentation

**Own:** deployment section `src/qml/CMakeLists.txt` (after Task 3), scoped staging checker if needed, `docs/architecture/milestones/qml-live.md`, Progress/build guides/Stage One status. Tests added only for missing meaningful behavior found by review.

- [x] Add opt-in app install plus official Qt QML/runtime deployment script and staged qt.conf. Stage outside source/build trees, run from neutral cwd with development import/plugin/library paths removed. Trace dependencies/imports to stage or OS, including static application QML and Basic Controls plugins. Check existing licensing inventory for shipped runtime.
- [x] Independent spec+quality review of all Checkpoint 4 changes from916ca7d, including lifetime/admission/persistence and real control tests. Resolve findings and rereview scoped fixes.
- [x] Final Debug/Release full builds/tests and all_qmllint; native Widgets regression, Quick software/OpenGL basic/threaded/DPR2 and live QML tests. Capture and inspect waiting/review/live/paused/fallback where feasible at both sizes. Record first failure separately if an inherited intermittent timeout appears.
- [x] Source-bind evidence with commit/source hashes, commands/results/captures and staged dependency traces. Document limitations (Release SDK, software GL, outstanding native Windows/hardware/performance gates), update progress and launch guides. Commit local reviewed implementation/evidence; retain worktree/QA; do not merge or push.

## Verification commands and gates

Configure each `linux-gcc-{debug,release}-sim-qml` preset with explicit prefix `.tools/qt-official;/home/mo/code/Lumora/out/vcpkg_installed/x64-linux-dynamic`. Build with at most three jobs; never concurrent configure/Ninja. Use registered CTest environments for headless tests. Native runs use Xvfb with matching official Qt platform plugin and temporary XDG cache. All successful commands must be recorded with source identity. Existing 75-test and native renderer evidence is baseline only, not proof of new integration.

Exit requires complete real live route, exact persistence and owner-release evidence, real QML controls and staged runtime. Full Stage Two editors and deferred external acceptance remain open. A standalone adapter or first image alone does not finish Checkpoint 4.
