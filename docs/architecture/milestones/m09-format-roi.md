# M9 Task 4C: stopped ROI and pixel-format changes

Date: 2026-09-12. Branch: `codex/m09-format-roi`. This continuation follows merged [Task 4B](m09-frame-rate.md#pr-21-integration), under the [stopped ROI/format design](../../superpowers/specs/2026-09-12-m09-format-roi-design.md) and [implementation plan](../../superpowers/plans/2026-09-12-m09-format-roi.md). The source and evidence below are local; publication and hosted CI are separate.

## Operator behavior

After Connect, use Stop before editing Camera settings. Viewer Pause continues acquisition. The dialog offers the full advertised pixel-format list and ROI x, y, width and height alongside FPS, exposure and gain. Apply submits an exact complete draft, displays actual readback, and requires fresh explicit Confirm followed by Start. There is no automatic stop/restart.

Format selection copies the entire advertised descriptor, including encoding, packing, alignment, valid bits, sample maximum and storage. ROI bounds, increments and sensor containment are validated without silently rounding the draft. The signed Qt editors explicitly disable unrepresentable unsigned ranges instead of truncating them. Actual readback includes the full descriptor. Stale or unrelated camera results invalidate the old dialog; a correlated successful source change shows actual readback and asks the operator to close/reopen for further editing.

The camera stays open and frame IDs keep increasing. New frames carry the new geometry and descriptor. The selected processing preset/draft and latest selected Original/Enhanced/Compare mode survive the resource change. Old image owners, pending paint receipts, pause, freshness and viewport state reset before the new context is acknowledged. Replacement bundles still undergo the existing display-mode availability/fallback checks. This extends the earlier reset-to-Enhanced behavior.

Confirmed full requested/actual settings use the existing typed preferences writer. Explicit saved Resume can cross only its own successful Apply generation transition and still requires matching identity, capabilities, requested settings and actual readback. Unrelated source changes, new edits, Stop and Disconnect cancel that continuation; launch never silently streams.

Production SIM-LIVE continues to advertise only Mono12, zero x/y offsets and a maximum 640×480 ROI. Its default remains 640×480 at 30 FPS. Smaller capability-valid ROI requests are available in the normal app. Alternate native descriptors and offset ROI are exercised through simulator capability fixtures; no physical-camera capabilities are invented.

## Resource and failure contract

A complete replacement context, pools, processor and processing worker are prepared on the control thread before camera mutation. Device calls stay on the acquisition worker. Same-mode FPS/exposure/gain edits retain the existing context; exact full ROI/descriptor changes use the bounded transaction. Successful actual ROI/descriptor must exactly match the candidate; FPS/exposure/gain retain their existing quantized-readback contract.

A failed Apply or mismatched source mode restores and verifies the previous full actual configuration. Verified rollback retains the old context and clears confirmation, requiring a new successful Apply before Confirm. Missing or unverifiable previous state retires the unsafe device. New camera status, context, resource assessment and Apply outcome publish together; Start requires presenter acknowledgement and fresh confirmation. Stop/Disconnect cancellation releases staged ownership without hiding the priority completion.

Each session keeps the existing resource budget and processing activation capacity. The transition admits the old and candidate sessions independently, checks aggregate arithmetic, and holds at most one staged candidate plus one retiring context. The default 512 MiB per-session limit therefore permits up to 1 GiB of accounted transient storage until retirement. Externally retained immutable handles and unknown custom-processor private storage retain their existing exclusions. This is resource admission, not a measured peak-RSS or performance claim.

## Verification

Implementation is committed as `0baee5c`; reviewed test-only Release compatibility correction `a452a7addd158710a7275ceae8ef883257d598a6` is the final verified source. All final command records have that same clean start/end revision. Independent task reviews, the full-branch source review and the scoped test correction review have no outstanding actionable findings.

| Check | Debug | Release | Evidence |
|---|---|---|---|
| Full simulator build, `cmake --build --preset linux-gcc-<configuration>-sim --parallel 2` | Passed | Passed | `verified-debug-build`, `verified-release-build` |
| Full headless suite, `ctest --preset linux-gcc-<configuration>-sim --output-on-failure --no-tests=error -LE 'hardware\|desktop'` | 62/62, 69.07 s | 62/62, 38.32 s | `verified-debug-test`, `verified-release-test` |
| Direct native X11 smoke binary under Xvfb | 1/1, 45 ms | 1/1, 34 ms | `verified-debug-native`, `verified-release-native` |
| Actual camera dialog at 560×560 and 720×640 under XCB/Xvfb | 1/1, 28 ms; both captures inspected | Not separately captured | `verified-native-dialog`, `verified-screenshots.json` |
| ASan/UBSan with leak detection: complete worker, LivePipeline and CameraReconfiguration registrations | 3/3, 29.09 s in separate sanitizer build | Not applicable | `verified-asan-build`, `verified-asan-test` |

The seven affected registrations pass together in 26.00 s before the Release-only test correction. Coverage includes 23 dialog cases, all 58 live-pipeline cases and 11 dedicated reconfiguration cases; the worker suite has 46 cases. Regressions cover actual resulting frames and native metadata, frame-ID continuity, preserved processing, old-owner release, acknowledgement/confirmation fences, preparation failures, rollback and unsafe retirement, priority cancellation, saved Resume, descriptor/ROI validation and generation correlation. Presenter regressions establish painted Compare before changing resources and verify all three display modes on replacement frames.

Review corrections were reproduced before fixing: queued cancelled Apply could overwrite the later Stop completion; the presenter reset selected modes to Enhanced; and dialog correlation accepted stale/mismatched/repeated generations or hid native descriptor details. The full suites and targeted sanitizer checks include those corrections. Initial Release compilation also rejected a copied optional outcome in a new test helper under GCC 15 `-O3 -Werror=maybe-uninitialized`. The one-line correction borrows the selected optional from the live local snapshot, with unchanged assertions and production code; independent review and Release verification pass.

The first sanitizer invocation passed every test body but LeakSanitizer failed because the sandbox uses ptrace. The unchanged host invocation completed with leak detection enabled. Final checks on `a452a7a` retain the original failed sanitizer and Release command records alongside their successful successors; neither failure was hidden or suppressed.

Commands, environment details, original failures, source/review records, hashes and captures remain in `/home/mo/code/Lumora/.worktrees/m09-format-roi/out/qa/m09-format-roi/` and `.superpowers/sdd/2026-09-12-m09-format-roi/`. `verification-manifest.json` validates nine successful commands on one clean source revision, both capture pairs and the two initial final-check failures. PNG conversions were verified pixel-identical to the original BMPs before inspection. These are synthetic rendered-window checks.

Fresh build trees reuse the retained pinned dependency installation at `/home/mo/code/Lumora/out/vcpkg_installed/x64-linux-dynamic` via `CMAKE_PREFIX_PATH`; GCC is 15.2.0, CMake 4.2.3 and Ninja 1.13.2. The retained Qt 6.11.1 installation lacks XCB, so native checks use only an isolated, matching-version Qt runtime extracted from the official `PySide6_Essentials 6.11.1` wheel, whose SHA-256 was checked against PyPI metadata. `LD_LIBRARY_PATH` and `QT_QPA_PLATFORM_PLUGIN_PATH` point to that temporary runtime, with `QT_QPA_PLATFORM=xcb`. Repository dependencies were not changed. This supplementary evidence does not claim the retained vcpkg desktop installation was repaired or establish Windows acceptance.

The independent final evidence/documentation audit approves the source and log/hash chains, retained failures, capture pixel identities, native runtime provenance, local links and limits of the claims. Its report is retained as `final-evidence-audit.md` in both QA and execution-record directories.

No push, PR or hosted CI run is included in this original local-continuation evidence. At that checkpoint, root `main` preserved the previous local documentation and integrated PR #21 source; the isolated branch contained this slice. Tasks 4C–4E subsequently entered [local main at `e2de210`](m09-camera-controls.md#local-main-integration). The verification above remains tied to its original source.

## Remaining work

This completes the bounded stopped ROI/format slice. At this checkpoint, richer camera capability metadata, per-camera profiles, administrator-managed installation orientation and the remaining camera panel workflow were subsequent Task 4 work. Tasks 4D–4E now complete that implementation and are [integrated on local main](m09-camera-controls.md#local-main-integration). Fullscreen and persisted UI preferences remain Task 5, following the selected QML migration; M10 capture follows remaining M9 work.

The inherited intermittent lifecycle/context-retirement timeout and previously recorded scripted-test acknowledgement concern remain unresolved. No processing algorithm or performance benchmark was changed. The recorded Linux 2048 Standard result remains below the 30 FPS target. Native Windows 11 visual/DPI checks, designated Windows/reference/performance work, the M6 hardware profile and separate milestone acceptance remain open.
