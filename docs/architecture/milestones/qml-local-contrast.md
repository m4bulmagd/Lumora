# QML local contrast — Stage 2C

This bounded slice follows the [tone editor checkpoint](qml-tone.md) `171ddec` through adapter `67e36a7`, QML/test source `5aaf37b` and popup correction `a784b55` on `codex/qml-foundation`, in `/home/mo/code/Lumora/.worktrees/qml-foundation`. The [migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and existing Widgets controls remain the parity baseline. Main remains `e4520ddd44d14256552fe1bd53111d5e61f195ce`; Widgets remains the default launcher.

## Behavior and ownership

The scrolling QML adjustment area now includes a Local contrast (CLAHE) switch, an exact clip-limit field and slider, and a whole-number tile-grid field. Clip limit accepts 0.1–40; its slider divides that range into 1,000 steps, matching Widgets. Tile grid accepts integers 2–32 and has no slider. Turning the stage off retains both values and disables its editors. Preset selection, Reset, activation feedback and the acknowledged identity stay above the scroll area.

The C++ adapter projects shortest-roundtrip double text and validates numeric and text commands before changing the complete model draft. In particular, fractional grid numbers are rejected before integer conversion; grid text accepts decimal digits with an optional leading plus and outer whitespace. It rejects decimal points, exponents, malformed signs, overflow and out-of-range values. Image dimensions do not silently clamp the grid: the existing processing engine reports an image-dependent preparation failure when necessary.

The shared exact-value component keeps its existing dirty-text, focus-loss and gesture-cancellation behavior, with optional whole-number input hints and a hidden slider. Focusing a loaded saved value does not select Custom. Enter or focus loss commits deliberate edits. Preset/Reset replacement cancels unfinished text and the remainder of a held slider gesture; a fresh keyboard or pointer action remains usable. Parameterless clip Release flushes the model's current draft.

Every accepted edit preserves the other processing stages and saved recipes. Activation, drag throttling, acknowledged-only persistence and failure rollback stay with the existing C++ model and coordinator. Processing algorithms, settings schema, camera commands, frame ownership and rendering are unchanged. Paused Compare keeps its exact completed pixels, frame identity and viewport while settings change; Resume uses newer frames.

## Verification

The final 407-file production/test/build source manifest is SHA-256 `a1eaa12f90d20af268b3a1648e78bb31881d09330da80892dfb98c93035004e4`. Documentation successors do not change it. Raw command records, source manifests, logs, captures and staged application evidence are retained under `out/qa/qml-local-contrast`; the execution and review ledger is `.superpowers/sdd/2026-09-14-qml-local-contrast`. Earlier QA directories remain intact. `out/qa/qml-local-contrast/verification.json` binds the 407 source files, 515 artifact hashes and 13 successful final command records; diagnostic command records remain separate. The independent final audit verifies every source/artifact hash, all command records and the actual stage inventory, with no remaining actionable findings. Manifest SHA-256: `7d55dd7956c3f09a6940513c620a5c31c4d786bd457e6c349e2fb9490b14d2d4`.

Adapter stubs produced six expected behavioral failures while the 25 inherited/guard cases passed. The implemented adapter passes 31/31. Its real 8×6 source accepts an enabled grid of 4 or 5; a subsequent grid 8 reaches the existing preparation path and fails with `clahe_image_too_small`. The test verifies restoration of exact acknowledged Custom values and unchanged attempted/saved preference revisions. Independent review corrected its disk oracle to await the successful save before inducing the failure.

The actual scene first failed on the absent local-contrast controls. Its completed 16-case route covers loaded exact text, no-edit focus, Enter and Tab commits, invalid input, stage retention, held-slider and dirty-text replacement, complete settings reopen, camera authority, paused Compare and minimum/default layouts. The initial paused screenshot check found two changed software-rendered bar-corner pixels while the frame identity and image rectangles stayed identical. Consecutive unchanged-scene captures matched; the change first appeared after the new switch's hover interaction. Frame-completion waits did not reliably resolve it and were removed. A pointer-exit wait also passed once but failed the full Debug suite. Disabling only the provisional hover popup then passed three consecutive scene runs. The final component removes that popup and keeps the expanded CLAHE description in accessibility metadata, consistent with the other QML processing controls. The test retains immediate exact pixel comparisons after invalid input, toggles, replaced gestures and Reset; it contains no render/hover waits, pixel tolerance or crop relaxation. The production renderer is unchanged. All diagnostic attempts and images are retained separately from final evidence.

Both full builds and `all_qmllint` pass. Full non-hardware/non-desktop CTest passes **77/77 Debug in 79.53 s** and **77/77 Release in 48.23 s**, including the 31-case adapter and 16-case actual-QML scene suites. Independent adapter, UI and final source reviews have no remaining actionable findings. Final verification uses `accepted-*` command records; earlier `verified-*` records retain the failed popup-dependent Debug attempt and are not substituted for final evidence.

The six native actual-scene runs each pass **16/16**, on the same source:

| Rendering setup | Debug | Release |
|---|---|---|
| X11/software | 16/16 | 16/16 |
| X11/threaded OpenGL | 16/16 | 16/16 |
| X11/threaded OpenGL, DPR 2 | 16/16 | 16/16 |

Captures preserve distinct Original/Compare layouts at 900×600 and 1280×800, exact local-contrast fields, validation feedback and paused images. Native source-bound geometry from the final Debug/software run supplies the staged driver's coordinates.

## Fresh staged application

A regenerated Release import scan and fresh `QmlPilot` installation at `/tmp/lumora-qml-local-contrast-stage.eut4f0d8` pass the existing dependency checker: 182 inventory entries and **113 ELF objects, zero errors**. The installed executable SHA-256 is `5c52946f307817b71739b4e0c309b599481846f783cf8fe85b44931f6a5f73d3`; installation rewrites RPATH, so its identity is recorded separately from the build executable.

The real executable runs from a neutral directory with isolated preferences/data/cache and a whitelisted environment. XTest drives Select → Connect → Apply → visible readback review → Confirm → explicit Start, then Standard. Three nondefault tone edits establish a preservation baseline. Actual Tab navigation reaches clip limit and tile grid; live edits persist exact `3.123456789012345` and `12`. Entering `3.5` into the grid produces the inspected whole-number validation message and restores `12`; the configuration bytes and modification time remain unchanged during six further samples over 0.9 seconds.

Compare/Fit presents visibly changing images before Pause. Both fitted image interiors contain meaningful detail: Original spans 0–255, Enhanced spans 52–255, with 13,344 bright pixels in each pane. Paused clip `4.23456789123456` and grid `16` acknowledge and save while every retained image-ROI sample stays identical. Each edit compares the complete cumulative pipeline and preserves nondefault tone values, the confirmed SIM camera profile and the saved-recipe collection. This fresh stage's collection is empty; nonempty saved-recipe preservation and reopen are covered by the compiled suites.

Reset persists Original with clip 2/grid 8 and CLAHE off, retaining the frozen image. Resume changes pixels; Stop settles them; restart changes them again. Normal `WM_DELETE_WINDOW` while streaming exits **0**, with no timeout or forced cleanup. The driver passes on its first run in **32.979 s**. Early and late runtime-map snapshots each identify 18 Qt libraries loaded from the stage. All 10 loaded plugins, including late image-format plugins, resolve inside the stage. Independent inspection of the full import and file-access traces finds no successful development-prefix or outside-stage Qt imports. The traces and inspected operator captures remain under `staged-ui-final/`.

For local evaluation:

```bash
/tmp/lumora-qml-local-contrast-stage.eut4f0d8/bin/lumora_qml_app
```

The toolchain remains GCC 15.2 with matching official Qt 6.11.1; both application configurations link the Release Qt SDK. Native OpenGL uses Mesa llvmpipe. These checks establish no Windows, camera hardware, physical-GPU/display or designated-workstation performance acceptance. The earlier full Stage 1 native Widgets/renderer matrices, relocation and performance records retain their original identities and were not repeated for this bounded editor slice. The internal stage retains the existing notice scope and is not an accepted installer.

## Continuation

Next are denoise, sharpen and invert editors. Preset save/rename/delete, stopped camera settings and installation editing remain in Stage 2; fullscreen, sidebar collapse and saved layout preferences follow in Stage 3. This local continuation adds no merge, push, hosted CI, native Windows, camera hardware or designated-workstation performance acceptance.
