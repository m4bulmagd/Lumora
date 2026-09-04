# Milestone 12 Reliability Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make failures deterministic and recoverable through tested timeout/reconnect policy, processing fallback, mandatory stale-image behavior, complete fault injection, lifecycle stress, and cross-platform simulator soak evidence.

**Architecture:** Recovery decisions live in application state machines and use injected time; adapters report facts but do not schedule policy. Stress and soak harnesses use the simulator and collect structured evidence from existing metrics.

**Tech Stack:** C++20, simulator fault scripts, application state machines, diagnostics, Qt integration tests, GoogleTest/CTest, Linux and Windows process-health adapters.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Manual Disconnect always cancels automatic reconnect.
- Three consecutive acquisition timeouts trigger reconnect; any successful frame resets the count.
- Reconnect attempts use approximately 0.5, 1, 2, 5, and 5 seconds, then enter Error.
- Three consecutive processing failures force Original presentation without stopping acquisition.
- No test relies on real-time sleeping when `ManualClock` can drive policy.
- No fault may create an unbounded queue, detached thread, or unreleased camera/pool resource.

---

### Task 1: Deterministic reconnect policy

**Files:**
- Create: `src/application/include/lumora/application/ReconnectPolicy.hpp`
- Create: `src/application/src/ReconnectPolicy.cpp`
- Create: `tests/unit/application/ReconnectPolicyTests.cpp`
- Modify: `src/application/src/CameraSessionStateMachine.cpp`

**Interfaces:**
- Consumes: `IClock`, desired camera ID/configuration/stream state, failure classification, and cancellation.
- Produces: `ReconnectPolicy::nextAttempt`, five-delay schedule, reset/cancel behavior, and exhausted state.

- [ ] **Step 1: Write failing schedule/reset tests**

```cpp
TEST(ReconnectPolicy, UsesBoundedScheduleThenExhausts) {
    ReconnectPolicy policy({500ms, 1s, 2s, 5s, 5s});
    EXPECT_EQ(policy.nextDelay().value(), 500ms);
    EXPECT_EQ(policy.nextDelay().value(), 1s);
    EXPECT_EQ(policy.nextDelay().value(), 2s);
    EXPECT_EQ(policy.nextDelay().value(), 5s);
    EXPECT_EQ(policy.nextDelay().value(), 5s);
    EXPECT_FALSE(policy.nextDelay().has_value());
}
```

- [ ] **Step 2: Verify policy is missing**

Build `lumora_application_tests`; expect failure.

- [ ] **Step 3: Implement stop-aware deadlines**

Compute deadlines from steady time, wake on stop/manual disconnect, retain the exact stable camera identity, and reset attempts only after a successful reconnect and valid frame—not merely device open.

- [ ] **Step 4: Integrate timeout threshold**

One/two consecutive timeouts retain Streaming with warning metrics. Third transitions to Reconnecting and tears down the device. Successful frame resets count. Nonrecoverable configuration/unsupported-format errors enter Error directly.

- [ ] **Step 5: Test cancellation and clock jumps**

Advance `ManualClock` through every delay, cancel at each boundary, issue manual Disconnect and Shutdown, and verify UTC clock changes have no effect.

- [ ] **Step 6: Commit policy**

```powershell
git add src/application tests/unit/application/ReconnectPolicyTests.cpp
git commit -m "feat(reliability): add bounded camera reconnect policy"
```

### Task 2: Reconnect orchestration and last-known configuration restore

**Files:**
- Modify: `src/application/src/AcquisitionWorker.cpp`
- Modify: `src/application/src/LivePipeline.cpp`
- Modify: `src/ui/src/WorkstationController.cpp`
- Create: `tests/integration/ReconnectionTests.cpp`

**Interfaces:**
- Consumes: reconnect policy, camera provider discovery/create, last successful applied settings, desired streaming state, and status/log sinks.
- Produces: automatic same-ID discovery/open/configure/start, bounded exhaustion, manual Retry, and visible state sequence.

- [ ] **Step 1: Write failing successful-reconnect sequence test**

Script disconnect at frame 20 and device restoration before attempt 3. Assert states `Streaming -> Reconnecting -> Connecting -> Streaming`, restored serial/settings, first new frame resets attempts, and UI stays responsive.

- [ ] **Step 2: Implement same-identity reconnect transaction**

At each deadline: discover; find exact `CameraId`; create/open; read capabilities; validate saved request; apply and read back; start only if desired stream was active; require one valid frame before success. Destroy each failed device before waiting again.

- [ ] **Step 3: Implement exhaustion and manual Retry**

After five failures enter Error with last failure and retry count. Retry starts a fresh policy only if desired camera still exists. Manual Disconnect moves directly to Disconnected and clears all pending deadlines.

- [ ] **Step 4: Test removal/configuration/error matrix**

Cover absent camera, wrong serial discovered, open failure, changed capabilities, apply failure/rollback, start failure, first-frame invalid, removal during reconnect, Retry, Disconnect, and Shutdown.

- [ ] **Step 5: Commit reconnection orchestration**

```powershell
git add src/application src/ui tests/integration/ReconnectionTests.cpp
git commit -m "feat(reliability): reconnect selected camera deterministically"
```

### Task 3: Processing failure circuit breaker and Original fallback

**Files:**
- Create: `src/application/include/lumora/application/ProcessingHealthPolicy.hpp`
- Create: `src/application/src/ProcessingHealthPolicy.cpp`
- Modify: `src/application/src/ProcessingWorker.cpp`
- Modify: `src/ui/src/WorkstationController.cpp`
- Create: `tests/integration/ProcessingFallbackTests.cpp`

**Interfaces:**
- Consumes: processing result by frame, active pipeline revision, and display mode.
- Produces: three-failure threshold, automatic Original presentation, recovery after valid pipeline/retry, and non-modal warning state.

- [ ] **Step 1: Write failing threshold/recovery tests**

One or two failed frames do not switch mode; a success resets count; three consecutive failures switch to Original and disable Enhanced/Compare. Applying a newly compiled valid revision clears the breaker only after its first successful enhanced frame.

- [ ] **Step 2: Implement processing health state**

Track failures per active pipeline revision, stable stage/error detail, and fallback state. Acquisition continues and Original path remains processed/displayed. Do not repeatedly log the same failure per frame; emit first, threshold, recovery, and rate-limited summary events.

- [ ] **Step 3: Test viewport/capture behavior during fallback**

Assert zoom/pan remain, paused frame remains unchanged, Processed/Both capture is disabled with reason when no enhanced frame exists, Original capture remains available, and recovery does not restart camera acquisition.

- [ ] **Step 4: Commit processing recovery**

```powershell
git add src/application src/ui tests/integration/ProcessingFallbackTests.cpp
git commit -m "feat(reliability): fall back to Original after processing faults"
```

### Task 4: Complete automated fault and lifecycle matrix

**Files:**
- Create: `tests/integration/FaultMatrixTests.cpp`
- Create: `tests/lifecycle/RepeatedLifecycleTests.cpp`
- Create: `tests/lifecycle/ShutdownStateTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: simulator fault scripts, fake storage/encoder/logger, manual clock, all application commands, and pool stats.
- Produces: deterministic test coverage for every reliability rule and CTest label `stress` for long variants.

- [ ] **Step 1: Enumerate the matrix as parameterized test data**

Rows include discovery failure, connect failure, timeout 1/2/3, malformed frame, device removal, wrong device on reconnect, configuration write/rollback failure, processing stage throw/error, every pool exhausted, capture queue full, disk full, access denied, destination loss, encoder failure, logger failure, camera stall, processing-publication stall, UI-presentation stall, paused ageing, and shutdown from every state.

- [ ] **Step 2: Define expected outcome for every row**

Each row records expected state, whether acquisition continues, visible operator message code, metrics counter, required log event, and resource-release assertion. Stall rows prove `STALE IMAGE / NOT LIVE` appears after `max(500 ms, 3 expected frame periods)`, remains visible in Compare/fullscreen, and clears only after successful fresh presentation. Pause rows prove PAUSED, frozen timestamp, and increasing age persist without incorrectly becoming Live. No row may use a generic “does not crash” assertion alone.

- [ ] **Step 3: Implement 1,000-cycle lifecycle stress test**

Each cycle performs discover/connect/apply/start, acquires at least three frames, optionally pauses/resumes, stop/disconnect, and asserts device closed, workers joinable/stopped, slots released, and pool in-use counts zero. Use deterministic simulator time.

- [ ] **Step 4: Implement shutdown-from-state test**

Initiate shutdown from Disconnected, Discovering, Connecting, ConnectedIdle, Streaming, Reconnecting delay, Error, processing active, capture active, and capture queued. Assert documented shutdown order and ticket outcomes.

- [ ] **Step 5: Run short and stress suites**

```bash
ctest --preset linux-gcc-release-sim --output-on-failure -R FaultMatrix
ctest --preset linux-gcc-release-sim --output-on-failure -L stress
```

Require the matching Windows/MSVC CI suites before accepting the task.

- [ ] **Step 6: Commit reliability matrix**

```powershell
git add tests/integration/FaultMatrixTests.cpp tests/lifecycle tests/CMakeLists.txt
git commit -m "test(reliability): cover fault and lifecycle matrix"
```

### Task 5: Eight-hour simulator soak and process-health evidence

**Files:**
- Create: `tools/soak-runner/main.cpp`
- Create: `tools/soak-runner/SoakScenario.cpp`
- Create: `src/diagnostics/include/lumora/diagnostics/ProcessHealth.hpp`
- Create: `src/diagnostics/src/ProcessHealthLinux.cpp`
- Create: `src/diagnostics/src/ProcessHealthWindows.cpp`
- Create: `tests/unit/diagnostics/ProcessHealthTests.cpp`
- Create: `docs/operations/soak-testing.md`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: simulator live pipeline, configurable duration/FPS/resolution/preset/fault schedule, platform process-health sampling, and metrics snapshots.
- Produces: JSON soak report with warm-up baseline, time-series memory/resource/thread counts, FPS/latency/drops/errors, final resource counts, and pass/fail criteria.

- [ ] **Step 1: Write report-calculation tests**

Given synthetic samples, assert warm-up begins at 15 minutes, a monotonic leak trend fails, a bounded oscillation passes, handle growth fails, and drops under deliberate overload are reported but not treated as queue growth.

- [ ] **Step 2: Implement platform process sampling**

Define `IProcessHealthSampler` and provide Linux (`/proc`/documented OS APIs) and Windows process implementations. Read private/working memory, committed private bytes where available, process resource/handle count, thread count, and pool stats every 30 seconds. Sampling failure records unavailable, not zero; platform differences are named fields rather than forced false equivalence.

- [ ] **Step 3: Implement parameterized soak runner**

Default run: 8 hours, 2048x2048 U16, 30 FPS, Standard preset, snapshot every 15 minutes to a temporary configured root, pause/resume hourly, stop/start every two hours, one scripted disconnect/reconnect halfway through.

- [ ] **Step 4: Define pass criteria**

No crash/deadlock; all queues remain at fixed capacity; no positive sustained linear memory/resource trend after warm-up beyond measurement noise; stale/paused indications pass; final leases return after shutdown. The P95 33.3 ms processing and below-100 ms live-latency gates apply only on the designated Windows reference workstation, never ordinary CI.

- [ ] **Step 5: Run a 10-minute smoke then eight-hour soak**

Archive command line, application/dependency versions, JSON report, logs, and machine profile under `artifacts/soak/<UTC timestamp>/`. Do not commit runtime artifacts.

- [ ] **Step 6: Commit soak tooling/docs**

```powershell
git add tools/soak-runner src/diagnostics tests/unit/diagnostics/ProcessHealthTests.cpp docs/operations/soak-testing.md src/CMakeLists.txt
git commit -m "test(reliability): add simulator soak evidence runner"
```

## Milestone 12 acceptance gate

- [ ] Timeout, reconnect, cancellation, exhaustion, and processing fallback tests pass deterministically.
- [ ] The 1,000-cycle lifecycle suite returns all camera and pool resources.
- [ ] Shutdown succeeds from every enumerated active state.
- [ ] Eight-hour simulator report shows fixed queue depths and no continuing memory/handle trend after warm-up.
- [ ] Every fault produces a specific state, operator result, metric, and log event.
- [ ] Camera, processing, and UI stalls reliably show/clear the mandatory stale indication; pause state shows immutable timestamp and increasing age.
- [ ] Linux and Windows process samplers both run, with timing thresholds enforced only on the designated Windows reference workstation.
