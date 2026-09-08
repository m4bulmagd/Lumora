# Lumora progress

**Updated:** 2026-09-08

**Latest integrated implementation checkpoint:** `17d1edee02ffdc51258a507bf14a222fa748b404` on `main`.

This is a status and evidence summary, not a replacement for the [PRD](../prd.md), [design and milestone authority](superpowers/README.md#document-authority), or separately recorded acceptance. Documentation-only commits may follow the implementation checkpoint above.

## Milestone status

| Milestone | Implementation | Acceptance |
|---|---|---|
| M1–M3: foundation, frames, camera API and simulator | Merged; Linux/GCC and Windows/MSVC verified | [M3 accepted for engineering/evaluation](architecture/milestones/m03-camera-api-simulator.md#acceptance-evidence) |
| M4: viewer and latest-frame presentation | Tasks 1–4 merged; automated checks and historical Windows stress passed | Pending native Windows 11 visual/DPI validation and formal closeout |
| M5: independent live pipeline | Tasks 1–5 implemented, reviewed and merged; no open recorded review findings | Pending deferred M4 checks, affected M5 native Windows UI checks and separate acceptance |
| M6 | Awaiting approved hardware profile; not implemented | Entry and acceptance gates remain in force |
| M7 | Tasks 1–4 reviewed and merged through [PR #8](https://github.com/m4bulmagd/Lumora/pull/8); Linux/GCC and Windows/MSVC PR Debug/Release passed; stall-test correction merged through [PR #9](https://github.com/m4bulmagd/Lumora/pull/9) with passing Linux/Windows Debug/Release CI | Preceding deferred gates and separate acceptance remain open |
| M8 | Tasks 1–2 reviewed and merged through [PR #10](https://github.com/m4bulmagd/Lumora/pull/10) and [PR #11](https://github.com/m4bulmagd/Lumora/pull/11). Tasks 3–5 merged through [PR #12](https://github.com/m4bulmagd/Lumora/pull/12) as `17d1ede`: detail stages, shared orientation, pooled output ownership and prepared native CLAHE are reviewed and pass Linux/Windows Debug/Release CI; exact detail-loop optimization also passes both platforms. Complete execution, resource admission and fallback/retry are independently reviewed and pass both platforms. Shared Standard, exact references and [evidence tooling](architecture/milestones/m08-continuation.md) are source-approved through `73eb61f`, with full normal allocation verification and passing Linux/Windows Debug/Release CI | Designated Windows references/performance and deferred acceptance gates remain open. Linux 2048 Standard performance is below target |
| M9–M14 | Planned, not implemented | Entry and acceptance gates remain in force |

The normal Linux application now provides synthetic live video through the production pipeline: select `SIM-LIVE`, Connect, Apply and review, Confirm, then Start. Pause freezes presentation while acquisition continues. See the [launch guide](development/build-linux.md#launch-the-desktop-application). No physical camera is connected by this composition.

## Historical M5 integrated verification

[PR #6](https://github.com/m4bulmagd/Lumora/pull/6) merged Task 3 as `2031848`; [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) merged Tasks 4–5 as `f01b408`, each after its own exact-head cross-platform checks passed.

At the merged implementation checkpoint `f01b408`:

| Verification | Debug | Release |
|---|---|---|
| Local Linux, including native desktop smoke | 35/35 | 35/35 |
| [Linux main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34138249120) | 34/34 plus native X11 1/1 | 34/34 plus native X11 1/1 |
| [Windows main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34138249132) | 34/34 | 34/34 |

These are recorded implementation results, not new test runs for later documentation edits. The [integration checkpoint](architecture/milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) retains exact commits, timings, the resolved Windows compiler failure and evidence limitations. Optional ten-minute stress was not rerun for M5. Hosted Windows CI is not native Windows 11 UI/DPI, installer or hardware acceptance.

## M7 integrated verification

[PR #8](https://github.com/m4bulmagd/Lumora/pull/8) merged the reviewed high-depth processing implementation as `d191097`. Its tree equals the verified PR head `34cf24a`: local GCC 15.2.0 passed 40 headless plus one native X11 check in Debug and Release. [Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34161362515) passed 40+1 checks per configuration, and [Windows PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34161362517) passed 40/40 per configuration. No PR CI fix was required. The post-merge Windows run later exposed a stall-test synchronization race; test-only correction `43a7401` and diagnostics `8c63f36` are reviewed and merged through [PR #9](https://github.com/m4bulmagd/Lumora/pull/9) as `61d91fb`, with passing Linux/Windows Debug/Release PR and main CI. A separate intermittent context-retirement timeout remains unresolved; failure-only diagnostics are retained in `8c63f36`.

SIM-LIVE uses Mono12 in U16 through normalization, window/level, terminal Gray8 mapping and the pooled frame engine. The M4 harness remains Mono8. A fresh tests-OFF/Basler-OFF Release app build and all task/final reviews passed. The [execution record](architecture/milestones/m07-preflight.md) retains exact source, commands, run links and evidence limits. Native Windows 11 checks, hardware work and formal acceptance remain pending.

## Repository cleanup snapshot

After integration on 2026-09-07, the merged `feat/m05-processing-worker` and `feat/m05-startup-and-integration` branches were deleted locally and remotely. Only local `main` and remote `origin/main` remained, with no unmerged branch work identified. The three linked worktrees were retained detached, preserving dependency installations, build caches and ignored QA/execution records. This is a dated cleanup snapshot, not a requirement to delete future development branches.

## Next gates

- Complete the [deferred native Windows 11 checks](architecture/milestones/m04-deferred-windows-validation.md) on the then-current build, including affected M5 controls, and record M4/M5 acceptance separately.
- Before M6 implementation, identify the exact Basler model, sensor, firmware, NIC/driver/link, capability-reported formats and feasible continuous ROI/FPS/exposure/gain mode. Documentation-only preflight may collect these inputs. The separate [M7 simulator continuation](architecture/milestones/m07-preflight.md) does not waive the M6 hardware entry gate.
- Keep later Windows packaging, hardware, performance and distribution gates intact. Daily development is on Linux; Windows 11 remains the official installation and hardware-acceptance target.

All current work is **EVALUATION — NOT FOR CLINICAL USE** and must not acquire or store real patient data. A future clinical diagnostic release for Egypt remains a separately gated program.

## M8 Task 1 development checkpoint

The [tone-stage record](architecture/milestones/m08-tone-stages.md) records the owner-authorized continuation and implementation at `246a73a`: standalone U16 brightness/contrast, cached gamma and inversion with fourteen focused cases and passing Linux Debug/Release application suites. Task and final reviews passed; Task 1 merged through PR #10 after Linux/Windows Debug/Release CI passed. Live activation remains rejected until Task 5 composes execution. Whole-frame zero allocation and designated Windows reference/performance evidence remain open. [Task 2 CLAHE](architecture/milestones/m08-clahe.md) merged through PR #11 as `c0c4102` after independent task/final review and Linux/Windows Debug/Release CI passed. [Remaining M8 implementation](architecture/milestones/m08-continuation.md) covers Tasks 3–5 without waiving the outstanding acceptance gates.

## M8 continuation development checkpoint

The isolated `feat/m08-completion` branch has completed Task 3 denoise/sharpen, Task 4 shared orientation, Task 5A pooled immutable output metadata and fixed timings, and Task 5B native prepared CLAHE. Independent reviews are approved. At `6ffc627`, Linux CI passed 48 headless plus one X11 check in both configurations, and Windows CI passed 48/48 in both. At `04215a9`, the additional exact detail-loop optimization is reviewed; Linux Debug/Release each pass all 13 Processing registrations, and complete 2048-square outputs match the prior implementation byte-for-byte. At `51d01ad`, Linux CI passes 48 headless plus one X11 check in each configuration, and Windows passes 48/48 in each. The [continuation record](architecture/milestones/m08-continuation.md) contains run links and scoped allocation/performance evidence.

Task 5C is implemented and independently approved through `637ca06`: complete execution, session resource admission, bounded concurrent activation, and Original-only fallback with a persistent warning and processing retry. Full local Debug/Release each pass 48 headless checks plus X11 smoke; the subsequent diagnostic-only fix passes all 31 engine cases in both configurations. Four targeted ownership/engine registrations pass with address, undefined-behavior and leak checks enabled. Task 5C subsequently passes Linux/Windows Debug/Release CI at `a4de229`, including the corrected MSVC Debug allocation-failure probe. Task 5D is implemented at `eebbbd0`: shared Standard, independent exact references, benchmark/reference/allocation tools, strict artifacts and bounded CI smoke. Local full Debug/Release each pass 51 headless checks plus native X11; a fresh tests-disabled Release app/tools build and bounded smoke pass. Evidence review corrections through `633a36d` pass four affected Release registrations, including new CMake provenance checks. Those follow-up checks are complete in the final source verification below. PR #12 subsequently merged this work; see the integrated verification below.

## M8 final source verification

All remaining M8 source is reviewed through `73eb61f`, with no Critical/Important findings and two recorded nonblocking evidence-code cleanups. [Linux CI](https://github.com/m4bulmagd/Lumora/actions/runs/34197858759) passes 53 headless checks plus native X11 in both configurations; [Windows CI](https://github.com/m4bulmagd/Lumora/actions/runs/34197858690) passes 53/53 in Debug and Release. The optional app/tools build without tests and explicit preload-guard checks also pass.

The full Linux Release run completes all 33 benchmark cases (100 warm-ups, 500 measured frames each) without drops, processing errors or measured C++ allocations/releases. Both Standard 64×48 orientation profiles complete 1,000 measured cycles with zero C++ allocations/bytes/releases; separately controlled glibc traces also contain no measured allocator events. Raw artifacts, controls, candidates and independent checks are retained under `out/qa/m08-final/` in the M8 worktree. The [continuation record](architecture/milestones/m08-continuation.md#normal-release-characterization-and-allocation-evidence) gives exact scope and provenance.

Performance remains unfinished: on the i7-10510U Linux laptop, Standard 2048 P95 is 282.6 ms with identity orientation and 411.0 ms with horizontal flip/90° rotation (3.70/2.51 FPS). These results do not meet 33.3 ms/30 FPS and are not designated Windows acceptance. The next development priority is measured Standard-pipeline latency reduction, followed by M9 presets, processing controls and synchronized Original/Enhanced/Compare UI. The reference Windows workstation still needs selection and its own performance/freshness evidence. PR #12 is merged; the latest integrated checkpoint above includes this implementation.

## M8 integrated verification

[PR #12](https://github.com/m4bulmagd/Lumora/pull/12) merged Tasks 3–5 as `17d1ede` after final source/evidence review and passing checks for PR head `a35030b`. The merged tree is identical to that head. [Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34200450036) passes 53/53 in Debug and Release plus native X11 in each; [Windows PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34200449995) passes 53/53 in both. The duplicate branch-push runs also pass. The local `main` workspace was clean and fast-forwarded to the merge; this integration update changes documentation only.

Post-merge verification is tracked by the [Linux main workflow](https://github.com/m4bulmagd/Lumora/actions/workflows/linux-simulator.yml?query=branch%3Amain) and [Windows main workflow](https://github.com/m4bulmagd/Lumora/actions/workflows/windows-simulator.yml?query=branch%3Amain). Exact run logs and the final handoff remain under `out/qa/m08-final/` and the ignored M8 execution record in the preserved feature worktree. M8 performance and external acceptance gates above remain open.


## M8 latency follow-up

The owner approved continued optimization after PR #12. The [Window/Level performance record](architecture/milestones/m08-window-level-performance.md) covers the first bounded slice on `perf/m08-window-level-identity`: an exact active-row copy for validated full-range mapping. Source `b836334` is independently task-reviewed; local Debug/Release each pass 53 headless checks plus native X11. Paired diagnostics show about 94% lower time for the standalone identity operation, with identical output hashes and a smaller full-pipeline improvement. Full normal characterization, final review and platform CI/integration remain pending. M8 acceptance is still open.
