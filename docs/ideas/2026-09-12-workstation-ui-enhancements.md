# Workstation UI enhancements for Lumora

Future-planning proposal · 12 September 2026

**Status:** Saved at the user's request for future planning; not scheduled for implementation.

**2026-09-17 follow-up:** Selected capture/review ideas are developed in the [C-arm workstation design](../superpowers/specs/2026-09-17-carm-workstation-design.md) and [UI/UX plan](../superpowers/plans/2026-09-17-carm-workstation-ui.md). The newer plan adds independent Live/Reference contexts and supersedes this proposal's single-viewer review direction for that work. This document remains historical inspiration, not a second implementation backlog.

**Direction:** Keep the current roadmap. Refine the interface around a large image, accessible everyday controls, and clear viewing/capture actions. Add review and recording interfaces when their underlying capabilities are available.

**Source:** The September 2026 discussion comparing Lumora with [Microvision Professional](https://microvision-software.com/en/microvision-professional/). Microvision's published feature descriptions informed the comparison; the layout below is a proposal for Lumora, not a reproduction or verified description of Microvision's screens.

**Collection:** [Future product ideas](README.md). Related: [M2-inspired features](2026-09-10-m2-inspired-features.md).

## Existing foundation

The comparison included merged GitHub `main` at [`f897a70`](https://github.com/m4bulmagd/Lumora/tree/f897a70c92cdb93995e203c5bbe56a5b41bbcd0b). At the time of writing, the local checkout contained an older implementation plus the saved M2 proposal. This dated baseline avoids treating newer merged features as missing; recheck current source before implementation.

Already implemented at that baseline:

- Original, Enhanced and synchronized Compare views.
- Pause/Resume, zoom, pan, Fit and 100% viewing.
- Preset selection, numeric entries/sliders, window/level, brightness/contrast, gamma, local contrast, denoise, sharpening, inversion and Reset processing.
- Stopped camera exposure, gain and FPS settings.
- Processing feedback, camera state, and paused/stale-image indications.

The main opportunity is to organize those capabilities more clearly. The current sidebar combines camera setup and many processing parameters. Its sharpening Advanced group disables its fields when unchecked but does not collapse them. The proposed interface should reuse the existing actions and settings rather than create competing controls or persistence.

## Proposed screen organization

| Area | Proposed contents | Intended behavior |
|---|---|---|
| Central image | Large Original/Enhanced viewport or synchronized Compare | Give the image most of the available space; preserve the existing shared zoom/pan behavior. |
| Collapsible sidebar | Preset selection, common image adjustments, expandable Advanced sections | Keep frequent controls easy to find; reveal technical parameters only when requested. |
| Viewing and capture toolbar | Original/Enhanced/Compare, Pause/Resume, Capture, Fit, 100%, zoom and fullscreen | Group short, frequent actions close to the image. Introduce Capture with M10. |
| Compact status strip | Connection state, viewing state and concise performance/health information | Show essential status at a glance; offer detailed diagnostics separately. |
| Future thumbnail strip | Successfully saved captures and selection state | Add with saved-image review; provide an explicit Return to Live action. |

Use consistent spacing, readable labels and large action targets with keyboard access. Keep the existing dark viewer treatment and make the primary Capture action easy to locate when available. Preserve mandatory evaluation, paused/stale, orientation and critical-error indications in normal, collapsed-sidebar and fullscreen layouts. Hiding panels must not hide those indications.

## UI feature proposals

### UI01 — Everyday controls and collapsible Advanced settings

**Benefit:** Adjust the image without searching through a long list of technical parameters.

**Proposal:** Keep presets, window/level and common brightness/contrast adjustments prominent. Group less frequent settings into genuinely expandable sections. Preserve access to gamma, local contrast, denoise, sharpening and inversion; consider tile grid, filter kernel, sharpening radius and threshold Advanced parameters. Collapsing a section changes visibility only, never its enabled state or processing values. Keep stage enable/disable separate from expansion.

**Acceptance considerations:** Expanded sections show current values; hidden settings still apply; all controls remain keyboard-accessible. Clearly distinguish Reset processing from resetting zoom or camera settings. Final grouping should be checked with representative operator tasks.

**Relationship:** Refinement of the existing [M9 processing controls](../superpowers/plans/2026-04-25-m09-presets-workstation-ui.md), not another processing system.

### UI02 — Compact camera setup

**Benefit:** Recover viewing space after the camera is configured.

**Proposal:** After connection and configuration review, allow the setup section to collapse into a compact camera/connection summary with a Settings action. Keep required startup actions and relevant warnings visible. Settings remain available without losing the current image context.

**Acceptance considerations:** Collapsing setup does not connect, start, stop or reconfigure acquisition. Required review and confirmation cannot be bypassed. Errors stay visible even when setup is collapsed. Preserve existing stopped-state exposure/gain/FPS rules.

**Relationship:** Presentation refinement of M9 camera settings. Installation flip/rotation remains in the planned administrator-managed, stopped-state workflow rather than becoming a new live toolbar action.

### UI03 — Fullscreen and sidebar collapse

**Benefit:** Maximize image size during viewing while retaining essential status.

**Proposal:** Complete fullscreen entry/exit, Escape handling, sidebar collapse and saved layout preferences. Preserve viewport state when switching layouts.

**Acceptance considerations:** Critical overlays remain visible; returning restores a usable layout; keyboard focus remains predictable. Check the roadmap's window sizes and Windows display-scaling settings.

**Relationship:** Already planned in M9 Task 5. This proposal reinforces that work rather than adding a duplicate feature.

### UI04 — Prominent exact-frame Capture

**Benefit:** Save the image being viewed with a clear indication of the result.

**Proposal:** Place Capture beside Pause/Resume. Expose the existing Original, Processed and Both capture choices without overcrowding the toolbar. Show saving, saved and failed states, with a useful failure explanation.

**Acceptance considerations:** A paused capture refers to the paused frame; success is shown only after persistence succeeds; saving does not block live interaction. Capture remains distinct from Pause and from future recording.

**Relationship:** The existing [M10 snapshot plan](../superpowers/plans/2026-04-25-m10-snapshot-capture.md) and M2 proposal F01 own capture behavior.

### UI05 — Compact status and diagnostics access

**Benefit:** Understand whether the displayed image is live and whether the workstation is keeping up.

**Proposal:** Use a concise strip for connection and viewing state, adding acquisition/display FPS and health indicators as diagnostics become available. Provide an expandable diagnostics view for detailed timings and counters.

**Acceptance considerations:** Distinguish acquisition state from presentation state: acquisition may continue while the viewer is paused or reviewing an image. Label acquired and displayed FPS separately. Do not imply that a displayed processing duration is end-to-end latency. Warnings use text as well as color.

**Relationship:** Reuses existing state feedback and the planned [M11 diagnostics UI](../superpowers/plans/2026-04-25-m11-diagnostics-supportability.md).

### UI06 — Capture thumbnails and saved-image review

**Benefit:** Revisit captures without leaving the workstation.

**Proposal:** Add a thumbnail only after a successful save. Selecting it opens the corresponding image with available Original/Enhanced views and existing zoom controls. Show a clear saved-image label, selected thumbnail and Return to Live action.

**Acceptance considerations:** Return to Live shows a fresh frame or explicit stale/disconnected feedback. Reviewing an image must not accidentally change acquisition state. Thumbnail loading remains bounded.

**Relationship:** Future extension after M10; reuse [M2 F02 gallery/review](2026-09-10-m2-inspired-features.md#f02--capture-gallery-and-saved-image-review). This is the UI treatment of that idea, not a second gallery backlog.

### UI07 — Viewing mask controls

**Benefit:** Focus the display on the useful image field.

**Proposal:** Provide an adjustable circular mask with enable/reset controls. Consider horizontal/vertical virtual shutters as a separate optional extension, inspired by Microvision's virtual-collimator feature. Define the relationship between mask shapes before implementing both.

**Acceptance considerations:** Apply a consistent mask to Original and Enhanced presentation, keep it aligned through zoom/pan, preserve native source pixels and identify masked exports. These are display masks, separate from camera ROI or physical collimator control.

**Relationship:** Reuse [M2 F03 circular viewing mask](2026-09-10-m2-inspired-features.md#f03--circular-viewing-mask); virtual shutters are an additional unscheduled option.

### UI08 — Recording and playback controls

**Benefit:** Record and inspect short sequences with clear feedback about their state.

**Proposal:** When recording is supported, add start/stop, a visible recording indicator, elapsed time and limits. Recorded review offers playback, a scrubber, frame stepping and capture/export of a selected frame. Label recorded playback distinctly from Live and Paused presentation.

**Acceptance considerations:** Display storage failures and skipped-frame information; keep live acquisition responsive. Specify what happens when entering review during recording before implementation.

**Relationship:** Reuse [M2 F07 recording/review](2026-09-10-m2-inspired-features.md#f07--short-sequence-recording-and-review). Record controls remain absent from shipping v1 UI until the separate recording design and implementation are ready.

## Relationship to the current roadmap

Keep the approved milestone sequence. Revisit UI01–UI03 with the remaining M9 work, UI04 with M10, and UI05 with M11. UI06–UI08 remain future extensions with the dependencies above. These are opportunities for future planning, not instructions to reopen completed tasks or expand milestone scope automatically.

Measurement/annotation tools, subtraction/roadmapping controls, study browsing and clinical reporting remain broader future capabilities. Their interfaces should be designed alongside their underlying workflows. Saving this UI proposal does not introduce clinical records or change the evaluation-release boundary.

## Questions to resolve when selected

- Which adjustments are frequent enough to remain visible, and which belong in Advanced?
- Should camera setup collapse automatically after successful review, or only on operator request?
- Where should the thumbnail strip sit at the supported window sizes, and should it be hideable?
- How should recording and saved-image review coexist while acquisition continues?
- Is a circular mask sufficient, or are virtual shutters also useful for the target equipment?

No application UI was implemented as part of saving this proposal. Link the resulting specifications and milestone decisions here when individual ideas are selected.
