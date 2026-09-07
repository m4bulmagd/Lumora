# M5 Task 5: production simulator live integration

**Date:** 2026-09-07

**Task baseline:** `dda79c09e0884da717625689723abd0bab4843ef`

**Branch:** `feat/m05-startup-and-integration`

**Initial implementation source:** `9f562426406041f577b25f7050ed2e42b5a7ed62`

**Reviewed persistence fix:** `2acc2be0995413fd3be4eb443974b04376048391`

**Lifecycle-test synchronization correction:** `1e749c5b0a8712f90d42c9d48774443d159fb8fc`

**Final reviewed implementation source:** `d113da90421b68de13507a609b76fe33e55f4b0a`

**Status:** Implemented and reviewed through `d113da9`, with the subsequent compiler-only correction reviewed and verified at `5e1c1ec`. Task reviews, both task fix rounds, final Standards/Spec review fixes and scoped re-reviews have no open findings. Exact-source Linux/GCC and Windows/MSVC Debug/Release CI passed; native Windows 11 checks and full M5 acceptance remain pending.

## Scope and authorization

The owner initially authorized continuing M5 locally without waiting for the Task 3 CI run. That continuation did not authorize a further push or merge. On 2026-09-07 the owner subsequently authorized merging the completed M5 work into `main`: [PR #6](https://github.com/m4bulmagd/Lumora/pull/6) was merged after exact-head Linux/Windows CI passed, and Tasks 4–5 were published separately in [PR #7](https://github.com/m4bulmagd/Lumora/pull/7). The continuation requires its own matching cross-platform checks before merge; milestone acceptance is separate.

Task 5 connects the existing acquisition/processing workers, [startup preferences and panel](m05-startup-preferences.md), and paint-aware presenter through the [approved ownership and startup contract](m05-preflight.md). The normal application receives a synthetic MovingBar source: full-range Mono8, 640 x 480, 30 FPS, continuous RealTime pacing and seed `0x4C554D4F`. The application must not link the non-shipping M4 viewer harness. Basler hardware, high-depth processing, the full parameter editor and automatic recovery remain M6, M7, M9 and M12 respectively.

The evaluation warning remains mandatory. This software is not for diagnosis or clinical use and must not acquire or store real patient data. A future clinical diagnostic release for Egypt remains a separately gated program.

## Interface decisions

The pipeline owns a bounded control worker in addition to the already specified camera and processing workers. It constructs checked session resources and retires workers outside UI-visible locks. The UI posts intents, reads latest status and acknowledges the bound session generation; it does not allocate pools or join a replaced worker during normal polling. Camera calls remain exclusively on the acquisition thread, processing on its worker, and presentation on the UI thread.

Pipeline status separates a stable ordinary-command outcome from priority completion. Stop/Disconnect cancel affected continuations at admission, since the acquisition worker's latest outcome may have overwritten an intermediate ordinary completion. Neither status nor commands become an unbounded history. A fresh context supplies fresh raw/bundle exchanges, and acknowledged presenter binding is required before Start. Old/new provider calls must never overlap.

Pool sizing checks the fixed request and `44 * width * height` bytes: ten Mono8 raw, nine U16 processing and sixteen Gray8 display buffers. The U16 pool is reserved for M7 and unused here. This accounting is not a new arbitrary memory ceiling. A Result-returning factory creates the session processor from its display pool on the control thread; production uses Mono8 pass-through, and deterministic tests can supply a finite/releasable stalled processor through the same boundary.

These choices resolve interfaces left unspecified by the plan. If they prove unsuitable, the cost is coordinated pipeline snapshot/factory/controller and lifetime/command-test changes before M9/M12 integration, not a change to frame identity or the saved schema.

After Disconnect joins the workers, explicit Refresh may prepare/bind a fresh generation and discover cameras only. It clears the retained image to Waiting but cannot reconnect or resume streaming, and manual-Disconnect suppression remains active. Reusing an existing fresh discovery worker avoids unnecessary replacement. This preserves the single camera-before-processing retirement policy; retaining the image through Refresh instead would require a different idle-worker lifetime design.

## Verification and review

At initial source `9f56242`, the controller independently ran:

```bash
cmake --build --preset linux-gcc-debug-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure
cmake --build --preset linux-gcc-release-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-release-sim --no-tests=error --output-on-failure
```

The native-inclusive matrix passed **35/35 Debug (22.10 s)** and **35/35 Release (17.69 s)**, including 28 pipeline cases, five controller cases and four panel cases. The unchanged source/test digest before and after was `04b153f6a311aac3ad103a80e668daf34a5c7be49b45dc0ddcd7901371244c06`. The direct configuration executable passed all 21 cases, including all ten existing store/preservation cases. Builds and whitespace checks were clean. These are pre-review-fix results, not evidence that the open race is resolved.

The 100-cycle test exercises same-device Stop/Start identity continuity, replacement identity reset, Pause while acquisition continues, newest-frame Resume and final zero leases after all presentation/slot/test owners release. Independent camera, processing and UI stalls use the completed-paint freshness boundary: Current at 499 ms, Stale at 500 ms for 30 FPS, retained context, and recovery only on fresh completed paint. UI-loop recovery does not claim drawing during a blocked loop. Controlled processing gates also verify normal UI polling continues.

Startup/failure cases cover first-run consent, saved exact-ID idle probing, explicit Resume, capability/readback drift, load/save warnings, cancellation while preparation/discovery/Resume is blocked, priority completion correlation, same-ID idle Retry, generation acknowledgement, immutable mode rejection and camera-before-processing join. Result and standard/unknown processor-factory failures fail closed. Secondary exception reporting is best effort, not allocator/mutex fault-injection evidence.

Fresh tests-OFF/Basler-OFF production builds completed for the app in separate Release and Debug directories. The controller's independent Debug directory was `out/build/m05-task5-controller-production` (88/88 build steps), using the retained pinned dependency prefix. Its app build graph contains no viewer-harness or Lumora test-executable linkage. This is not a Windows/package build.

A controller-owned native XCB/Xvfb check at 900 x 600 used the production composition and temporary real preference storage. It kept the actual camera popup open across polling, selected by mouse, connected/applied/confirmed/started, and exercised the real Pause button. Waiting/Live/Paused captures showed a clear unselected placeholder, synthetic moving bar and paused timestamp/age; the evaluation banner and Pause remained visible. The startup panel fit at 208 x 457 with no scroll overflow at this size. Saving and final zero leases passed with no warnings. This is not physical-monitor or Windows DPI acceptance.

Initial lifecycle and subsequent targeted fixes have observed RED/GREEN evidence. The second RED incorrectly batched three independent behaviors; later passing coverage is identified as characterization where appropriate. A too-strong cancellation assertion and an asynchronous snapshot-lag test helper were corrected without claiming production REDs. The execution report retains these process limitations; test counts alone do not prove strict vertical TDD throughout.

Independent task review found one Important race: confirmation before background load completion was rejected but recorded as submitted, so it was never retried. Fix `2acc2be0995413fd3be4eb443974b04376048391` extends the service's existing single pending-save slot to accept/coalesce during load after successful start, then write only after safely loading/preserving the whole document. The accepted record survives Disconnect and drains during shutdown; unsafe load produces a typed failure without guessed-default writes. The controller marks only successful admission as submitted, without retrying permanent failures each poll. This avoids duplicating pending-save ownership on the UI; the cost is service admission/status/caller tests, not schema or camera migration. Scoped re-review found this addressed with no new breakage; the separate lifecycle assertion failure below kept Task5 open at that point and was then corrected independently.

Fix1 adds three pipeline cases and five configuration/service cases. Observed individual RED/GREEN covers the lost confirmation, unsafe-source write prevention and settling an accepted revision after a load exception. Composed close, stop-drain, newest-only coalescing, failed-load and rejection-no-poll-retry cases are characterization. Root direct configuration passed26/26, and repeated native XCB startup/live/Pause with temporary real preferences passed saved1/finalzero-leases1 at unchanged fix1 source. The retained initial loaded record is not rewritten to claim successful persistence.

## Lifecycle verification correction

The persistence fix passed its implementer's Debug/Release runs. The controller's independent repeat passed Debug35/35(22.71 s) but failed Release34/35(17.80 s): cycle28 of the 100-cycle case observed one raw buffer still in use at its zero-lease assertion. All other30 pipeline cases passed, including the new persistence cases. The production-only Debug rebuild passed. The failing log is preserved. Read-only diagnosis, independently checked against the control loop, found that Connect completion may be published before the acknowledged retiring context is destroyed on the control thread. The test incorrectly treats that completion as synchronous owner release. Test-only correction `1e749c5` waits within its existing two-second deadline for all three old pools to reach zero, retaining individual assertions and final shutdown; it does not alter production ownership, introduce a fixed sleep or excuse a leak. A persistent retained lease still fails the test. The cost if this synchronization choice proves wrong is a focused test revision, not a product/API change.

The retained root failure is this correction's RED evidence; no intermittent failure was artificially induced. The implementer then passed focused Debug/Release31/31 pipeline cases, full native-inclusive Debug35/35(22.35 s) and Release35/35(17.76 s), and five exact 100-cycle Release repetitions under a60-second watchdog. Those repetitions took3336/3347/3327/3331/3340 ms; they are not long-duration stress or hardware evidence. Scoped re-review confirmed the finding addressed with no new breakage: the real three-pool condition remains bounded, individual assertions remain, and final shutdown cannot mask a persistent leak.

At exact source `1e749c5`, the controller independently repeated the full build/native-inclusive test commands above: **35/35 Debug (21.83 s)** and **35/35 Release (17.73 s)**. Both builds were clean/no-work. The frozen source/test digest was `5ee8b1740b9b0cc5ce24337d5b0ae5a5cffd4861a4a36469fe772628f8790c09`. The suite contains31 pipeline, five controller and four panel cases; configuration contains26 total cases, including ten existing store/preservation cases and16 startup codec/service cases. Fix2 changes only the lifecycle test; the production-only rebuild/native UI evidence at fix1 therefore covers identical production bytes. Whole-range and working-tree whitespace checks passed. The earlier failed run remains part of the record, not superseded as though it never occurred.

## Final local branch review

The final independent reviews covered all seven local Tasks4–5 commits from `ccabaae` through `c2e8d66`, not the already reviewed Task3 PR. Standards found one Important issue: only the warning prefix was translated, leaving its runtime English body outside extractable Qt translation lookup. Spec found one Important issue: shutdown did not capture a confirmation that completed after the last controller poll. Both axes also retained the same Minor configuration-mode-test isolation concern from Task4; production validation itself is present. Neither axis found a Critical issue or unjustified scope expansion.

The sole combined fix wave is committed as `d113da90421b68de13507a609b76fe33e55f4b0a`. It preserves the existing UI/core boundaries and startup policy: typed warnings map to extractable UI translation literals and a translated fallback, with unchanged diagnostic codes/details; shared confirmed-record capture runs before shutdown cleanup without invoking startup/Resume continuations or inferring confirmation from a pending command. New capture/reporting work is exception-contained independently of cleanup; secondary allocation failures are not fault-injected.

Observed RED/GREEN covers the exact-context/source translator test and a confirmation that completes on the pipeline after the last UI poll while preference loading remains blocked. The close test then verifies the queued record drains without another poll. A separate Resume-close characterization observes no device Start and only one saved submission. The Minor correction keeps the valid capability baseline, changes only the requested acquisition-mode enumerator and expects `startup_configuration_invalid`; it is test-isolation evidence, not a new production RED. No schema, public interface, dependency or new file was introduced.

At unchanged `d113da9`, the controller independently passed **35/35 native-inclusive Debug (22.38 s)** and **35/35 Release (17.80 s)** using the full commands above. Direct configuration passed **26/26** and plain startup validation **8/8**. There are 33 pipeline, five controller and five panel cases. Both builds were clean/no-work; the independent tests-OFF/Basler-OFF production Debug app rebuilt 8/8 steps, and the agent's separate production Release cache also rebuilt successfully. Source/test digest before and after was `566b959b1843e0c8c945d9e966a57b855e9de5833337decf2ea4a30196546efa`; full-range whitespace passed. Repeated native XCB QA at this source passed actual selection, Waiting/Live/Pause, saved preference and final zero leases, with no warnings and the unchanged 900 x 600 layout.

The scoped final re-review confirmed all three unique findings addressed with no new breakage. Standards has zero open findings: translated warning bodies and the shared test-isolation concern are resolved. Spec has zero open findings: final capture of an unpolled successful confirmation and that same test concern are resolved. No second broad review or additional fix wave was used. These findings enforce existing requirements rather than changing startup consent or adding a new release capability.

At the end of the local review, all continuation changes were committed on `feat/m05-startup-and-integration`; Task3's branch/PR remained separate. The worktree and verification records were retained for the user's next integration decision. No additional push, merge, CI polling or milestone acceptance was performed during that earlier review checkpoint. The subsequent authorized publication and cross-platform evidence are recorded below.

## Integration verification and Windows compiler correction (2026-09-07)

Before publication at `97b74977af42718ce0be3f451b67d930e4ffe157`, fresh local builds and the full native-inclusive commands above passed **35/35 Debug (22.24 s)** and **35/35 Release (17.86 s)**. PR #7 then ran its own Linux/Windows CI. The initial [Linux PR run](https://github.com/m4bulmagd/Lumora/actions/runs/34135725607) passed; the [Windows PR run](https://github.com/m4bulmagd/Lumora/actions/runs/34135725644) and independent push run failed while compiling `LivePipeline.cpp`, before runtime tests.

MSVC `/W4 /WX` reported C4456/C2220: the ordinary `command` local inside the `else` branch shadowed the priority `command` declared in the enclosing `if` initializer. The existing Linux warning set did not enable that diagnostic. A focused compile of the actual source with `-Wshadow -Werror` reproduced the same two declaration locations and exit 1. Because the diagnostic identified one precise declaration collision, no speculative multi-cause search or runtime-behavior regression was substituted for the compiler reproduction.

Fix `5e1c1ecc73d5a0ce0774fd81d532413fa7ced458` renames only the priority local and its use to `priorityCommand`. It changes no control flow, public API, runtime policy, dependency or warning flags. The exact same focused command then passed:

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -Wshadow -fsyntax-only -Isrc/application/include -Isrc/camera/api/include -Isrc/core/include -Isrc/processing/include -Iout/build/linux-gcc-debug-sim/src/generated src/application/src/LivePipeline.cpp
```

Full local native-inclusive Debug and Release builds/tests at this fix passed **35/35 (22.39 s)** and **35/35 (17.84 s)** respectively. Separate tests-OFF/Basler-OFF production Debug and Release app rebuilds also passed. Independent scoped review of `97b7497..5e1c1ec` found no Critical, Important or Minor issues: both declaration and sole use were changed, with unchanged name resolution, lifetimes, moves, locking and cancellation. This is compiler RED/GREEN and unchanged-behavior regression evidence, not a new behavioral TDD claim. The existing Windows build remains the permanent regression gate; no test or warning was disabled. The initial failed runs remain historical evidence.

The following PR-event runs both report exact head `5e1c1ecc73d5a0ce0774fd81d532413fa7ced458`, completed successfully, and retain successful configure/build/test steps:

| Platform | Exact-source CI evidence |
|---|---|
| Linux, GCC 13.3.0 | [Run 34136247078](https://github.com/m4bulmagd/Lumora/actions/runs/34136247078): Debug **34/34 (21.45 s)** plus native X11 **1/1 (0.16 s)**; Release **34/34 (17.15 s)** plus native X11 **1/1 (0.04 s)**. Completed at 15:07:24 UTC. |
| Windows, MSVC 19.44.35228.0 | [Run 34136247037](https://github.com/m4bulmagd/Lumora/actions/runs/34136247037): Debug **34/34 (29.85 s)** and Release **34/34 (20.76 s)**. Completed at 15:09:29 UTC. Optional ten-minute stress steps were skipped, not passed. |

Linux's 35 local entries include its native desktop test; Windows has 34 standard entries and no Linux desktop entry. Neither difference indicates a missing standard suite. The documentation follow-up changes no source, tests, CMake, dependency or workflow files. [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) retains the final documentation head's separate checks and merge event; those checks must pass before merging rather than borrowing success from this earlier source head.

## Remaining platform and acceptance gates

Matching Windows/MSVC Debug/Release CI is verified above; affected native Windows UI checks remain required. The [deferred M4 Windows 11 visual/DPI gate](m04-deferred-windows-validation.md) is still open. Linux Xvfb paint/window checks and hosted Windows CI do not establish physical-monitor appearance, Windows scaling, installer or hardware acceptance. Final persistence can wait for filesystem I/O; the camera's controlled stop budget is not a whole-application-exit guarantee. Full M5 acceptance must be recorded separately after all required evidence exists.
