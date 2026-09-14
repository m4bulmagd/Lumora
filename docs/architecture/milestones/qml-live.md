# Qt Quick SIM-LIVE workstation — Checkpoint 4

This record follows the [approved migration design](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) and [implementation plan](../../superpowers/plans/2026-09-14-qml-live-workstation.md). The local implementation on `codex/qml-foundation` continues from `916ca7d` through source checkpoint `146f10a254b09360d778a2db3137edde6118a298`. Main remains `e4520ddd44d14256552fe1bd53111d5e61f195ce`. This record describes the SIM-LIVE pilot; it does not grant milestone or release acceptance.

## Integrated boundary

`lumora_qml_app` composes the existing SIM-LIVE provider, clock, installation/preferences services and default LivePipeline. `QmlWorkstation` owns the shared WorkstationCoordinator and FramePresenter, typed Camera/Processing/Viewer adapters and an application-owned QuickImageItem. A QML-created ViewerSurface attaches that item visually without taking QObject ownership. Required root properties receive the adapters. Backend request/session/revision authority and all image pixels remain in C++.

The shell supports guarded selection, Connect, Apply, requested/current review, Confirm, Start, priority Stop/Disconnect, Retry and explicit eligible saved Resume. No automatic stream is introduced. Requested/readback and installation orientation are read-only. Original/Enhanced/Compare uses completed presenter state and one same-bundle renderer receipt. Pause, stale state, UTC/age, orientation and errors follow the displayed image. Window/level edits preserve the loaded pipeline and use the existing model's Commit/Drag/Release admission, matching activation and acknowledged persistence.

`LumoraQmlPilot` provides a separate stable preference identity from Widgets. Existing codecs, user schema 5, machine schema 2 and capability fingerprint 2 remain unchanged. Full camera/preset/installation editors, fullscreen and layout preferences are later stages. Widgets remains the default launcher.

## Ownership and resource admission

The normal SIM composition reserves maximum 640×480 Compare renderer storage before constructing the default processor factory: current conversion images 2,457,600 bytes, replacement images 2,457,600 bytes, nominal textures 4,915,200 bytes; total 9,830,400 bytes. The checked sum enters `externalSessionStorageBytes`; existing default-engine admission also accounts for raw, working and display pools, stages, orientation and executor storage. Pool sizes and algorithms are unchanged. The reserve remains conservative for smaller saved ROI and is charged per session under the existing one-old/one-candidate limit.

This is known requested storage, not measured driver, GPU or RSS overhead. It applies to the fixed SIM capability envelope, not unknown future hardware.

Context replacement retains the old context until actual renderer retirement, then acknowledges the exact C++ handoff. Closing intercepts both window Close and Qt application Quit, stops ordinary admission, joins acquisition/processing while retaining contexts, and continues the event loop until the renderer receipt permits release. Only then may the engine/window/services be destroyed. Hidden/deleted/never-shown host cases require the same explicit release evidence. Pre-ticket scene-graph initialization errors are projected independently of presenter ticket errors.

## Verification record

Source checkpoint `146f10a` has production/test/build manifest SHA-256 `9fa9e7d7d48f04aef77eafca0864ee76b31361b11fa982e6042dd0f0df08a6bc`. Documentation-only successors do not change that manifest. Commands, logs, source and binary hashes, native captures and stage inventories are retained in `out/qa/qml-live/verification.json` in the preserved worktree. Task and final reviews are under `.superpowers/sdd/2026-09-14-qml-live-workstation`.

Full Linux Debug and Release builds and `all_qmllint` pass. The complete non-hardware/non-desktop CTest suites pass **77/77** in **75.31 s Debug** and **45.05 s Release**. The QML-disabled Release application build also passes. The native matrix passes every row in both configurations:

| X11 check | Debug | Release |
|---|---|---|
| Existing Widgets controls/UI | 10/10 | 10/10 |
| Existing camera/live integration | 8/8 | 8/8 |
| Quick renderer, each rendering setup | 24/24 | 24/24 |
| QML runtime, each rendering setup | 16/16 | 16/16 |
| Real QML scene, each rendering setup | 13/13 | 13/13 |

The four rendering setups are software, OpenGL basic, OpenGL threaded, and OpenGL threaded at DPR 2. All 28 native invocations have separate `complete-native-*.json` command records. Source-bound software captures cover waiting, selected/connected, review/confirmed, live Original and Compare at both sizes, paused Compare, stopped/Stale and fallback. Inspection confirms pixels remain inside the viewport and mandatory status/controls remain visible; scrolling preserves readback and processing content at minimum size.

Tests exercise the real default engine and resource rejection, guarded startup and explicit saved Resume, late processing settings, precise edits and acknowledged save/reopen, activation failure without save, fallback/retry, same-bundle Compare, pending Pause, exact context replacement and visible/hidden/deleted/never-shown host shutdown. Actual QML tests use keyboard selection/numeric input, real button clicks and pixel checks at 900×600 and 1280×800. The final 13-case scene suite checks Tab/Shift+Tab access to scoped viewport shortcuts while numeric typing remains local to its field.

Independent runtime and processing reviews, staging-code review, final source review and scoped rereviews are approved. The staging author's final integration review excludes their own helper/checker; a different reviewer supplies that approval. The independent final evidence audit approves this scoped local checkpoint: all 402 source entries, 203 hashed artifact paths, 42 successful command records, full/native results, staged/relocated inventories and runtime traces match the recorded claims. No required correction remains.

### Findings retained and corrected

- Native visual inspection caught images escaping the hosted viewport despite earlier control-test passes. A translated-ancestor pixel regression reproduced the failure; an identity `QSGTransformNode` root preserves inherited transforms in Qt's software scene graph. Separate review corrected right-pane Compare zoom anchoring using the consumed mode. Ticket, frame ownership and retirement protocols are unchanged.
- Review found the viewport absent from Tab navigation. `activeFocusOnTab` plus actual forward/reverse Tab and zoom tests covers the corrected behavior.
- The first GL basic real-scene run at `f172017` failed the Apply-to-Confirm step and cascaded into 11 failures. The test sampled button geometry before GridLayout polish; captures had masked this with an 80 ms delay. The helper now waits for Qt's actual polish completion and verifies each button emits `clicked`. The capture-free native GL suite then passes 13/13. The original failure remains `final-native-debug-gl-basic-workstation.log`; it is not characterized as a backend lifecycle fix.
- A new first-focus assertion found an untouched Window field changed from `65535` to empty. Conditional bindings now retain text while focused; the regression checks both fields and exact minus/backspace input. `window-focus-red.log` and `window-focus-green.log` retain the before/after evidence.

Checkpoint 3's conversion/performance measurements remain tied to their original source. These integration fixes have new functional evidence, not a new performance comparison. The shared presenter intentionally reports Paused with increasing age; Stale is checked after stopping while presentation remains Live.

## Scope of platform and staging evidence

The local toolchain is GCC 15.2 with matching official Qt 6.11.1 libraries and non-Qt vcpkg dependencies. Both Debug and Release application builds link the Release Qt SDK. Native Linux Xvfb/OpenGL here uses Mesa llvmpipe. This does not establish Windows, physical GPU/display, camera hardware, designated workstation performance or deferred M4/M5 acceptance.

The Linux-only `QmlPilot` install component uses Qt's QML/runtime deployment helpers and relative executable/plugin paths. The staged-runtime checker inventories every file and verifies ELF dependencies against the stage or host system. Actual QML imports and a real UI workflow were also verified outside development prefixes, as recorded below. The stage includes existing repository notices; it is an internal runtime check, not M13 installer or redistribution-license completion. Complete dependency notice/source-offer inventory remains a distribution gate.

### Staged and relocated executable

The final Release stage is `/tmp/lumora-qml-stage.Vu6wn1`; the relocated copy is `/tmp/lumora-qml-relocated.oacn7604`. Both contain the same 182 inventory entries, including 42 symlinks. The checker inspects all 113 ELF objects with **zero errors** in each prefix. Their executable SHA-256 is `311feba319004f1c2ab7fe5d6da56eb26225de2fa60aa2e7980992f51b49359c`; install-time RPATH rewriting means this is recorded separately from the build executable hash. The executable uses `$ORIGIN/../lib`, and `bin/qt.conf` sets `Prefix=..`.

The generated Release import scan was refreshed before deployment and contains 24 Qt module entries. The application module is statically linked and loads from `qrc:/qt/qml/Lumora/Workstation`; Qt's helper deploys its external QML modules, Basic Controls and platform plugins. Successful runtime traces show 18 Qt libraries and 10 plugins loaded from each run's own stage, with no successful development-prefix file access. The relocated run also has no successful access to either earlier stage prefix.

Both real executable runs use Xvfb/xcb/software, a neutral temporary working directory, isolated temporary preferences/data/cache, and a whitelisted environment without development QML/plugin/library paths. XTest sends actual keyboard/mouse input using control positions captured by the QML scene test. Each run selects SIM-LIVE, Connects, Applies, captures the review before Confirm, confirms the persisted Mono12/640×480/1000 µs readback, and Starts. Each verifies three distinct live image-region hashes, frozen Pause, changing Resume, settled Stop, and changing restart. A normal `WM_DELETE_WINDOW` while streaming exits **0** in both runs; no timeout or forced cleanup is counted as success. Total driver durations are 15.694 s and 15.713 s. The separately tested adapter/QML cases establish window/level editing and acknowledged persistence; the staged driver does not claim that additional editing coverage.

The concrete deployment and verification commands are preserved in `complete-stage-*.json`, `complete-relocated-*.json`, `staged-ui-complete/result.json` and `staged-ui-relocated/result.json`, including file-access logs, maps, screenshots, environment choices and confirmed synthetic preferences. The inventory outputs use distinct names (`complete-stage-inventory.json`, `complete-relocated-inventory.json`) from command metadata to avoid an earlier evidence-file collision. The earlier stage at `f172017` remains supplemental history only.

For another internal stage from this retained build:

```bash
cmake --install out/build/linux-gcc-release-sim-qml \
  --prefix /tmp/lumora-qml-pilot --component QmlPilot
python3 tools/qa/verify-qml-stage.py /tmp/lumora-qml-pilot \
  --output /tmp/lumora-qml-pilot-inventory.json
/tmp/lumora-qml-pilot/bin/lumora_qml_app
```

The copied runtime closure includes official Qt 6.11.1 and ICU 73.2 from `.tools/qt-official`, plus OpenCV 4.12.0, spdlog 1.17.0, fmt 12.2.0 and zlib 1.3.2 from the recorded non-Qt vcpkg prefix. Qt archive provenance/SBOMs and vcpkg package copyright files remain in those retained prefixes. The stage copies this repository's LICENSE, NOTICE and copied-source OpenCV notice. Complete dependency license texts and source-offer obligations are still a distribution gate; this internal pilot stage is not that deliverable.

## Continuation

Stage 1 is locally implemented and verified. Next is Stage 2 editor parity with the existing Widgets workstation: presets and processing controls, stopped camera settings, capability-aware presentation and per-camera/installation authority. Fullscreen/sidebar/layout preferences follow in Stage 3. Main is unchanged, the branch/worktree and evidence are retained, and no merge, push, hosted CI, hardware, native Windows or formal milestone acceptance is claimed.
