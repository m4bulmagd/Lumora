# QML fullscreen, panels and saved layout

## Scope and status

This implements the local Qt Quick/QML development scope for M9 Task 5: fullscreen, collapsible camera/processing panels, optional diagnostics and saved window preferences. The owner authorized this work after local integration of the verified QML migration. Requirements come from [PRD sections 19.11–19.12](../../../prd.md#1911-sidebar-behavior), [original M9 Task 5](../../superpowers/plans/2026-04-25-m09-presets-workstation-ui.md#task-5-fullscreen-collapsible-sidebar-and-persisted-ui-preferences), and the [bounded implementation plan](../../superpowers/plans/2026-09-15-qml-layout.md).

**Implemented, verified and integrated into local main at `5f6b82f`.** The preceding migration was verified separately at `3cca2c0` under `out/qa/qml-main-integration`. The layout feature has its own final source-bound evidence below. Nothing has been pushed; Windows and hardware acceptance remain separate.

## Implemented presentation behavior

`Main.qml` retains one ApplicationWindow and one ViewerSurface attached to the existing application-owned C++ image sink. Collapsing panels hides the fixed camera and processing columns and gives their space to the viewer. Fullscreen temporarily hides both columns without changing the remembered panel choice. Exiting returns to the previous ordinary or maximized window state. Resizing uses the existing image transform rather than replacing the viewport or resetting processing, frame ownership or acquisition.

The workstation-owned `LayoutAdapter` observes the actual QQuickWindow, handles F11 and Escape, and exposes layout state to QML. Escape exits fullscreen even when an editor popup is open. A visible fullscreen action also provides entry/exit. Panel toggling is disabled while fullscreen; the remembered choice returns on exit. F remains the existing viewer-local Fit shortcut.

Before a layout transition moves focus or hides editors, `layoutChanging` cancels uncommitted processing input, dismisses the preset popup, closes camera/installation drafts and focuses the retained viewer. Header actions use TabFocus so a pointer press cannot commit a valid numeric draft through an earlier focus-loss event. Layout changes imply no processing edit, camera Apply, Confirm or Start. Camera and installation authority remains in the existing adapters and shared policy.

Evaluation text, acquisition/disconnection status, image timestamp and increasing age, orientation, viewer/camera errors, processing fallback remain outside the panels. Processing load/validation/model/persistence messages keep their original sidebar space during ordinary editing and appear in the persistent status area when panels are hidden, avoiding new ordinary-viewer resizing. The image has a persistent PAUSED or STALE IMAGE — NOT LIVE indication when applicable. Compact Stop and Disconnect controls retain the existing priority command admission. Diagnostics toggle only optional details; required status never depends on it. No control auto-hide or Record action is introduced.

## Preferences and ownership

Typed `application::UiPreferences` stores ordinary geometry, panel collapse, maximized/fullscreen flags and diagnostics visibility. The `ui.layout` version-1 codec preserves unrelated UI keys and the surrounding configuration document. Signed monitor coordinates remain valid. Structurally invalid/current-version layout data can recover to defaults with a warning; an unknown future layout version prevents UI writes. Unsafe whole-document loads retain the existing refusal to write any section.

UI updates use `StartupPreferencesService`, the same asynchronous worker that owns camera and preset persistence. UI revisions and coalescing are independent, while successful writes still serialize one complete merged document. Admission and durable save status remain distinct. There is no QSettings path or second writer to the same preference file.

`UiPreferencesUpdate` carries optional dirty fields rather than an unconditional default-filled snapshot. An absent field retains the loaded value; present empty geometry deliberately clears that field. Updates accepted before a delayed load merge into the loaded UI on the worker, in field/revision order. This fixes shutdown-before-load loss of untouched saved flags or geometry. Camera profiles, saved presets, unrelated JSON keys and protected machine profiles retain their own ownership.

The adapter debounces ordinary geometry changes and submits pending updates before the existing shutdown worker drain. Fullscreen and minimized bounds are not stored as ordinary geometry. Restorable geometry must leave useful content and the title area reachable on an available screen; negative coordinates and reachable overhang are preserved. Invalid or inaccessible geometry falls back to a centered 1280×800 rectangle bounded by the available screen, subject to the window's minimum size. Fullscreen retains the prior maximized choice for exit and persistence.

## Development corrections and test coverage

Development RED tests cover the new preference, adapter and scene contracts. Follow-up review corrected whole-snapshot persistence before delayed load to dirty-field updates, retained the maximized return state, and added a persistent projection of processing messages when panels are hidden. The full scene suite caught an ordinary-viewer height regression when messages were always in the status area; restoring their original sidebar space while expanded preserved the existing exact-pixel checks. Ordinary scene fixtures now deliberately select 1280×800 after layout loading; the saved-layout reopen bypasses this normalization. A separate native test waits for window-manager geometry notifications and maximized client expansion before releasing its held load. The full DPR 2 matrix then exposed a one-logical-pixel viewer resize during preset replacement. Failure images showed a shifted Compare divider and PAUSED badge; fixed camera/processing column widths and post-selection layout polish remove that instability while retaining exact image assertions. Failed and superseded records remain preserved.

Scene tests cover the same ViewerSurface and paused Compare frame identity/timestamp through panel/fullscreen transitions, increasing age, retained zoom and image-space center, visible state overlays, real keyboard and pointer cancellation of valid uncommitted numeric input, and a preset popup with a different highlighted but unselected row. Separate cases exercise minimum-size priority controls, fullscreen fallback/disconnection, and a non-fallback validation explanation while panels are hidden. Close/reopen checks cover durable layout state, unchanged non-UI document sections and explicit acquisition resume.

Native fullscreen checks require a managed window when asserting actual display-sized geometry; a state flag alone is insufficient evidence. Development logs, failed attempts, source manifests and focused captures remain under `out/qa/qml-layout`. Their presence is not a substitute for the final source-bound matrix below.

## Final source identity

All evidence below is retained in `/home/mo/code/Lumora/.worktrees/qml-foundation/out/qa/qml-layout`.

| Binding | Value |
|---|---|
| Implementation commit | `5f6b82f7df75e69286aa679fbf48312e675226ca` |
| Source files / aggregate SHA-256 | 431 / `0c40c6b37f6ebdc616dfa8d8dd0bf2acfe49dcc398f764ce702f036cbe82cbcc` |
| Manifest file | `source-0c40c6b37f6ebdc616dfa8d8dd0bf2acfe49dcc398f764ce702f036cbe82cbcc.json` |
| Manifest file SHA-256 | `f389cf44f0ecdd26c7ce7323070acb3d35377dbec06604031fe00c223690640d` |
| Aggregate record / SHA-256 | `verification.json` / `ed71b15f279a9da0f11351046cb49e2321a93731830dcdbcbcf8dbe8f0b31ec8` |
| Source review | `source-review.json`, approved, no outstanding findings |
| Native visual review | `native-visual-review.json`, 28 screenshots in two modes; all six command results bound |
| Stage reviews | `stage-evidence-review.json` and `staged-visual-review.json`, approved |

Final commands ran on baseline `3cca2c0` with the implementation pending commit. `source-commit-binding.json` verifies every source file against the later implementation commit. Documentation is outside the 431-file source manifest. The aggregate binds the final command records/logs, all native captures and geometry, stage inventory, stage result and artifacts, and independent reviews.

## Final verification

| Command record | Duration | Result |
|---|---|---|
| verified3-debug-configure | 0.692 s | Pass |
| verified3-debug-build | 4.831 s | Pass |
| verified3-debug-ctest | 64.56 s | Pass |
| verified3-release-configure | 0.686 s | Pass |
| verified3-release-build | 30.149 s | Pass |
| verified3-release-ctest | 41.06 s | Pass |
| verified3-native-debug-software | 39.381 s | Pass |
| verified3-native-debug-gl-threaded | 42.183 s | Pass |
| stable-panels-dpr2 | 68.656 s | Pass |
| verified3-native-release-software | 32.097 s | Pass |
| verified3-native-release-gl-threaded | 35.085 s | Pass |
| verified3-native-release-gl-threaded-dpr2 | 46.572 s | Pass |
| verified3-stage-install | 0.7 s | Pass |
| verified3-stage-check | 2.074 s | Pass |
| verified3-staged-ui3 | 27.496 s | Pass |

Both compatibility simulator presets build the sole normal QML application with legacy Widgets tests OFF. Debug and Release full CTest each pass **66/66** groups, including all ten LayoutAdapter cases. Both full builds and QML lint pass. All six actual-scene native configurations pass **28/28**, covering software rendering and threaded Mesa llvmpipe OpenGL, including DPR 2, in both configurations. `stable-panels-dpr2` is the final-source Debug DPR 2 result, reused without another run. Xvfb provides a 2560×2160 display managed by isolated Openbox. Fullscreen fills that display: 2560×2160 logical pixels at DPR 1 and 1280×1080 at DPR 2. Qt is the retained **Release Qt 6.11.1 SDK**, including for the Debug application.

Independent visual review covers all eight layout states and six representative ordinary/minimum-size states in Debug software and Debug DPR 2 OpenGL. Required text and priority controls remain visible while panels are hidden. The existing camera-settings popup can occlude status at minimum size while open; comparison with the frozen preceding migration confirms this inherited limitation. Layout transitions dismiss that popup, and no new material visual finding remains.

The fresh Release stage at `/tmp/lumora-qml-layout-stage.o0zv_i0w` contains **182 entries and 113 ELF files**. File/symlink hashes, dependency paths and the complete maps/import/file traces are independently audited. The ordinary applications have no Qt Widgets dependency or legacy UI compilation entries. Native Windows packaging is not inferred from this Linux stage.

`verified3-staged-ui3` passes the real installed-app workflow in **27.496 s** using pointer/keyboard input, production Lumora preference identity and isolated configuration. Two separate processes exit normally with code 0. The second receives no geometry override: it restores fullscreen, collapsed panels and Details, waits for explicit Resume, and Escape restores the exact ordinary 1280×800 client geometry. All non-UI configuration fields remain equal through layout actions and shutdown. Fourteen staged screenshots are inspected: Compare and PAUSED retain the same timestamp while age increases, and the viewer expands with required status still visible.

Two preceding staged attempts exposed test-driver issues: a missing ordinary paused geometry reference, then an undefined toolbar sample rectangle. They are preserved as `verified3-staged-ui` and `verified3-staged-ui2` with their outputs and preceding driver copies. The final driver explicitly uses `camera-settings-paused-1280x800.json` solely for ordinary main-scene geometry; its native popup screenshot is not an image-content oracle. Final staged screenshots establish the actual unobstructed state. These corrections changed no application source.

## Local main integration

Local main fast-forwarded from `3cca2c0` to implementation `5f6b82f`. The ordinary main Debug and Release full builds and QML lint pass in 102.557 s and 150.305 s. Main Debug full CTest passes **66/66** in 65.584 s. Main source and application hashes, command records and logs are bound in `/home/mo/code/Lumora/out/qa/qml-layout/main-verification.json`, SHA-256 `7bb29c13cf0e132f782272d736d1f4b55bfce95a8262dfd13ddf23bd49ae22f4`. The final Release 66/66, native and stage results above remain the matching-source worktree runs; they were not repeated from main. Both normal main launch configurations are refreshed. The retained worktree, local SDK and all prior QA remain available. No remote operation was performed.

## Remaining external gates

Linux simulator, Xvfb, managed-window and software/Mesa evidence does not establish native Windows graphics/DPI, Windows packaging, physical-display quality, camera hardware behavior or formal release acceptance. No push is authorized. Capture, recording and custom-preset management remain outside this feature.
