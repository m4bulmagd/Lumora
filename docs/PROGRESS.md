# Lumora progress

**Updated:** 2026-09-08

**Latest integrated implementation checkpoint:** `d6f94e183d9b5f5f74238a4b2036a7be839bc14e` on `main`.

This is a status and evidence summary, not a replacement for the [PRD](../prd.md), [design and milestone authority](superpowers/README.md#document-authority), or separately recorded acceptance. Documentation-only commits may follow the implementation checkpoint above.

## Milestone status

| Milestone | Implementation | Acceptance |
|---|---|---|
| M1–M3: foundation, frames, camera API and simulator | Merged; Linux/GCC and Windows/MSVC verified | [M3 accepted for engineering/evaluation](architecture/milestones/m03-camera-api-simulator.md#acceptance-evidence) |
| M4: viewer and latest-frame presentation | Tasks 1–4 merged; automated checks and historical Windows stress passed | Pending native Windows 11 visual/DPI validation and formal closeout |
| M5: independent live pipeline | Tasks 1–5 implemented, reviewed and merged; no open recorded review findings | Pending deferred M4 checks, affected M5 native Windows UI checks and separate acceptance |
| M6 | Awaiting approved hardware profile; not implemented | Entry and acceptance gates remain in force |
| M7 | Tasks 1–4 reviewed and merged through [PR #8](https://github.com/m4bulmagd/Lumora/pull/8); Linux/GCC and Windows/MSVC PR Debug/Release passed; stall-test correction merged through [PR #9](https://github.com/m4bulmagd/Lumora/pull/9) with passing Linux/Windows Debug/Release CI | Preceding deferred gates and separate acceptance remain open |
| M8 | Task 1 reviewed and merged through [PR #10](https://github.com/m4bulmagd/Lumora/pull/10); Linux Debug/Release 42/42 and Windows 41/41; Task 2 CLAHE implemented and task-reviewed locally through `88a9ad8`, Linux Debug/Release 43/43; final review and Windows CI pending | Tasks 2–5, designated Windows references/performance, allocation and deferred acceptance gates remain open |
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

The [tone-stage record](architecture/milestones/m08-tone-stages.md) records the owner-authorized continuation and implementation at `246a73a`: standalone U16 brightness/contrast, cached gamma and inversion with fourteen focused cases and passing Linux Debug/Release application suites. Task and final reviews passed; Task 1 merged through PR #10 after Linux/Windows Debug/Release CI passed. Live activation remains rejected until Task 5 composes execution. Whole-frame zero allocation and designated Windows reference/performance evidence remain open. [Task 2 CLAHE](architecture/milestones/m08-clahe.md) has passed independent task review and complete Linux application verification; final branch review and Windows CI remain pending.
