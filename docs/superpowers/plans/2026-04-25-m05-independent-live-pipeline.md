# Milestone 5 Independent Live Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect camera acquisition, processing, and Qt presentation through independently owned workers and capacity-one latest-frame exchanges.

**Architecture:** One camera thread owns the camera session, one processing thread consumes newest raw frames, and the UI polls newest bundles. Commands and state are bounded, stop-aware, and testable without Qt event traffic per frame.

**Tech Stack:** C++20 `std::jthread`, Qt 6 presentation adapter, camera API/simulator, core bounded primitives, GoogleTest/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

**Execution and integration record (2026-09-07):** Read the [M5 preflight contracts](../../architecture/milestones/m05-preflight.md) and [scoped M4 deferral](../../architecture/milestones/m04-deferred-windows-validation.md). Windows stress and matching cross-platform CI passed at `6c054a7`. Tasks 1–2 were implemented, reviewed, merged and pushed with explicit authorization; matching Linux/Windows Debug/Release CI passed at `7295fb9`, as recorded in the [Task 2 checkpoint](../../architecture/milestones/m05-acquisition-worker.md#merged-cross-platform-checkpoint-2026-09-07). The owner then authorized Task 3 publication and local Tasks 4–5 development without waiting for CI, followed by separate authorization to publish and merge the completed work. [PR #6](https://github.com/m4bulmagd/Lumora/pull/6) merged Task 3 as `2031848` after exact-head checks at `ccabaae`; [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) merged Tasks 4–5 as `f01b408` after its own final-head checks passed. The [merged integration checkpoint](../../architecture/milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) records passing post-merge Linux/Windows Debug/Release CI and separately authorized merged-branch cleanup. All task/review work is complete; native Windows 11 manual checks and separate M4/M5 acceptance remain open. No later milestone-entry exception is implied.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Camera calls execute only on the camera worker.
- Processing executes only on the processing worker.
- Raw and bundle exchanges have capacity one and replace stale values.
- Viewer Pause does not stop acquisition.
- First run requires explicit camera selection, configuration confirmation, and Start; later runs may offer Resume Live for the unchanged last identity/capabilities but never auto-stream silently.
- All workers stop with stop tokens and are joined in deterministic order.
- This milestone uses only a minimal Mono8 pass-through processor; high-depth processing begins in Milestone 7.
- Minimal startup controls and saved startup preferences belong here; the complete parameter editor and per-camera profiles extend them in M9.
- Failure classification and third-timeout/removal teardown belong here; scheduled automatic recovery and stream restoration belong in M12. M5 Retry is one operator-requested same-ID open attempt, never silent Start.
- Camera/processing/application values remain Qt-free. JSON/disk work runs in the composition-owned background configuration adapter, not on UI, camera, or processing threads.

## Build and test execution rules

Register sources/tests as part of each task, using the [target map](../../architecture/milestones/m05-preflight.md#6-build-wiring-and-verification-evidence). New source targets are `lumora_application`/`lumora::application` and `lumora_processing`/`lumora::processing`. Unit targets use `GTest::gtest_main`; existing Qt targets keep `support/QtTestMain.cpp` and `GTest::gtest`. Extend the existing `lumora_integration_tests`, rather than redefining it. Keep target-scoped warnings/includes, the configuration-matched `minimal` QPA plugin, existing M4 tests, and simulator-only/tests-OFF application support.

For each focused suite below: reconfigure, build the named target, list the CTest entry, and run it. A missing target, missing test, or unrelated M4-only pass is not a behavioral red test. Start with compiling interface stubs and a failing assertion, then implement behavior and repeat the same command at green. Each example's fixture helpers are test-only and must have bounded waits and RAII cleanup; they are not additions to the production interface.

```bash
cmake --preset linux-gcc-debug-sim
cmake --build --preset linux-gcc-debug-sim --target lumora_application_tests
ctest --preset linux-gcc-debug-sim -N -R '^Application\.'
ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure -R '^Application\.'
```

Apply the same configure/build/test cycle to Release and matching Windows presets. Preserve any ignored local native-dependency prefix configuration; never commit absolute machine paths or overwrite another worktree's build cache.

---

### Task 1: Camera session state machine and command mailbox

**Files:**
- Create: `src/application/include/lumora/application/CameraSessionStateMachine.hpp`
- Create: `src/application/include/lumora/application/CameraCommandMailbox.hpp`
- Create: `src/application/include/lumora/application/CameraCommand.hpp`
- Create: `src/application/include/lumora/application/ApplicationState.hpp`
- Create: `src/application/src/CameraSessionStateMachine.cpp`
- Create: `src/application/src/CameraCommandMailbox.cpp`
- Create: `tests/unit/application/CameraSessionStateMachineTests.cpp`
- Create: `tests/unit/application/CameraCommandMailboxTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CameraId`, `CameraConfiguration`, `AppliedCameraConfiguration`, capabilities, `Error`, stop tokens, and fixed bounded storage. The M2 generic FIFO is unchanged.
- Produces: `CameraSessionState`, `CameraSessionEvent`, `struct CameraCommand`, `CameraStatusSnapshot`, and `CameraCommandMailbox::{post,tryPop,waitPop,completeBarrier,close,size,stats}` with the [exact priority/admission contract](../../architecture/milestones/m05-preflight.md#1-camera-commands-and-bounded-mailbox).

- [x] **Step 1: Write the failing transition-table tests**

```cpp
TEST(CameraSessionStateMachine, CannotStreamBeforeConnection) {
    CameraSessionStateMachine machine;
    auto result = machine.apply(CameraSessionEvent::StartRequested);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(machine.state(), CameraSessionState::Disconnected);
}

TEST(CameraSessionStateMachine, RemovalWhileStreamingRequestsReconnect) {
    auto machine = streamingMachine();
    ASSERT_TRUE(machine.apply(CameraSessionEvent::DeviceRemoved).hasValue());
    EXPECT_EQ(machine.state(), CameraSessionState::Reconnecting);
}
```

- [x] **Step 2: Register and run the failing state/mailbox suites**

Create/register `lumora_application`, `lumora_application_tests`, `Application.CameraSessionStateMachine` and `Application.CameraCommandMailbox` with the target map, then:

```bash
cmake --preset linux-gcc-debug-sim
cmake --build --preset linux-gcc-debug-sim --target lumora_application_tests
ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure -R '^Application\.(CameraSessionStateMachine|CameraCommandMailbox)$'
```

Expected: a transition or priority assertion fails against compiling stubs, not an unknown target.

- [x] **Step 3: Implement the explicit state/event table**

```cpp
enum class CameraSessionState {
    Disconnected, Discovering, Connecting, ConnectedIdle,
    Streaming, Reconnecting, Error, ShuttingDown
};

struct CameraCommand final {
    std::uint64_t requestId;
    std::variant<Discover, Connect, Disconnect, ApplyConfiguration,
        ConfirmConfiguration, StartStream, StopStream, Retry, Shutdown> payload;
};
```

Declare each command payload from the preflight command table in `CameraCommand.hpp`. Declare the state/event enum and snapshot in `ApplicationState.hpp`; keep `CameraSessionStateMachine::apply(CameraSessionEvent) -> core::Result<void>` and `state() const noexcept -> CameraSessionState`. The worker validates payload/revision guards before emitting a permitted event; the pure state machine enforces state ordering. Define requested/succeeded/failed events for discovery, open, apply, and start plus confirmation, stop, removal, timeout threshold, retry, manual disconnect, and shutdown, covering the [state/event table](../../architecture/milestones/m05-preflight.md#2-state-startup-guards-and-milestone-recovery-policy). `streamingMachine()` is a test helper that applies the valid connect/open/apply/confirm/start-success event sequence and fails immediately if any setup event is rejected.

Reject invalid transitions without mutation; preserve idempotent same-ID Connect, Start, Stop, Disconnect, and Shutdown. Manual Disconnect clears desired connection and all pending automatic/startup continuation. Do not map every error to Reconnecting: preserve typed timeout, malformed-frame, exhaustion, configuration-rejection, cancellation, and terminal-failure outcomes. M12 supplies timed recovery, not the base classification.

Keep startup intent/confirmation separate from camera and viewer state. Task 4 supplies the typed saved record/panel, Task 5 wires its flow. Start requires a successful applied revision and matching confirmation on the camera worker; a UI button alone is not authorization. A later Resume action must revalidate identity/capabilities and actual readback before Start; drift cancels the continuation and requires review.

- [x] **Step 4: Implement mailbox priority/coalescing**

Use fixed storage for 32 commands with one lock around admission/coalescing/selection. Shutdown > Disconnect > Stop > ordinary FIFO. Priority commands remain admissible under full load by cancelling superseded/oldest ordinary work; repeated priority commands coalesce. Stop cancels pending Start/Confirm, Disconnect cancels all pending camera-specific work, and Shutdown seals admission. Coalesce Apply only within the same generation and lifecycle-barrier segment. Return `Result<void>` with `ResourceExhaustion / camera_mailbox_full` or `Cancelled / cancelled`, not an unexplained bool. Implement the pending Stop/Disconnect admission fences and nonblocking `tryPop` described in the preflight.

- [x] **Step 5: Exhaustively test state/event pairs and mailbox concurrency**

Generate every enum pair, assert the documented next state or unchanged typed error, and test concurrent producers, a full mailbox for each priority command, late Start/Connect during pending and in-flight barriers, generation-isolated Apply coalescing and preserved Apply/Confirm/Start payloads, configuration coalescing across lifecycle barriers, bounded diagnostic counters, close/cancellation, and idle wakeup. Execution-time stale-session Apply/Confirm/Start rejection belongs to Task 2's worker tests, because this mailbox has no authoritative current session. Rerun the Step 2 command at green; all registered focused cases must pass.

- [x] **Step 6: Commit state control**

```powershell
git add src/application tests/unit/application src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(app): add camera state machine and command mailbox"
```

### Task 2: Acquisition worker with exclusive device ownership

**Files:**
- Create: `src/application/include/lumora/application/AcquisitionWorker.hpp`
- Create: `src/application/src/AcquisitionWorker.cpp`
- Create: `tests/unit/application/AcquisitionWorkerTests.cpp`
- Modify: `src/application/include/lumora/application/ApplicationState.hpp`
- Modify: `src/application/include/lumora/application/CameraSessionStateMachine.hpp`
- Modify: `src/application/src/CameraSessionStateMachine.cpp`
- Modify: `src/application/include/lumora/application/CameraCommandMailbox.hpp`
- Modify: `src/application/src/CameraCommandMailbox.cpp`
- Modify: `tests/unit/application/CameraSessionStateMachineTests.cpp`
- Modify: `tests/unit/application/CameraCommandMailboxTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ICameraProvider`, `CameraCommandMailbox`, raw `BufferPool`, `LatestValueSlot<RawFrame>`, `IClock`, and a latest `CameraStatusSnapshot` slot.
- Produces: start/join lifecycle, command execution, bounded 250 ms retrieval, raw publication, and categorized acquisition counters.

Task 2's consumer-driven interface clarifications are in the preflight: latest discovery/acquisition status, validated initial-state construction, priority-only mailbox checks/close observation, and explicit fresh-context replacement. Retry on a fresh Reconnecting worker opens the retained identity once to idle; the Task 5 owner retires an old context before forwarding an action that creates a replacement device. The first successful Apply fixes this worker's mode; composition selects that initial mode and matching pools.

- [x] **Step 1: Write failing ownership and stale-replacement tests**

```cpp
TEST(AcquisitionWorker, AllDeviceCallsOccurOnWorkerThread) {
    AcquisitionFixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    ASSERT_TRUE(fixture.connectApplyConfirmAndStart());
    ASSERT_TRUE(fixture.waitForAcquiredCount(3));
    fixture.worker.requestStop();
    fixture.worker.join();
    EXPECT_EQ(fixture.recorder.uniqueCallingThreadCount(), 1U);
    EXPECT_NE(fixture.recorder.onlyCallingThread(), std::this_thread::get_id());
}
```

`AcquisitionFixture` owns the recorder, provider, clock, raw pool, mailbox, raw/status slots, and worker in dependency order; its destructor requests stop and joins before dependencies die. Its provider creates a **closed** fake on the worker, and recording covers provider discovery/create plus device construction, every method, and destruction. `connectApplyConfirmAndStart()` posts the full explicit command sequence and awaits each successful status; `waitForAcquiredCount(n)` awaits the counter with a bounded condition-variable watchdog. Both return bool for fatal assertions; no sleep or hidden UI-thread device setup.

- [x] **Step 2: Register and run the failing worker suite**

Append the worker source and test to the existing application targets and register `Application.AcquisitionWorker`; reconfigure/build, then run:

```bash
ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure -R '^Application.AcquisitionWorker$'
```

Expected: the ownership, admission, or publication assertion fails before behavior is implemented.

- [x] **Step 3: Implement worker loop and command execution**

```cpp
class AcquisitionWorker final {
public:
    AcquisitionWorker(camera::ICameraProvider& provider,
                      CameraCommandMailbox& commands,
                      core::BufferPool& rawPool,
                      core::LatestValueSlot<core::RawFrame>& rawSlot,
                      core::IClock& clock,
                      core::LatestValueSlot<CameraStatusSnapshot>& statusSlot,
                      CameraStatusSnapshot initialStatus);
    core::Result<void> start();
    core::Result<void> post(CameraCommand command);
    void requestStop() noexcept;
    void join() noexcept;
};
```

The constructor borrows dependencies; it creates no device and performs no camera operation. `initialStatus` is the validated nonterminal, device-free control snapshot supplied by the session owner; the first fixture starts Disconnected, with no desired identity or confirmation. `start()` launches the worker once; `requestStop()` wakes idle waits and cancels retrieval, and `join()` is idempotent. Destroy the worker only after join. The session owner supplies a fresh worker/exchanges for a replacement device and preserves desired-connection intent as specified in the preflight; an ordinary same-device Stop/Start reuses them.

While idle use mailbox `waitPop(stopToken)`; while streaming use `tryPop()` with the bounded priority-first batch, then `retrieve(250ms, rawPool, stopToken)`. Recheck stop/priority before retrieval and before Start. Publish successful validated frames, release failures immediately, and never sleep while holding a device result or pool lock. Check cancellation before publication even if retrieval returned a frame. Catch worker exceptions into typed errors, clean up on the owning thread, and report failed cleanup honestly.

- [x] **Step 4: Handle pool exhaustion and replacement explicitly**

If the camera returns `ResourceExhaustion`, increment `droppedNoRawBuffer`. If raw publication replaces an unconsumed frame, increment `droppedBeforeProcessing`. Neither case changes camera state.

- [x] **Step 5: Test start/stop/disconnect/shutdown paths**

Use scripted simulator/fakes to cover commands while idle/streaming, timeout counts 1/2/3 and valid-frame reset, malformed frames, pool exhaustion, settings rejection, removal, manual Retry, Disconnect, full-mailbox priority, confirmation guards, and shutdown during retrieve. Assert acquisition stop/join during cancellable retrieval completes within 250 ms + 250 ms scheduling allowance; this is not a bound on arbitrary driver or filesystem calls. Rerun the focused suite at green and confirm every case is registered.

- [x] **Step 6: Commit acquisition worker**

```powershell
git add src/application tests/unit/application/AcquisitionWorkerTests.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(app): add isolated acquisition worker"
```

### Task 3: Frame processor port and processing worker

**Merged checkpoint (2026-09-07):** Implemented, reviewed and merged through PR #6 as `2031848`, after matching Linux/Windows Debug/Release CI passed at `ccabaae`. See the [Task 3 record](../../architecture/milestones/m05-processing-worker.md) for original local evidence, review fixes and exact-head CI. This task's evidence does not independently verify Tasks 4–5 or pass full M5 acceptance.

**Files:**
- Create: `src/processing/include/lumora/processing/IFrameProcessor.hpp`
- Create: `src/processing/include/lumora/processing/Mono8PassThroughProcessor.hpp`
- Create: `src/processing/src/Mono8PassThroughProcessor.cpp`
- Create: `src/application/include/lumora/application/ProcessingWorker.hpp`
- Create: `src/application/src/ProcessingWorker.cpp`
- Create: `tests/unit/application/ProcessingWorkerTests.cpp`
- Create: `tests/unit/processing/Mono8PassThroughProcessorTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: newest `RawFrame`, display pool, processor port, and bundle latest slot.
- Produces: `IFrameProcessor::process(shared_ptr<const RawFrame>) -> Result<shared_ptr<const FrameBundle>>`, processing worker lifecycle, and replacement/error counters.

- [x] **Step 1: Write failing slow-processor freshness test**

```cpp
TEST(ProcessingWorker, ProcessesNewestAvailableFrameAfterDelay) {
    ProcessingFixture fixture;
    ASSERT_TRUE(fixture.worker.start().hasValue());
    fixture.publishRaw(1);
    ASSERT_TRUE(fixture.processor.waitForCall(1));
    fixture.publishRaw(2);
    fixture.publishRaw(3);
    fixture.processor.releaseCall(1);
    ASSERT_TRUE(fixture.processor.waitForCall(2));
    EXPECT_EQ(fixture.processor.inputId(2), 3U);
    fixture.processor.releaseCall(2);
    ASSERT_TRUE(fixture.waitForBundleId(3));
}
```

`ProcessingFixture` owns pools/slots, a blocking `IFrameProcessor` test adapter, and worker. `publishRaw(id)` creates a valid pooled Mono8 raw frame and publishes it. The fake numbers process calls from 1, records each input ID on entry, and blocks until `releaseCall(n)`; `waitForCall(n)` and `waitForBundleId(id)` have bounded watchdogs. Its RAII cleanup releases all current/future fake waits before requesting stop/join, even on fatal assertion. Call 1 legitimately returns frame 1; do not require its output to be frame 3. Add a separate test that publishes 1/2/3 **before starting** the worker and asserts first input 3.

- [x] **Step 2: Register and run failing processing suites**

Create/register `lumora_processing`, `lumora_processing_tests`, `Processing.Mono8PassThrough`, and `Application.ProcessingWorker`; append the processing worker to application and add application -> processing linkage. Reconfigure/build the two test targets, then:

```bash
ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure -R '^(Processing.Mono8PassThrough|Application.ProcessingWorker)$'
```

Expected: pixel/ownership/freshness assertions fail against compiling stubs.

- [x] **Step 3: Implement minimal Mono8 processor**

The pass-through processor accepts the exact full-range Mono8 descriptor and copies active row bytes into a separate pooled Gray8 display buffer, using the [mapping/orientation/factory contract](../../architecture/milestones/m05-preflight.md#5-processor-and-deterministic-tests). Original and raw share the source ID, not mutable pixels. Enhanced members are null. Unsupported descriptors return `Processing / processing_format_not_available`, and display exhaustion retains `ResourceExhaustion`; Milestone 7 replaces this adapter in production composition. Test padded rows, descriptor mismatch, all 256 sample values, unchanged raw bytes, shared owner release, and exhausted pools.

- [x] **Step 4: Implement stop-aware processing loop**

`ProcessingWorker(rawSlot, bundleSlot, processor)` borrows dependencies and provides `start() -> Result<void>`, `requestStop() noexcept`, and `join() noexcept`. Wait for a raw revision newer than the last consumed revision, process that input, publish a successful bundle, and categorize processing error or replacement. A newer arrival cannot alter an in-flight input; the next consume selects the newest value. Check stop/closed before processing and stop again before publication; `LatestValueSlot::waitForNewer` may return a retained value after cancellation. A closed/empty slot terminates rather than spins. Never drain a historical queue or add a stop parameter to the M7 processor interface without a separate contract change.

- [x] **Step 5: Run freshness, error, and cancellation tests**

Cover replacement before first consume, an in-flight frame followed by newest input, bundle replacement, processor failure, stop while waiting, cancellation during a releasable in-flight call, closed-empty slot, and pool exhaustion. Rerun the focused suites at green. The last bundle ID must match the last explicitly consumed input, not a frame that arrived after that process call began.

- [x] **Step 6: Commit processing worker**

```powershell
git add src/processing src/application tests/unit/processing tests/unit/application src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(app): add newest-frame processing worker"
```

### Task 4: Minimal startup controls and saved preferences

**Merged checkpoint (2026-09-07):** Implemented and independently task-reviewed; the initial `5b4bff8` checkpoint passed native-inclusive Linux Debug/Release33/33 and tests-OFF builds. The remaining test-isolation Minor was resolved in `d113da9`, with scoped re-review clear. Task 4 merged with Task 5 through PR #7 as `f01b408`, with passing post-merge Linux/Windows Debug/Release CI. The [Task 4 record](../../architecture/milestones/m05-startup-preferences.md) preserves the review/fix history and pending native Windows/full-acceptance gates.

**Files:**
- Create: `src/application/include/lumora/application/StartupPreferences.hpp`
- Create: `src/application/src/StartupPreferences.cpp`
- Create: `src/configuration/include/lumora/configuration/StartupPreferencesService.hpp`
- Create: `src/configuration/src/StartupPreferencesService.cpp`
- Create: `src/ui/include/lumora/ui/CameraStartupPanel.hpp`
- Create: `src/ui/src/CameraStartupPanel.cpp`
- Create: `tests/unit/application/StartupPreferencesTests.cpp`
- Create: `tests/unit/configuration/StartupPreferencesTests.cpp`
- Create: `tests/unit/ui/CameraStartupPanelTests.cpp`
- Modify: `src/configuration/include/lumora/configuration/ApplicationConfiguration.hpp`
- Modify: `src/configuration/src/ConfigurationCodec.cpp`
- Modify: `tests/unit/configuration/ConfigurationStoreTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: stable identity, capabilities, requested/actual settings, application status, and `ConfigurationStore`.
- Produces: plain `StartupPreferences`, canonical capability comparison, schema-2 migration/codec, background `StartupPreferencesService`, and `CameraStartupPanel` intents. The [startup contract](../../architecture/milestones/m05-preflight.md#3-minimal-startup-ui-and-persistence) defines the fields, thread ownership, validation, and exact first/later-run sequences.

- [x] **Step 1: Write failing startup value, codec, and panel tests**

Use `StartupPreferencesTest` as a fixture over public value/codec/store interfaces, with an injected temporary path and matching/mismatched capability fixtures. Its `loadSchema1()` loads a valid old document through `ConfigurationStore`; its `roundTripConfirmed()` creates, encodes, and decodes a version-1 startup record using schema 2. These test helpers return the real `Result<ApplicationConfiguration>`; errors are asserted before reading values.

```cpp
TEST_F(StartupPreferencesTest, Schema1DoesNotInferConfirmation) {
    const auto loaded = loadSchema1();
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value().schemaVersion, 2);
    EXPECT_FALSE(loaded.value().startup.has_value());
}

TEST_F(StartupPreferencesTest, ConfirmedRecordRoundTrips) {
    const auto loaded = roundTripConfirmed();
    ASSERT_TRUE(loaded.hasValue());
    ASSERT_TRUE(loaded.value().startup.has_value());
    EXPECT_TRUE(loaded.value().startup->confirmed);
}
```

Add cases for set-order-independent capability equality, changed descriptor maximum/ROI/numeric limits/modes, wrong identity, duplicate/nonfinite capabilities, unconfirmed/future/corrupt record, save failure preserving the prior file, and background I/O thread affinity. Panel tests use `CameraStartupPanel` with immutable status: Start disabled before Confirm, requested versus actual values visible, priority actions enabled while an ordinary operation is pending, and no camera/device calls from UI signals.

- [x] **Step 2: Register and run the failing startup suites**

Extend existing application/configuration/UI targets and register `Application.StartupPreferences`, `Configuration.StartupPreferences`, and `CameraStartupPanel` (the latter with the existing minimal-plugin environment). Keep UI's Qt test main; configuration/application tests keep their current non-widget main. Reconfigure/build those targets, then:

```bash
ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure -R '^(Application.StartupPreferences|Configuration.StartupPreferences|CameraStartupPanel)$'
```

Expected: migration/eligibility/panel assertions fail, not missing registration.

- [x] **Step 3: Implement typed startup values and schema migration**

Add optional `startup` to `ApplicationConfiguration`, with this plain application-owned record in `StartupPreferences.hpp` (types qualified from existing camera/core headers):

```cpp
struct StartupPreferences final {
    std::uint32_t recordVersion{1};
    camera::CameraId cameraId;
    core::CameraIdentity identity;
    camera::CameraCapabilities confirmedCapabilities;
    camera::CameraConfiguration requested;
    camera::CameraConfiguration lastApplied;
    bool confirmed{false};
};
```

`StartupPreferences.cpp` supplies validation and canonical capability/configuration comparison used by codec and startup guards; tests cross these plain-value functions, not Qt JSON. `StartupPreferencesStatus` is the plain service-result value described in the preflight. Increment `CurrentSchemaVersion` to 2; decode schema 1 by preserving all existing sections and setting startup absent, then validate as schema 2. Serialize all startup identity/capability/configuration fields explicitly with version checks and structural validation. Canonicalize unordered capability sets without changing semantic numeric values. Do not use memory bytes or platform-dependent hashes for the fingerprint. Preserve the existing atomic-save/invalid-file behavior and tests.

- [x] **Step 4: Implement background preferences and minimal panel**

Implement `StartupPreferencesService` with one background worker, one pending coalesced immutable save and a latest status/result slot. It owns loaded-document read/modify/write and calls `ConfigurationStore` only off the UI/camera/processing threads; expose start/post-save/request-stop/join and never spawn a new thread per save. The panel emits selection/Connect/Apply/Confirm/Start/Stop/Disconnect/Retry/Resume intents; it does not schedule commands, persist, or control the presenter. Supply fixed-mode read-only configuration review now; M9 expands the editor. Keep all strings localization-ready and distinguish Resume Live (camera startup) from viewer Resume (unpause).

- [x] **Step 5: Verify startup contracts at green**

Rerun the Step 2 suites plus the existing `ConfigurationStore` tests. Verify no UI-thread file I/O, no lost unrelated JSON sections, typed failed-load/save warnings, worker join on exit, and no claim of persistence before the atomic save completes. End-to-end first/later-run actions are wired and tested in Task 5; this task's panel remains independently testable from immutable status.

- [x] **Step 6: Commit startup modules**

```powershell
git add src/application src/configuration src/ui tests/unit/application tests/unit/configuration tests/unit/ui/CameraStartupPanelTests.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(app): add explicit startup controls and saved preferences"
```

### Task 5: LivePipeline orchestration and Qt presentation integration

**Merged checkpoint (2026-09-07):** Implemented, reviewed and merged through PR #7 as `f01b408`; all recorded task/final review findings and the Windows compiler correction are resolved. The [integration record](../../architecture/milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) records local Debug/Release35/35 and passing post-merge Linux/Windows CI. Native Windows 11 checks and separate milestone acceptance remain pending.

**Files:**
- Create: `src/application/include/lumora/application/LivePipeline.hpp`
- Create: `src/application/src/LivePipeline.cpp`
- Create: `src/ui/include/lumora/ui/WorkstationController.hpp`
- Create: `src/ui/src/WorkstationController.cpp`
- Create: `tests/integration/LivePipelineTests.cpp`
- Create: `tests/unit/ui/WorkstationControllerTests.cpp`
- Create: `src/app/SimulatorComposition.hpp`
- Create: `src/app/SimulatorComposition.cpp`
- Modify: `src/app/main.cpp`
- Modify: `src/ui/include/lumora/ui/MainWindow.hpp`
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `src/ui/src/CameraStartupPanel.cpp` (authorized integration guard/selection-stability corrections)
- Modify: `tests/unit/ui/CameraStartupPanelTests.cpp` (preserve the corrected intent fixture)
- Modify: `src/configuration/include/lumora/configuration/StartupPreferencesService.hpp` (review-authorized pending-load admission contract)
- Modify: `src/configuration/src/StartupPreferencesService.cpp` (retain accepted confirmation through delayed load and shutdown)
- Modify: `tests/unit/configuration/StartupPreferencesTests.cpp` (safe delayed-load persistence regressions)
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: camera provider, clock, session-owned pools/slots/workers, background preferences, `CameraStartupPanel` intents and the existing `WorkstationView`/`FramePresenter`.
- Produces: `LivePipeline::{start,post,shutdown}` for application lifecycle/commands, bounded session-context/status exchange and UI binding acknowledgement; `WorkstationController` for camera/startup state translation; simulator-backed application. Viewer Pause/Resume remain presenter operations, not Qt-dependent methods on `LivePipeline`.

Integration checks authorized narrowly scoped corrections to the existing panel: actions match the worker's supported states/retained identity, Disconnect stays available during pending startup/discovery, and unchanged descriptor lists do not reset an open selector on each controller poll. These enforce the [startup contract](../../architecture/milestones/m05-preflight.md#3-minimal-startup-ui-and-persistence); they do not introduce a second camera policy or M9 editor.

Task review additionally authorized bounded service acceptance while initial load is pending: confirmed records must survive Disconnect/shutdown without blocking imaging, and must never overwrite an unsafe source. Writes still occur only after safe whole-document load; acceptance is not durability. The preflight records this narrow service-policy amendment and its migration-free cost.

- [x] **Step 1: Write failing end-to-end lifecycle test**

```cpp
TEST(LivePipeline, PauseKeepsAcquiringAndResumeJumpsToNewest) {
    LivePipelineFixture fixture;
    ASSERT_TRUE(fixture.startWithExplicitConfirmation());
    ASSERT_TRUE(fixture.waitForCompletedPaint());
    const auto shown = fixture.presenter().presentedBundle();
    ASSERT_NE(shown, nullptr);
    const auto acquiredBefore = fixture.acquiredCount();
    fixture.presenter().pause();
    ASSERT_TRUE(fixture.acquireAndProcessMoreFrames(10));
    EXPECT_GT(fixture.acquiredCount(), acquiredBefore);
    EXPECT_EQ(fixture.presenter().presentedBundle(), shown);
    const auto newest = fixture.latestBundle();
    ASSERT_NE(newest, nullptr);
    fixture.presenter().resume();
    ASSERT_TRUE(fixture.waitForCompletedPaintOf(newest->sourceFrameId()));
}
```

`LivePipelineFixture` owns the production pipeline/controller/view/presenter and an injected deterministic simulator clock. Helpers drive discovery/Apply/Confirm/Start through real commands, count actual successful retrievals, and await real completed widget paints. `acquireAndProcessMoreFrames(n)` advances manual pacing until n further bundles are produced, then holds further source ticks; `latestBundle()` returns that last bundle, so the Resume assertion has a stable target without requiring an extra publication. Accessors cross the public application/controller interfaces. Cleanup shuts down and clears all retained owners; local `shown`/`newest` handles must be released before a zero-lease assertion.

- [x] **Step 2: Register and run the failing orchestration suites**

Append `LivePipelineTests.cpp` to the **existing** `lumora_integration_tests`, add the pipeline/controller/composition sources and required linkage, and register `LivePipeline` plus `WorkstationController`. Reconfigure/build integration/UI targets, list the matching entries, then:

```bash
ctest --preset linux-gcc-debug-sim -N -R '^(LivePipeline|WorkstationController)$'
ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure -R '^(LivePipeline|WorkstationController)$'
```

Expected: new lifecycle/startup/freshness assertions fail before orchestration exists. An unchanged M4 suite passing is not this red check.

- [x] **Step 3: Implement ownership and shutdown order**

Implement the [ownership and handoff sequence](../../architecture/milestones/m05-preflight.md#4-ownership-source-handoff-and-shutdown). `LivePipeline` owns session pools/slots and workers; provider/clock outlive it. Connect can open idle to read capabilities, but retrieval cannot start before checked pools, processing worker, and acknowledged presenter binding exist. Stop/join camera before processing; clear/destroy presenter while the view lives, clear viewport QImages, release all slot/context/test owners, and only then assert zero leases through retained pool handles. Closing a slot or stopping a timer alone is insufficient. The controller owns the presenter, not a worker thread or device.

- [x] **Step 4: Wire simulator composition**

`main.cpp` creates clock, one provider from `SimulatorComposition`, pipeline, background preferences service, main window and controller in dependency order. The pipeline, not main, creates 10 raw / 9 U16 processing / 16 Gray8 display buffers after checked mode sizing. Explicitly select full-range Mono8, MovingBar, 640 x 480, 30 FPS, seed `0x4C554D4F`, Continuous and RealTime pacing; do not inherit Fastest or link the non-shipping harness. Add the typed `MainWindow::workstationView()` accessor and host `CameraStartupPanel` in its sidebar. Wire startup per Task 4: no initial stream, Apply/readback/Confirm guard, background save, safe matching-record Resume, drift/readback mismatch cancellation, and manual Disconnect suppression. Keep camera status separate from presenter freshness.

Carry forward the [M4 source-session and presentation contracts](../../architecture/milestones/m04-preflight.md#resolved-implementation-contracts). When replacing a device/session, quiesce the old publisher, supply a fresh bundle slot owned for the new session, and call `FramePresenter::resetSource` before accepting its frames. Do not compare the new device's IDs against the previous session or reuse a slot containing old-session values. Ordinary stop/start of the same device retains its ID sequence. Freshness advances on completed presentation of a fresh frame, not on acquisition or processing activity.

- [x] **Step 5: Exercise 100 lifecycle cycles**

Run the integration test with first-run confirmation, later-run Resume Live, changed-capability and changed-readback review, failed load/save, Disconnect during startup/Resume, repeated connect/start/pause/resume/stop/disconnect, and application shutdown from each state. Verify exact-ID matching with no camera substitution and no silently started stream. Stall camera retrieval, processing publication, and UI presentation independently; each must produce `STALE IMAGE / NOT LIVE` within the specified M4 deadline while retaining the last contextual frame. Exercise 100 bounded lifecycle cycles, replacement-session handoffs, same-device Stop/Start ID continuity, rejected M5 resolution changes, new-session reset, and shutdown while paused/processing. Zero pool use is asserted only after final owner release, not while preserving a contextual image. Record thread joins and capacity-one exchanges. Use a 180-second outer watchdog for this suite; do not sleep through 100 real-time cycles.

Rerun the focused suites at green and full Debug/Release simulator suites. Separately configure/build `lumora_app` with `LUMORA_BUILD_TESTS=OFF` and `LUMORA_ENABLE_BASLER=OFF` in a fresh build directory; prove production has no harness dependency. Require matching Windows Debug/Release CI and native UI checks affected by the controls before acceptance; no dispatch/push is implied by this document.

- [x] **Step 6: Commit live integration**

```powershell
git add src/application src/configuration src/ui src/app tests/integration/LivePipelineTests.cpp tests/unit/configuration/StartupPreferencesTests.cpp tests/unit/ui/CameraStartupPanelTests.cpp tests/unit/ui/WorkstationControllerTests.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(app): connect independent live pipeline"
```

## Milestone 5 acceptance gate

Tasks 1–5 are implemented, reviewed and merged through `f01b408`; [Task 5's checkpoint](../../architecture/milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) records source, resolved review/compiler findings, final PR-head checks and passing post-merge Linux/Windows Debug/Release CI. Completed task steps or automated CI do not pass the milestone gate below: native Windows 11 evidence and separately recorded acceptance remain required. The acceptance checkboxes are intentionally unchanged by this progress synchronization.

- [ ] Camera/device methods run on exactly one non-UI thread.
- [ ] Slow processing/display causes categorized replacement, never queue growth.
- [ ] Pause continues acquisition and Resume jumps to newest.
- [ ] No startup path streams silently, and Manual Disconnect prevents reconnect until an explicit Connect.
- [ ] Camera, processing, and presentation stalls all produce and then clear the mandatory stale indication correctly.
- [ ] Shutdown succeeds from every camera/viewer state and returns all pool leases.
- [ ] Simulator live view remains responsive during artificial 100 ms processing delay.
- [ ] Full-mailbox Stop/Disconnect/Shutdown, stale-session rejection, and lifecycle-barrier coalescing pass focused concurrency tests.
- [ ] First-run and saved Resume workflows, schema-1 migration, capability/readback drift, and failed load/save pass without UI-thread persistence or silent Start.
- [ ] Timeout 1/2/3, removal, malformed-frame/exhaustion/cancellation classification, interim manual Retry, and manual Disconnect behavior match the M5/M12 split.
- [ ] New tests are explicitly registered; complete Linux/GCC and matching Windows/MSVC Debug/Release simulator suites pass at the recorded source, and tests-OFF/Basler-OFF application composition builds without the M4 harness.
- [ ] M4 acceptance, including its deferred native Windows 11 checks, is closed; affected M5 native UI checks and traceability are recorded, and M5 acceptance is written separately from implementation commits. The scoped exception permits development only.
