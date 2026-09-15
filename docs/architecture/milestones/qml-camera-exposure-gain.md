# QML stopped exposure and gain — Stage 2G

This slice follows the [Invert checkpoint](qml-invert.md) `996abea` on `codex/qml-foundation`, in `/home/mo/code/Lumora/.worktrees/qml-foundation`. The [migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and existing Widgets camera workflows remain the parity baseline. Main remains `e4520dd`; Widgets remains the default launcher.

## Behavior and ownership

The Camera settings button opens a nonmodal QML dialog with advertised exposure/gain modes and exact numeric entries in microseconds and decibels. It leaves the left camera controls accessible for Stop and Disconnect, uses a stable dialog height, and scrolls its contents when needed. Opening an already-open dialog retains the current draft. Closing discards unapplied edits; reopening obtains a new source-bound draft.

The former Widgets draft lifecycle now lives in `presentation::CameraSettingsDraft`, used by both frontends. It captures camera identity, generation, capabilities, requested revision and current readback. It normalizes fixed fields while retaining writable operator choices, protects unfinished editing from late preference reconciliation, recognizes its own admitted Apply, and invalidates stale camera/session/capability/fixed-readback or external-request changes. Widgets retains its existing controls and numeric/ROI representability checks.

The typed QML adapter owns only editor text, cached manual values and presentation. Every deliberate command checks current shared state. Editing remains local, including incomplete text; invalid text stays visible and blocks Apply. Mode and value access are independent. Automatic requests omit numeric values; switching back to Manual restores the cached writable entry or authoritative fixed readback. A missing read-only Manual measurement remains absent and cannot be applied. Capability increments are step hints, not numeric alignment constraints: finite fractional requests within inclusive bounds retain their precision.

Apply submits the complete source-tagged configuration through the existing coordinator, preserving FPS, ROI, format and acquisition mode. Actual readback remains distinct from the requested values; the simulator can round a fractional request to its register increment. Apply does not Confirm, Start or persist a confirmed profile. The operator reviews current readback and explicitly confirms before starting. Streaming permits inspection only, including while the viewer is paused. Installation work, pending camera operations, source replacement and shutdown retain their C++ guards.

Processing definitions, algorithms, frame ownership, rendering, camera workers and persistence schemas are unchanged. The production simulator models exposure/gain registers and readback, not changes to scene brightness.

## Verification

Production source is `44a33e2`; final verification uses test-only successor `d041c76`, with source manifest SHA-256 `de689e0c3c2350d5f9efca2a2c4925dc1aec25ee815b1ded5c66f27ac77aed75` covering 416 production/test/build files. New evidence is retained under `out/qa/qml-camera-exposure-gain`; the execution/review ledger is `.superpowers/sdd/2026-09-15-qml-camera-exposure-gain`. Prior evidence remains unchanged.

The shared model's compileable stubs produced seven expected failures; its completed tests pass **7/7**, and all **33/33** unchanged Widgets dialog tests pass through the extraction. The adapter first failed four real-coordinator tests against stubs. Its final **7/7** suite covers exact local text, cancellation, invalid finite/range input, full fractional requests versus rounded actual values, Automatic request omission, independent mode/value access, missing read-only measurement, repeated-open retention, current-state transitions, installation pending, session drift and closing. A focused review regression failed on misleading read-only guidance before the fix.

The actual scene first failed at the missing Camera settings button. Its completed **20-case** suite and QML lint pass. The new workflow uses pointer/keyboard entry, Tab/Enter, cancel and invalid input; observes unchanged camera revisions, complete requested/readback values and preference bytes before Apply; checks exact requested `2345.678912345678` µs / `3.456789123456789` dB against actual `2346` / `3`; requires fresh confirmation; and verifies durable saving and settings reload in a new runtime. Streaming and viewer Pause reject editing; real Stop/Disconnect remain usable with the dialog open. Original/Compare layouts cover 900×600 and 1280×800.

The first full scene run exposed a test timing error at fixture restoration: a retained stopped image satisfied `hasFrame()` before the asynchronous Start completed. The corrected oracle waits for Streaming and a newly displayed source frame. All 20 cases then pass. Independent shared-policy and QML source reviews have no open findings.

The first native Release threaded OpenGL attempt then exposed a distinct test assumption: Paused alone did not ensure a visible completed bundle while the renderer restored a temporarily withdrawn image around screenshot capture. The test pinned an empty ID and then saw restored frame 10; its early exit caused later shared-fixture failures. The test-only successor waits for Paused plus a visible completed frame, pins a nonempty ID, and still requires exactly that ID after capture and restoration. It also strengthens the earlier Start wait to require a new displayed frame. A focused native reproduction passes **4/4** including startup/cleanup; the independent addendum approves the strict identity check. No presenter, renderer or production behavior changed. The earlier `accepted-*` records remain diagnostic evidence; the successor uses separate `verified-*` records.

The final full Linux Debug and Release builds, including `all_qmllint`, pass. Their complete non-hardware/non-desktop CTest suites pass **79/79** each in **85.15/53.61 seconds**. All 77 previous test groups remain registered; the shared draft and QML settings adapter add two groups. Both configurations use GCC 15.2 and the matching official Qt 6.11.1 Release SDK. The configuration review recomputes the complete source manifest and confirms preserved Widgets coverage. QML lint retains only the inherited ViewingToolbar unused-import information message.

All six final native actual-scene runs pass **20/20**, with no failed, skipped or blacklisted cases:

| Backend | Debug | Release |
| --- | ---: | ---: |
| Software | 26.129 s | 20.532 s |
| Threaded OpenGL | 29.147 s | 21.734 s |
| Threaded OpenGL, DPR 2 | 65.993 s | 37.716 s |

These are QtTest suite times, not performance measurements. Xvfb OpenGL uses Mesa llvmpipe. Inspected native screenshots show exact request/current readback, retained invalid input, accessible priority camera controls and contained 900×600/1280×800 dialogs, including DPR 2. All 13 final Debug/software geometry states used by the staged driver match the previously inspected control positions, sizes and availability.

## Installed application

A fresh Release `QmlPilot` stage at `/tmp/lumora-qml-camera-exposure-gain-stage.mz9y5sd4` passes inventory/loader checks for **182 entries and 113 ELF objects**, with zero errors. Its executable SHA-256 is `4abb62b6ea19f891df9417af524b392af39f034592a31462884e87e68ea9c7f4`. The real application runs under native X11/software rendering from a neutral directory with isolated HOME/XDG preferences and a minimal explicit environment.

The complete staged workflow passes in **45.857 seconds** (`verified-staged-ui2`, `staged-ui-final2/result.json`). Real pointer/keyboard input verifies canceled drafts restore the original fields, invalid input does not save, Apply leaves Start disabled and preferences unchanged until explicit Confirm, and the full confirmed profile distinguishes exact requested values from actual 2346 µs / 3 dB. Streaming and viewer-paused inspection reject numeric input; real Stop works with the dialog open. A second process reopens the exact requested values without acquisition, then applies/confirms exposure `3456.789123456789` µs while preserving gain and every unrelated persisted value; actual exposure is 3457 µs. Both processes close normally, with no forced cleanup or X11 errors.

The paused-pixel check covers an unobscured Original pane after real Compare/Fit input. It retains the same pixel hash through disabled-input/no-write probes and verifies actual image detail: the inspected interior contains values 0–255 with 85,860 low and 12,960 high pixels. It cannot pass on an empty black region hidden by the dialog. Inspected screenshots show the two requested/readback pairs and truthful waiting-for-frame state after reopening.

The first staged attempt remains preserved separately: its driver waited for a focused field to settle while its caret blinked. The corrected helper transfers focus to the preceding mode control after enabled text entry, retaining the full-field comparison and unchanged disabled-field probes. This QA-only change leaves production and the source manifest untouched. Earlier geometry review also corrected the gain position after an exposure validation message and the settings-button position when saved Resume is offered. The final driver records its own hash and the unchanged Invert helper dependency; the original failed driver is retained byte-for-byte.

Independent staged review recomputes the entire installed inventory, four startup/late library maps, complete file traces including relative paths and resumed calls, the full persisted documents, and screenshot pixel hashes. It finds no successful development fallback or outside-stage Qt/plugin load and no open workflow finding. The newly created saved-preset collection is empty in this staged scenario; existing populated-preset behavior remains covered separately by the scene and earlier checkpoints.

`out/qa/qml-camera-exposure-gain/verification.json` binds **13 final commands, 416 source files and 1,815 evidence artifacts**, including diagnostics, captures, binaries, helpers and frozen independent reviews. Its SHA-256 is `7b42e74d9c932053832b3b8827b3a0360c97f1116479f1cb3891c2a0ffe8173b`. Shared, QML source, test-oracle, configuration and staged reviews have no open findings. The separate ledger report `final-audit.md` binds the manifest and final documentation; it is excluded from the artifact hashes to avoid a self-referential manifest.

## Continuation

Stopped FPS editing is the next small camera slice. ROI/format editing, remaining capability-aware controls, operator/admin installation orientation and preset save/rename/delete remain in Stage 2. Stage 3 adds fullscreen, sidebar collapse and saved layout preferences. The default launcher switch follows later Linux/Windows runtime verification. This local checkpoint adds no merge, push, hosted CI, camera-hardware, Windows or performance acceptance.
