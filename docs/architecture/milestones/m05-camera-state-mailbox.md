# M5 Task 1: camera state machine and command mailbox

**Date:** 2026-09-07

**Implementation source:** `51cdc04b8f7b8d19ffbd536892d5849cd6e403a2`

**Status:** Task 1 implemented and task-reviewed locally; Windows CI and full M5 acceptance pending. Not a milestone acceptance record.

The [M5 plan](../../superpowers/plans/2026-04-25-m05-independent-live-pipeline.md) and [preflight contracts](m05-preflight.md) define the work. Development proceeds under the [M4 native Windows 11 validation deferral](m04-deferred-windows-validation.md); those checks remain pending, not passed. No new source was pushed for this checkpoint.

## Implemented boundary

- Qt-free `lumora::application` state, command, status and mailbox values. Existing camera/core interfaces and the generic FIFO are unchanged.
- Eight camera states and 24 worker-reported events, with typed invalid-state rejection and terminal cancellation. Start/Stop/Disconnect requests do not report device success before the corresponding success fact.
- Nine owning command payloads, session/revision fields, and bounded latest status/outcome values. The future worker—not this pure state machine—authorizes identity, revisions, outstanding operations and device effects.
- A fixed 32-pending-command, multiple-producer/single-worker mailbox. Shutdown, Disconnect and Stop take priority over ordinary FIFO work. Stop/Disconnect admission fences survive dequeue until the actual popped request is completed. Further ordinary/Stop/Disconnect dequeue waits for completion; Shutdown remains selectable.
- Generation-isolated Apply coalescing within lifecycle segments, retained payload values, typed full/cancelled admission errors, stop-aware waiting, close/discard, and saturating aggregate coalesced/cancelled counters. No unbounded result history or queue growth.

Request IDs correlate barrier completions and must not be reused while an earlier completion can still arrive. A successful `post` acknowledges admission, never camera execution or durable persistence. Coalesced duplicate priority requests retain the pending command's original ID.

This task adds no camera worker, processing, persistence, startup panel or production live composition. The normal app still waits for an image; the separate M4 synthetic viewer remains unchanged. Tasks 2–5 own those later behaviors and their tests.

## Test-first and review evidence

Compiling interface stubs produced real assertion failures in both registered suites before implementation. Subsequent RED/GREEN slices covered the state table, priority/capacity/fences, generation/payload coalescing, wait/close/concurrency, and two self-review regressions: premature Disconnect completion and a duplicate pending Stop incorrectly splitting the Apply segment.

| Registered CTest suite | Coverage |
|---|---|
| `Application.CameraSessionStateMachine` | Four tests, including all **192 state/event pairs** against literal expected rows, valid startup/removal/retry sequences, and configuration rejection remaining idle |
| `Application.CameraCommandMailbox` | **22 tests**: capacity/FIFO, full-load priority admission, pending/executing fences, exact completion IDs, duplicate priority, generation and lifecycle isolation, full payload retention, immutable-by-copy stats, stop/close/wakeup, concurrent producers and late commands |

The independent task reviewer inspected the complete implementation diff against the brief/preflight: spec compliant, task quality approved, no Critical/Important/Minor findings. Cross-task obligations are explicitly deferred to their planned consumers: camera-thread ownership and guarded execution/cleanup in Task 2; capacity-one processing exchange in Task 3; persistence in Task 4; startup/Resume and session orchestration in Task 5. None is represented as tested by Task 1.

## Independent Linux verification

Linux/GCC 15.2.0, CMake 4.2.3, C++20 with warnings treated as errors. Existing pinned Qt/OpenCV dependency installations were reused; this is not fresh vcpkg-bootstrap evidence. Native desktop smoke used authorized Xvfb; no Qt plugin/dependency was substituted to bypass sandbox display restrictions.

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

A fresh local build directory `out/build/m05-tests-disabled` was configured with Ninja, Release, the same pinned dependency prefix, `LUMORA_BUILD_TESTS=OFF`, `BUILD_TESTING=OFF`, and `LUMORA_ENABLE_BASLER=OFF`. Building `lumora_application` and `lumora_app` passed. This checks Task 1's build independence; it is not evidence for Task 5's still-unimplemented production composition.

## Decisions and remaining gates

- Keep execution-time stale-session rejection in Task 2, while Task 1 tests generation isolation and payload retention: the mailbox has no authoritative active session. If this split proves inadequate, revisit the worker boundary before integration.
- Add a read-only mailbox statistics snapshot for the already-required admission diagnostics. If its shape proves inadequate, adjust that small API before Task 2; no queue capacity or per-command history was added.

Matching Windows/MSVC CI for this source has not run. Native Windows 11 checks remain deferred, and M4/M5 are not fully accepted. No hardware, installer, throughput/latency, soak or clinical validation is claimed. The next implementation step is **M5 Task 2: acquisition worker with exclusive device ownership**.
