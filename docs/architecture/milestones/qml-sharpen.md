# QML sharpen — Stage 2E

This bounded slice follows the [denoise checkpoint](qml-denoise.md) `26c761d` on `codex/qml-foundation`, in `/home/mo/code/Lumora/.worktrees/qml-foundation`. The [migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and existing Widgets processing behavior remain the parity baseline. Main remains `e4520ddd44d14256552fe1bd53111d5e61f195ce`; Widgets remains the default launcher.

## Behavior and ownership

The QML processing area gains a Sharpen switch, exact Amount entry and a linear 1,000-step slider. Amount accepts finite doubles from 0 to 5. An Advanced disclosure starts closed and reveals exact Radius (0.5–5) and Threshold (0–65535) fields without sliders. Threshold retains fractional values despite the Widgets numeric step of 1. Radius remains the exact Gaussian sigma used by the existing backend, without snapping to derived kernel sizes.

Advanced is QML presentation state: opening it does not edit or submit the pipeline or save preferences. Its open state survives preset/reset and stage toggles within a window, and starts closed in a new window. The migration design's disclosure keeps advanced settings out of the default adjustment view; Widgets uses an initially unchecked group to gate those same parameters. Disabling sharpen retains all three values and disables its editors.

Exact text uses the C++ adapter's existing roundtrip formatting and validation. Amount drags reuse the shared model's coalescing; parameterless Release flushes the current complete draft. Preset/Reset replacement cancels unfinished field text and the remainder of a held slider gesture. The model and coordinator retain activation, rollback and acknowledged-only persistence. Processing algorithms, schemas, camera commands, frame ownership and rendering remain unchanged.

## Verification

Adapter `53fc550` and QML/scene source `009f8c0` implement the slice. The final 409-file production/test/build source manifest is SHA-256 `1dfa0ddc6f97ff17cd257336b5e1a81815aea84c64fb7dd609791cfcf5bc8fa6`. Command records, source manifests, captures and packaged-application checks are retained under `out/qa/qml-sharpen`; the execution/review ledger is `.superpowers/sdd/2026-09-14-qml-sharpen`. Earlier evidence is preserved. `verification.json` binds all 409 source files, **1,274 artifact hashes and 13 successful final commands**; 21 diagnostic commands remain separate. Its SHA-256 is `145d09733a06c36aadba0a79802e3d55f35793d3df560ab4636e22fc055d16cb`. The final audit separately binds this documentation to avoid a circular manifest hash. Documentation successors do not change the verified source.

Compileable adapter stubs produced five expected behavioral failures. The completed adapter passes **43/43**, including exact loaded values without submission, finite/range validation, lifecycle guards, complete fresh-draft preservation, retained disabled values, coalesced Amount drags, current-draft Release, and durable save/reopen against the full seed-derived pipeline and a nonempty saved collection. All valid sharpen radii prepare on the real 8×6 fixture, including Radius 5's derived kernel 31. Existing real CLAHE failures continue to exercise shared activation rollback; no artificial sharpen image-size rejection was introduced.

The actual scene first failed at the absent Sharpen control. After implementation it exposed a focus-scroll timing defect: expanding Advanced changes content height after the initial focus reveal. Scheduling the existing guarded reveal function on content-height changes fixes this. The test helper now checks the full control rectangle rather than a text field's glyph bounds. A final keyboard route reaches Amount by real forward Tab navigation from the preset selector before capturing geometry for the staged driver. The full scene passes **18/18**, with diagnostic failures retained separately.

The new scene checks exact saved text, UI-only disclosure without activation or persistence, absent Radius/Threshold sliders, Enter/Tab commits, invalid input, disabled retention, keyboard/pointer Amount editing and stale text/gesture cancellation after preset/reset replacement. Paused Compare retains exact full-viewport pixels, frame identity, image rectangles and camera readback; Resume advances the displayed frame. Reopening settings retains the complete pipeline and starts Advanced closed. Original/Compare layouts are captured at 900×600 and 1280×800. No pixel tolerance or crop relaxation was introduced.

Independent review strengthened the disclosure oracle to require unchanged active and attempted/saved preference revisions, in addition to the same named recipe and full pipeline. This rules out a redundant submission or save of identical values. The correction is in the verified source; independent adapter, UI and whole-slice source reviews have no remaining actionable findings.

Both full builds and `all_qmllint` pass. Full non-hardware/non-desktop CTest passes **77/77 Debug in 82.35 s** and **77/77 Release in 51.04 s**, including the adapter and actual-scene suites. QML lint retains the existing informational unused-import message in ViewingToolbar.

All six native actual-scene runs pass on the final source:

| Rendering setup | Debug | Release |
|---|---|---|
| X11/software | 18/18 | 18/18 |
| X11/threaded OpenGL | 18/18 | 18/18 |
| X11/threaded OpenGL, DPR 2 | 18/18 | 18/18 |

The final Debug/software geometry supplies the staged driver's coordinates. Both application configurations use the matching official Qt 6.11.1 Release SDK; native OpenGL uses Mesa llvmpipe. Earlier full Stage 1 native Widgets/renderer matrices, relocation and performance records retain their original source identities.

## Fresh staged application

A regenerated Release import scan and fresh `QmlPilot` installation at `/tmp/lumora-qml-sharpen-stage.xgn363sk` pass the dependency checker: 182 inventory entries and **113 ELF objects, zero errors**. The installed executable SHA-256 is `1e8aebdc7028bbbda45c9e8e2ebd587b33efbdc69bdd16d811fa5b235bbe8842`. Installation rewrites RPATH, so this identity is separate from the build executable.

The first real-app driver attempt passed live exact edits, disclosure, stage retention and invalid-value rejection, then timed out expecting Home to move the Amount slider to zero. A minimal matching-Qt diagnostic confirms Home leaves Slider unchanged without a `moved` signal, while Right changes the value and emits that signal. The corrected driver enters zero through the actual field, then requires Right to save 0.005 and Left to save zero. Application source and installed executable are unchanged. The original driver, failed run and diagnostic remain retained separately from final acceptance.

The corrected two-process driver passes in **113.887 s**, with isolated preferences/data/cache, a whitelisted environment and a neutral working directory. It follows Select → Connect → Apply → visible readback review → Confirm → explicit Start, then Standard. Distinct nondefault tone, gamma, CLAHE and Gaussian-denoise values establish the preservation baseline before exact Amount `1.234567891234567`, Radius `2.34567891234567` and fractional Threshold `13.4567891234567` edits. Every accepted save compares the complete cumulative pipeline, confirmed camera profile and saved collection. Stage off/on preserves all three sharpen values.

Advanced opening/closing and rejected values retain configuration bytes and nanosecond modification time through six samples over 0.9 seconds. Live invalid Radius `0.25` and Threshold `65535.5`, and paused Radius `5.01` and Threshold `-1`, show the inspected finite-number/range messages and restore the accepted text. Immediate `toggled` captures can precede rendering; later focused/validation captures establish settled open/closed states. Retaining the disclosure after Reset is explicitly asserted by the compiled scene; it is below the viewport in the staged Reset screenshot.

Both live and paused keyboard checks save 0.005 then zero. Real pointer drags start at the current handle and traverse 25/50/75% positions, ending at Amount **3.765** with every other pipeline field preserved. Focused and held screenshots match the final scene's 259×40 slider at `(1007, 746)`. Exact field entry restores the intended tuple after slider checks.

Paused Compare has meaningful detail in both fitted image interiors: Original spans 0–255 and Enhanced 56–255. Exact edits, disclosure, validation, keyboard and held/released pointer changes, and Reset preserve the same image ROI throughout the workflow. Reset saves Original with sharpen off and Amount/Radius/Threshold 1/1/0. Resume changes pixels, Stop settles them, and restarting acquisition changes them again.

Before closing, the driver restores nondefault preceding stages and all sharpen parameters. A second actual process opens those settings while connected and stopped, with Advanced initially collapsed. Opening it writes no preferences and presents the exact saved Radius/Threshold. An Amount-only edit to `2.23456789123456` creates a new durable save retaining every other field; final Reset saves Original. Both normal `WM_DELETE_WINDOW` closes exit **0**, with no forced cleanup or X11 errors. The fresh staged collection is empty; nonempty saved-recipe preservation and reopen are established by the compiled tests.

Independent staged review reconstructs all **56 acknowledged pipeline snapshots**, verifies ten no-write observations and all 200 recorded capture hashes, and recomputes **48 identical paused-image ROI captures**. All 182 inventory entries match the installed files or symlink targets. Four process-map snapshots each show 18 Qt libraries from the stage; both processes load all ten plugins from the stage. Both complete import logs and file-access traces, including relative operands and symlink targets, show no successful development-prefix or outside-stage Qt dependency access. The final route contains 464 recorded steps. Audit reports and raw evidence retain these identities and the visual-evidence limits above.

For local evaluation:

```bash
/tmp/lumora-qml-sharpen-stage.xgn363sk/bin/lumora_qml_app
```

This internal stage retains the existing notice scope. It establishes no Windows, physical-GPU/display, camera-hardware, installer or designated-workstation performance acceptance.

## Continuation

Invert is the remaining processing editor. Preset save/rename/delete, stopped camera settings and installation editing remain in Stage 2; fullscreen, sidebar collapse and saved layout preferences follow in Stage 3. This local continuation adds no merge, push, hosted CI, native Windows, camera hardware or designated-workstation performance acceptance.
