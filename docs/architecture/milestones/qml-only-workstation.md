# QML-only workstation

Date: 2026-09-15

This milestone implements the [QML-only workstation plan](../../superpowers/plans/2026-09-15-qml-only-workstation.md) on `codex/qml-foundation`, continuing from baseline `87753ba`. The implementation is committed locally as `920b4f6`. Main remains at `e4520dd`; this branch has not been merged or pushed and adds no hosted CI evidence.

## Implemented scope

The normal `lumora_app` is the QML workstation. Its normal configuration and launch path do not require Qt Widgets. The former Widgets application is not shipped as an alternate frontend. Unique legacy regression tests may still be built when the explicitly disabled `LUMORA_BUILD_LEGACY_WIDGETS_TESTS` option is enabled; that option exists for regression coverage, not as a second application mode.

QML owns workstation layout and interaction. Typed adapters and the shared presentation drafts own local editing and command admission. The existing C++ camera, acquisition, processing, configuration, preferences, frame-bundle, rendering-retirement and installation services retain their authority. Camera IDs and image pixels do not pass through JavaScript numeric values or arrays. Stop and Disconnect remain priority actions, and camera startup retains the explicit Stop → Apply → readback review → Confirm → Start sequence.

The stopped camera-settings dialog includes exposure, gain, frame rate, advertised pixel format and ROI. Frame-rate edits retain full finite fractional precision in the requested configuration even when hardware or the simulator reports a rounded actual value. ROI text is parsed as strict unsigned integers and checked against advertised ranges, increments and sensor containment. Unsupported formats and invalid raw FPS or ROI text remain visible and repairable, while blocking the complete request. Every accepted edit preserves the other requested camera fields. A source-changing Apply binds the full request to the current camera identity, generation and capabilities, then leaves the dialog review-only after rebind; it does not persist the unconfirmed camera request, Confirm or Start automatically.

Installation settings have separate operator and administrator presentations. Operators may inspect source, active and saved orientation without gaining write authority. The administrator route retains OS authorization and machine-profile authority, explicit orientation confirmation and separate repair consent. The dialog shows paired asymmetric synthetic Original/Enhanced reference previews of the proposed orientation; these previews illustrate horizontal/vertical flips followed by clockwise rotation and never transform camera pixels. Saving a machine orientation does not activate it. Activation remains part of an explicit camera Apply/review/Confirm workflow. Repair remains a separate deliberate operation.

The application uses the production Lumora organization/application identity and its existing production preferences. QML pilot preferences remain untouched. Any later pilot import must be explicit, validated, and refuse to overwrite an existing production destination. Protected machine installation profiles remain independent of user preferences and keep their administrator and filesystem controls.

## Verification

Focused C++ evidence under `out/qa/qml-only-workstation/` passes the three new authority seams:

- `Qml.CameraSettingsAdapter`: **12/12** tests.
- `Qml.InstallationAdapter`: **7/7** tests.
- `Presentation.InstallationSettingsDraft`: **6/6** tests.

The camera adapter coverage includes repairable incomplete/nonfinite FPS, exact fractional requests versus rounded readback, unsupported formats, ROI syntax/overflow/alignment/geometry, fixed and unavailable controls, preservation of the complete request, source rebind into review-only state, external invalidation and the absence of premature save or Start. Installation coverage includes operator inspection, administrator editing, source drift, pending and streaming guards, explicit confirmation and repair consent, asynchronous outcomes and preservation of unrelated profiles.

Native QML evidence has passed the 900×600 minimum-size keyboard route and all **16** paired-preview orientation cases. The preview oracle checks independent flip-before-rotation geometry and exact rendered corner colors, allowing at most one logical pixel for Qt centered-anchor rounding. A separate FPS test-driver correction uses QTest’s character overload to enter a literal incomplete exponent. Camera controls remain reachable by keyboard at minimum size, including the acquisition tab and raw invalid-input repair.

An installation-session regression was captured before correction: closing and reopening during a held save could seed a new draft from the old saved orientation. The focused RED record is preserved as `installation-reopen-red`; the subsequent focused adapter evidence passes after new draft initialization waits for both installation pending flags to clear, then seeds from the completed save.

The current evidence also confirms the central authority boundaries: local dialog edits issue no Apply, save, Confirm or Start; saving installation orientation leaves active orientation unchanged; full camera requests preserve unedited configuration; and source replacement requires readback review before a new editing session.

## Full application and packaging checks

The exact source manifest covers **424** files, with aggregate SHA-256 `5626179b29588aaab2547ba631f1e35eae9abb7231a948e6086fa6e53da87d1e`. The manifest file itself hashes to `9deadd7abd68532ee8980ad0569a9b7000d8d9bba0ee661b2e95420f6adccada`. Final execution records retain the pre-commit HEAD `87753ba` together with this complete source hash; the files in implementation commit `920b4f6` match that manifest. Subsequent documentation changes do not change the verified source.

All evidence is retained under `out/qa/qml-only-workstation/` in the implementation worktree. The aggregate `verification.json` (SHA-256 `2c16f9ccd38b755a69257ff3806c02de6e947c4f3ed57046d8c5924832426595`) binds the command records, logs, stage inventory and independent reviews. Earlier failed development checks and their drivers are retained separately from successful final results.

| Check | Debug | Release |
|---|---|---|
| Normal build and QML lint | Pass | Pass |
| Full default CTest suite | **65/65**, 62.59 s | **65/65**, 38.04 s |
| Real QML scene, software | **24/24**, 32.590 s | **24/24**, 27.464 s |
| Real QML scene, threaded OpenGL | **24/24**, 33.378 s | **24/24**, 27.206 s |
| Real QML scene, threaded OpenGL, DPR 2 | **24/24**, 65.582 s | **24/24**, 42.257 s |

Native durations above are command wall times. Both application configurations use the same **Qt 6.11.1 Release SDK**. Native scenes run on Linux Xvfb with software rendering or Mesa llvmpipe. QML lint reports only the previously recorded unused-import information in `ViewingToolbar.qml`.

The default suite count changes because **16** Widgets-dependent registrations are now opt-in; it does not represent lost assertions. Backend integration cases that need no Widgets moved unchanged to `lumora_backend_integration_tests`. The optional legacy configuration builds and passes **16/16** registrations in Debug (17.94 s), then is restored to `LUMORA_BUILD_LEGACY_WIDGETS_TESTS=OFF`. Both final normal configurations exclude Widgets sources from compilation and have no direct Widgets linkage. The old application source remains a historical reference, without an executable target; the compatibility `lumora_qml_app` target depends on the sole `lumora_app`.

A fresh Release install at `/tmp/lumora-qml-only-stage.tsqunexr` passes the inventory and ELF loader check: **113 ELF objects, zero errors**. Its `bin/lumora_app` SHA-256 is `286e5e46453beb6adce0b8274831779d854aae54860d63551f71cbcc814d11ac`. The historical `QmlPilot` install-component name remains a build compatibility name; the executable and application identity are now production Lumora.

The staged real-application run `verified-staged-ui3` passes in **27.496 s**, with artifacts in `staged-ui-final3`. It uses actual X11 keyboard/pointer input and two separate launches from outside the development tree. The first process connects, applies, reviews, confirms and starts SIM-LIVE, then stops and edits exact requested FPS `12.3456789123456` and ROI `320×240`. Apply produces rounded actual `12` FPS, leaves Start disabled and preserves the complete saved preference file contents. Confirm then durably stores only the intended FPS/ROI changes. The operator inspects installation orientation read-only. A second launch reopens the exact saved request, stays stopped until explicit Resume, preserves the whole saved document and closes normally. Both processes exit zero without forced cleanup or X11 errors.

Independent review approves all **20 staged screenshots** (`staged-visual-review.json`) and both complete process traces, import/plugin logs, four maps snapshots and exact preferences (`audit.json`). The trace audit reconstructs 1,390 completed syscalls in the first process and 1,292 in the second, including relative accesses. Every mapped Qt library and loaded Qt plugin resolves inside the stage. There are no development-library or Widgets loads, no machine-profile writes and no unresolved import errors. These review records are hash-bound in the aggregate verification file.

Installation administrator save/repair evidence comes from the compiled real-QML fixture using temporary profile stores and controlled authority, including all sixteen previews and a later explicit Resume. The staged smoke is deliberately an **operator** run and makes no machine-profile write or staged administrator claim.

## Review findings and corrections

The minimum-size installation repair test exposed focus reveal occurring before QML layout had settled. Rechecking the focused control after viewport/content height changes fixes this. The final matrix covers separate repair consent, confirmation reset, save, Apply/Confirm and subsequent explicit Resume. Independent source reviews cover adapter authority, pending/rejection handling, QML keyboard reachability, build topology and preserved regression registrations.

Visual review of the final native software/OpenGL/DPR 2 scenes finds no blocking geometry or orientation issue. Fixed readback and footer controls remain visible. In a scrolled review-only camera dialog, disabled Width/Height controls may be partly clipped; the complete current readback and Close remain visible, and the fields are reachable through the scroller. A DPR 2 draft screenshot truthfully shows Stale after capture delay and is not cited as a freshness invariant.

Two staged-driver assumptions were corrected before the final pass, with the failed runs retained. First, the window lookup still required the old pilot title. Second, its post-Apply assertion rejected any preference modification time change. ROI rebind legitimately re-acknowledges and persists the existing processing recipe through unchanged shared C++ policy. The failed run's complete preferences matched the initial bytes exactly. The corrected check requires byte-for-byte equality after Apply, records the identical atomic rewrite separately, and retains strict contents-plus-mtime checks for local typing and read-only installation inspection. Both before/after files and independent diagnosis are retained. No production change was needed for either driver correction.

## Remaining scope

Fullscreen, sidebar collapse and saved UI preferences remain future M9 Task 5 work in QML. This milestone migrates existing controls; it does not add capture workflows or custom-preset management that were not available in the prior frontend.

Linux simulator and virtual-display results do not establish Windows packaging, physical-camera behavior, administrator integration on Windows, hardware performance, physical-display quality or formal release acceptance. Those external gates remain separate from this completed local QML migration.
