# Workstation UI Simplification Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development for independent tasks and review the integrated result.

**Goal:** Reduce repeated controls and permanent explanatory text while preserving precise adjustments and guarded camera startup.

**Architecture:** Retain C++ policy and existing processing draft/acknowledgement boundaries. Introduce small reusable QML disclosure and effect-menu components, compact exact editing, and presentation-only setup collapse. Enable effects atomically with valid adapter edits.

**Tech Stack:** C++20, Qt 6.11.1 Quick/QML, CMake, Qt Test and GoogleTest.

**Spec:** `docs/superpowers/specs/2026-09-16-ui-simplification-design.md`

## Global Constraints

- Work in the existing workspace; preserve unrelated changes and dependencies.
- No changes to image algorithms, camera policy, installation authority or persistence format.
- Preserve exact numeric editing, draft cancellation, bypass values and acknowledged status.
- Preserve evaluation, orientation, paused/stale overlays, frozen timestamp/frame age and errors.
- Expansion changes visibility only; accepted parameter edits enable their effect atomically.

## Task 1: Direct processing edits and integration tests

**Files:** `src/qml/ProcessingAdapter.cpp`, `tests/integration/QmlProcessingAdapterTests.cpp`, `tests/integration/QmlWorkstationTests.cpp`.

- [x] Add a real-adapter regression that edits disabled effects, verifies the edited effect becomes enabled, verifies invalid input leaves it disabled and verifies bypass keeps tuned parameters.
- [x] Build `lumora_qml_processingadapter_tests` and verify direct-edit enablement, rejected input, and late-release preservation with the real adapter.
- [x] Set `stage->enabled = true` only after validation, alongside each accepted numeric/mode edit and before the existing single `model->edit(...)` call. Release callbacks must only flush a gesture and cannot re-enable a bypassed stage.
- [x] Update real UI tests to open effect disclosure and use effect menus. Add a user-path regression proving disclosure alone does not submit processing changes and hidden camera readback reappears during required review.

## Task 2: Compact processing UI

**Files:** `src/qml/qml/ExactProcessingField.qml`, processing panel and per-effect QML files, new `EffectSection.qml`, new `EffectMenu.qml`, `src/qml/CMakeLists.txt`.

**Interfaces:** Retain current adapter properties/commands and field/slider object names. `EffectSection` owns disclosure and exposes content; `EffectMenu` accepts current enabled state and emits an enable request. Parent controls dispatch existing stage enable setters.

- [x] Use a single compact label/value row with a slider below and focus/hover treatment on the exact value.
- [x] Replace standalone stage checkboxes with effect options and bypass state. Keep common controls expanded, additional effects collapsed initially, and advanced parameters behind chevrons.
- [x] Replace healthy-state copy with conditional loading/applying text, one optional active summary, and persistent error messages.
- [x] Keep preset/reset in one compact row; keep Invert a binary button.
- [x] Register QML components and run `all_qmllint`; resolve new warnings.

## Task 3: Compact source setup and status

**Files:** `src/qml/qml/CameraStartup.qml`, `src/qml/qml/StatusStrip.qml`, `src/qml/qml/Main.qml` only if layout integration needs it.

- [x] Collapse configured setup into source/status plus a setup disclosure, retaining Stop/Disconnect and recovery actions.
- [x] Reopen or force setup visible when selection or confirmation is required; only display requested/current readbacks during review or deliberate expanded inspection.
- [x] Remove repeated setup instructions and consolidate warnings/errors in the global strip, preserving pending/failed states and all paused/stale/orientation information.
- [x] Exercise guarded startup, explicit stopped settings/review, disconnect/reconnect and hidden-panel controls through real QML tests.

## Task 4: Integrated verification and review

- [x] Build app, adapter tests and workstation tests; run QML lint and all non-desktop QML tests.
- [x] Run normal non-hardware/non-desktop CTest regressions.
- [x] Run native software workstation tests using Xvfb and capture 900×600 and 1280×800 states; inspect readable controls, popups, scrolling and viewport space.
- [x] Review the final diff against the spec, fix material issues, and record evidence and remaining platform limits.

## Verification evidence — 16 September 2026

- Debug app, processing adapter tests and workstation tests built successfully with Qt 6.11.1. `all_qmllint` passed; its only message was the existing unused `QtQuick.Layouts` import in `ViewingToolbar.qml`.
- `ctest --test-dir out/build/linux-gcc-debug-sim -LE 'hardware|desktop' --output-on-failure -j 2`: **70/70 passed**, including the real adapter and workstation regressions.
- Native X11 software rendering under Xvfb/Openbox: **30 passed, 0 failed**. Captures cover 900×600 and 1280×800 layouts, exact editing, expanded effects, camera review, paused Compare and fullscreen.
- Visually inspected compact live controls at 1280×800, the scrolled processing panel at 900×600, and camera settings with reachable Stop/Disconnect at 900×600.
- Independent review covered cancellation before pointer focus, approximate value labels, required setup visibility, and test synchronization with native window transitions. Material findings were fixed; final review found no remaining issue.
- Logs and screenshots are under `out/qa/ui-simplification-native-verified/`. `git diff --check` passed.

Verification scope: Linux Debug with the simulated camera and software renderer. This change has not been exercised on physical camera hardware, Windows, or the OpenGL renderer.
