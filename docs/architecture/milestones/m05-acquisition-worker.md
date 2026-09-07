# M5 Task 2: dedicated acquisition worker

**Date:** 2026-09-07

**Initial implementation source:** `a3a1821ab583315102ba5940e18b747868c3aacc`

**Review-fix source:** `1dc19e65381a87fa6660f82fed0a593d9de4a9cb`

**Status:** Implemented, verified and task-reviewed locally; final Standards/Spec review pending. Windows CI and full M5 acceptance pending. Not a milestone acceptance record.

The [M5 plan](../../superpowers/plans/2026-04-25-m05-independent-live-pipeline.md) and [preflight contracts](m05-preflight.md) define this task. Development continues under the [M4 Windows 11 manual-validation deferral](m04-deferred-windows-validation.md); those checks remain pending, not passed. Nothing was pushed or merged for this checkpoint.

## Implemented boundary

- A Qt-free acquisition worker with explicit start, command submission, cancellation and join. Provider discovery/create, every device operation, and device destruction run on its owned camera thread. Borrowed dependencies outlive the worker.
- Explicit Discover/Connect/Apply/readback/Confirm/Start, current-generation and configuration-revision checks, same-device Stop/Start, manual Disconnect, and one same-ID operator Retry. Successful command admission is not reported as device execution success.
- A bounded priority-first command loop and 250 ms retrieval budget. Posted Shutdown directly requests cancellation; cleanup completes before its outcome. Stop/Disconnect admission fences survive until actual cleanup completes.
- Validated immutable raw frames published through a capacity-one slot. Saturating counters distinguish acquisition, timeouts, invalid frames, raw-pool exhaustion, replacement before processing and session-terminating failures. Adapter frame timestamps remain unchanged.
- First/second timeout stay Streaming; a valid frame resets the streak; malformed or exhausted frames are dropped without resetting it. Third timeout or recoverable removal tears down to Reconnecting. Terminal failures enter Error, and failed cleanup is not reported as healthy idle.
- One device lifetime per source context, with an explicit replacement-required fact for the future session owner. Retry on a fresh Reconnecting worker opens the retained identity once to idle, never silently starts, and never substitutes another camera.

The initial supported processing-compatible mode is full-range Mono8. The worker checks the first applied mode against capabilities and raw-pool capacity, then rejects subsequent descriptor/ROI changes before device mutation. The future composition selects the initial approved dimensions and matching downstream pools; 640x480 is not hardcoded in this worker.

There is no new visible UI behavior yet. Processing, preferences, startup controls, presenter handoff and the normal application's live composition remain Tasks 3–5. Automatic retry scheduling and stream restoration remain M12. No hardware or clinical-use support is claimed.

## Test and process evidence

The compiling ownership/publication stub failed both expected readiness assertions before implementation; the new CTest entry was listed before execution. Expanded RED failed 15 worker cases and two consumer-seam cases while the original 26 application tests stayed passing. The initial tests and first implementation were horizontally batched, rather than strict one-test-at-a-time development; this is a recorded process limitation.

Two subsequently discovered Shutdown defects had individual observed RED/GREEN regressions: cleanup failure missing from the Shutdown outcome, and posted Shutdown failing to cancel an active retrieval directly. Cancellation tests also use an explicit entry latch so a successful-after-stop fake frame is genuinely prepared through that path, rather than passing because cancellation happened too early.

| Suite | Local coverage |
|---|---|
| `Application.CameraSessionStateMachine` | Five cases: existing 192 state/event pairs plus validated initial device-free states |
| `Application.CameraCommandMailbox` | 23 cases: existing priority/coalescing/fences plus priority-only pop preserving ordinary FIFO and close observation |
| `Application.AcquisitionWorker` | 28 cases after review fixes: ownership, explicit startup, stale generations/revisions, fixed mode/readback, timeout/drop classification, replacement/Retry, real pool exhaustion, saturation, full-mailbox priority, failure cleanup, closed mailbox and cancellation |

Tests use real pooled leases, scripted camera/provider boundary adapters, manual clock values, bounded waits and RAII release/join on all exits. The 500 ms cancellation assertion applies to controlled cooperating retrieval tests, not arbitrary driver teardown, filesystem operations or whole-application exit.

## Independent controller verification

At exact source `a3a1821`, Linux/GCC 15.2.0 and CMake 4.2.3, C++20 with warnings treated as errors. Existing pinned native Qt/OpenCV installations and build caches were reused; this is not fresh dependency-bootstrap evidence. Native desktop smoke used authorized Xvfb.

```bash
cmake --build --preset linux-gcc-debug-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure
cmake --build --preset linux-gcc-release-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-release-sim --no-tests=error --output-on-failure
cmake --build out/build/m05-tests-disabled --target lumora_application lumora_app --parallel 4
ctest --preset linux-gcc-debug-sim -N -R '^Application\.'
out/build/linux-gcc-debug-sim/tests/lumora_application_tests --gtest_brief=1
git diff --check f0bd99d...HEAD
```

Full native-inclusive CTest passed **28/28 Debug (16.26 s)** and **28/28 Release (13.52 s)**. The direct application run passed **54/54 tests (1034 ms)**. Listing found exactly three Application entries, each with a 60-second watchdog. The tests-OFF/Basler-OFF application/library rebuild and committed-range whitespace check passed. No dependency, preset or warning-suppression change was made.

The implementer independently recorded final full runs of 28/28 Debug (16.14 s) and 28/28 Release (13.60 s), plus the tests-disabled build. The controller results above are a separate verification of the committed source.

## Independent review and fixes

The task reviewer found two Important issues in the initial implementation: non-configuration Apply failures incorrectly retained ConnectedIdle and prior confirmation, and recursive priority execution inside Start allowed the interrupted Start's cancellation to overwrite a priority cleanup outcome. There were no Critical or Minor findings. The controller verified both paths against the source and the simulator's connection-loss result.

Follow-up `1dc19e6` routes non-configuration Apply failures through owning-thread teardown to Error while retaining desired identity for owner-mediated Retry. Genuine configuration rejection keeps the prior applied/confirmed settings. Its regression failed before the fix for both connection and internal errors, then passed afterward.

The same follow-up replaces recursive execution with one bounded, local deferred priority command. Interrupted Start completes first; the priority payload then executes and reports its result last, even when cancellation is already requested. Admission fences still complete after their corresponding device work. Deterministic public latch tests verify priority cleanup errors survive later cancellation/join. They passed before and after this ordering fix and are **not** direct RED evidence for the narrow final-Start race.

Current public interfaces cannot deterministically force a priority post between Start dequeue and its final priority check. That interval has no injectable camera or clock call. No production scheduling hook, artificial clock call, platform linker interception or race-dependent test was added solely to force it. The exact branch requires structural review; this remains an explicit automated-coverage limitation, not a claim that another test exercised the interleaving.

At exact fix source `1dc19e6`, the controller rebuilt and reran the full commands above: **28/28 Debug (16.26 s)** and **28/28 Release (13.50 s)**, including native desktop smoke. The application executable passed **56/56 tests (1035 ms)**; the tests-OFF/Basler-OFF library/application rebuild and committed-range whitespace check passed. These results supersede the initial-source figures for current code. The implementer separately recorded 28/28 Debug (16.21 s) and Release (13.60 s).

Independent scoped re-review verified both findings addressed and no new Critical/Important breakage. It structurally checked the final-Start deferral and confirmed priority results are published last. A duplicate-ID concern was withdrawn after checking the pre-existing public rule: request IDs must not be reused while an earlier completion can still arrive. Under that supported-input contract, completing interrupted Start cannot clear the distinct deferred Stop/Disconnect barrier. Task review has no open findings; the narrow-path test limitation remains recorded above.

## Contract decisions and integration costs

1. Add latest discovery/acquisition facts, a validated initial-state factory, and priority-only/closed mailbox reads. These make the required worker behavior observable without extra command history. If their shape proves inadequate, revise these small interfaces before the UI/session owner consumes them.
2. Require fresh source context after a device is destroyed. The worker exposes `sourceReplacementRequired`; Task 5 must join the retiring worker, provide fresh slots/pools/generation and forward the explicit Connect/Retry action. If this handoff proves inadequate, redesign it before integration; reusing old-context frame IDs is not an acceptable fallback.
3. Fix the context's mode at the first successful Apply/readback. Composition selects the initial mode and matching pools. If this division is insufficient, introduce an explicit accepted-mode value before integration; future editable modes require stopped-state resource rebinding.
4. Use a stable worker-owned stop source shared by waits/discovery/retrieval, while the owner serializes the `jthread` handle's start/join. This avoids producer-thread access to that changing handle. If the lifecycle model expands, revisit synchronization without weakening direct cancellation or joined ownership.
5. Correct the narrow Start/priority outcome ordering with bounded deferral and require direct structural review, while documenting the deterministic-coverage gap above. If the reasoning is wrong, that scheduling defect may evade automated tests; revisit the seam if execution boundaries change, without treating probabilistic tests as deterministic evidence.

## Remaining gates

Final Standards/Spec review is pending. Matching Windows/MSVC CI has not run for this source. Native Windows 11 checks remain deferred; neither M4 nor M5 is fully accepted. No installer, hardware, throughput/latency, long-duration soak or clinical validation is supplied by these tests.

The next planned implementation step is **M5 Task 3: frame processor port and processing worker**.
