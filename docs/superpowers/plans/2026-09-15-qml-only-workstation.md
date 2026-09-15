# Complete camera controls and make QML the application

Status: Implemented locally at `920b4f6`; see the [completion and verification record](../../architecture/milestones/qml-only-workstation.md).

**Goal:** Finish the camera workflows already available in Widgets, then use Qt Quick/QML for the normal Lumora application.

**Authority:** Owner decision on 2026-09-15: “Finish remaining controls, then switch to QML only.” This advances the default-switch order in the [migration design](../specs/2026-09-13-qt-quick-qml-migration-design.md), without claiming Windows release acceptance.

**Architecture:** QML owns controls and layout; typed adapters and shared presentation policy own drafts and command admission. Existing C++ acquisition, processing, configuration, frame bundles, renderer retirement and protected installation service remain authoritative.

**Stack:** C++20, matching Qt 6.11.1 Quick/Controls, CMake/Ninja, GoogleTest and native Qt Quick scene tests.

## Global constraints

- Continue `codex/qml-foundation` from `87753ba`; leave main and preserved QA evidence unchanged. No merge or push.
- Preserve Stop → Apply → review → Confirm → Start, priority Stop/Disconnect, acknowledged persistence and same-bundle Compare. No implicit Apply, confirmation, save or acquisition start.
- Keep full precision requests distinct from actual readback. C++ validates all capabilities and source/session bindings. IDs and pixels do not pass through JavaScript numbers or arrays.
- Preserve installation administrator/OS authority, explicit orientation confirmation and separate repair consent. Saving does not activate orientation.
- Normal build and application must not require Widgets. Legacy tests with unique lifecycle coverage may remain behind an explicitly disabled regression option; they are not an alternate application.
- Fullscreen, saved layout and custom preset CRUD are later features absent from both current frontends.
- One build/test/native-app executor at a time, no more than three compile jobs. Linux simulator evidence does not establish Windows/hardware acceptance.

## Task 1: Complete stopped camera settings

Extend `src/qml/CameraSettingsAdapter.{hpp,cpp}` and its real-coordinator integration tests with FPS, advertised pixel formats and ROI.

Contract: `frameRateText/Range/Reason/Enabled`, `pixelFormats`, `pixelFormat`, `pixelFormatEnabled/Reason`, and `roiXText/roiYText/roiWidthText/roiHeightText`, `roiEnabled/Range/Reason`. Commands: `editFrameRateText(QString)`, `setPixelFormat(QString)`, and `editRoiText(QString field, QString text)` accepting only x/y/width/height. Keep invalid raw input repairable and block whole-request Apply. FPS accepts finite positive fractional values; ROI uses strict unsigned integer parsing and capability increments/bounds. All edits use the existing CameraSettingsDraft and fresh authority checks.

Write failing behavioral tests first: incomplete/nonfinite FPS; exact fractional request versus rounded readback; unsupported format; ROI overflow/increments/invalid geometry; fixed/absent controls; full-request preservation; successful source rebind becoming review-only; external invalidation and no premature save/start. Root supplies QML controls in tabs to retain a compact nonmodal dialog and accessible priority controls.

## Task 2: Installation orientation

Extract Qt-free `presentation::InstallationSettingsDraft` from the legacy dialog lifecycle, then expose `InstallationAdapter` with open/editable/saveEnabled/pending/repairVisible, flipHorizontal/flipVertical/rotationIndex, confirmationChecked/repairChecked, sourceSummary/activeSummary/savedSummary/status. Commands open/close, set flips/rotation/confirmation/repair consent, save; C++ refresh and shutdown hooks.

Keep source identity/capability binding, load-once draft initialization, confirmation invalidation and duplicate-save protection. Explicitly release submission state after synchronous rejection. Test operator inspection, administrator edits, source drift, pending/streaming rejection, repair, asynchronous outcome, unchanged active orientation until explicit Apply, and preservation of other profiles. Use controlled temporary-path service authority in tests only.

Root wires the adapter into QmlWorkstation and creates a nonmodal QML dialog with paired asymmetric vector reference previews. Flips precede clockwise rotation; raw camera pixels are never transformed in QML. Add deliberate `--installation` parsing to QML main, retaining actual OS authority checks.

## Task 3: Make the normal application QML

After camera parity implementation, make `lumora_app` the sole real QML application target and `run-lumora` launch it. Promote ordinary build presets and CI dependency configuration to Quick. Conditionalize Widgets dependencies, library and unique legacy regression tests under `LUMORA_BUILD_LEGACY_WIDGETS_TESTS=OFF`; do not ship the old Widgets executable. Move Qt-free mixed integration tests unchanged into a backend-only test target.

Align target checks, staging/deployment helpers and build guides. Use production Lumora configuration identity. Preserve existing production preferences and leave pilot data untouched; any pilot import must be explicit, validated and refuse an existing destination. Machine profiles keep their independent protected path.

## Task 4: Integrate, verify and review

Add real-scene tests for FPS, ROI/format, orientation and minimum-size keyboard access. Run focused tests while implementing, then full Debug/Release suites, QML lint and representative software/threaded OpenGL/DPR 2 native checks. Verify a fresh staged application outside development imports, normal startup/settings/rebind/confirmation, installation save versus activation, persistence and clean shutdown. Preserve execution commands and results under a new QA directory.

Review adapter authority/ownership separately from build/default/scene behavior. Resolve material findings, update PROGRESS/ADR/current design status and build guides with verified scope and remaining external gates. Retain the legacy test option only for unique assertions not yet ported; document it accurately.
