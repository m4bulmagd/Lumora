# M4 minimal live viewer preflight

Prepared: 2026-09-05. Updated: 2026-09-06. Status: **preflight complete; implementation not started**.

The [M4 plan](../../superpowers/plans/2026-04-25-m04-minimal-live-viewer.md) remains the task plan. Its [M3 entry gate is accepted](m03-camera-api-simulator.md): the exact merged source commit `2c88ec90ab58e3e6719be5e236dc49388dbc72dd` passed all 18 CTest entries in Linux/GCC and Windows/MSVC Debug and Release CI. The reviewed clarifications below are propagated into PRD §19.7–19.8, design §7/§8.2/§11.2, and the M4 plan. Planning completion is not M4 implementation or acceptance.

## Integration facts from the merged code

| Area | Existing contract | M4 consequence |
|---|---|---|
| Frames | `DisplayFrame::create` and `FrameBundle::create` return validated immutable shared owners | Use the factories and retain the display owner for every borrowed `QImage` paint lifetime. An Original-only bundle has null enhancement members. |
| Frame identity | `DisplayFrame::sourceFrameId`; `FrameBundle::sourceFrameId()` | Use these actual names rather than assuming each type has a `frameId` member. |
| Latest delivery | `LatestValueSlot<FrameBundle>::consumeAfter(revision)` returns an optional `{revision, value}`; slot capacity is one | Poll without waiting for a publication. Track slot revision independently from source frame ID; source ID zero is valid, so absence uses optional state. |
| Timing | Raw metadata contains `hostReceiptTime` (steady), `acquisitionUtcTime` (UTC), and actual applied FPS | Keep age/freshness arithmetic monotonic and display the frozen UTC timestamp. Do not infer freshness solely from camera activity. |
| Camera ownership | `retrieve(timeout, pool, stopToken)`; all device calls are thread-confined | The test harness worker must create/use/stop/close its device on the worker thread. Production UI receives core frames, not a device reference. |
| Build integration | `lumora_ui` currently links Qt Widgets only; all UI tests share one executable and one `QApplication` main | Add core linkage, sources, and focused CTest registration as each task lands. In Task 4 move the main into reusable test support, compiling it once per Qt test executable. No Qt Test module is needed. |
| Qt startup | Simulator test presets select `minimal`; `MainWindowSmoke` resolves its Debug/Release plugin directory through the imported Qt target | Every new Qt CTest entry inherits that configuration-matched plugin environment and a bounded timeout; headless image assertions use `QWidget::render` into `QImage`. |
| Harness | No `tools/viewer-harness` or `lumora_integration_tests` target exists yet | Task 4 must create and register both explicitly and keep the harness excluded from install/package targets. |

## Resolved implementation contracts

- **Fit at extreme sizes:** Fit uses the exact aspect-preserving minimum ratio and shows the whole image, even below 0.05 or above 32. The `[0.05, 32.0]` limits apply only to manual zoom. The plan defines the transition from out-of-range Fit so a zoom-out request never enlarges the image, nor zoom-in shrinks it. Zero-sized transient geometry remains finite and recovers when valid sizes return.
- **100% scaling:** One source pixel maps to one logical viewport pixel, scale exactly `1.0`. Remove the unnecessary DPR parameter from the unimplemented transform API. Test image rendering at DPR 1.0/1.25/1.5/2.0; native Windows scaling checks remain separate acceptance evidence.
- **Headless rendering:** Use the supported `minimal` plugin and configuration-matched `QT_QPA_PLATFORM_PLUGIN_PATH` on every Qt CTest entry. Render widgets into images; do not silently add or rely on `offscreen`. Use focused GoogleTest filters so the existing smoke entry does not run all new suites again.
- **Session identity:** `FramePresenter::resetSource(freshSlot)` follows old-publisher quiescence and binds a slot that contains only the new session. It clears retained images/bundles, slot revision, optional source-ID watermarks, freshness, and session counters. The new source can start at ID 1 (or zero in core fixtures) without being suppressed by an old ID 100. M5 owns the worker/slot handoff; ordinary stop/start of the same device does not restart its IDs.
- **Paint and Pause ownership:** `ImageViewport::present` stages validated pixels; successful paint is a separate synchronous UI-thread observation, not a per-frame queued signal. Retain at most one pending and one completed image/bundle. Pause cancels an unpainted candidate and freezes the actual completed bundle. Resume rereads the latest slot even if that candidate had already been consumed before Pause.
- **Stale presentation:** A new completed paint advances the presentation timestamp; camera activity, slot receipt, scheduling, and view-only repaints do not. The unchanged deadline is `max(500 ms, 3 expected frame periods)`. An already-old host-receipt timestamp cannot clear stale on Resume. Use monotonic time for age and the stored UTC timestamp for labels. Before the first completed frame show WaitingForFrame. A responsive event loop must continue status updates during an image-path stall; an entirely blocked UI event loop reevaluates at its first opportunity after recovery, not while it is unable to draw.
- **Scope and test duration:** M4 uses prepared Original-only full-range Mono8/Gray8 frames, no processing enhancements or physical camera. Shared test-only simulator feed code belongs to the non-shipping harness, not `lumora_app`; production worker composition remains M5. Normal integration is 10 seconds. Register the 10-minute `stress` case only with `LUMORA_ENABLE_STRESS_TESTS=ON` (default OFF), since a label alone would not exclude it from existing CI.

## Checks retained for M4 acceptance

- Test-first implementation and actual review evidence for all four tasks, including the new source-session, pending-paint Pause, and stale deadline cases.
- Full Linux/GCC and Windows/MSVC Debug/Release CI at the implemented source SHA, with explicit long Release stress runs on both platforms.
- Native Windows 11 visual checks at logical client sizes 1280x720/1920x1080 and 100%/125%/150%/200% display scaling on a sufficiently large screen. Record physical/effective logical geometry, insufficient-workspace cases, and safety-indication visibility; synthetic headless images are not a substitute for this check.
- Updated requirements traceability and a separate M4 acceptance commit before M5. Windows installation/upgrade remains M13, real camera/NIC acceptance remains M14, and any clinical release remains a separate program.

## Next executable task

After review of the updated plan, begin **Task 1: Viewport transform model**, with focused geometry tests first. Then add safe Gray8 rendering, workstation state/controls, and the simulator presenter in the existing task order. No remaining product choice blocks Task 1; no M4 source code, build targets, or tests have been added by this documentation update.

Preflight self-review checked document authority, task order, current core API names, planned file creation/modification paths, focused Qt test registration, and separation from later milestones. Local Markdown links/anchors passed validation across the PRD, README, and documentation set. All ten C++ excerpts (public contracts, fixtures, and test bodies) passed syntax-only checking together against the existing core/Qt/GoogleTest headers using C++20 and the repository's GCC warning-as-error flags. No M4 implementation was linked or exercised; these are documentation checks, not passing viewport tests or Windows visual evidence.
