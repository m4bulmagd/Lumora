# M9 Task 2: processing controls and Enhanced preview

Date: 2026-09-10. Branch: `codex/m09-processing-controls`, based on integrated Task 1 and documentation checkpoint `2c02ddb`. Status: implemented, independently reviewed and merged through PR #18 after Linux/Windows Debug/Release CI passed.

The owner approved publication of Task 1 and continuation with Task 2. The [implementation plan](../../superpowers/plans/2026-09-09-m09-processing-controls.md) refines the original M9 plan against the actual interfaces. Task 1 merged through [PR #17](https://github.com/m4bulmagd/Lumora/pull/17) after Linux/Windows Debug/Release CI passed. This continuation does not close M8 performance, native Windows 11, M6 hardware or milestone acceptance gates.

## Operator behavior

The processing panel lists Original, Standard, High Contrast, Soft Detail, Custom and loaded user recipes. Manual changes select Custom. Stages remain in canonical order: mandatory Normalize, Window/Level, Brightness/Contrast, Gamma, Local contrast (CLAHE), Denoise, Sharpen and Invert. Optional stage switches, exact numeric entries and sliders use validated ranges. Median mode atomically limits kernels to 3/5 and sets sigma to zero. Sharpen radius/threshold are Advanced controls. Reset selects shipped Original and preserves custom recipes, camera configuration/connection/streaming, pause and viewport transform. Saving/deleting user recipes through a UI is outside this slice; Task 1 provides those domain operations.

A single Enhanced preview makes the controls' effect visible. It falls back to Original when the current bundle lacks an Enhanced display. The label follows the actual completed paint; a paused Enhanced image retains its label across later processing failures. Pause retains the last painted bundle and its freshness/age semantics. Changes affect future frames and do not recompute the paused image. Original/Enhanced switching and Compare remain M9 Task 3.

Camera and processing panels share a vertically scrollable sidebar. Warning, Retry enhancement and Pause/Live remain outside that scroll. Numeric labels/buddies, accessible names and keyboard focus are retained. Numeric display uses locale-aware round-trip text; displaying or committing an untouched value must not quantize it. Sliders span 1,000 positions, with numeric entry for exact values.

## State and execution

The UI model holds a complete validated draft, one in-flight submission and the last acknowledged state. It coalesces drags using the monotonic clock with at least 33,333,334 ns between publications. Release flushes exact final values; duplicate/unchanged release only flushes existing work, preserving a named preset selected during a canceled drag. Preset/Reset replace pending drag values. Older completions cannot overwrite a newer draft. A final rejection restores the last acknowledged values and displays an error. Each new prepared session receives the current desired state with a new runtime revision; retired-session outcomes are ignored.

LivePipeline admits a generation-tagged full definition with a nonzero increasing revision and exposes an explicit processing outcome independent of camera outcomes. One admitted/executing activation is allowed; the UI retains later edits. Preparation runs on the existing control owner outside the public mutex and frame loop, after priority lifecycle dispatch. Completion does not require frames, so idle/stopped sessions can acknowledge settings. Actual frame metadata separately records which revision executed. The default processor adapter reports unsupported activation; the production engine validates/prepares a candidate before its atomic swap. Validation/preparation errors leave prior settings and camera lifecycle intact.

The existing preferences service remains the sole whole-document writer. It publishes the initial typed preset state and uses independent camera/preset save revisions and separate coalescing slots. Pending sections merge before a save; durable status reflects all represented sections, including a later successful retry through another section. Unsafe load errors do not write defaults. Stop drains accepted work. Only acknowledged preset state is submitted for persistence; a save failure remains visible without undoing a successfully activated pipeline.

Processing edits wait for the one asynchronous initial preset load. Loading settings never requests camera Connect/Apply/Start; existing explicit streaming and saved-camera probing behavior remains unchanged. Camera use during slow settings load remains supported with the engine's initial Original definition. A failed load leaves controls unavailable and reports the error. Closing captures an already completed activation without issuing new processing or camera commands.

## Verification record

Behavioral RED was recorded for the model, panel, service, activation interface, Enhanced presentation and controller binding. Focused GREEN includes all eight affected CTest entries (`ProcessingControls`, `ProcessingConfiguration`, `Configuration.StartupPreferences`, `FramePresenter`, `WorkstationView`, `MainWindowSmoke`, `LivePipeline` and `WorkstationController`) in 13.21 s before the source commit.

Independent reviews covered application activation, preferences ownership and UI model/controller/panel integration. Findings were fixed and rereviewed with no remaining actionable items: adapter exception containment, complete facade validation, test cleanup lifetime, numeric round-trip precision, canceled-slider release, stale admission preserving the desired draft, and older rejection preserving newer edit status. Local `review-record.md` summarizes the findings and regression evidence.

The final checks below ran from clean source `e55cddf4817f0026621e64c33edae466a6aafebe`. Documentation-only commits follow this source.

| Check | Debug | Release | Evidence label |
|---|---|---|---|
| Full simulator build, `cmake --build --preset linux-gcc-<configuration>-sim --parallel 3` | Passed | Passed | `final-debug-build`, `final-release-build` |
| Full headless suite, `ctest --preset linux-gcc-<configuration>-sim --output-on-failure -LE 'hardware\|desktop'` | 58/58, 79.70 s | 58/58, 28.66 s | `final-debug-test`, `final-release-test` |
| Native X11, `xvfb-run -a ctest --preset linux-gcc-<configuration>-sim --output-on-failure -L desktop --no-tests=error` | 1/1, 0.12 s | 1/1, 0.06 s | `final-debug-x11`, `final-release-x11` |
| Actual `ProcessingPanel.CompleteWorkstationKeepsEveryControlReachableAtSupportedSizes` under XCB/Xvfb | 1/1, 0.249 s | Not separately run | `final-native-layout` |

The layout case checks both supported window sizes, horizontal containment, every control's scroll reachability and viewer prominence. Its final 1280×800 screenshot was inspected: labels and entries are readable, and the sidebar scroll leaves the viewer dominant. Xvfb evidence does not verify a physical monitor or Windows DPI. An earlier screenshot-only export failed because this pinned Qt build could not save PNG; BMP export and lossless PNG conversion succeeded without a source change. That failed command is retained alongside the successful final evidence.

Local evidence is under `out/qa/m09-controls/` in the preserved `.worktrees/m09-controls` checkout. `verification-manifest.json` verifies all seven successful command records, matching clean start/end source revisions and SHA-256 log hashes. The final screenshot is `processing-panel-final.bmp` (with a PNG format conversion for inspection). Task 2 has not been published or run hosted platform CI. Next integration is publication and matching Linux/Windows Debug/Release CI, followed by Task 3 Original/Enhanced switching and Compare. No processing algorithm or performance measurement changed; M8 performance and deferred native Windows/hardware/acceptance gates remain open. The inherited intermittent lifecycle timeout is not claimed fixed by this work.

## PR #18 integration

On 2026-09-10, [PR #18](https://github.com/m4bulmagd/Lumora/pull/18) merged publication head `bf21ceec60d69a04b176f4ae76c69756ad806719` as `c3f1c49e36bd208c6a62bfede734a56199f5d1ea`. The merge tree equals the verified PR-head tree. Production source at publication remains identical to `e55cddf`; its following commit changes documentation only.

| Check | Debug | Release | Evidence |
|---|---|---|---|
| Fresh local full builds/headless suites at `bf21cee` | 58/58, 80.97 s | 58/58, 28.85 s | `publish-debug-*`, `publish-release-*` |
| Fresh local native X11 | 1/1 | 1/1 | `publish-debug-x11`, `publish-release-x11` |
| [Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34456078784) | 58/58, 79.00 s; X11 1/1, 0.12 s | 58/58, 24.83 s; X11 1/1, 0.04 s | Job `102802653357` |
| [Windows PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34456078794) | 58/58, 97.85 s | 58/58, 41.67 s | Job `102802653714` |

All required job steps passed. Optional ten-minute Windows stress was skipped. No CI source correction was needed. `pr18-integration.json` and `pr18-test-results.json` under the preserved controls worktree's `out/qa/m09-controls/` retain the matching source, merge, job results and test-log summaries.

Task 3 Original/Enhanced switching and synchronized Compare is the owner-approved continuation. This integration does not close the inherited intermittent lifecycle timeout, 30 FPS target, designated/native Windows evidence, hardware or milestone acceptance gates.
