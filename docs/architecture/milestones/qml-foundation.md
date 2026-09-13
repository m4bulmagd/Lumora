# QML foundation: optional interface preview

Date: 2026-09-13

Checkpoint 1 of the [QML Stage One plan](../../superpowers/plans/2026-09-13-qml-stage-one.md) is implemented in the isolated `.worktrees/qml-foundation` checkout on `codex/qml-foundation`, starting from `e4520dd`. The default `lumora_app` remains the Widgets workstation. This record does not claim local-main integration, publication, hosted CI or completion of the live QML migration.

## Delivered behavior

`LUMORA_BUILD_QML_UI` defaults OFF. Enabling it adds `lumora_qml_app`, a separate QGuiApplication/QQmlApplicationEngine composition, and the static `Lumora.Workstation` QML module. Both the executable and smoke test link the generated static plugin explicitly, retaining the compiled resources. Type information, QML cache generation and lint targets are enabled.

The preview uses Basic controls, a shared dark theme, a dominant empty image area and explicit **Interface preview**, **Waiting for image**, and **EVALUATION — NOT FOR CLINICAL USE** indications. It supports 900×600 and 1280×800. Camera/viewing/processing controls are disabled and visually muted. The enabled Preview details dialog works through Tab, Space and Escape. The application identity is `LumoraQmlPreview`; it performs no configuration I/O and does not create a camera provider, pipeline or live presenter.

The optional Linux/Windows Debug/Release `-sim-qml` presets use separate build directories and `out/vcpkg_qml_installed`. The `qml-ui` manifest feature adds Qt Declarative and matching Qt dependencies. Qt Base remains 6.11.1#1; Declarative, Language Server, Shader Tools and SVG are pinned to 6.11.1. Linux enables Qt Base's OpenGL feature only through `qml-ui`. Option OFF retains the prior Qt Core/Widgets lookup.

Qt package lookup now runs at directory scope through the dependency macro: the old function scope discarded QML policy registration before `qt_add_qml_module`. QTP0001/0004/0005 are explicitly NEW. No camera, core, application, processing, configuration or existing Widgets behavior source was changed.

## Verification environment and evidence

Verification used Linux/GCC 15.2.0. A complete matching official Qt 6.11.1 Linux SDK supplies all Qt libraries and tools; its release Qt libraries are used with both Debug and Release application configurations. The existing vcpkg installation supplies non-Qt dependencies. This avoids mixing independently built Core and Quick libraries. Archive URLs, upstream SHA1 checks, recorded SHA256 hashes and sizes are retained in `.tools/qt-official/download-manifest.json`.

The actual vcpkg baseline `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4` was bootstrapped, and a Linux `qml-ui` dependency dry run succeeded with the pinned module graph and OpenGL enabled. A full vcpkg Qt rebuild was not performed. The supplemental SDK configure emits unused-vcpkg-variable notices because its dependency prefix is supplied directly rather than through the vcpkg toolchain. Native Windows configuration cannot be validated from this Linux host; the attempted Windows graph check correctly rejected the unavailable Visual Studio developer environment.

Evidence is retained under `out/qa/qml-foundation/` in the worktree. `verification.json` records source-file and log/capture hashes. The existing lifecycle/context-retirement intermittency remains unresolved; this change does not claim to fix it.

| Check | Result |
|---|---|
| Original Widgets baseline at `e4520dd` | Debug 68/68 (70.05 s); Release 68/68 (38.83 s) |
| QML-enabled full headless suite, including QML smoke | Debug 69/69 (93.91 s); Release 69/69 (41.22 s) |
| Both application targets with QML enabled | Debug and Release builds passed |
| Generated `all_qmllint` | Debug and Release passed without diagnostics |
| Fresh QML-OFF Release Widgets application | Built using the original Widgets dependency prefix |
| XCB/software window smoke under Xvfb | Debug and Release each passed all five test invocations, plus setup/cleanup; 900×600 and 1280×800 captures inspected |
| XCB/OpenGL RHI window smoke under Xvfb | Debug passed all five test invocations, plus setup/cleanup, using Mesa llvmpipe OpenGL 4.5 |
| Pinned vcpkg `qml-ui` Linux dependency graph | Dry run passed; full Qt package rebuild not performed |

The full-suite commands exclude `hardware` and `desktop` labels. Native QML window smoke is recorded separately above; previous Widgets native evidence is not relabeled as a fresh run. Test durations are execution records, not performance comparisons.

## Relevant verification commands

With the matching SDK first and the existing non-Qt dependency prefix second in `CMAKE_PREFIX_PATH`, both QML presets were configured and built. Normal vcpkg build instructions are in the [Linux](../../development/build-linux.md#optional-qml-interface-preview) and [Windows](../../development/build-windows.md#optional-qml-interface-preview) guides.

```bash
cmake --build --preset linux-gcc-debug-sim-qml --parallel 3
cmake --build --preset linux-gcc-debug-sim-qml --target all_qmllint
ctest --preset linux-gcc-debug-sim-qml -LE 'hardware|desktop' --output-on-failure
cmake --build --preset linux-gcc-release-sim-qml --parallel 3
cmake --build --preset linux-gcc-release-sim-qml --target all_qmllint
ctest --preset linux-gcc-release-sim-qml -LE 'hardware|desktop' --output-on-failure
```

The QML test alone uses offscreen/software rendering; existing Widgets registrations retain their minimal-platform setup. QML smoke covers resource/module loading without warnings, supported-size bounds and text, disabled preview commands, and keyboard interaction. Actual XCB windows were checked separately under Xvfb with the software scene graph and OpenGL RHI using Mesa llvmpipe. This is not physical GPU/display or Windows evidence.

## Corrections and review

The initial real module-load test failed because linking only the static backing library did not retain/register the root QML module. Explicit generated-plugin linkage fixed both executable and test consumers. A minimum-size capture exposed source-selector clipping; the label was shortened. Root palette group overrides did not visibly differentiate disabled controls, so they were replaced by a centralized disabled opacity, verified in refreshed captures. An invalid Accessible attachment to Dialog was moved to its Item content.

Independent checkpoint spec/quality review approved the change with no material findings. A scoped follow-up review approved the final styling and command-availability test.

## Remaining migration work

Checkpoint 2 extracts shared C++ command/presentation policy while retaining Widgets behavior coverage. Checkpoint 3 must prove ticketed asynchronous rendering, exact bundle/receipt identity, Pause ordering, freshness and owner retirement before context acknowledgement. Checkpoint 4 integrates SIM-LIVE and window/level through the existing command/activation/persistence path. No live-frame or processing performance is measured by this preview.

Native Windows compilation, graphics/DPI checks, staged production packaging, hardware and all existing milestone/performance acceptance gates remain open.
