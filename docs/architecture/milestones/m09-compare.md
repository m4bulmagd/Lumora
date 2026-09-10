# M9 Task 3: Original, Enhanced and synchronized Compare

Date: 2026-09-10. Branch: `codex/m09-compare`. Status: implemented and independently reviewed locally, unpublished; clean-source final verification is pending.

The owner authorized Task 2 publication and merge after both platform CI jobs passed, followed by Task 3 development. [Task 2 merged through PR #18](m09-processing-controls.md#pr-18-integration) as `c3f1c49`, followed by integration documentation `270c438` on `main`. The Compare worktree began at Task 2 publication head `bf21cee`. The [refined Task 3 plan](../../superpowers/plans/2026-09-10-m09-compare.md) records the implementation contract and final integration checks. This continuation preserves the M4/M5 native Windows deferral, M6 hardware prerequisites and M8 performance/acceptance gates.

## Operator behavior

Original, Enhanced and Compare are mutually exclusive display modes. Original shows the bundle's display-mapped Original image, including its active window/level and shared installation orientation. Enhanced shows the matching processed display. Compare places Original on the left and Enhanced on the right, with labels and a centered divider that cannot be dragged.

Display mode changes only which existing images are shown. Selecting **Original mode** does not change the processing preset, stage values, camera or stream. Selecting the **Original processing preset**, or **Reset processing**, changes the processing definition as documented in Task 2. Raw samples remain unchanged in both cases. Display mode persistence belongs to later UI-preference work.

Compare shares zoom and pan between its equal-width panes. Fit uses each pane's bounds and preserves the complete image's aspect ratio. 100% remains one source pixel per logical viewport pixel; Windows display scaling is not an extra zoom factor. Wheel zoom is centered at the pointer in either pane, drag pans both views, and double-click returns both to Fit. Switching between single and Compare views preserves manual scale and source center subject to the existing edge clamps; Fit recomputes for the available bounds. Processing changes do not reset the viewport.

Before an image is available, all mode actions are disabled. The initial desired mode is Enhanced. A live bundle without an Enhanced image falls back to Original and disables Enhanced/Compare with a visible reason. A later valid pair re-enables those choices while leaving Original selected until the operator chooses another mode. Checked actions, the displayed-mode label and the viewport's accessible name follow the completed paint.

Pause freezes the last painted bundle, including both images. The operator may switch modes or adjust the shared viewport on that frozen pair. New processing output or a later processing failure cannot replace the frozen images. The persistent PAUSED overlay retains the frame's UTC timestamp and shows increasing age; changing display mode does not make the frame fresh. Resume selects the newest available bundle and reevaluates mode availability. The evaluation banner, paused/stale indications and Pause/Live remain visible alongside Compare.

## Rendering and ownership

The existing `ImageViewport` handles all modes in one canvas with one `ViewportTransform` and one pending/completed presentation. The older plan's separate ComparisonViewport widget is replaced by this extension; the existing standalone frame API and workstation viewport accessor remain available. No second renderer, independent pane transform or processing pipeline is introduced.

`ViewportPresentation` carries a token, mode and immutable Original/Enhanced display owners. Pair submissions validate matching source IDs, dimensions, installation orientation and display mapping/revision. Every selected Gray8 image wrapper is prepared before pending state or geometry is replaced. A rejected submission preserves the previous presentation. The display-buffer leases remain held for the complete paint lifetime, and the already applied installation orientation is not applied again.

One completion receipt follows the entire selected paint. `FramePresenter` accepts it only when its token, mode and exact frame owners match the pending bundle. A mode-only repaint of the completed bundle does not increment the live-frame count or renew the successful-presentation timestamp. Live freshness still checks both host-receipt age and the deadline since a new source frame completed painting. Source reset releases both planes and clears pending/completed frame, mode and freshness state; retired presentation receipts cannot complete a new source.

## Verification record

Development tests cover pair validation and literal left/right pixels, shared transforms, immutable ownership and rejection rollback, completed-paint mode identity, pause and newest-frame resume, fallback/recovery, stale repaint behavior and source reset. The real `CompareWorkstation` fixture hosts MainWindow, camera controls and ProcessingPanel at 900×600 and 1280×800. It checks distinct paired pixels, frozen source identity, the PAUSED timestamp/increasing age, visible mode/Pause controls, horizontal containment and viewer prominence. Its optional `LUMORA_COMPARE_SCREENSHOT` prefix writes live and paused BMP screenshots for native inspection.

A development XCB/Xvfb run of the real workstation fixture passed. The 900×600 live/paused and 1280×800 paused screenshots were inspected: both plane labels, mode/Pause controls and evaluation/paused timestamp/age indications were visible, with equal panes and no horizontal overflow. Development screenshots use `compare-{900x600,1280x800}-{live,paused}.bmp`, with PNG format conversions for inspection. Final source review and the scoped renderer rereview passed with no actionable findings. The development full Debug suite passed 60/60 in 65.54 s. Clean-source final Debug/Release builds and full suites, native X11 checks and an exact-source evidence manifest remain pending. Development evidence is retained under `out/qa/m09-compare/` in the preserved `.worktrees/m09-compare` checkout. Focused development passes are not a final branch-verification or hosted-CI claim. Task 3 has not been published or run hosted Linux/Windows CI.

No processing algorithm or performance measurement changes in this slice. The intermittent lifecycle timeout, 30 FPS target, designated Windows references/performance, native Windows visual/DPI, hardware, packaging and separate milestone acceptance remain open.

## Next planned slice

[M9 Task 4](../../superpowers/plans/2026-04-25-m09-presets-workstation-ui.md#task-4-camera-selection-and-safe-settings-dialog) expands camera selection/status and adds a capability-driven settings dialog. It must reuse the existing explicit selection, Apply/readback/Confirm/Start, Resume and single preferences-writer policies. Stopped-state resolution rebinding, per-camera records and administrator-managed installation orientation require their own bounded Task 4 implementation steps. Fullscreen and persisted UI layout remain Task 5; physical Basler integration still requires the M6 hardware profile.
