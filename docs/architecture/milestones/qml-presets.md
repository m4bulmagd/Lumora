# QML preset selection and Reset processing — Stage 2A

The [approved plan](../../superpowers/plans/2026-09-14-qml-presets.md) is implemented locally on `codex/qml-foundation`, from baseline `55aae99` through source `209222a853328f881cc30b7e0137623b21c8fb6d`. Work remains in `/home/mo/code/Lumora/.worktrees/qml-foundation`; main remains `e4520ddd44d14256552fe1bd53111d5e61f195ce`. Widgets is still the default launcher. This is the first editor-parity slice after the [Stage 1 live workstation](qml-live.md), with no merge or push.

## Behavior and boundary

The processing sidebar now offers Original, Standard, High Contrast, Soft Detail, the existing Custom editing state, and loaded saved recipes. Rows preserve repository order, exact IDs and saved names. Numeric editing returns the selection to Custom. Reset processing uses the shared model's Reset to Original; it preserves camera state/readback/confirmation, display mode, Pause and viewport zoom/pan.

`ProcessingAdapter` projects preset metadata and the selected draft ID into typed QML properties. It sends deliberate selection/reset commands to the existing `ProcessingControlsModel`. The ComboBox sends a preset command only on operator activation; refreshing its model or focusing the selector does not submit a preset. Row notifications remain stable across numeric and pending-state refreshes. Loading, unavailable and closing commands reject before model mutation.

The coordinator retains submission, matching activation completion and persistence authority. The selector shows the draft while the acknowledged-active summary retains the accepted recipe until completion. Failed activation restores the accepted selection and does not save the failed recipe. Camera workflows, processing definitions/algorithms, schema and renderer/frame ownership are unchanged. The pilot retains its separate `LumoraQmlPilot` preference identity.

## Verification

The final production/test/build source manifest is SHA-256 `a640a2eaeabf6519917eb232dea12c89edc3bd171c72616bb538104a6a376ac4`. Documentation successors do not change it. `out/qa/qml-presets/verification.json` retains command/log hashes, source and binary identities, captures, stage inventory and review pointers. The task ledger and independent adapter/UI reviews remain under `.superpowers/sdd/2026-09-14-qml-presets`.

Both complete Linux builds and `all_qmllint` pass. Full non-hardware/non-desktop CTest passes **77/77 Debug in 76.92 s** and **77/77 Release in 45.89 s**. The adapter suite now contains 16 cases, and the actual QML scene contains 14 cases. Native real-scene results are:

| Rendering setup | Debug | Release |
|---|---|---|
| X11/software | 14/14 | 14/14 |
| X11/threaded OpenGL | 14/14 | 14/14 |
| X11/threaded OpenGL, DPR 2 | 14/14 | 14/14 |

The adapter tests cover delayed/failed load, exact saved IDs/names, stable metadata notifications, invalid/unavailable/closing commands, Custom after numeric editing, pending versus acknowledged state, complete recipe/collection persistence and reopening. Standard deliberately fails real CLAHE preparation on the unchanged 8×6 fixture; rollback, unchanged active revision and no failed-preset save are verified. A later Reset clears the model error.

The actual QML tests select all four shipped recipes and a saved fractional recipe through keyboard input, use the Reset button, wait for acknowledgment and codec persistence, and reopen the accepted High Contrast selection alongside the existing guarded saved-camera Resume flow. During paused Compare, both panes must contain visible simulator detail; their exact pixels, source ID, image rectangles, camera session/confirmation and readback remain unchanged across selection and Reset. Resume displays a newer frame. Existing exact numeric input, viewport Tab navigation, minimum/default layouts, fallback/retry and renderer-aware close coverage also passes.

Independent adapter and UI reviews are approved with no open findings. A separate final audit approves the 403 source hashes, 136 artifact hashes, 13 successful final command records, staged runtime/persistence evidence and corrected pixel oracle. No open actionable finding remains. Full Stage 1 renderer/Widgets native matrices, relocation and performance results retain their original identities; they were not repeated for this adapter/UI-only slice. The full CTest runs include inherited model, runtime and renderer regressions.

### Test-first work and corrected finding

Compileable adapter stubs produced six expected behavioral failures before implementation. The actual scene then failed on absent preset/reset controls before the QML component was added. Those RED and GREEN logs are retained.

Visual inspection and independent review found that the first new paused-pixel check could compare two all-black crops: centered 100% plus 1.25× zoom hid the simulator's moving bar. A bright/dark-detail assertion reproduced this weakness. The corrected fixture uses Fit plus 1.1× zoom and a verified nonzero pan, then requires a non-null image with visible detail in each pane before checking equality. Fresh native captures show the preserved bar across selection and Reset. No renderer change was needed. Minimum-size layout captures can still crop the bar after resizing an intentionally preserved manual zoom; they establish control geometry, not the paused-pixel oracle.

Earlier command manifests before the new QML component was tracked omit that then-untracked file and are only development history. Final build/native/stage records include all final tracked source files. The `visible-pixels-green` run precedes recompilation of the added Reset session assertion; the final Debug/Release builds and all final runs include it. No incomplete early manifest or intermediate binary supplies final acceptance evidence.

## Fresh staged application

The Release import scan was regenerated before building and installing the Linux `QmlPilot` component at `/tmp/lumora-qml-presets-stage.qoH6F6`. Its 182-entry inventory includes 42 symlinks; the existing checker passes all **113 ELF objects with zero errors**. The staged executable SHA-256 is `4848700ca3c1daef0424fd6ce95c344eadf677921f951b1fd44b12bd35533c22`, recorded separately from the build executable because install rewrites RPATH.

The real staged executable runs from a neutral directory with isolated temporary preferences/data/cache and a whitelisted environment, using its own `qt.conf`. XTest drives Select → Connect → Apply → readback review → Confirm → Start, then High Contrast and Standard while live. It verifies their durable selected IDs/full pipeline envelopes and unchanged confirmed camera preferences. While paused, selecting High Contrast and Reset to Original preserve the identical image ROI throughout polling and captures. Resume changes pixels; Stop freezes them; restart changes them again. Normal `WM_DELETE_WINDOW` while streaming exits **0**, without timeout or forced cleanup, in **20.528 s** for the full driver.

Runtime evidence confirms 18 Qt libraries and 10 plugins load from that stage, QML imports use stage paths or the compiled application resource, and there are zero successful development-prefix accesses. Saved custom recipe selection/reopening and rejected activation are verified by the compiled tests; the staged driver covers built-ins and Reset. This stage was not relocated again; Stage 1 retains that separate proof.

Commands and artifacts are `final-stage-install.json`, `final-stage-check.json`, `final-stage-inventory.json`, `final-staged-ui.json` and `staged-ui-final/` under the new QA directory. Stage 1 evidence is untouched. The retained stage can be launched directly for local evaluation:

```bash
/tmp/lumora-qml-presets-stage.qoH6F6/bin/lumora_qml_app
```

The toolchain remains GCC 15.2 and matching official Qt 6.11.1; both application configurations link the Release Qt SDK. Native OpenGL uses Mesa 26.0.8 llvmpipe, not a physical GPU. This establishes no native Windows, camera hardware, physical-display, designated-workstation performance or formal acceptance. The internal stage retains the existing notice scope and is not an accepted redistributable installer.

## Continuation

Next are the remaining processing editors: brightness/contrast, gamma, local contrast, denoise, sharpen and invert. Preset save/rename/delete, stopped camera settings and installation editing remain in Stage 2; fullscreen/sidebar/layout preferences follow in Stage 3. The [migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and preserved Widgets behavior remain the parity baseline.
