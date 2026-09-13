# QML Stage One Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce an optional QML simulator workstation through four independently reviewable checkpoints, beginning with a small build/theme preview.

**Architecture:** Keep acquisition, processing, configuration and immutable frames in their existing C++ modules. Extract shared presentation policy above `LivePipeline`; Widgets and typed QML adapters use that same implementation. A C++ Quick image item implements a bounded asynchronous rendering contract.

**Tech Stack:** C++20, CMake 3.28+, pinned Qt 6.11.1 modules, Qt Quick Controls Basic, GTest, Qt Test/QuickTest.

**Spec:** [Qt Quick/QML migration design](../specs/2026-09-13-qt-quick-qml-migration-design.md).

**Status:** Checkpoints 1 and 2 are implemented locally on `codex/qml-foundation`, continuing from `e4520dd`. See the [foundation record](../../architecture/milestones/qml-foundation.md) and [shared-policy record](../../architecture/milestones/qml-shared-policy.md) for source-bound verification and remaining limits. Checkpoints 3–4 remain planned. The preview does not complete the integrated Stage 1 workstation.

## Global constraints

- “No automatic stream starts.” Preserve select → Connect → Apply → review → Confirm → Start; use Stop before reconfiguration. Explicit eligible saved-settings Resume Live retains its existing guarded continuation.
- “Both frontends should use the same C++ policy implementation”; they are alternative application runs, not concurrent consumers of one live session.
- “application/core/processing targets must not gain QML, Quick or Widgets dependencies.”
- “Reuse user schema 5, machine schema 2 and capability fingerprint 2.”
- “Keep request/session/revision bookkeeping in C++.”
- “Do not apply orientation or a second tonal adjustment in QML.”
- “A texture upload must not be described as zero-copy.”
- Preserve same-bundle Compare, exact processing release commits, authoritative readback, installation authority, acknowledged persistence and priority Stop/Disconnect.
- Preserve the evaluation overlay, English/localization-ready strings, keyboard behavior, 900×600 minimum and 1280×800 default. Later native Windows checks cover 100%, 125%, 150% and 200% scaling.

---

## Planning baseline architecture and evidence

| Module | Current responsibility | Migration treatment |
|---|---|---|
| `src/app` | Owns clock, SIM-LIVE provider, installation/preferences workers, pipeline and Widgets lifetime. Simulator requests 640×480 Mono12 in UInt16 at 30 FPS. | Separate QML composition reuses `SimulatorComposition.cpp` and existing services. |
| `src/core`, `src/camera` | Immutable pooled frames; newest-value slots; camera interfaces and simulator. Camera operations are confined to acquisition worker. | Preserve pixel storage and camera contracts. Basler hardware integration remains outside this pilot. |
| `src/application` | Control thread, acquisition/processing workers, camera state machine and priority mailbox, session preparation/rebinding, processing outcomes. | Consume `LivePipeline` commands/snapshots; keep admission distinct from completion. |
| `src/processing` | Native normalization to canonical U16, prepared tone/detail stages, terminal Gray8 mapping, common orientation and paired output. | Reuse algorithms and resource admission without altering output. |
| `src/configuration` | Typed codecs, background preference writes and separate machine installation authority. | Reuse schemas/services with an isolated QML development preference location. |
| `src/ui` | Widgets, startup/persistence coordinator, processing draft model, presenter and synchronous paint receipts. | Extract shared policy; retain Widgets controls and renderer as regression coverage. |

This table describes the pre-extraction architecture at `e4520dd`; the current shared ownership is recorded in Checkpoint 2 and its milestone record.

Useful seams are `WorkstationController.cpp`, `CameraStartupPanelPresentation` and the action eligibility in `CameraStartupPanel.cpp`. `ProcessingControlsModel`, `ViewportTransform`, `DisplayMode`, `WorkstationStatus` and camera draft normalization are already independent of QWidget in their behavior. Merely wrapping the current controller in a QObject would still pull Widgets into QML.

The live path is acquisition → newest raw slot → processing → immutable `FrameBundle` → newest bundle slot → presenter → viewport. The bundle contains Raw/Original and an optional complete Enhanced pair. Compare validation and retention must remain atomic across both displayed planes.

The [progress log](../../PROGRESS.md#local-main-integration-2026-09-13) records 68/68 Debug and Release headless groups after local integration, plus native camera UI checks. These are recorded results, not tests rerun while writing this plan. Existing lifecycle/context-retirement intermittency, native Windows/hardware acceptance and the 2048 processing performance shortfall remain separate.

## Checkpoint 1: Optional build and visible theme preview

**Files:** Modify `CMakeLists.txt`, `cmake/Dependencies.cmake`, `vcpkg.json`, `CMakePresets.json`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, and both build guides. Create `src/qml/CMakeLists.txt`, `src/qml/main.cpp`, `src/qml/qml/Main.qml`, `src/qml/qml/Theme.qml`, and `tests/qml/ThemeSmokeTests.cpp`.

**Deliverable:** `lumora_qml_app`, clearly labeled “Interface preview”, with an image-dominant dark layout, waiting state and mandatory evaluation indication. This batch creates no camera commands or frame transport.

- [x] Verify a usable vcpkg checkout and the baseline's `qtdeclarative` feature names before changing the manifest. The main checkout currently has installed Widgets libraries but no `.tools/vcpkg` checkout or Qml/Quick modules. Its working caches use `CMAKE_PREFIX_PATH`; they do not establish a Quick toolchain.
- [x] Add `LUMORA_BUILD_QML_UI` defaulting OFF and a manifest feature `qml-ui`. Resolve declarative/Quick Controls and their transitive Qt modules at versions matching pinned qtbase 6.11.1. Report dependency mismatch explicitly rather than mixing installed versions.
- [x] Add opt-in Linux/Windows simulator presets ending in `-qml`, with `VCPKG_MANIFEST_FEATURES=qml-ui`, separate build directories and a separate QML dependency installation directory. Retain existing target names and original presets.
- [x] Under the option, find `Qml`, `Quick`, `QuickControls2`; find `Test` and `QuickTest` for enabled QML tests. Use `QGuiApplication`, `QQmlApplicationEngine`, compiled resources and `qt_add_qml_module` with URI `Lumora.Workstation`. Use generated type information, normal QML cache generation and its generated lint target.
- [x] Add Basic-style standard controls and a small singleton theme. Keep frame/status indications immediate. Set a distinct application name such as `LumoraQmlPreview` before resolving standard paths; do not write production preferences in this preview.
- [x] Verify the root loads from resources with no import warnings, resize at both supported sizes, keyboard focus and evaluation/waiting text. Inspect captures. Build the original Widgets application with the option OFF; build both executables with it ON.

Proposed verification commands after the new presets exist:

```bash
cmake --preset linux-gcc-debug-sim-qml
cmake --build --preset linux-gcc-debug-sim-qml --target lumora_app lumora_qml_app all_qmllint
ctest --preset linux-gcc-debug-sim-qml -R '^Qml.ThemeSmoke$' --output-on-failure
```

Register QML test execution explicitly; do not inherit `QT_QPA_PLATFORM=minimal` as evidence of accelerated rendering. Repeat applicable build/smoke checks in Release. Native render verification starts at Checkpoint 3.

**Exit:** Both application builds and QML resource/lint/theme checks pass. Record resolved dependency versions and preview captures. This is the first small implementation batch, not completion of the integrated milestone.

## Checkpoint 2: Shared policy with Widgets regression coverage

**Files:** Create `src/presentation/CMakeLists.txt`, `include/lumora/presentation/WorkstationCoordinator.hpp`, `WorkstationState.hpp`, `CameraActionPolicy.hpp` and corresponding implementation files beneath `src/presentation`. Extract from `src/ui/src/WorkstationController.cpp`, `CameraStartupPanel.cpp` and their headers. Relocate reusable `ProcessingControlsModel`, camera draft normalization and viewport value types into the shared target; update affected includes and `src/CMakeLists.txt`. Add `tests/unit/presentation/WorkstationCoordinatorTests.cpp` and `CameraActionPolicyTests.cpp`; retain frontend control tests.

**Interface:** `lumora_presentation` owns startup continuations, desired requests, source/session/revision checks, priority barriers, processing submission/completion and persistence coordination. Its state has no widget pointers. Widgets binding owns panel/label creation, signal wiring and visual disclosure. Typed QML objects will project shared state and call deliberate commands; frame/session IDs are never accepted back from JavaScript as authority.

- [x] Cover policy through the new shared test surface: selected/connected source mismatch; ordinary pending versus priority Stop/Disconnect; newer failed Apply versus old Confirm; legacy fingerprint review; changed Resume readback; installation save versus activation and authority.
- [x] Extract camera availability facts from the panel and enforcement/continuations from the controller into one shared implementation. Keep C++ rejection even if a QML caller invokes an unavailable action directly. Preserve the existing eligible startup connection probe without granting automatic stream authority.
- [x] Reuse processing draft/pending/acknowledged state, 30 Hz drag coalescing, exact Release submission and matching completion persistence. Verify rejection and an old completion cannot overwrite a newer draft or bind to a replacement session.
- [x] Make the Widgets controller a binder using shared state and commands. Leave concrete dialogs/layout in Widgets. Move only policy needed for Stage 1; camera numeric editor parity follows in Stage 2.
- [x] Preserve context handoff order. The shared coordinator must support waiting for renderer retirement before `acknowledgeContext(generation)`; the Widgets adapter can finish retirement synchronously. Keep Start unavailable while context binding is incomplete.

**Execution note:** Shared coordinator tests were written before extraction, but no coordinator-only red executable ran before implementation because its header/target was not yet buildable. Existing tests provided characterization; shared action policy and panel binding separately have recorded red-to-green evidence.

**Exit:** Existing startup, camera settings, installation, processing controls and integration checks pass through the shared implementation. Shared tests run without linking Qt Widgets or Qt Quick. No second startup/persistence policy is introduced for QML. The shared-policy record preserves final review and verification; small UI compatibility headers retain source compatibility for relocated value/model types.

## Checkpoint 3: Presentation protocol and renderer feasibility

Implementation is in progress from `42c2340`; the [renderer experiment plan](2026-09-13-qml-renderer-experiment.md) defines its task interfaces, lifecycle rules and verification sequence.

**Files:** Create `src/presentation/include/lumora/presentation/PresentationProtocol.hpp` and shared `FramePresenter` files. Adapt `src/ui/src/FramePresenter.cpp`, `ImageViewport.cpp` and `WorkstationController.cpp` to that contract. Create `src/qml/QuickImageItem.hpp/.cpp`, `tests/unit/presentation/PresentationProtocolTests.cpp`, `tests/support/ControlledPresentationSink.hpp` and `tests/qml/QuickImageItemTests.cpp`. Record measurements in `docs/architecture/milestones/qml-renderer-experiment.md`.

Proposed C++ ticket values, kept outside QML:

```cpp
struct PresentationTicket {
    std::uint64_t sessionGeneration;
    std::uint64_t sourceFrameId;
    std::uint64_t presentationRevision;
    bool operator==(const PresentationTicket&) const noexcept = default;
};
struct PresentationReceipt {
    PresentationTicket ticket;
    std::chrono::steady_clock::time_point completedAt;
};
```

The submission carries one retained `shared_ptr<const FrameBundle>` and a display mode. The renderer validates both planes before admitting a submission. Completion and retirement are separate events; a canceled ticket can release owners without counting as presentation.

- [ ] First implement the shared protocol against a deterministic controlled sink. Serialize submissions while a ticket is in flight. Keep at most one in-flight and one completed/frozen bundle in the presenter; leave the newest candidate in the existing slot until capacity is available. Coalesce mode intent without creating a frame backlog.
- [ ] Capture completion time at the renderer boundary, then deliver the receipt to the GUI. Accept only the current session and exact admitted ticket. Count/refresh only a new source frame, never a mode-only repaint or duplicate receipt.
- [ ] Define Pause as an ordered barrier: close source admission immediately, cancel unsynchronized work where possible, and settle work already consumed by rendering before publishing Paused. Show a pending Pause indication until the barrier proves the frozen visible bundle equals the reported bundle. A late receipt cannot replace that frozen bundle. Resume reads the newest available source.
- [ ] Retire old scene-graph/upload owners before acknowledging replacement context; retain the old context handle until retirement completes. Cover close/invalidation even when no further frame will render. Do not wait indefinitely for a hidden window's next swap to release owners.
- [ ] Implement one C++ image item rendering prepared Gray8 through public scene-graph texture facilities. Prove grayscale/channel handling and padded stride before selecting the final upload representation. Compare uses one validated submission, one shared transform and one receipt; QML never carries pixel arrays or independent plane URLs.
- [ ] Correlate the item's consumed ticket with `QQuickWindow::frameSwapped`, capturing monotonic time on that render-thread boundary. A window frame caused only by another control must not complete an image ticket. This measures presentation submission, not physical display scan-out.
- [ ] Inventory retained pool leases, texture pairs, staging/conversion copies and measured upload/display latency at 640×480 and representative larger workloads including 2048². Reserve known external storage through existing preparation options where applicable. Report GPU/driver overhead separately; the 512 MiB default session accounting and existing CPU allocation evidence do not already cover it. Do not expand pools or claim zero allocation without evidence.

**Required pass/fail cases:**

| Experiment | Pass condition |
|---|---|
| Delayed rendering; separately delayed GUI receipt delivery | Submission does not advance status; the original completion timestamp survives delivery delay. Both host age and time since new-source completion enforce `max(500 ms, 3 actual frame periods)`. |
| Duplicate, old-session, out-of-order or canceled receipt | No displayed count, freshness, labels or frozen owner changes. |
| Pause before synchronization; Pause during render; mode changes while paused | Barrier completes only with matching visible/reported frozen bundle; mode repaint preserves freshness and source identity. |
| Compare, missing Enhanced, invalid second plane | Both panes come from one bundle and complete together; fallback labels describe what was presented; invalid submission preserves prior valid state. |
| Padded grayscale ramps and asymmetric orientation fixtures | Correct pixels, no second orientation, shared pan/zoom, Fit and logical 100% across DPR. |
| Hidden/zero-size/minimized item; initialization/upload failure | No false completion; waiting/stale/unavailable state and an actionable error as applicable. |
| Reset, close, invalidation/recreation, delayed destruction | Old receipts rejected and owners retired before session release is asserted; no pool exhaustion under repeated transitions. |

Use shared deterministic tests, a software/offscreen path where supported, and actual on-screen Linux rendering. Add native Windows tests for the selected graphics backend before claiming portable renderer verification. Any unresolved Pause/retirement/pixel-identity failure blocks Checkpoint 4; a theme preview can still be reviewed independently.

## Checkpoint 4: First integrated SIM-LIVE workstation

**Files:** Extend `src/qml/main.cpp` and `qml/Main.qml`; create `CameraAdapter.hpp/.cpp`, `ProcessingAdapter.hpp/.cpp`, `ViewerAdapter.hpp/.cpp`, `qml/CameraStartup.qml`, `qml/ViewingToolbar.qml`, `qml/WindowLevelControls.qml`, `qml/StatusStrip.qml` and `tests/integration/QmlWorkstationTests.cpp`. Update build guides, dependency inventory and `docs/PROGRESS.md` with scoped evidence.

- [ ] Register small typed adapters in the QML module and inject application-owned instances through required root properties. Keep request/session/revision handling in C++ and numeric window/level editing precise.
- [ ] Compose the existing simulator, installation/preferences services and `LivePipeline`. Connect shared startup, Stop/Disconnect/Retry and explicit eligible saved Resume. Show authoritative readback and active processing state.
- [ ] Connect Original/Enhanced/Compare, Pause/Resume, Fit, logical 100%, zoom/pan and evaluation/paused/stale/orientation/error indications to shared state and the proven renderer.
- [ ] Implement only window/level editing through the existing model → admission → completion → acknowledged persistence route. Reopen using isolated development preferences and verify only successfully activated settings were saved.
- [ ] Run the real select → Connect → Apply → review → Confirm → Start flow and negative command tests. Preserve Stop/Disconnect while work is pending. Exercise processing fallback/retry and context replacement/teardown with the same production backend.
- [ ] Run Debug/Release regression suites, lint and QML tests; inspect both supported window sizes. Stage the compiled QML resources and required Qt imports/plugins outside the source/build trees and verify imports without development search paths. Record native Windows/DPI work separately if unavailable.

**Exit:** A usable simulator workstation meets the Stage 1 behavior and renderer criteria. Full camera/preset editor parity, fullscreen/sidebar preferences, default launcher switch, Capture and diagnostics remain subsequent stages. No hardware, 30 FPS processing, Windows acceptance or M13 packaging gate closes from a successful pilot launch.

## Baseline commands and execution order

Before behavior extraction, build and retain source-bound Widgets evidence:

```bash
cmake --build --preset linux-gcc-debug-sim
ctest --preset linux-gcc-debug-sim -LE 'hardware|desktop' --output-on-failure
cmake --build --preset linux-gcc-release-sim
ctest --preset linux-gcc-release-sim -LE 'hardware|desktop' --output-on-failure
```

The inspected caches enumerate 69 groups because desktop smoke is enabled; the exclusion above selects the recorded 68 headless groups. Use the existing native verification procedure separately. If lifecycle/context-retirement fails, retain the first failure and characterize it; a passing rerun does not close that inherited issue.

Implement Checkpoint 1 first. Review the visible preview and dependency evidence, then execute shared extraction, protocol/render proof and integrated behavior sequentially. Split coordinator extraction and presenter protocol into separate commits/reviews; do not turn Stage 1 into one large replacement patch.

## References checked during planning

- [Qt scene-graph threading and synchronization](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html).
- [QQuickWindow frameSwapped](https://doc.qt.io/qt-6/qquickwindow.html#frameSwapped): render-thread presentation-submission signal.
- [qt_add_qml_module](https://doc.qt.io/qt-6/qt-add-qml-module.html): generated type information, cached resources and lint targets.

Online documentation resolved to Qt 6.11.2 during this exploration; implementation must verify against the matching installed 6.11.1 toolchain rather than changing the project's pin implicitly.
