# M5 Task 3: frame processor and processing worker

**Date:** 2026-09-07

**Baseline:** `7295fb95028714c16bdf8bf35cc77c920ccd2035`

**Implementation source:** `914f56888aff17e40acc099a897a153a149c024a`

**Review-fix source:** `f4e594075ed19b4941860a76d87b2c0c26a08a52`

**Status:** Implemented, task-reviewed and independently verified locally; final Standards/Spec checkpoint reviews pending. Not pushed, merged or accepted as a milestone.

## Scope and contracts

The owner approved Task 3 after the [merged Tasks 1–2 cross-platform checkpoint](m05-acquisition-worker.md#merged-cross-platform-checkpoint-2026-09-07). The [M5 plan](../../superpowers/plans/2026-04-25-m05-independent-live-pipeline.md#task-3-frame-processor-port-and-processing-worker) and [preflight](m05-preflight.md#5-processor-and-deterministic-tests) remain the implementation contracts.

This task supplies the Qt-free frame processor port, minimal full-range Mono8-to-Gray8 pass-through processor, and separately owned newest-frame processing worker. Raw pixels and metadata remain immutable; display pixels use a separate pool lease. The worker consumes slot revisions, publishes matching bundles, records bounded error/drop facts, and stops/joins without detaching. A finite in-flight processor call finishes and its unpublished output is discarded on cancellation.

No startup UI, saved preferences, production live composition, high-depth normalization, enhancement stages, clinical/patient features, dependency changes, push or merge are authorized by this task. Tasks 4–5 own the remaining startup and visible live integration. M7–M8 replace the temporary Mono8 processor through the same port.

## Implemented boundary

- `IFrameProcessor` is a Qt-free port with the approved immutable raw-frame input and typed bundle result. The Mono8 adapter borrows a display pool; it accepts the exact full-range descriptor and positive finite actual FPS, copies active row bytes into a separate tightly packed Gray8 lease, and preserves the raw owner, source ID and metadata. Original uses mapping `{0, 255, 255, 1}` and identity orientation; Enhanced members are absent.
- `ProcessingWorker` owns one joined thread, borrows its dependencies and consumes the newest raw revision. An in-flight frame remains unchanged by newer arrivals. Stop/closed checks suppress further processing/publication; the stable stop source supports concurrent cancellation without touching the owner-serialized thread handle. A cancelled finite in-flight call returns before join completes.
- A valid result must retain the exact input raw frame and source ID. Null or mismatched successful results are typed processing failures. Display-pool exhaustion is separately classified, with no fallback pixel allocation.
- A bounded, mutex-protected snapshot exposes saturating skipped-input, replaced-bundle and categorized failure totals plus the complete current error. The output slot must be fresh for the session, with this worker its exclusive publisher. Returned revision advancement acknowledges an accepted publication; a close-race rejection cannot clear the error.

## Test and process evidence

The initial compiling stubs failed the expected worker-entry and successful-copy assertions, then passed after the first vertical slice. Subsequent observed RED/GREEN cases covered the zero diagnostics stub, failure retention/clearing and Start after cancellation. The initial missing test include was a setup failure, not behavioral RED.

Some processor assertions were already green when added because the first successful bundle path necessarily established factory mapping, ownership and validation; these are characterization/contract coverage, not additional claimed RED cycles. A focused repetition run found a test race: consuming bundle 1 while awaiting bundle 3 legitimately prevented replacement. A dedicated latch-driven test now leaves the prior bundle unconsumed and proves publication progress through subsequent processor entry. Both focused CTest suites then passed 50 repetitions. This is focused stability evidence, not the opt-in ten-minute milestone stress test.

| Suite | Local coverage |
|---|---|
| `Processing.Mono8PassThrough` | Eight cases: padded active-row copy, all 256 sample values, Original-only identity-oriented Gray8 mapping, complete descriptor rejection, invalid FPS, raw immutability, shared-owner release and real display-pool exhaustion |
| `Application.ProcessingWorker` | 15 cases after review fixes: pre-start and in-flight newest selection, deterministic bundle replacement, typed error retention/clearing and exhaustion separation, waiting/in-flight cancellation, closed-empty termination, null/wrong-input/same-ID substituted-owner rejection, closed-output suppression, one-shot lifecycle and internal saturation arithmetic |

The tests use real pooled frames and capacity-one exchanges with releasable processor boundary adapters. Bounded entry/publication waits and RAII release of current/future calls keep failed assertions from stranding a blocked worker. No timing guarantee for an uncooperative processor, physical camera or whole-application shutdown is claimed.

## Independent controller verification

Before implementation, the isolated branch's existing native-inclusive Linux Debug build/test baseline passed 28/28 CTest entries in 16.29 seconds. This is baseline evidence only, not Task 3 behavior evidence.

At frozen source committed as `914f568`, the controller independently ran:

```bash
cmake --build --preset linux-gcc-debug-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure
cmake --build --preset linux-gcc-release-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-release-sim --no-tests=error --output-on-failure
cmake --build out/build/m05-tests-disabled --target lumora_application lumora_processing lumora_app --parallel 4
ctest --preset linux-gcc-debug-sim -N -R '^(Processing.Mono8PassThrough|Application.ProcessingWorker)$'
out/build/linux-gcc-debug-sim/tests/lumora_application_tests --gtest_filter='ProcessingWorker.*' --gtest_brief=1
out/build/linux-gcc-debug-sim/tests/lumora_processing_tests --gtest_brief=1
git diff --check 7295fb9...HEAD
```

Full native-inclusive Linux CTest passed **30/30 Debug (16.24 s)** and **30/30 Release (13.59 s)**. The direct focused runs passed **13/13 worker** and **8/8 processor** cases; listing found exactly the two required CTest entries. The tests-OFF/Basler-OFF processing/application/app rebuild and committed-range whitespace check passed. GCC 15.2.0, CMake 4.2.3, pinned native dependencies and existing caches were reused; no dependency, preset or warning-suppression change was made.

The implementer separately recorded 30/30 Debug (16.29 s), 30/30 Release (13.61 s) and the tests-disabled build. An earlier sandbox-only Debug invocation passed 29/30 because native XCB could not connect to the temporary display; the same suite passed with scoped native Xvfb access. No test was disabled to obtain the passing result.

## Review and contract decision

Independent task review found three Important issues at `914f568`: the four diagnostic counters wrapped instead of saturating; observing bundle publication did not synchronize the test's subsequent error-snapshot assertion; and comparing numeric frame IDs alone accepted a different raw object with the same ID. No Critical or Minor issue was reported.

Fix `f4e5940` uses one shared overflow-safe operation behind private saturating increment/add helpers, waits explicitly for the snapshot's cleared-error predicate, and requires exact selected raw-handle equality as well as matching source ID. The same-ID substituted-owner regression failed against the old worker and then passed. A private arithmetic seam reproduced wraparound at `UINT64_MAX` and overflow during addition before the fix, then passed zero/ordinary/exact-limit/overflow cases. This does not claim a public worker test emitted `UINT64_MAX` events, add public lifecycle test hooks or refactor unrelated workers. The snapshot race correction is justified by publication's wake-before-status-update ordering; no probabilistic reproduction is claimed.

At exact fix source `f4e5940`, the controller reran the full build/test commands: **30/30 Debug (16.19 s)** and **30/30 Release (13.64 s)**, including native desktop smoke. Direct focused runs passed **15/15 worker** and **8/8 processor** cases. Tests-OFF/Basler-OFF processing/application/app rebuild and the complete committed-range whitespace check passed. These results supersede the initial-source figures above for current code. The implementer separately passed both focused suites for 50 repetitions after the fixes.

Independent scoped re-review confirmed all three findings addressed, with no new breakage or out-of-scope observations. Task review has no open findings. Final Standards/Spec checkpoint reviews remain pending.

The plan required counters/current errors without specifying an observation method. The approved `snapshot()` supplies that bounded value without callbacks, history or a processor-signature change. `rawFramesSkipped` measures revision gaps at actual input selection and overlaps acquisition's `droppedBeforeProcessing`; downstream metrics must not sum them. Only `ResourceExhaustion / display_buffer_pool_exhausted` counts as actual display-pool exhaustion, not metadata allocation failures in the same category. Other errors retain their category/code and count under processing errors; normal cancellation is not a failure, and only an accepted non-cancelled publication clears the current error. Snapshot string copying is not `noexcept`.

If the observation shape proves inadequate, revise this small API before Task 5/M11 consumes it. Incorrect aggregation would otherwise misstate dropped frames. This is a clarified integration contract, not new UI behavior or acceptance evidence.

## Remaining gates

Matching Windows evidence for Task 3 remains pending; the green `7295fb9` runs cover only Tasks 1–2. Startup controls/preferences and visible production live composition remain Tasks 4–5. Nothing in this checkpoint authorizes those tasks, a push or a merge.

The [M4 native Windows 11 deferral](m04-deferred-windows-validation.md) remains open. No M4/M5 acceptance, installer/hardware validation, performance guarantee or authorization for clinical use follows from this checkpoint.
