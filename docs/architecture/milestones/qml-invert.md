# QML Invert — Stage 2F

This bounded slice follows the [sharpen checkpoint](qml-sharpen.md) `3bafb3c` on `codex/qml-foundation`, in `/home/mo/code/Lumora/.worktrees/qml-foundation`. The [migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and existing Widgets behavior remain the parity baseline. Main remains `e4520dd`; Widgets remains the default launcher.

## Behavior and ownership

The processing panel gains one Invert checkbox after Sharpen. It projects `ProcessingAdapter::invertEnabled` and submits deliberate clicks through `setInvertEnabled(bool)`. Invert has no numeric parameters. The existing typed stage-edit helper copies the current complete draft, changes only Invert's enablement and uses the shared model/coordinator for activation, rollback and acknowledged-only persistence. Loading, unavailable and closing guards remain in that common path.

All shipped presets and Reset to Original disable Invert; saved recipes may enable it. Existing same-value behavior is retained: an explicit edit to a named recipe selects Custom, while repeating the same value in an already-Custom draft causes no further submission. Refreshing or focusing the checkbox alone does not edit the recipe.

The existing C++ stage computes the exact canonical U16 complement, `65535 - input`, for Enhanced output. Raw/Original, camera commands, processing algorithms, schemas, frame ownership and rendering are unchanged. A paused image stays frozen across processing edits; Resume presents newly processed frames.

## Verification

Implementation source is `77f55e9`, with production/test/build source manifest SHA-256 `e48d8cde59c9cbd48ee93dee13778d14d13f39d30bfc9ef63aa8e788c7b3b157` covering 409 files. New evidence is retained under `out/qa/qml-invert`; the execution/review ledger is `.superpowers/sdd/2026-09-14-qml-invert`. Earlier records remain untouched. `verification.json` binds **960 artifact hashes and 13 successful final commands**; eight diagnostic commands remain separate. Its SHA-256 is `c8915ca55b63484d3c1a3b1a4457b04f098b2590e92f41bea9355a5f17d1e2f0`. Source, helper, configuration and staged audits are included in those hashes; the final `newfinal-audit.md` separately binds the manifest and final documentation to avoid a circular hash. Documentation successors do not change the verified source.

The adapter's compileable stub produced three expected behavior failures, with the new lifecycle guard and inherited cases passing. The completed adapter passes **47/47**. Four new tests cover loaded enabled recipes without submissions, both toggle directions with full preceding-stage preservation, shared same-value semantics, loading/load-failure/closing guards, and real 8×6 activation followed by durable save and reopen of a complete seed-derived pipeline and nonempty saved collection. Existing CLAHE tests retain real preparation-failure rollback coverage; Invert has no artificial image-size rejection.

The scene first failed specifically at the absent `invertEnabled` control. The completed **19-case** suite passes with QML lint. It checks saved checked-state projection without writes, actual forward Tab reachability, pointer and Space toggles, full durable recipe comparisons, Standard/Reset replacement, connected/stopped settings reopen and minimum/default Original/Compare layouts. Paused Compare retains exact full-viewport pixels, source identity, shared rectangles and confirmed camera readback. Resume advances the source frame. No pixel tolerance or crop relaxation was introduced. Independent source review approves all five changed files with no open findings.

Full Linux Debug/Release builds and `all_qmllint` pass. Non-hardware/non-desktop CTest passes **77/77 Debug in 83.61 s** and **77/77 Release in 53.01 s**, including the focused adapter and actual-scene suites. The existing informational unused-import message in ViewingToolbar remains. Both configurations use the matching official Qt 6.11.1 Release SDK and the existing GCC/Ninja/dependency configuration; this slice changes none of those inputs.

All six native actual-scene runs pass **19/19** on the final source: X11/software, X11/threaded OpenGL and X11/threaded OpenGL at DPR 2, in both Debug and Release. Original/Compare captures cover 900×600 and 1280×800. The staged driver's dedicated focus geometry comes from the final Debug/software run, with Advanced collapsed and Invert reached through forward Tab navigation. General layout captures also cover Invert with Advanced expanded. OpenGL uses Mesa llvmpipe; no physical GPU/display or Windows acceptance is implied.

## Fresh staged application

A regenerated Release import scan and fresh `QmlPilot` installation at `/tmp/lumora-qml-invert-stage.duzpdqrx` pass the dependency checker: **182 inventory entries and 113 ELF objects, zero errors**. The installed executable SHA-256 is `283a0e8a3690a5978c7cfc8dd41a0aba50c8a5c51be84c24d8818f3b7b70661f`. Installation rewrites RPATH, so this identity is separate from the build executable.

The two-process real-app driver passes on its first attempt in **87.153 s**, with **335 steps and 142 PNG captures**. It uses isolated preferences/data/cache, a whitelisted environment and a neutral working directory. Startup follows Select → Connect → Apply → visible readback review → Confirm → explicit Start, then Standard. Distinct nondefault tone, gamma, CLAHE, Gaussian-denoise and sharpen parameters establish the preservation baseline. Live pointer-on and Space-off edits save the complete expected pipeline while preserving the confirmed camera profile and saved collection.

Paused Compare remains pixel-identical across Space/pointer toggles, Reset to Original and Standard replacement. Independent review recomputes **18 identical paused-image ROI captures** and all 142 recorded capture hashes. Both fitted images contain meaningful detail: Original spans 0–255 and Enhanced 44–255. Resume changes pixels, Stop settles them and restarting acquisition changes them again. The compiled scene additionally covers zoom/pan, source identity and shared image rectangles.

Before closing, the driver restores the nondefault preceding stages and enables Invert. A second actual process reopens the complete recipe while connected and stopped, with no automatic acquisition. The settled checkbox is checked; an Invert-only off edit produces a new durable save preserving every other field, followed by an on save and Reset. Both normal `WM_DELETE_WINDOW` closes exit **0**, with no forced cleanup or X11 errors. Settled screenshots verify checked/unchecked states and the actual 83×40 checkbox pointer geometry. The fresh staged saved collection is empty; the compiled tests separately establish nonempty saved-collection preservation.

Independent staged review reconstructs all **40 acknowledged preference snapshots** against complete expected pipelines, including camera and collection preservation, and verifies four no-write disclosure/navigation observations. All 182 inventory entries match the installed files or symlink targets. Four process maps each show 18 stage-local Qt libraries; both complete import/plugin logs show ten stage-local plugins. Both complete file-access traces, including resumed calls, relative operands and symlink targets, show no successful development-prefix access or outside-stage Qt/QML/plugin loads. Source, driver, installed executable and all eight geometry inputs match their recorded hashes. No open findings remain in the staged audit.

For local evaluation:

```bash
/tmp/lumora-qml-invert-stage.duzpdqrx/bin/lumora_qml_app
```

## Continuation

Invert completes the basic QML processing editors. Preset save/rename/delete, stopped exposure/gain/FPS/ROI/format settings, capability-aware camera controls and operator/admin installation orientation remain in Stage 2. Stage 3 adds fullscreen, sidebar collapse and saved layout preferences. The default launcher switch follows later Linux/Windows runtime verification. No merge, push, hosted CI, Windows, camera-hardware or performance acceptance is added by this local checkpoint.
