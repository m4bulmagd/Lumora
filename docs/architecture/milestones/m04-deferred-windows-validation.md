# M4 automated evidence and deferred Windows 11 validation

**Decision date:** 2026-09-07

**Status:** Development-only exception; M4 implementation and automated verification complete, native manual acceptance pending. This is not an M4 acceptance record.

## Authorization and limits

The project owner asked to defer the manual check and move on, then approved the proposal to record that deferral and start M5 Task 1. M5 development may therefore proceed while the M4 native Windows 11 visual/DPI checks remain **pending, not passed**. The initial execution scope was the camera state machine and priority mailbox; subsequent task approvals are recorded in the [M5 plan](../../superpowers/plans/2026-04-25-m05-independent-live-pipeline.md). No push is authorized by this deferral.

This is a scoped exception to the normal M4-to-M5 entry order, not a general milestone waiver. Full M4 and M5 acceptance remain open until their evidence exists. Native Windows 11 validation must be completed before Windows release acceptance. Linux/GCC and matching Windows/MSVC CI, later UI checks, installer/upgrade, camera/NIC, performance, licensing/signing, and the separate future clinical-release program are unchanged. The software remains evaluation-only, with no real patient data or diagnostic-use authorization.

## Completed automated evidence

The CI/evidence-collector follow-up consists of `d01607213c4cc9225e407ebb1142795669804af1` and `6c054a7404c32a9be61c0922e6aa115e21eefe73`. All runs below passed at the latter exact source:

| Verification | Result |
|---|---|
| [Linux/GCC push CI 34062850169](https://github.com/m4bulmagd/Lumora/actions/runs/34062850169) | Debug and Release passed |
| [Windows/MSVC push CI 34062850162](https://github.com/m4bulmagd/Lumora/actions/runs/34062850162) | Debug and Release passed |
| [Windows stress dispatch 34062886902, attempt 1](https://github.com/m4bulmagd/Lumora/actions/runs/34062886902) | Short Debug/Release tests, stress build/run, restore-OFF and evidence upload passed |

The explicit `windows-msvc-release-sim` stress ran `SimulatedViewer.Stress` / `SimulatedViewer.StressTenMinutes`: one test, zero failures/disabled/skipped, `ctest_exit_code=0`, JUnit duration **600.198 seconds** (CTest 600.20 s). Recorded counts: **16,831 observed publications, 12,122 completed paints, maximum 2 retained bundles, zero retrieval timeouts**. Metadata start/end: `2026-09-06T22:08:22Z` / `2026-09-06T22:18:22Z`.

The host was Windows DataCenter Server Build 20348, runner image `win22` version `20260830.290.1`, CMake 3.31.6—not a Windows 11 desktop. Metadata, JUnit and retained successful CTest output agree on source, run/attempt, duration, test identity and counters. This establishes simulator liveness, interaction and bounded retention, not 30 FPS throughput, hardware latency, physical scan-out or full memory/handle-soak acceptance. Earlier Linux Release 600-second evidence and reviewed M4 implementation history remain in the [M4 execution record](m04-preflight.md#task-4-local-execution-evidence-2026-09-06).

Artifact ID `9998241758`, name `windows-stress-6c054a7404c32a9be61c0922e6aa115e21eefe73-34062886902-1`, was downloaded before its GitHub expiration (`2026-10-06T22:18:26Z`). The preserved, ignored local archive contents are under `out/windows-stress-34062886902/attempt-1-Nt1gVE/` in the main checkout. File SHA-256 values:

```text
85028fd07d582e74bd4f1cb7d18b6597872985c2725ca8575277cd1f873d8ec4  metadata.txt
8497477a3912639aa61b88a9397db0903e9012daf717806cf9272263233ddca6  results.xml
840a2345f72e7ad3f5eccb6cd75f581624c05fbcdcb6980fb1d158c0e8565562  ctest.log
```

The tracked summary preserves the result when hosted artifacts expire; it does not substitute for retaining raw release evidence. Historical CI/stress results do not cover later unpublished M5 changes.

## Deferred manual checks and closure

Owner: project owner or designated Windows 11 tester. Validation environment: native Windows 11 x64 and a sufficiently large physical display. Use the [M4 visual-check procedure](../../superpowers/plans/2026-04-25-m04-minimal-live-viewer.md#task-3-workstation-shell-layout-and-status-model) and [Windows build guide](../../development/build-windows.md).

- [ ] Check logical client sizes 1280x720 and 1920x1080 at 100%, 125%, 150% and 200% display scaling; record physical/effective logical geometry and any combination that cannot fit.
- [ ] Record screenshots and readable, unclipped evaluation, PAUSED timestamp/age and STALE IMAGE / NOT LIVE indications in normal/fullscreen-capable layouts.
- [ ] Exercise keyboard controls, Fit, logical-pixel 100%, zoom, pan and resize; record aspect ratio and scaling behavior.
- [ ] Record Windows/build, display/driver, tester/date and exact tested source; fix defects and rerun affected automated/manual checks.
- [ ] Close the deferral in a separate acceptance record and update traceability. Validate M5's later controls on the then-current release candidate, not merely the historical M4 commit.

Residual risk while development continues: native font metrics, layout/scaling, focus or display-driver behavior may reveal defects that headless CI does not exercise. Later UI work may require rework. The deferral accepts that development sequencing risk, not the risk of releasing unvalidated software.
