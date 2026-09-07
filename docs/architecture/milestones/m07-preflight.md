# M7 simulator development and verification

**Date:** 2026-09-07

**Status:** Tasks 1–4 implemented, independently reviewed and merged through PR #8 as `d191097`; Linux/GCC and Windows/MSVC Debug/Release checks passed. Milestone acceptance remains pending.

## Development sequence and authority

After reviewing the remaining gates, the project owner stated: “I will Complete Windows 11 validation for M4/M5 later, you can take over and continue the development.” The continuation uses the already designed, hardware-independent M7 processing work with simulator inputs. It does not invent a Basler hardware profile or start M6 camera implementation.

This records a scoped development exception: M7 simulator implementation may proceed while M4/M5 native Windows validation and acceptance, and M6 hardware integration and acceptance, remain pending. It reconciles the simulator-continuation provision in the M6 plan with the roadmap's normal numeric order. It does not authorize skipping subsequent milestone gates, external publication, or release acceptance. M7 acceptance remains pending until its own matching Linux/Windows evidence and the preceding deferred gates are closed.

The [M4 native Windows checklist](m04-deferred-windows-validation.md#deferred-manual-checks-and-closure) still applies to the then-current application, including affected M5 and subsequent controls. The M6 entry gate still requires an approved exact camera/sensor/firmware/NIC/driver/link, source-format and feasible continuous acquisition profile. The build remains `EVALUATION — NOT FOR CLINICAL USE`.

## Implementation scope

Execute the [existing M7 plan](../../superpowers/plans/2026-04-25-m07-high-bit-depth-window-level.md): validated fixed-order processing contracts, deterministic U16 normalization, window/level and terminal Gray8 mapping, then a pooled frame engine integrated with the simulator pipeline. Original numeric samples remain immutable. M8 enhancement algorithms, orientation editing, M9 processing UI and M6 vendor SDK work are outside this continuation.

Extend the existing `IFrameProcessor` boundary rather than introducing a second worker. Preserve the fixed accepted camera mode per session, explicit selection/confirmation/Start, latest-value exchanges, completed-paint freshness, Pause behavior, acknowledged source handoff and joined shutdown. Mode editing remains a later stopped-state resource-rebinding workflow.

## Resolved implementation contracts

- `SensorNative` accepts numeric U8 or U16 application samples; `CanonicalU16` is normalized. RawFrame packing/alignment fields preserve source provenance. M6's adapter has already copied/unpacked sensor values before publication, so normalization must not unpack or shift them again. Validate the complete descriptor/storage and reject numeric samples above its declared maximum.
- Reuse core orientation and pipeline-version types. Schema/order versions must be supported; configuration revision zero remains valid because the existing core contract does not reserve it.
- Default definitions contain all eight stages in the fixed order, with M8 enhancements disabled. Optional stages may be omitted, but a present stage cannot be reordered, duplicated, or carry invalid parameters even while disabled. Normalize is first and enabled. Return all validation violations in a stable documented order, using the existing Result template's custom error parameter; no core Result change is needed.
- A compiled definition owns its configuration and traits rather than borrowing a registry. The compiler validates future-stage definitions; the M7 executor must reject activation of enabled, unavailable M8 stages, leaving the active definition unchanged. It must not implement them as silent no-ops.
- The earlier M7 acceptance checklist mentioned inversion despite assigning its implementation to M8. Following design §17's milestone scope, M7 verifies normalization, window/level and display mapping; M8 retains inversion and all enhancement reference checks. No algorithm or release verification is removed from the roadmap.
- Original always uses the configured window/level parameters, including when that stage is disabled for the Enhanced route. An omitted window/level definition uses the identity default `window=65535.0, level=32767.5`. Enhanced respects the enabled-stage sequence.
- Window bounds are `level-window/2` and `level+window/2`, clipped to `[0,65535]` before interpolation. Values at/below the clipped lower endpoint map to zero, at/above the upper endpoint to 65535, and interior values use rounded interpolation between those endpoints. Terminal Gray8 mapping is `(value+128)/257`. Validate finite parameters before processing.
- Borrowed image views validate layout, span, storage and row access; immutable input never exposes writable memory. Stages reject incompatible extents/storage and overlapping input/output. Padded rows and unaligned byte storage must not cause undefined typed access.
- Extend the existing processor factory to receive the prepared source layout and processing/display pools. The session still has a fixed accepted mode. Checked allocation accounts for 10 native raw, 9 U16 processing and 16 Gray8 buffers: 44 bytes per U8 source pixel or 54 per U16 source pixel. No fallback heap pixel storage is permitted.
- Published Enhanced U16 and display buffers stay immutable while any bundle retains them. Workspace leases must be sealed and replaced through the bounded pools, never reused in place while published. Configuration activation occurs between whole-frame operations, so every product in a bundle carries the same revision and source ID.

## Verification and evidence

Each task adds registered focused tests before implementation and records its failing and passing result. Run complete Linux Debug/Release simulator suites and a tests-OFF/Basler-OFF production build. Reuse only the pinned dependency installation, and record the actual compiler and source used. Windows/MSVC CI and native Windows validation are separate evidence; Linux results do not mark them passed.

### Tasks 1–2 checkpoint

Contracts/compiler/views are committed in `6bc55ec`, with independent review fixes in `114b469`. The fixes reuse `core::PipelineVersion` and report duplicate unknown stage identifiers. The scoped re-review approved both spec compliance and code quality. Normalization is committed in `3192d62`; independent review approved it, with one minor coverage suggestion for a destination-storage rejection branch carried to final review.

At `3192d62`, GCC 15.2.0 passed all four registered Processing CTest suites in Debug and Release. Builds reused the pinned dependency installation from the retained Linux desktop worktree through `CMAKE_PREFIX_PATH`; no dependency baseline changed. Normalization tests cover exact 8/10/12/16-bit scaling, non-power-of-two maxima, padded/unaligned rows, alias rejection, unchanged input, source provenance and first-invalid-sample diagnostics.

Task 2's initial failing check was a missing-file configuration failure, not a behavioral test failure. After implementation, temporary mutations removing rounding and the excessive-sample guard caused the corresponding tests to fail. Source was restored byte-for-byte and the focused tests passed again. Later tasks require compile-ready stubs and observed assertion failures before implementation.

### Task 3 checkpoint

Window/level and terminal display mapping are committed in `0eeb0b3`. Independent review approved spec compliance and code quality, with minor direct-branch coverage suggestions carried to final review. Fourteen new cases include every U16 sample across twelve window/level settings, the identity mapping, fractional parameters, clipped endpoints, Gray8 conversion, padded/unaligned rows, metadata and alias rejection. The reference window calculations use independent integer arithmetic.

Compile-ready placeholders first produced observable failures in both stage and mapper tests. After implementation, GCC 15.2.0 passed all six registered Processing CTest suites in Debug and Release at `0eeb0b3` (51 individual processing cases). No floating tolerance replaces the exact pixel assertions.

### Task 4 and complete local verification

The pooled engine and live integration are committed in `aaf93f0cf90246529ff58cd1fa5acc8321b73e5d`. Independent task review approved spec compliance and quality with no findings. The production route now publishes immutable raw samples, canonical Enhanced U16, and paired Original/Enhanced Gray8 displays from one frame and one configuration snapshot. Enabled unavailable M8 stages cannot activate. Failed activation retains the previous configuration.

The engine shares normalization and window/level work where both views need it. Its two U16 and two Gray8 workspace leases come from bounded pools; published buffers are sealed and replaced through those pools. Timings identify shared work or Original-only window/level explicitly. Pixel processing never falls back to heap image storage.

LivePipeline uses one checked resource plan with 10 native raw, 9 U16 and 16 Gray8 buffers. It passes the prepared layout and both output pools to the processor factory, and the prepared camera mode to acquisition. The acquisition constructor keeps optional first-successful-Apply binding for standalone callers; production always supplies a prepared mode. Complete descriptor and ROI matching prevent drift from the prepared resources, while valid input padding and exposure/gain/FPS readback behavior remain supported.

Shipping SIM-LIVE is now Mono12 in U16 at 640x480, configured for 30 FPS. The existing Original label has translated `Original (display mapped)` help explaining that raw samples stay unchanged. This is help on an existing label, not an M9 processing editor. A saved M5 Mono8 request differs from the fixed M7 request and correctly keeps startup disconnected until explicit selection, Connect, Apply/review, Confirm and Start.

Nine engine tests first failed against compile-ready placeholders. Separate native-depth acquisition and live tests then failed against the old Mono8 behavior. During integration, two test assumptions were corrected: capability bounds had rejected ROI offsets before the prepared-mode guard, and a changed saved request correctly remained disconnected instead of probing automatically. Production validation and startup behavior were preserved. Final focused Debug/Release verification passed 148 cases in ten suites, including retained raw/U16/display bytes, configuration snapshots, all four bit depths, overflow, pool recovery, source replacement, and the existing 100-cycle/stall/shutdown tests.

At source `aaf93f0`, with no uncommitted source or test changes:

| Verification | Debug | Release |
|---|---|---|
| Complete headless CTest | 40/40, 22.87 s | 40/40, 17.98 s |
| Native Linux XCB window smoke | 1/1, 0.16 s | 1/1, 0.07 s |
| Total registered checks | 41/41 | 41/41 |
| Tests-OFF/Basler-OFF application | — | Fresh configure and build passed |

All builds used GCC 15.2.0 and the pinned dependency prefix recorded above. The production executable contains FrameProcessingEngine, with no pass-through processor symbol, test/harness target or test/pylon link dependency. Full build logs contain no warning/error diagnostics. An initial production configure supplied an unused desktop-test option; removing that irrelevant option produced the clean final configuration without a source change.

Commands used from the feature worktree:

```bash
cmake --build --preset linux-gcc-debug-sim --parallel 3
cmake --build --preset linux-gcc-release-sim --parallel 3
ctest --preset linux-gcc-debug-sim --output-on-failure --no-tests=error -LE desktop
ctest --preset linux-gcc-release-sim --output-on-failure --no-tests=error -LE desktop
xvfb-run -a ctest --preset linux-gcc-debug-sim --output-on-failure --no-tests=error -L desktop
xvfb-run -a ctest --preset linux-gcc-release-sim --output-on-failure --no-tests=error -L desktop
cmake --fresh -S . -B out/build/m07-production -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_PREFIX_PATH=/home/mo/code/Lumora/.worktrees/linux-desktop/out/vcpkg_installed/x64-linux-dynamic \
  -DLUMORA_BUILD_TESTS=OFF -DLUMORA_ENABLE_BASLER=OFF -DLUMORA_BUILD_BENCHMARKS=OFF
cmake --build out/build/m07-production --target lumora_app --parallel 3
```

Local execution logs, detailed task reports and the production link/symbol audit are retained in ignored `out/qa/m07-2026-09-07/`. Xvfb ran outside the execution sandbox because the unchanged baseline could not open its display sockets inside it. This verifies XCB window exposure, not physical-monitor appearance or Windows validation. Ten-minute stress and hardware/performance acceptance were not rerun.

### Review and remaining gates

All four independent task reviews are approved. Final whole-branch review of `7ceec0d..a69a24f` approved technical readiness with no Critical or Important findings and no required code changes. It triaged the two Task 2–3 coverage suggestions as nonblocking: direct destination-storage rejection for normalization, and direct degenerate-window/source-storage rejection checks for mapping. Those defensive branches remain safe to defer; they are not known implementation failures. The review report is retained with the local QA evidence. Review approval does not authorize integration or record milestone acceptance.

At this local review checkpoint, the branch had not yet been pushed or merged and matching Windows/MSVC checks were pending; the subsequent integration evidence follows below. Deferred M4/M5 native Windows validation and acceptance, and M6 profile/hardware work remain open. M8 enhancement algorithms and later UI, capture, performance and distribution work remain separate milestones.

## PR integration — 2026-09-07

The owner approved the proposed PR, cross-platform CI and merge workflow with “Ok do that.” [PR #8](https://github.com/m4bulmagd/Lumora/pull/8) merged exact head `34cf24a28857204aead7533d37fb0af3250d2031` as `d191097ccb5c20bf4f44d9c5ae0440a963bd4bcb`. The merged tree equals the checked head; the last code commit remains `aaf93f0`. No CI source fix was needed. Local `main` was fast-forwarded without discarding changes.

| Exact-head verification | Debug | Release |
|---|---|---|
| Local GCC 15.2.0 headless | 40/40, 22.75 s | 40/40, 18.04 s |
| Local native X11 smoke | 1/1, 0.12 s | 1/1, 0.06 s |
| [Linux PR CI, GCC 13.3.0](https://github.com/m4bulmagd/Lumora/actions/runs/34161362515) | 40/40, 21.33 s; X11 1/1, 0.09 s | 40/40, 17.64 s; X11 1/1, 0.04 s |
| [Windows PR CI, MSVC 19.44.35228.0](https://github.com/m4bulmagd/Lumora/actions/runs/34161362517) | 40/40, 26.98 s | 40/40, 20.94 s |

Both branch push checks also passed. Local refresh commands were `ctest --preset linux-gcc-{debug,release}-sim --output-on-failure -LE 'hardware|desktop'` and `xvfb-run -a ctest --preset linux-gcc-{debug,release}-sim --output-on-failure --no-tests=error -L desktop`, run separately for each configuration. Logs remain in the retained M7 worktree's `out/qa/m07-2026-09-07/` (`pr-head-debug.log`, `pr-head-release.log`, `pr-linux.log`, `pr-windows.log`).

These automated results satisfy M7 cross-platform implementation checks. Hosted Windows Server CI does not supply deferred native Windows 11 visual/DPI checks. No optional ten-minute stress, physical camera, designated-workstation performance or milestone acceptance is claimed.

### Post-merge verification

At `d191097`, [Linux main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34161792586) passed Debug 40/40 in 21.82 s plus X11 1/1 in 0.11 s, and Release 40/40 in 17.37 s plus X11 1/1 in 0.04 s.

[Windows main CI attempt 1](https://github.com/m4bulmagd/Lumora/actions/runs/34161792587) passed Debug but failed Release: `LivePipeline.IndependentCameraProcessingAndPresentationStallsUseCompletedPaintDeadline` observed acquired count 2 against baseline 1 in its camera-stall boundary. The other 39 CTest entries passed. This post-merge failure is retained separately from the passing PR checks; the reproduced cause and correction follow below.

### Stall-test synchronization correction

Test-only commit `43a7401` addresses the post-merge failure without changing acquisition or freshness behavior. Raw/bundle publication can precede the camera-status snapshot; the test now waits for the acquisition count to cover the presented frame ID before sampling its baseline. It snapshots the prior bundle before stepping manual clocks, advances host before source, and waits for a newer completed bundle when recovering processing/presentation stalls. The exact 499 ms/current, 500 ms/stale, retained contextual frame, stopped camera count and pool-release assertions remain.

The original count mismatch reproduced on Linux Release iteration 6; the separate-clock startup race reproduced in Debug iteration 143. After the final correction, 100 focused repetitions and all 37 LivePipeline cases passed separately in both Debug and Release, with clean builds and whitespace checks. No production code, timeout expansion or sleep was added. Independent review approved spec compliance and quality with no findings. Corresponding Windows CI was pending at this local checkpoint; the subsequent PR #9 integration record follows below. Detailed diagnosis and logs remain under `out/qa/m07-2026-09-07/stall-fix/`.

### Additional context-retirement timeout remains unresolved

A later Linux Release check of the local M8 integration timed out in `PipelineRetainsOldContextUntilReplacementBindingAcknowledgement` while waiting for the Disconnect outcome. The original condition reproduced once in 100 focused Release runs. A bounded independent audit found no concrete completion-loss path in command correlation, mailbox wake/barrier handling, status revision/value publication or worker retirement. No production fix is claimed.

Commit `8c63f36` adds failure-only scalar diagnostics while preserving the original condition and timeout. A discarded request-ID hypothesis did not explain the absent outcome; an unconditional diagnostic snapshot itself retained the old context and was removed. The retained snapshot exists only on fatal failure, so passing runs gain no owner.

Final bounded characterization passed: context test 100/100 in each Linux configuration, prior stall test 10/10 each, and all 37 LivePipeline cases once each. These passing reruns do not resolve the intermittent timeout. The next failure must retain the camera/pipeline outcome and error diagnostics, with thread stacks if available. Reports and logs remain in `out/qa/m07-2026-09-07/context-fix/`. Follow-up publishing and matching Windows verification were pending at this diagnostic checkpoint; the subsequent PR #9 integration record follows below. Milestone acceptance remains open.


## Follow-up PR integration — 2026-09-08

The owner explicitly authorized publishing `fix/m07-stall-test-synchronization` to `m4bulmagd/Lumora`, running CI and merging when both platforms passed. [PR #9](https://github.com/m4bulmagd/Lumora/pull/9) merged verified head `89b095325249436f93c9b120ee0e018ef544115e` as `61d91fb36a0be9a0d56d6ce04b5f68874a7f5283`; the trees match.

| Verification | Debug | Release |
|---|---|---|
| Local GCC 15.2.0 | 40/40 headless, 22.78 s; X11 1/1, 0.13 s | 40/40 headless, 18.07 s; X11 1/1, 0.06 s |
| [Linux PR CI, GCC 13.3.0](https://github.com/m4bulmagd/Lumora/actions/runs/34169085574) | 40/40, 22.14 s; X11 1/1, 0.13 s | 40/40, 17.42 s; X11 1/1, 0.04 s |
| [Windows PR CI, MSVC 19.44.35228.0](https://github.com/m4bulmagd/Lumora/actions/runs/34169085512) | 40/40, 31.23 s | 40/40, 21.48 s |

Both branch-push runs passed before merge. Post-merge [Linux main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34169533444) and [Windows main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34169533508) also completed successfully at `61d91fb`. Local main was fast-forwarded without discarding changes. Full PR logs and the integration report remain in the retained M7 worktree under `out/qa/m07-2026-09-08/`. The initial local sandboxed X11 attempt could not connect to its display; the host smoke test passed without source changes.

This closes the follow-up publishing and automated platform checks, not the separate intermittent context-retirement timeout. No timeout fix, native Windows 11 manual validation, hardware test, optional stress or milestone acceptance is claimed.
