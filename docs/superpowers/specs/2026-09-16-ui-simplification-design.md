# Workstation UI simplification

Approved in conversation on 16 September 2026: common adjustments stay visible,
advanced controls expand on demand, and adjustments work directly with optional
bypass in an effect menu.

## Interaction

- Keep preset, Window/Level, Brightness and Contrast visible in the processing panel.
- Present each parameter as a label, compact numeric preview and optional slider.
  Show the full exact value on focus and in a tooltip; mark rounded previews with
  an approximation sign. Editing never rounds the stored value. The value is
  editable by pointer or keyboard without a separate full-width input.
- Group Gamma, Local Contrast, Denoise and Sharpen into collapsible effect sections.
  Expansion changes visibility only. It never changes settings or stage enablement.
- A valid parameter edit enables its effect in the same processing draft edit.
  Invalid input cannot enable an effect. Preset replacement and layout changes still
  cancel uncommitted text/slider gestures. Temporary bypass preserves all values.
- Effect options expose bypass/enable; off state remains apparent in the header.
  Invert stays a compact binary action. Advanced uses disclosure, not a checkbox.
- Keep technical parameters (tile grid, denoise algorithm/kernel/sigma, sharpen
  radius/threshold) inside expanded/advanced effect content.

## Layout and text

- Keep camera setup expanded while selection/configuration review is required.
  Once configured, show a compact source/status summary with access to setup.
  Stop and Disconnect remain reachable, including while panels are hidden.
- Preserve distinct Connect, Apply, review, Confirm and Start operations and all
  existing policy enablement. Expanding/collapsing setup issues no camera commands.
- Show requested/current configuration during review or on demand.
- Remove permanent processing save instructions and duplicate active summaries.
  Keep one active summary behind Details and show loading/applying states as needed.
- Consolidate global camera warnings/errors in status; retain processing validation
  next to processing controls and in status when those controls are hidden.
- Preserve evaluation, orientation, paused/stale overlays, frozen timestamp/frame age,
  errors, recovery actions, exact values, presets, and persisted layout behavior.

## Verification

Use real adapter tests to prove atomic enabling, validation and bypass value
preservation. Adapt real QML workstation interaction tests to disclosure/menu
controls and add coverage for expansion without configuration changes. Run QML
lint, the workstation and adapter suites, normal non-hardware regressions, and
capture the real app at supported sizes with a virtual X11 display.
