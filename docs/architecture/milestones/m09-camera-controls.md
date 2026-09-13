# M9 Task 4E: capability access and compact camera controls

Date: 2026-09-13. Branch: `codex/m09-camera-controls`. This local continuation builds on [camera profiles and installation orientation](m09-camera-profiles.md), under the [controls design](../../superpowers/specs/2026-09-13-m09-camera-controls-design.md) and [implementation plan](../../superpowers/plans/2026-09-13-m09-camera-controls.md).

## Operator behavior

The Camera panel separates acquisition state from the displayed image's Live/Paused/stale state. Contextual actions, a requested/actual review disclosure, physical camera identity and current installation guidance replace the long always-visible startup checklist. Apply opens review when confirmation is required. Missing or unavailable camera selections remain explicit. Settings can be inspected while streaming, with editing requiring Stop.

Capability access distinguishes unavailable, read-only, writable while stopped and writable while streaming. Mode and numeric-value access are independent. A camera without gain, or with read-only FPS, can still expose editable exposure. Fixed fields come from authoritative current readback; writable operator choices remain repairable and source-tagged. Complete requests are validated before admission and again against fresh device facts immediately before Apply. A read never grants confirmation or starts acquisition. Failed newer Apply cannot reuse an older confirmation to Start.

The workflow remains Stop → Apply → review → Confirm → Start. Priority Stop/Disconnect, exact rebind correlation, presenter acknowledgement, immutable loaded preferences and machine-profile authority remain in force. Native Original data and processing algorithms are unchanged.

## Camera and persistence contracts

`ICameraDevice::readConfiguration()` supplies validated acquisition-thread facts after Connect and immediately before Apply. `currentConfiguration` is distinct from historical applied state and clears on session replacement. The shared camera validator produces a field-level write mask; backends skip unchanged fixed nodes and independently validate actual facts. Rollback follows the same path. Auto readback may contain a readable measured value, while an Auto request carries no numeric setter value. The simulator models register/readback semantics without inventing an Auto algorithm; shipping simulation remains manual-only.

Current user schema is 5, machine schema 2 and capability fingerprint 2. All seven access facts participate in structural comparison. Legacy fingerprint-1 records preserve requests, actual values, selection, orientation, revision and confirmation provenance on read; mixed old/new collections remain supported. They require review before Resume or installation binding. Administrator Save upgrades only the selected installation record at the next revision, without treating a valid legacy file as corrupt. Filesystem authority, atomic save and read-only load policies are unchanged.

## Verification

Verified source: `2be3a852408293ead22607c0f71800f3da273e06`. Independent task and whole-branch reviews: approved, with no outstanding findings. Full Debug/Release and native records bind to this clean production commit at start and end. Test-only successor `ef280522f8466c0bb8cdad1e49302e1d9043c9cf` changes only the large-frame cancellation recovery fixture; the manifest proves that exact one-file delta and unchanged production trees. Focused Debug/Release simulator checks and the complete targeted sanitizer rerun bind to that clean successor. The retained manifest verifies all 14 successful command records and 18 capture hashes; it does not claim the full 68 groups were rerun after the test-only correction.

| Check | Debug | Release | Evidence |
|---|---|---|---|
| Full simulator build | Passed | Passed | `verified-debug-build`, `verified-release-build` |
| Full headless suite | 68/68, 72.86 s | 68/68, 38.65 s | `verified-debug-test`, `verified-release-test` |
| Native camera UI + desktop smoke | 52 + 1 passed | 52 + 1 passed | `verified-native-debug`, `verified-native-release` |
| Corrected simulator fixture suite | 59/59, 2.63 s | 59/59, 0.87 s | `fixture-debug-test`, `fixture-release-test` |
| ASan/UBSan with leak detection | 22/22 groups, 42.79 s | Not applicable | `fixture-sanitizer-build`, `fixture-sanitizer-test` |

Full headless command: `ctest --preset linux-gcc-<configuration>-sim --output-on-failure --no-tests=error -LE 'hardware|desktop'`. Build command: `cmake --build --preset linux-gcc-<configuration>-sim --parallel 2`. Sanitizer selection covers camera contracts/simulation, acquisition, startup/profile codecs, controller/reconfiguration/installation, LivePipeline, frame presentation and camera UI. ASan/UBSan runs with `detect_leaks=1:halt_on_error=1` and `halt_on_error=1`; leak detection is not disabled.

Native X11 checks run under Xvfb using the retained official matching PySide6_Essentials 6.11.1 Qt runtime because the pinned dependency installation lacks XCB. No repository dependency changed. Captures cover confirmed and expanded-review states at 900×600 and 1280×800, a paused/stale state at 900×600, and ordinary plus absent/read-only camera dialogs at 560×560 and 720×640. Verification checks keyboard reachability, horizontal overflow, complete ROI editor height and persistent processing warnings; captures are visually inspected. The nine Debug/Release capture pairs have identical pixels (`native-pixel-comparison.json`). These checks do not establish physical-display or Windows acceptance.

Behavioral RED and failed intermediate commands remain in the QA directory. Corrections include stale applied-confirmation authority, malformed/legacy capability provenance, repairable writable drafts, missing physical identity, dialog/panel minimum-size propagation and overlapping recovery controls. Final review also corrected fixed-Auto guidance and publication of synchronous Apply rejection warnings; native inspection corrected warning contrast. The stale saved-format integration assertion now clicks Apply and checks visible review guidance, rejection before admission, unchanged revision and preserved request, followed by the original explicit reselection/Apply/Confirm/Start path.

The first targeted sanitizer run passed 21/22 groups and exposed an unchanged cancellation test whose successful 4096×4096 recovery render had a one-second budget. A focused diagnostic confirmed `acquisition_timeout` at 1002 ms, without sanitizer or leak errors. Independent comparison found the test and measured production path identical to the base; the related same-size deadline test already allowed five seconds and took about 1.23 seconds per instrumented render. The test-only correction gives recovery that existing five-second budget and adds the returned error to a failed assertion. Cancellation timing, lease/frame-ID assertions and dedicated production-deadline checks remain intact. All 59 simulator tests then passed under sanitizers, followed by the complete 22-group rerun. The failed run and diagnostic remain retained.

Builds reuse `/home/mo/code/Lumora/out/vcpkg_installed/x64-linux-dynamic` (GCC 15.2.0, Qt 6.11.1, OpenCV 4.12, GoogleTest 1.18). Sanitizers execute on the host because the sandbox ptrace boundary prevents LeakSanitizer completion.

## Next and limits

This completes the bounded local Task 4E implementation. The owner subsequently chose Qt Quick/QML for a new session; [ADR 0001](../../adr/0001-qt-quick-qml-frontend.md) and the [migration handoff](../../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md#next-session-handoff) record that direction. Next is a bounded migration plan using this Widgets implementation as the parity baseline. Fullscreen, a collapsible sidebar and persisted UI preferences remain M9 Task 5 for the selected frontend. M10 capture follows the remaining M9 work.

Windows native compilation/ACL/UAC/DPI execution, physical-camera testing, reference performance and formal milestone acceptance remain open. The inherited intermittent lifecycle/context-retirement timeout is not claimed fixed. No performance benchmark changed; the prior Linux 2048 Standard result remains below 30 FPS. The original verification above was local to the continuation branch; the subsequent local-main integration is recorded below. There has been no push, PR or hosted CI for Tasks 4C–4E.

## Local evidence

Commands, logs, captures and `verification-manifest.json`: `/home/mo/code/Lumora/.worktrees/m09-camera-profiles/out/qa/m09-camera-controls/`. Task reports, review snapshots and decisions: `.superpowers/sdd/2026-09-13-m09-camera-controls/`. The documentation-only successor to the test fixture commit records completion without changing compiled source. Manifest SHA-256: `c1f7e2ee65c4a4eaabee8c25974067fd06bc7499c6a545f46f70cac5ef9d30b3`.

## Local main integration

On 2026-09-13, at the owner's request, local `main` was fast-forwarded from `cd4c8038195f9f313b0305d0229d4cdae05c8b8d` to `e2de210cd6d8208191daefd969917a4ac3fde054`. The 15-commit continuation adds Tasks 4C–4E and the QML handoff without a merge conflict or production change beyond the tested branch. Subsequent integration documentation changes no compiled source. This was a local merge only; publication, hosted CI, Windows and milestone acceptance remain separate.

Before integration, fresh builds and full Linux Debug/Release headless suites passed **68/68 groups each** at clean `e2de210` (69.99/38.69 s). This new evidence is separate from the production `2be3a85` and test-only `ef28052` records above.

After integration, both build caches in the main checkout were refreshed with `cmake --preset linux-gcc-<configuration>-sim --fresh -DCMAKE_PREFIX_PATH=/home/mo/code/Lumora/out/vcpkg_installed/x64-linux-dynamic`. This replaced references to the removed `linux-desktop` worktree with the retained pinned dependency installation. Both full builds passed without compiler warnings, followed by **68/68 headless groups in Debug (71.35 s) and Release (39.27 s)**. The source was `e2de210` with only integration documentation being updated; no build, source or test files changed. The configure-only unused-vcpkg-variable warnings reflect direct reuse through `CMAKE_PREFIX_PATH`, not a dependency update.

The opt-in `LUMORA_TEST_LINUX_DESKTOP` target was then enabled and built in each local cache. **52 native camera UI tests plus one desktop smoke passed in each configuration**, using Xvfb and the same retained matching Qt 6.11.1 XCB runtime described above. This adds automated native Linux evidence, with no new Windows or physical-display acceptance claim. Original source-bound capture and sanitizer evidence remains unchanged.

Integration commands, logs, hashes and `verification-manifest.json` are retained in `/home/mo/code/Lumora/out/qa/main-integration-2026-09-13/`. The manifest validates 16 successful command records, the source component identities, documentation-only working changes and the original draft/evidence preservation hashes. It separates branch checks, refreshed main builds/tests and supplemental native checks. Manifest SHA-256: `16e96b9531218438b31f0131ef2abd21ce05c7a46bc7cd08aadb8565c6bab464`.

The former branch/worktree is retained because its ignored QA artifacts contain the original verification evidence. An untracked QML draft on `main` was preserved byte-for-byte before the tracked handoff occupied its path: `/home/mo/code/Lumora/out/qa/main-integration-2026-09-13/original-qml-migration-design.md`, SHA-256 `288a4c27d60eb82f61cc4ff2a723d032c0dae0a8cfcc3e7dce9bb7a5d8300f92`. Start the next session from local `main`; the earlier worktree now serves as an evidence archive.
