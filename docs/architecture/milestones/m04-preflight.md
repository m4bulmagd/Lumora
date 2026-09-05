# M4 minimal live viewer preflight

Prepared: 2026-09-05. Status: **preparation only; implementation not started**.

The [M4 plan](../../superpowers/plans/2026-04-25-m04-minimal-live-viewer.md) remains the task plan. Its entry gate is the [M3 acceptance record](m03-camera-api-simulator.md); Windows/MSVC evidence is still missing. This preflight does not waive that gate or approve new behavior.

## Integration facts from the merged code

| Area | Existing contract | M4 consequence |
|---|---|---|
| Frames | `DisplayFrame::create` and `FrameBundle::create` return validated immutable shared owners | Use the factories and retain the display owner for every borrowed `QImage` paint lifetime. An Original-only bundle has null enhancement members. |
| Frame identity | `DisplayFrame::sourceFrameId`; `FrameBundle::sourceFrameId()` | Use these actual names rather than assuming each type has a `frameId` member. |
| Latest delivery | `LatestValueSlot<FrameBundle>::consumeAfter(revision)` returns an optional `{revision, value}`; slot capacity is one | Poll without blocking the UI thread. Track slot revision independently from source frame ID. |
| Timing | Raw metadata contains `hostReceiptTime` (steady), `acquisitionUtcTime` (UTC), and actual applied FPS | Keep age/freshness arithmetic monotonic and display the frozen UTC timestamp. Do not infer freshness solely from camera activity. |
| Camera ownership | `retrieve(timeout, pool, stopToken)`; all device calls are thread-confined | The test harness worker must create/use/stop/close its device on the worker thread. Production UI receives core frames, not a device reference. |
| Build integration | `lumora_ui` currently links Qt Widgets only; all UI tests share one executable and one `QApplication` main | Add core linkage, source files, and focused CTest registration as each task lands, rather than waiting for Task 4. Preserve the single Qt test entry point. |
| Harness | No `tools/viewer-harness` or `lumora_integration_tests` target exists yet | Task 4 must create and register both explicitly and keep the harness excluded from install/package targets. |

## Items to settle before their implementation tasks

- **Fit at extreme sizes:** Task 1's unconditional `[0.05, 32.0]` scale clamp conflicts with exact Fit for sufficiently small or large viewports. For example, a 4096-pixel-wide image in a 100-pixel-wide viewport requires `100 / 4096`, below 0.05. Resolve the Fit-versus-manual-zoom limit explicitly in the plan; do not silently crop a mode presented as Fit. Recommendation for review: apply the limits to manual zoom and permit Fit's exact aspect-preserving scale.
- **100% scaling:** Design §11.2 already defines one source pixel per logical image pixel. Task 1's `devicePixelRatio` argument must not silently turn that into one physical screen pixel. Carry the approved logical-pixel semantics into tests at 100%, 125%, 150% and 200% display scaling.
- **Headless rendering:** Task 2 names `QT_QPA_PLATFORM=offscreen`, but the current reduced Qt dependency supplies `minimal`, and both test presets use `minimal`. Update that instruction to use the supported headless plugin and image-based widget rendering, or explicitly approve/build the additional plugin before relying on it. This is separate from manual Windows display-scaling validation.
- **Session identity:** Task 4's “newer IDs” rule can only be applied within one source session. New simulator device instances start at ID 1. Define presenter reset/rebinding behavior before later reconnect or camera-switch integration; do not let a stale high ID suppress a new session forever.
- **Stale presentation:** Tests must distinguish receiving a bundle, accepting it for display, and completing presentation. Suppressing UI refresh must not keep an old image classified as live merely because capture continues. The existing `max(500 ms, 3 expected frame periods)` deadline remains unchanged.

## Next executable task

After M3 is accepted and the Fit limit is resolved, begin **Task 1: Viewport transform model**, with focused geometry tests first. Then add safe Gray8 rendering, workstation state/controls, and the simulator presenter in the existing task order. No processing enhancements, capture, Basler SDK, or physical camera are needed for this milestone.
