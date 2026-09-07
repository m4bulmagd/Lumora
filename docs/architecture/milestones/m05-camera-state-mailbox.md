# M5 Task 1: camera state machine and command mailbox

**Date:** 2026-09-07

**Initial implementation source:** `51cdc04b8f7b8d19ffbd536892d5849cd6e403a2`

**Reviewed follow-up source:** `dca908e796e51767bafe45f50e279038d1b27090`

**Status:** Task 1 implemented, reviewed and merged with Task 2, with matching Linux/Windows Debug/Release CI at `7295fb9`. Subsequent Tasks 3–5 are also merged through `f01b408`; see the [merged integration checkpoint](m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07). Native Windows 11 checks and full M5 acceptance remain pending. Not a milestone acceptance record.

The [M5 plan](../../superpowers/plans/2026-04-25-m05-independent-live-pipeline.md) and [preflight contracts](m05-preflight.md) define the work. Development proceeded under the [M4 native Windows 11 validation deferral](m04-deferred-windows-validation.md); those checks remain pending, not passed. No new source was pushed at the initial local Task 1 checkpoint recorded below; subsequent integration is summarized at the end.

## Implemented boundary

- Qt-free `lumora::application` state, command, status and mailbox values. Existing camera/core interfaces and the generic FIFO are unchanged.
- Eight camera states and 24 worker-reported events, with typed invalid-state rejection and terminal cancellation. Start/Stop/Disconnect requests do not report device success before the corresponding success fact.
- Nine owning command payloads, session/revision fields, and bounded latest status/outcome values. The future worker—not this pure state machine—authorizes identity, revisions, outstanding operations and device effects.
- A fixed 32-pending-command, multiple-producer/single-worker mailbox. Shutdown, Disconnect and Stop take priority over ordinary FIFO work. Stop/Disconnect admission fences survive dequeue until the actual popped request is completed. Further ordinary/Stop/Disconnect dequeue waits for completion; Shutdown remains selectable.
- Generation-isolated Apply coalescing within lifecycle segments, retained payload values, typed full/cancelled admission errors, stop-aware waiting, close/discard, and saturating aggregate coalesced/cancelled counters. No unbounded result history or queue growth.

Request IDs correlate barrier completions and must not be reused while an earlier completion can still arrive. A successful `post` acknowledges admission, never camera execution or durable persistence. Coalesced duplicate priority requests retain the pending command's original ID.

At this Task 1 checkpoint, the task added no camera worker, processing, persistence, startup panel or production live composition. The normal app still waited for an image; the separate M4 synthetic viewer was unchanged. Tasks 2–5 subsequently implemented those behaviors and their tests; the Task 1 evidence below does not independently verify them.

## Test-first and review evidence

Compiling interface stubs produced real assertion failures in both registered suites before implementation. Subsequent RED/GREEN slices covered the state table, priority/capacity/fences, generation/payload coalescing, wait/close/concurrency, and two self-review regressions: premature Disconnect completion and a duplicate pending Stop incorrectly splitting the Apply segment.

| Registered CTest suite | Coverage |
|---|---|
| `Application.CameraSessionStateMachine` | Four tests, including all **192 state/event pairs** against literal expected rows, valid startup/removal/retry sequences, and configuration rejection remaining idle |
| `Application.CameraCommandMailbox` | **22 tests**: capacity/FIFO, full-load priority admission, pending/executing fences, exact completion IDs, duplicate priority, generation and lifecycle isolation, full payload retention, immutable-by-copy stats, stop/close/wakeup, concurrent producers and late commands |

The independent task reviewer inspected the complete implementation diff against the brief/preflight: spec compliant, task quality approved, no Critical/Important/Minor findings. Cross-task obligations are explicitly deferred to their planned consumers: camera-thread ownership and guarded execution/cleanup in Task 2; capacity-one processing exchange in Task 3; persistence in Task 4; startup/Resume and session orchestration in Task 5. None is represented as tested by Task 1.

## Independent Linux verification

Initial source `51cdc04`: Linux/GCC 15.2.0, CMake 4.2.3, C++20 with warnings treated as errors. Existing pinned Qt/OpenCV dependency installations were reused; this is not fresh vcpkg-bootstrap evidence. Native desktop smoke used authorized Xvfb; no Qt plugin/dependency was substituted to bypass sandbox display restrictions.

Commands from the implementation checkout:

```bash
cmake --build --preset linux-gcc-debug-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure
cmake --build --preset linux-gcc-release-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-release-sim --no-tests=error --output-on-failure
out/build/linux-gcc-debug-sim/tests/lumora_application_tests --gtest_brief=1
ctest --preset linux-gcc-debug-sim -N -R '^Application\.(CameraSessionStateMachine|CameraCommandMailbox)$'
git diff --check
```

Both builds passed. Full native-inclusive CTest passed **27/27 Debug (15.17 s)** and **27/27 Release (12.49 s)**. The direct application run passed **26/26 tests**; listing found exactly the two required entries, both with 60-second CTest watchdogs. Whitespace checks passed. An earlier GCC Release warning in test variant construction was fixed without suppressions before these final runs.

A fresh local build directory `out/build/m05-tests-disabled` was configured with Ninja, Release, the same pinned dependency prefix, `LUMORA_BUILD_TESTS=OFF`, `BUILD_TESTING=OFF`, and `LUMORA_ENABLE_BASLER=OFF`. Building `lumora_application` and `lumora_app` passed. This checks Task 1's build independence; it is not evidence for Task 5's production composition, which was not implemented at that time.

## Final whole-branch review and follow-up

The final Spec review of `6c054a7...2720679` found zero issues. Standards review found no documented-standard violation or blocking issue, and suggested one optional cleanup: share the identical pending-command cancellation loop used by Shutdown and close. Follow-up `dca908e` extracts that private helper without changing locks, accounting, sealing or notifications. It also replaces five Markdown hard-break trailing spaces with blank metadata separators. Independent scoped re-review confirmed both corrections and no new breakage; no findings remain open in either review axis.

At the exact follow-up source, the controller independently rebuilt and reran the same full commands: **27/27 Debug (15.16 s)** and **27/27 Release (12.51 s)** passed, including desktop smoke. The tests-disabled/Basler-disabled application/library rebuild also passed. `git diff --check 6c054a7...HEAD` passed for the complete committed branch, in addition to working-tree checks. These results supersede the initial-source timing figures for the final Task 1 code; Windows CI had not run at that local checkpoint. Subsequent merged verification is recorded below.

## Decisions and remaining gates

- Keep execution-time stale-session rejection in Task 2, while Task 1 tests generation isolation and payload retention: the mailbox has no authoritative active session. If this split proves inadequate, revisit the worker boundary before integration.
- Add a read-only mailbox statistics snapshot for the already-required admission diagnostics. If its shape proves inadequate, adjust that small API before Task 2; no queue capacity or per-command history was added.

Tasks 1–2 subsequently merged with matching Linux/GCC and Windows/MSVC Debug/Release CI at `7295fb9`; see the [merged acquisition checkpoint](m05-acquisition-worker.md#merged-cross-platform-checkpoint-2026-09-07). Tasks 3–5 are now also implemented, reviewed and merged through `f01b408`, with passing post-merge CI recorded in the [integration checkpoint](m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07). Native Windows 11 checks remain deferred, and M4/M5 are not fully accepted. No hardware, installer, throughput/latency, soak or clinical validation is claimed by this Task 1 record. See [current progress and next gates](../../PROGRESS.md) rather than treating the historical Task 1 checkpoint as the next-work instruction.
