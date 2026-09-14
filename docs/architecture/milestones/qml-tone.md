# QML brightness/contrast and gamma — Stage 2B

The next processing-editor slice is implemented locally on `codex/qml-foundation`, from the [preset/reset checkpoint](qml-presets.md) `70ebe66` through production source `dd6369b3316cccfe9a190996bb2bd6d57a8e3b0c` and test-only successor `fe556a5`. Work remains in `/home/mo/code/Lumora/.worktrees/qml-foundation`; main remains `e4520ddd44d14256552fe1bd53111d5e61f195ce`. Widgets remains the default launcher. The [migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and existing Widgets controls provide the parity baseline.

## Behavior and ownership

The QML sidebar now has Brightness/Contrast and Gamma stage switches, exact numeric fields and sliders. Brightness uses the native range −1…1, contrast 0…4 and gamma 0.1…5. Sliders divide each range into 1,000 steps, matching Widgets; fields preserve full native double precision. Disabled stages retain their values, and their editors are disabled until enabled again.

`ProcessingAdapter` projects typed values, bounds and shortest-roundtrip text from the existing shared model. Every deliberate edit uses a fresh complete draft, preserving other stages and saved recipes. Drag throttling, matching activation completion and persistence remain with `ProcessingControlsModel` and the coordinator. Invalid input cannot mutate or save the draft; inherited real-engine activation-failure coverage verifies rollback to the acknowledged recipe.

Window/level and tone fields share a small exact-value component. Only edited text commits, on Enter or focus loss; focusing a named preset's fields does not create Custom. Preset selection and Reset cancel unfinished input, including Reset while already Original. A held slider's remaining movement and release cannot replace a newer preset with obsolete values; a fresh gesture or keyboard adjustment remains usable. Parameterless tone Release flushes the current draft.

Preset selection, Reset, activation feedback, errors and the acknowledged active identity remain above the scrolling adjustment area. Keyboard focus brings the relevant editor into view at 900×600 and 1280×800. The full acknowledged recipe remains available below the editors.

Processing algorithms, schema, camera authority, completed-frame presentation and renderer ownership are unchanged. The actual-scene tests retain source-frame identity, exact pixels in both paused Compare panes, zoom/pan rectangles, camera session/confirmation and readback throughout edits and Reset. Resume presents a newer frame. Only acknowledged settings persist and reopen under the separate `LumoraQmlPilot` preference identity.

## Review and regression history

Adapter stubs first produced seven expected behavioral failures; the implemented adapter passes all 24 cases. The actual scene first failed on the missing controls. Review then identified an untested held-slider replacement route. The added native test reproduced a defect: High Contrast restored brightness zero, but the final mouse release emitted `moved` and submitted the old 0.352 value as Custom. The shared QML component now cancels the rest of that gesture and restores its authoritative binding. The regression checks subsequent movement, release, displayed value and a fresh keyboard edit, as well as preset replacement while a text field remains enabled and focused. Independent adapter and UI reviews are approved with no open findings.

Error-visibility and window-resize assertions now wait for Qt Quick layout polish before measuring geometry. Earlier build failures and the `ui-red` run of an older binary after a failed build are retained only as diagnostic history; they supply no final acceptance evidence. A later Release/threaded OpenGL/DPR 2 run exposed an inherited input-test race: requested display mode was already Compare while no completed bundle was available, so Pause was briefly disabled. The click helper now waits for published enabled state, retaining the actual click and outcome assertions. The test-only successor passes the reproducing conditions. Earlier `final-*` records belong to the preceding source; final acceptance uses `verified-*` records from `fe556a5`. The source-manifest helper includes new, untracked source files as well as tracked files, avoiding the earlier slice's intermediate-manifest omission.

## Verification

The final 406-file production/test/build source manifest is SHA-256 `fece9cd3e8648d662a676b08be63ce94aa7e3a19127e75f702549c829d2494d4`. Documentation successors do not change it. `out/qa/qml-tone/verification.json` records final commands, source/binary/log/capture hashes, stage inventory and independent reviews. The task ledger remains under `.superpowers/sdd/2026-09-14-qml-tone`; previous QA directories are preserved.

Both full builds and `all_qmllint` pass. Full non-hardware/non-desktop CTest passes **77/77 Debug in 77.39 s** and **77/77 Release in 46.93 s**. These include the 24-case adapter and 15-case actual-QML scene suites. The native scene covers exact saved-value loading, no-edit focus, Enter and Tab commits, malformed/out-of-range rejection, toggles, keyboard/mouse sliders, replacement cancellation, complete settings reopen, guarded camera startup, Pause/Resume, minimum/default layouts, failure/retry and rendering-aware close.

The independent final audit approves the 406 source hashes, 314 artifact hashes, 13 successful final command records, staged settings/pixel evidence and documentation, with no remaining actionable findings.

Native actual-scene results on final source are:

| Rendering setup | Debug | Release |
|---|---|---|
| X11/software | 15/15 | 15/15 |
| X11/threaded OpenGL | 15/15 | 15/15 |
| X11/threaded OpenGL, DPR 2 | 15/15 | 15/15 |

## Fresh staged application

A regenerated Release import scan and fresh `QmlPilot` installation at `/tmp/lumora-qml-tone-stage.u9iioqb4` pass the existing dependency checker: 182 inventory entries and **113 ELF objects, zero errors**. The installed executable SHA-256 is `668eb91493664bb2dc9a150c4c8a11bcb7e24ca7095ffb05a7867040d40da300`; install rewrites RPATH, so this identity is recorded separately from the build executable.

The actual executable runs from a neutral directory with isolated temporary preferences/data/cache and a whitelisted environment. XTest drives Select → Connect → Apply → readback review → Confirm → Start, selects Standard, then enters six full-precision values through the real fields using Tab navigation and Enter. Each edit is acknowledged and durably saved as a complete Custom pipeline, preserving every unrelated stage, the saved-recipe collection and confirmed camera preferences.

The driver switches to Compare and Fit, verifies changing pixels, and pauses. Both fitted image interiors must contain meaningful detail before frozen-pixel assertions: the observed intensity ranges are 0–255 and 53–255, with more than 13,000 bright pixels in each pane. All three paused tone edits and Reset preserve the exact image ROI. Reset persists Original with brightness 0, contrast 1, gamma 1 and both tone stages off. Resume changes pixels; Stop freezes them; restart changes them again. Normal `WM_DELETE_WINDOW` while streaming exits **0**, without timeout or forced cleanup. The complete driver passes on its first run in **30.387 s**.

The runtime audit confirms 18 Qt libraries and 10 plugins load from the stage. QML imports resolve inside the stage or compiled resources, and the file-access trace contains no successful development-prefix accesses. Command, settings snapshots, runtime trace and inspected input/acknowledgment captures are retained in `verified-staged-ui.*` and `staged-ui-final/`. Driver coordinate metadata comes from the same production source's native software captures before the test-only readiness correction; its camera/control geometry is unaffected by that assertion change.

The toolchain remains GCC 15.2 with matching official Qt 6.11.1; both application configurations link the Release Qt SDK. Native OpenGL uses Mesa 26.0.8 llvmpipe. These checks establish no Windows, camera hardware, physical-GPU/display or designated-workstation performance acceptance. The full Stage 1 native Widgets/renderer matrices, relocation and performance records retain their original identities and were not repeated for this bounded adapter/UI slice. The internal stage retains the existing notice scope and is not an accepted installer.

For local evaluation:

```bash
/tmp/lumora-qml-tone-stage.u9iioqb4/bin/lumora_qml_app
```

## Continuation

Next are local contrast, denoise, sharpen and invert editors. Preset save/rename/delete, stopped camera settings and installation editing remain in Stage 2; fullscreen, sidebar collapse and layout preferences follow in Stage 3. Existing Windows, hardware, performance and formal-acceptance gaps remain open. There is no merge, push or new hosted CI result.
