# Milestone 11 Diagnostics and Supportability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide structured rotating logs, complete categorized metrics, rolling performance statistics, operator health presentation, and field troubleshooting documentation.

**Architecture:** Workers update counters and bounded timing windows directly; the UI consumes an immutable snapshot twice per second. spdlog writes bounded asynchronous JSON-lines events, while high-frequency frame facts remain metrics rather than logs.

**Tech Stack:** C++20, spdlog, Qt Core JSON and Widgets, platform-neutral diagnostics interfaces, GoogleTest/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Never log frame payloads or emit a normal log line for every frame.
- Log queue and timing windows are bounded.
- Log rotation is five files of 10 MiB each.
- Metrics distinguish camera loss, grab failure, raw replacement, processed replacement, pool exhaustion, and capture rejection.
- Host `steady_clock` is the only live-latency clock.
- Logging failure is visible but does not stop imaging.

---

### Task 1: Typed JSON-lines diagnostic events

**Files:**
- Create: `src/diagnostics/include/lumora/diagnostics/DiagnosticEvent.hpp`
- Create: `src/diagnostics/include/lumora/diagnostics/JsonEventFormatter.hpp`
- Create: `src/diagnostics/src/DiagnosticEvent.cpp`
- Create: `src/diagnostics/src/JsonEventFormatter.cpp`
- Create: `tests/unit/diagnostics/JsonEventFormatterTests.cpp`
- Modify: `src/diagnostics/include/lumora/diagnostics/Logging.hpp`
- Modify: `src/diagnostics/src/Logging.cpp`

**Interfaces:**
- Consumes: stable event ID/name, event schema version, severity, UTC timestamp, thread identity, and typed scalar fields.
- Produces: `DiagnosticEvent`, valid single-line JSON serialization, `Logging::write(DiagnosticEvent)`, and bounded async logger health.

- [ ] **Step 1: Write failing escaping/schema tests**

```cpp
TEST(JsonEventFormatter, EscapesVendorTextAndKeepsOneLine) {
    DiagnosticEvent event = eventForTest("camera_error")
        .field("model", "Camera \"A\"\nLine")
        .field("nativeCode", std::int64_t{42});
    auto json = JsonEventFormatter::format(event);
    EXPECT_EQ(std::count(json.begin(), json.end(), '\n'), 0);
    EXPECT_EQ(parseJson(json)["model"], "Camera \"A\"\nLine");
}
```

- [ ] **Step 2: Verify formatter is missing**

Build `lumora_diagnostics_tests`; expect failure.

- [ ] **Step 3: Implement schema and safe serialization**

Every line contains schema version, stable event ID/name, release class, UTC timestamp, severity, application version, process ID, thread ID/name, and versioned typed event fields. Serialize through Qt JSON to guarantee escaping; optional values are omitted only when absence is semantically distinct from null. These stable events are future traceability hooks, not a claim that evaluation logs are a compliant clinical audit trail.

- [ ] **Step 4: Convert logger to bounded asynchronous operation**

Use spdlog's async thread pool with queue capacity 8192 and one logging worker. Overflow policy is overrun-oldest; increment `logEventsDropped` and expose unhealthy status. Keep five 10 MiB rotating files and Debug console sink.

- [ ] **Step 5: Test rotation, overflow, and sink failure**

Inject a small queue/file size in tests, verify every produced line parses as JSON, rotation count is bounded, overflow counter increases without blocking the caller, and an unwritable sink returns health failure without terminating.

- [ ] **Step 6: Commit structured logging**

```powershell
git add src/diagnostics tests/unit/diagnostics
git commit -m "feat(diagnostics): add bounded structured event logging"
```

### Task 2: Metric counters and rolling timing snapshots

**Files:**
- Create: `src/diagnostics/include/lumora/diagnostics/Metrics.hpp`
- Create: `src/diagnostics/include/lumora/diagnostics/RollingStatistics.hpp`
- Create: `src/diagnostics/src/Metrics.cpp`
- Create: `src/diagnostics/src/RollingStatistics.cpp`
- Create: `tests/unit/diagnostics/MetricsTests.cpp`
- Create: `tests/unit/diagnostics/RollingStatisticsTests.cpp`

**Interfaces:**
- Consumes: per-boundary events/timings, pool stats, state, and `IClock`.
- Produces: `MetricsRecorder`, immutable `MetricsSnapshot`, two-second FPS windows, 512-sample timing windows, latest/median/P95, and counter reset only at application start.

- [ ] **Step 1: Write failing rate/percentile tests**

```cpp
TEST(Metrics, ComputesDisplayedFpsFromUniquePresentedFrames) {
    ManualClock clock;
    MetricsRecorder metrics(clock);
    for (int i = 0; i < 60; ++i) metrics.frameDisplayed(i + 1);
    clock.advance(2s);
    EXPECT_DOUBLE_EQ(metrics.snapshot().displayedFps, 30.0);
}
```

- [ ] **Step 2: Verify metrics types are missing**

Build `lumora_diagnostics_tests`; expect failure.

- [ ] **Step 3: Implement categorized counters**

Use atomics for acquired, processed, displayed unique IDs, camera skipped, timeout, grab failure, invalid frame, raw replacement, processed replacement, raw/process/display pool exhaustion, capture rejected, capture failed, and log events dropped.

- [ ] **Step 4: Implement bounded timing windows**

Store at most 512 processing and latency samples per window. Snapshot copies under a short lock then computes median/P95 outside producer-critical sections. Reject negative/mixed-clock durations.

- [ ] **Step 5: Test concurrency and no-reset snapshots**

Use four recording threads and one snapshot reader for 100,000 events. Assert exact cumulative counts, finite rates, correct percentile definition (nearest-rank), fixed sample capacity, and monotonic counters.

- [ ] **Step 6: Commit metrics**

```powershell
git add src/diagnostics tests/unit/diagnostics/MetricsTests.cpp tests/unit/diagnostics/RollingStatisticsTests.cpp
git commit -m "feat(diagnostics): add bounded frame and latency metrics"
```

### Task 3: Instrument camera, processing, presentation, pools, and capture

**Files:**
- Modify: `src/application/src/AcquisitionWorker.cpp`
- Modify: `src/application/src/ProcessingWorker.cpp`
- Modify: `src/application/src/LivePipeline.cpp`
- Modify: `src/ui/src/FramePresenter.cpp`
- Modify: `src/capture/src/CaptureService.cpp`
- Modify: `src/core/src/BufferPool.cpp`
- Create: `tests/integration/DiagnosticsIntegrationTests.cpp`

**Interfaces:**
- Consumes: shared `MetricsRecorder` and `Logging` event sink supplied by composition root.
- Produces: one unambiguous counter/timing update at each success/failure boundary and rate-limited drop summaries.

- [ ] **Step 1: Write failing category-attribution test**

Inject one camera skip, one timeout, two raw replacements, three bundle replacements, one no-buffer drop, one capture rejection, and one processing failure; assert each snapshot field exactly and no neighboring category changes.

- [ ] **Step 2: Add instrumentation at ownership boundaries**

Camera receipt stamps host steady/UTC times and increments acquired only for a validated frame. Processing duration spans processor entry/exit. Display latency spans host receipt to a paint/presentation acknowledgement for a new frame ID. Repainting the same paused ID does not increment displayed count.

- [ ] **Step 3: Add required lifecycle/error events**

Emit startup/shutdown/version/release class, configuration changes/migrations, discovery summary, camera identity/connect/disconnect, stream start/stop, timeout/error/removal, processing fallback, installation-orientation change, stale-image enter/clear, capture outcome, storage failure, and shutdown warning events.

- [ ] **Step 4: Implement drop summary limiter**

Accumulate drops continuously and log a summary no more than every ten seconds while counts change, plus immediately on stream stop/error. Unit-test with `ManualClock`; do not sleep.

- [ ] **Step 5: Run integration diagnostics matrix**

Assert metrics values, JSON event names/identity, absence of per-frame log lines during 10,000 healthy frames, and exactly one summary interval for a controlled drop burst.

- [ ] **Step 6: Commit instrumentation**

```powershell
git add src/core src/application src/ui src/capture tests/integration/DiagnosticsIntegrationTests.cpp
git commit -m "feat(diagnostics): instrument complete live pipeline"
```

### Task 4: Operator health UI and support documentation

**Files:**
- Create: `src/ui/include/lumora/ui/DiagnosticsPanel.hpp`
- Create: `src/ui/src/DiagnosticsPanel.cpp`
- Create: `tests/unit/ui/DiagnosticsPanelTests.cpp`
- Create: `docs/operations/diagnostics.md`
- Create: `docs/operations/configuration-and-data-locations.md`
- Modify: `src/ui/src/WorkstationController.cpp`

**Interfaces:**
- Consumes: `MetricsSnapshot`, camera/application state, and log/config/capture locations.
- Produces: compact footer health, optional detailed panel, copyable diagnostic text, and operator/support location guide.

- [ ] **Step 1: Write failing state/metric formatting tests**

Assert Disconnected/Connecting/Reconnecting/Error/Live/Paused text, acquisition/display FPS, processing P95, current latency, categorized total drops, acquisition failures, pool high-water, and logging health. Missing data displays `—`, never zero.

- [ ] **Step 2: Implement low-clutter presentation**

Header/footer show essential state and acquisition/display FPS. Detailed panel is collapsed by default and updates at most twice per second. Errors remain visible in fullscreen through the existing overlay.

- [ ] **Step 3: Document diagnostic collection**

Describe exact Windows `%LOCALAPPDATA%\Lumora\Config`, `%LOCALAPPDATA%\Lumora\Logs`, `%USERPROFILE%\Pictures\Lumora\Captures`, and admin-write/operator-read `%PROGRAMDATA%\Lumora\Config` installation-profile locations plus the corresponding Linux XDG/injected system paths. Cover retention, ACL expectations, event schema, release class, camera identity fields, how to enable the diagnostics panel, and which files can be shared. Explicitly state evaluation data must be phantom/test/synthetic/properly anonymized, captures are not part of routine log collection, and logs are not a regulated audit trail.

- [ ] **Step 4: Run UI and log-location tests**

Use fake snapshots and temporary directories; verify no update occurs faster than 500 ms, status remains readable without color, and copyable text contains application version/camera serial but no pixel data.

- [ ] **Step 5: Commit supportability UI/docs**

```powershell
git add src/ui tests/unit/ui/DiagnosticsPanelTests.cpp docs/operations
git commit -m "feat(diagnostics): expose workstation health and support guide"
```

## Milestone 11 acceptance gate

- [ ] Every required log line is valid JSON and normal streaming emits no per-frame lines.
- [ ] Rotation, async queue, timing windows, and UI update rates are bounded.
- [ ] Metrics attribute each injected drop/failure to exactly one category.
- [ ] Latency uses one host monotonic clock and displayed FPS counts unique presented IDs.
- [ ] Logging failure is visible and nonfatal.
- [ ] Stable versioned event IDs include release class, orientation change, and stale enter/clear; documentation distinguishes diagnostic logs from future clinical audit requirements.
