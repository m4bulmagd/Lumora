# Lumora progress

**Updated:** 2026-09-07

**Latest integrated implementation checkpoint:** `f01b408c8d93913cf8095470a9a6e5b8bcce1ba3` on `main`.

This is a status and evidence summary, not a replacement for the [PRD](../prd.md), [design and milestone authority](superpowers/README.md#document-authority), or separately recorded acceptance. Documentation-only commits may follow the implementation checkpoint above.

## Milestone status

| Milestone | Implementation | Acceptance |
|---|---|---|
| M1–M3: foundation, frames, camera API and simulator | Merged; Linux/GCC and Windows/MSVC verified | [M3 accepted for engineering/evaluation](architecture/milestones/m03-camera-api-simulator.md#acceptance-evidence) |
| M4: viewer and latest-frame presentation | Tasks 1–4 merged; automated checks and historical Windows stress passed | Pending native Windows 11 visual/DPI validation and formal closeout |
| M5: independent live pipeline | Tasks 1–5 implemented, reviewed and merged; no open recorded review findings | Pending deferred M4 checks, affected M5 native Windows UI checks and separate acceptance |
| M6 | Awaiting approved hardware profile; not implemented | Entry and acceptance gates remain in force |
| M7 | Tasks 1–4 implemented and task-reviewed on `feat/m07-high-bit-depth`; local Debug/Release 41/41; [evidence](architecture/milestones/m07-preflight.md) | Pending matching Windows checks and preceding deferred gates |
| M8–M14 | Planned, not implemented | Entry and acceptance gates remain in force |

The normal Linux application now provides synthetic live video through the production pipeline: select `SIM-LIVE`, Connect, Apply and review, Confirm, then Start. Pause freezes presentation while acquisition continues. See the [launch guide](development/build-linux.md#launch-the-desktop-application). No physical camera is connected by this composition.

## Integrated verification

[PR #6](https://github.com/m4bulmagd/Lumora/pull/6) merged Task 3 as `2031848`; [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) merged Tasks 4–5 as `f01b408`, each after its own exact-head cross-platform checks passed.

At the merged implementation checkpoint `f01b408`:

| Verification | Debug | Release |
|---|---|---|
| Local Linux, including native desktop smoke | 35/35 | 35/35 |
| [Linux main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34138249120) | 34/34 plus native X11 1/1 | 34/34 plus native X11 1/1 |
| [Windows main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34138249132) | 34/34 | 34/34 |

These are recorded implementation results, not new test runs for later documentation edits. The [integration checkpoint](architecture/milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) retains exact commits, timings, the resolved Windows compiler failure and evidence limitations. Optional ten-minute stress was not rerun for M5. Hosted Windows CI is not native Windows 11 UI/DPI, installer or hardware acceptance.

## M7 development branch verification

The unmerged `feat/m07-high-bit-depth` branch implements high-depth normalization, window/level, terminal Gray8 mapping and the pooled live engine. At `aaf93f0`, GCC 15.2.0 passed all 40 headless checks plus native XCB smoke in both Debug and Release. A fresh tests-OFF/Basler-OFF Release application build also passed. The branch's normal SIM-LIVE composition uses Mono12 in U16; the separate M4 harness remains Mono8. All four task reviews and final whole-branch review passed; only nonblocking coverage suggestions remain. The [execution record](architecture/milestones/m07-preflight.md) retains scope, decisions, source and evidence.

## Repository cleanup snapshot

After integration on 2026-09-07, the merged `feat/m05-processing-worker` and `feat/m05-startup-and-integration` branches were deleted locally and remotely. Only local `main` and remote `origin/main` remained, with no unmerged branch work identified. The three linked worktrees were retained detached, preserving dependency installations, build caches and ignored QA/execution records. This is a dated cleanup snapshot, not a requirement to delete future development branches.

## Next gates

- Complete the [deferred native Windows 11 checks](architecture/milestones/m04-deferred-windows-validation.md) on the then-current build, including affected M5 controls, and record M4/M5 acceptance separately.
- Before M6 implementation, identify the exact Basler model, sensor, firmware, NIC/driver/link, capability-reported formats and feasible continuous ROI/FPS/exposure/gain mode. Documentation-only preflight may collect these inputs. The separate [M7 simulator continuation](architecture/milestones/m07-preflight.md) does not waive the M6 hardware entry gate.
- Keep later Windows packaging, hardware, performance and distribution gates intact. Daily development is on Linux; Windows 11 remains the official installation and hardware-acceptance target.

All current work is **EVALUATION — NOT FOR CLINICAL USE** and must not acquire or store real patient data. A future clinical diagnostic release for Egypt remains a separately gated program.
