# QML denoise — Stage 2D

This bounded slice follows the [local contrast checkpoint](qml-local-contrast.md) `008b46c` on `codex/qml-foundation`, in `/home/mo/code/Lumora/.worktrees/qml-foundation`. The [migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and existing Widgets controls remain the parity baseline. Main remains `e4520ddd44d14256552fe1bd53111d5e61f195ce`; Widgets remains the default launcher.

## Behavior and ownership

The QML processing area gains a Denoise switch, Gaussian/Median mode selection, supported kernel choices and an exact sigma field. Gaussian supports kernels 3, 5 and 7 with finite sigma from 0 to 5; zero requests the existing automatic sigma calculation. Median supports kernels 3 and 5 and keeps sigma at zero. Sigma remains visible but disabled in Median, and has no slider. Disabling denoise retains its parameters and disables the editors.

Switching from Gaussian to Median changes the complete draft atomically: kernel 7 becomes 5 and sigma becomes zero. Returning to Gaussian retains the current kernel and zero sigma. Choosing the already selected mode or kernel leaves a named preset selected. Exact saved text is projected by the C++ adapter; deliberate Enter or focus-loss edits use the existing whole-draft commit path.

A mode replacement cancels unfinished sigma input before the field is disabled. Preset/Reset replacement also cancels unfinished text and dismisses open denoise dropdowns. Input validation happens before conversion or submission. The existing C++ model and coordinator continue to own activation, rollback and acknowledged-only persistence. Processing algorithms, settings schema, camera commands, frame ownership and rendering are unchanged.

## Verification

Adapter `a52afee`, QML/scene source `ff6759d` and test-only successor `ae76851` implement the slice. The final 408-file production/test/build source manifest is SHA-256 `43feda741cb33575928d3affc567b725b7beea01fef2a0a709b9fa10bdf2f10c`. Documentation successors do not change it. Command records, source manifests, captures and packaged-application checks are retained under `out/qa/qml-denoise`; the execution/review ledger is `.superpowers/sdd/2026-09-14-qml-denoise`. Earlier evidence is preserved. `out/qa/qml-denoise/verification.json` binds all 408 source files, 706 retained artifact hashes and 13 successful final commands; diagnostic commands remain separate. Manifest SHA-256: `805aaf1c1bc9a47369c6d25b15ae0b5b4786cc71c77eb81e50a1db69e38b9c8f`. The independent audit reports are retained with the evidence.

The adapter's compileable stubs produced five expected behavioral failures; all 31 inherited cases and the new lifecycle guard passed. The completed adapter passes **37/37**, including exact loaded values without submission, atomic mode replacement, same-option no-ops, invalid input, loading/closing guards, durable activation and reopening a nonempty recipe collection. The real 8×6 source supports all valid denoise kernel choices; no artificial image-size rejection was introduced. Existing real CLAHE failure tests continue to cover the shared activation rollback path.

Independent review strengthened the save/reopen oracle to compare the full active pipeline directly with the seed recipe after changing only denoise. The earlier persisted-to-acknowledged comparison could have missed a shared accidental reset. That test-only correction is `ae76851`; its focused suite passes 37/37. The initial full verification batch was interrupted to include this correction; its partial Debug log remains diagnostic. An initial adapter compile error and a direct test invocation without an available Qt platform are also retained separately from successful verification.

The actual scene first failed at the absent Denoise control. Its completed **17-case** route checks saved exact text, visible mode/kernel labels, no sigma slider, no-op selections, Enter/Tab commits, invalid input, stage retention, atomic mode changes, dirty sigma cancellation, open-dropdown cancellation on Reset, full settings reopen and minimum/default Original/Compare layouts. Paused Compare preserves exact full-viewport pixels, source frame identity, viewport rectangles and confirmed camera readback throughout edits and Reset. Resume advances the displayed frame. The scene uses no pixel tolerance or crop relaxation.

Both full builds and `all_qmllint` pass. Full non-hardware/non-desktop CTest passes **77/77 Debug in 79.50 s** and **77/77 Release in 49.82 s**, including the adapter and actual-scene suites. QML lint retains the existing informational unused-import message in ViewingToolbar. Independent adapter, UI and whole-slice source reviews have no remaining actionable findings. The six native actual-scene runs each pass **17/17** on the final source:

| Rendering setup | Debug | Release |
|---|---|---|
| X11/software | 17/17 | 17/17 |
| X11/threaded OpenGL | 17/17 | 17/17 |
| X11/threaded OpenGL, DPR 2 | 17/17 | 17/17 |

Distinct captures retain denoise controls in Original/Compare at 900×600 and 1280×800, including the DPR 2 runs. The final Debug/software geometry supplies the staged driver's coordinates.

## Fresh staged application

A regenerated Release import scan and fresh `QmlPilot` installation at `/tmp/lumora-qml-denoise-stage.cfjgq7bd` pass the dependency checker: 182 inventory entries and **113 ELF objects, zero errors**. The installed executable SHA-256 is `0020117f8cc2d26bc1fa6c78d29de9a73c0f5da115f04f7d0673c938f9e4bba9`. Installation rewrites RPATH, so its identity is recorded separately from the build executable.

The actual executable runs from a neutral directory with isolated preferences/data/cache and a whitelisted environment. XTest drives Select → Connect → Apply → visible readback review → Confirm → explicit Start, then Standard. Nondefault brightness, contrast, gamma and CLAHE values establish the preservation baseline. Actual Tab navigation reaches sigma and both dropdowns. The live route sets exact sigma `3.123456789012345` and kernel 7, toggles denoise off/on without losing them, switches to Median/5/0, then returns to Gaussian/5/0. Each save compares the complete cumulative pipeline, confirmed camera profile and saved-recipe collection.

Entering sigma `-1` while live and paused produces the visually inspected finite-number/range message and leaves configuration bytes and nanosecond modification time unchanged through six samples over 0.9 seconds each. Paused Compare contains meaningful detail in both fitted image interiors: Original spans 0–255 and Enhanced 59–255. Exact sigma edits, kernel/mode changes, rejection and Reset preserve every retained image-ROI sample. Reset saves Original with Gaussian/3/0/off. Resume changes pixels, Stop settles them, and camera restart changes them again.

Before closing, the driver saves Custom Gaussian/7/`4.23456789123456`. A second actual process opens the same isolated settings and visibly presents that exact value while the camera is connected and stopped, awaiting explicit Resume saved Live. Changing only sigma to `3.123456789012345` creates a new durable whole-pipeline save retaining kernel 7; this cannot be satisfied by rereading the old file. Final Reset saves Original. Both normal `WM_DELETE_WINDOW` closes exit **0**, with no timeout or forced cleanup. The complete two-process driver passes on its first run in **57.464 s**.

The fresh staged collection is empty; preservation and reopening of nonempty saved recipes are covered by the compiled tests. Atomic normalization and same-option no-ops are also established by compiled tests rather than inferred from the staged file snapshots. The independent staged audit reconstructs all 29 saved snapshots against expected complete pipelines and recomputes 30 identical paused-image ROI captures. All 182 stage inventory entries match the actual files or symlink targets. All four early/late map snapshots show 18 Qt libraries from the stage, and both processes load all ten plugins from the stage. Inspection of both complete import logs and reconstructed file-access traces, including relative pathname operands, finds no successful development-prefix or outside-stage Qt dependency access. The reports and raw evidence remain under the execution ledger and `staged-ui-final/`.

For local evaluation:

```bash
/tmp/lumora-qml-denoise-stage.cfjgq7bd/bin/lumora_qml_app
```

Both application configurations use the matching official Qt 6.11.1 Release SDK. Native OpenGL uses Mesa llvmpipe. These checks establish no Windows, physical-GPU/display, camera-hardware or designated-workstation performance acceptance. Earlier full Stage 1 native Widgets/renderer matrices, relocation and performance records retain their original source identities. The internal stage retains the existing notice scope and is not an accepted installer.

## Continuation

Next are sharpen and invert editors. Preset save/rename/delete, stopped camera settings and installation editing remain in Stage 2; fullscreen, sidebar collapse and saved layout preferences follow in Stage 3. This local continuation adds no merge, push, hosted CI, native Windows, camera hardware or designated-workstation performance acceptance.
