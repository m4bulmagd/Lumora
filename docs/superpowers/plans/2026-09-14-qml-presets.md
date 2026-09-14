# QML preset selection and Reset processing implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development for the adapter task with independent review; root owns the QML integration and final verification. The owner approved this bounded Stage 2A scope in conversation. Continue without another approval menu.

**Goal:** Select shipped and loaded saved processing presets and reset to Original from the live QML workstation, preserving camera, paused-frame, viewport and acknowledged-persistence behavior.

**Architecture:** Extend the existing ProcessingAdapter as a typed snapshot/command boundary over ProcessingControlsModel. The existing coordinator alone admits processing changes, matches completion and persists acknowledged state. A small QML PresetControls component joins the existing adjustment sidebar; pixels and renderer ownership remain C++.

**Tech Stack:** C++20, matching Qt 6.11.1, Qt Quick Controls Basic, Qt Test/GTest and existing CMake presets.

**Spec:** Approved Stage 2A design in this conversation and `docs/superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md`, Stage 2 editor parity. The broader Stage 2 remains subsequent work.

**Baseline:** `55aae99` on `codex/qml-foundation`, `/home/mo/code/Lumora/.worktrees/qml-foundation`. Main remains `e4520dd`.

## Global constraints

- Offer Original, Standard, High Contrast, Soft Detail, the existing Custom editing state, and all valid saved presets returned by the shared model. Preserve repository order, exact IDs and saved names.
- Reset processing selects Original through `ProcessingControlsModel::reset()`; it does not reset the camera, viewport, display mode or Pause state.
- Preserve existing draft → admission → matching completion → acknowledged persistence. Refresh must not submit, consume completions or write settings. Invalid IDs and loading/unavailable/closing commands must not mutate the draft or persist settings.
- Keep current numeric precision, input focus, Commit/Drag/Release behavior and saved preset collection. No new algorithms, preset definitions, schema, save/delete/rename UI or camera editors.
- QML uses typed adapter properties and deliberate operator commands; only operator activation sends a preset selection. Model refresh/rebinding must not activate a preset.
- Preserve separate active and pending/error feedback. A rejected activation restores the shared model's accepted state; it must not be presented or saved as successful.
- Keep pixels, frame/session/revision authority and renderer lifetime in C++. Preserve the default Widgets launcher and separate LumoraQmlPilot preferences.
- Builds use existing official Qt SDK caches, at most three jobs, one build/configure owner at a time. Debug links Release Qt SDK. Native Linux is Xvfb/software or llvmpipe, not Windows/physical-GPU/hardware/performance acceptance.
- Commit locally; retain branch, worktree, QA and ledger. No merge, push or deletion.

## Task 1: preset adapter boundary and persistence tests

**Own:** `src/qml/ProcessingAdapter.hpp`, `src/qml/ProcessingAdapter.cpp`, `tests/integration/QmlProcessingAdapterTests.cpp`.

**Consumes:** `ProcessingControlsModel::presets()`, `draft()`, `selectPreset(const PresetId&)`, `reset()`, `acknowledged()` and the existing WorkstationCoordinator. `PresetRepository::list()` already includes dynamic Custom plus saved recipes.

**Produces:**

```cpp
Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
Q_PROPERTY(QString selectedPresetId READ selectedPresetId NOTIFY stateChanged)
// Getter signatures: QVariantList presets() const; QString selectedPresetId() const;
Q_INVOKABLE bool selectPreset(const QString& id);
Q_INVOKABLE bool resetProcessing();
// presets rows are QVariantMap { "id": QString, "name": QString,
//                              "description": QString }.
// Emit presetsChanged only when those rows change, not on numeric/pending refresh.
// Existing available, pending, activeSummary and error properties remain authoritative.
```

- [ ] Extend focused tests before production behavior. Check delayed load has empty list/unavailable commands; after release built-ins and an explicitly seeded saved recipe appear. Preserve exact saved ID/name and stable-list notification on numeric edits/repeated refresh. Invalid IDs, failed load and shutdown must not mutate/save.

```cpp
EXPECT_FALSE(f.adapter.selectPreset("standard")); // before delayed load
// After loaded(), find rows by the literal IDs, including seeded "saved-fractional".
EXPECT_TRUE(f.adapter.selectPreset("saved-fractional"));
EXPECT_EQ(f.adapter.selectedPresetId(), QStringLiteral("saved-fractional"));
EXPECT_TRUE(f.adapter.pending());
EXPECT_EQ(f.adapter.activeSummary(), previousActive); // before coordinator poll
```

- [ ] Observe a behavioral RED using temporary method stubs if needed to compile. Record commands/output under `out/qa/qml-presets` before implementing selection/reset.
- [ ] Project list/selected ID from the shared model, reusing existing built-in name translations. Guard both commands with model existence and controlsEnabled; clear obsolete numeric validation on an admitted deliberate command. Dispatch exactly to model methods, refresh and return Result success. Keep list notification independent of general state changes.
- [ ] Test successful selection of a safe complete recipe, persisted exact selectedId/full recipe/saved collection, and reopening via existing codec. Reset must acknowledge/save Original. Numeric editing must return selection to Custom. Test rapid newer selection where useful using existing model boundaries, without duplicating lower-layer completion tests.
- [ ] Exercise Standard on the existing real 8×6 fixture: the real engine rejects its CLAHE grid. Assert selection rolls back, active summary/revision and persisted ID remain unchanged, no failed preset save; a subsequent Reset clears the model error. Do not weaken this fixture or mock engine preparation.
- [ ] Build the adapter test target and run `ctest --preset linux-gcc-debug-sim-qml -R '^Qml.ProcessingAdapter$' --output-on-failure`. Report actual RED/GREEN and commit only these owned files. Root serializes build handoff and supplies independent spec/quality review.

## Task 2: actual preset controls, live/paused reset and persisted selection

**Own (root):** new `src/qml/qml/PresetControls.qml`, `src/qml/qml/WindowLevelControls.qml`, `src/qml/CMakeLists.txt`, `tests/integration/QmlWorkstationTests.cpp`.

**Consumes:** Task 1 API and the existing actual-QML fixture, real runtime/engine, viewport geometry and temporary ConfigurationStore.

- [ ] Add actual-QML tests first: missing selector/reset controls must fail before implementation. Seed one safe saved fractional recipe in the explicit test configuration, preserve it across the existing saved-camera Resume flow. Select by keyboard via the actual ComboBox, not by directly invoking the adapter.
- [ ] Verify Original/Standard/High Contrast/Soft Detail and the saved preset through actual operator selection. Start from Streaming, set Compare, Pause, set a nondefault zoom/pan, capture camera session/readback/confirmation, frozen source ID and image rectangles. Select a preset and reset after matching completion; assert camera state and confirmation, display mode, frozen frame and image rectangles are unchanged. Resume shows a newer frame; latest processing selection reaches the real engine and persisted config.
- [ ] Show an accessible ComboBox and Reset processing button at the top of the existing sidebar. Keep the existing draft/active/error distinction and viewport shortcuts. Register the new component in the compiled static QML module.

```qml
ComboBox {
    objectName: "presetSelector"
    model: root.processing.presets
    textRole: "name"
    valueRole: "id"
    currentIndex: indexOfValue(root.processing.selectedPresetId)
    enabled: root.processing.available
    Accessible.name: qsTr("Processing preset")
    onActivated: root.processing.selectPreset(currentValue)
}
Button {
    objectName: "resetProcessingButton"
    text: qsTr("Reset processing")
    enabled: root.processing.available
    Accessible.name: qsTr("Reset processing to Original")
    onClicked: root.processing.resetProcessing()
}
```

- [ ] Keep selection bound to shared draft after edits and rollback. Avoid binding overwrite on focus, accidental commit from index changes or popup recreation during polls. Extend capture geometry names for the new controls. At 900×600/1280×800 verify controls and mandatory indications remain usable via scrolling and keyboard.
- [ ] Run actual QML tests and lint after observing the RED. Inspect native captures, then commit these owned changes. Have a different agent review this integration against the whole bounded scope from baseline.

## Task 3: final verification, staged smoke and documentation

**Own (root):** `docs/PROGRESS.md`, build-guide/pilot pointers as needed, new `docs/architecture/milestones/qml-presets.md`, plan completion state. Ignored QA/ledger stays under this task's own directories.

- [ ] Run full Debug/Release builds and non-hardware/non-desktop CTest suites plus both all_qmllint targets. Preserve any first failure and diagnose it before repeating. Existing Stage 1 results remain source-bound history.
- [ ] Run the changed actual-QML suite natively in software and threaded OpenGL for Debug/Release, plus DPR 2; run focused adapter/model and inherited renderer/runtime regressions through full CTest. Do not repeat the entire Stage 1 matrix or benchmark unchanged renderer code without a new concern.
- [ ] Refresh Release import scan, install a fresh QmlPilot stage under /tmp, verify all ELF/import paths with the existing checker and isolated real-app driver. Exercise the new preset/reset controls, successful selection persistence and live/Pause invariants. Clean window closure must exit zero; an execution timeout is a failure.
- [ ] Package exact source/command/binary/capture/stage hashes, reviews and limitations. Update Progress with Stage 2A complete and remaining processing/camera editors next; retain Stage 1 evidence unchanged. Commit documentation locally and verify main and compiled source identity are unchanged by the docs commit.

**Acceptance:** Both built-ins and saved presets can be selected in the real QML app, Reset returns processing to Original, failures never become persisted successes, and camera/Pause/viewport behavior is preserved. No other Stage 2/3 scope or external acceptance is implied.
