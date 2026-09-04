# Milestone 5 Independent Live Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect camera acquisition, processing, and Qt presentation through independently owned workers and capacity-one latest-frame exchanges.

**Architecture:** One camera thread owns the camera session, one processing thread consumes newest raw frames, and the UI polls newest bundles. Commands and state are bounded, stop-aware, and testable without Qt event traffic per frame.

**Tech Stack:** C++20 `std::jthread`, Qt 6 presentation adapter, camera API/simulator, core bounded primitives, GoogleTest/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

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

---

### Task 1: Camera session state machine and command mailbox

**Files:**
- Create: `src/application/include/lumora/application/CameraSessionStateMachine.hpp`
- Create: `src/application/include/lumora/application/CameraCommandMailbox.hpp`
- Create: `src/application/include/lumora/application/ApplicationState.hpp`
- Create: `src/application/src/CameraSessionStateMachine.cpp`
- Create: `src/application/src/CameraCommandMailbox.cpp`
- Create: `tests/unit/application/CameraSessionStateMachineTests.cpp`
- Create: `tests/unit/application/CameraCommandMailboxTests.cpp`

**Interfaces:**
- Consumes: `CameraId`, `CameraConfiguration`, `Error`, and the generic bounded queue.
- Produces: `CameraSessionState`, `CameraSessionEvent`, `CameraCommand`, `CameraStatusSnapshot`, and a 32-command mailbox with stop/disconnect priority and configuration coalescing.

- [ ] **Step 1: Write the failing transition-table tests**

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

- [ ] **Step 2: Verify state contracts are missing**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_application_tests`

Expected: FAIL.

- [ ] **Step 3: Implement the explicit state/event table**

```cpp
enum class CameraSessionState {
    Disconnected, Discovering, Connecting, ConnectedIdle,
    Streaming, Reconnecting, Error, ShuttingDown
};

using CameraCommand = std::variant<Discover, Connect, Disconnect,
    ApplyConfiguration, StartStream, StopStream, Retry, Shutdown>;
```

Reject invalid transitions without mutation. Manual Disconnect transitions to Disconnected, clears desired connection, and disables automatic reconnect until a new explicit Connect. UnexpectedFailure records the error and transitions to Reconnecting; retry timing is implemented in Milestone 12.

Add startup intent separate from camera state: first run discovers but requires explicit selection, configuration confirmation, and Start. A later run may match the stored stable identity and unchanged capabilities and expose one-click `Resume Live`; it must not start acquisition without operator action. Identity or capability drift returns to review-required state.

- [ ] **Step 4: Implement mailbox priority/coalescing**

Capacity is 32. Shutdown supersedes all pending work; Disconnect supersedes Connect/Start/Apply; a new ApplyConfiguration replaces an older pending ApplyConfiguration. `post` returns false if an unrelated full mailbox cannot accept a command.

- [ ] **Step 5: Exhaustively test state/event pairs and mailbox concurrency**

Generate every enum pair, assert either the documented next state or an unchanged typed error, and run concurrent producers while verifying capacity, coalescing, and Shutdown wakeup.

- [ ] **Step 6: Commit state control**

```powershell
git add src/application tests/unit/application
git commit -m "feat(app): add camera state machine and command mailbox"
```

### Task 2: Acquisition worker with exclusive device ownership

**Files:**
- Create: `src/application/include/lumora/application/AcquisitionWorker.hpp`
- Create: `src/application/src/AcquisitionWorker.cpp`
- Create: `tests/unit/application/AcquisitionWorkerTests.cpp`

**Interfaces:**
- Consumes: `ICameraProvider`, `CameraCommandMailbox`, raw `BufferPool`, `LatestValueSlot<RawFrame>`, `IClock`, and a latest `CameraStatusSnapshot` slot.
- Produces: start/join lifecycle, command execution, bounded 250 ms retrieval, raw publication, and categorized acquisition counters.

- [ ] **Step 1: Write failing ownership and stale-replacement tests**

```cpp
TEST(AcquisitionWorker, AllDeviceCallsOccurOnWorkerThread) {
    ThreadRecordingCamera fake;
    AcquisitionWorker worker(fakeProvider(fake), pools(), clock());
    worker.start();
    postConnectAndStart(worker);
    waitForFrames(3);
    worker.stopAndJoin();
    EXPECT_EQ(fake.uniqueCallingThreadCount(), 1U);
    EXPECT_NE(fake.onlyCallingThread(), std::this_thread::get_id());
}
```

- [ ] **Step 2: Verify worker test fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_application_tests`

Expected: FAIL.

- [ ] **Step 3: Implement worker loop and command execution**

```cpp
class AcquisitionWorker final {
public:
    Result<void> start();
    bool post(CameraCommand command);
    void requestStop() noexcept;
    void join() noexcept;
    LatestValueSlot<RawFrame>& rawFrames() noexcept;
};
```

When streaming, process priority commands before calling `retrieve(250ms, rawPool)`. Publish successful frames, release failures immediately, and never sleep while holding a device result or pool lock.

- [ ] **Step 4: Handle pool exhaustion and replacement explicitly**

If the camera returns `ResourceExhaustion`, increment `droppedNoRawBuffer`. If raw publication replaces an unconsumed frame, increment `droppedBeforeProcessing`. Neither case changes camera state.

- [ ] **Step 5: Test start/stop/disconnect/shutdown paths**

Use scripted simulator/fakes to cover commands while idle/streaming, timeout, malformed frame, explicit disconnect, mailbox full, and shutdown during retrieve. Assert stop and join complete within the retrieval timeout plus 250 ms scheduling allowance.

- [ ] **Step 6: Commit acquisition worker**

```powershell
git add src/application tests/unit/application/AcquisitionWorkerTests.cpp
git commit -m "feat(app): add isolated acquisition worker"
```

### Task 3: Frame processor port and processing worker

**Files:**
- Create: `src/processing/include/lumora/processing/IFrameProcessor.hpp`
- Create: `src/processing/include/lumora/processing/Mono8PassThroughProcessor.hpp`
- Create: `src/processing/src/Mono8PassThroughProcessor.cpp`
- Create: `src/application/include/lumora/application/ProcessingWorker.hpp`
- Create: `src/application/src/ProcessingWorker.cpp`
- Create: `tests/unit/application/ProcessingWorkerTests.cpp`
- Create: `tests/unit/processing/Mono8PassThroughProcessorTests.cpp`

**Interfaces:**
- Consumes: newest `RawFrame`, display pool, processor port, and bundle latest slot.
- Produces: `IFrameProcessor::process(shared_ptr<const RawFrame>) -> Result<shared_ptr<const FrameBundle>>`, processing worker lifecycle, and replacement/error counters.

- [ ] **Step 1: Write failing slow-processor freshness test**

```cpp
TEST(ProcessingWorker, ProcessesNewestAvailableFrameAfterDelay) {
    BlockingFrameProcessor processor;
    ProcessingWorker worker(rawSlot, bundleSlot, processor);
    worker.start();
    publishRawFrames(rawSlot, {1, 2, 3});
    processor.releaseOne();
    EXPECT_EQ(waitForBundle(bundleSlot)->sourceFrameId(), 3U);
    worker.stopAndJoin();
}
```

- [ ] **Step 2: Verify missing worker/port fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_processing_tests lumora_application_tests`

Expected: FAIL.

- [ ] **Step 3: Implement minimal Mono8 processor**

The pass-through processor accepts only a `SourcePixelFormat` descriptor representing Mono8, copies rows into a pooled `Gray8` display buffer, and creates a bundle whose Original display shares the source frame ID. Unsupported descriptors return `processing_format_not_available`; Milestone 7 replaces this class in production composition.

- [ ] **Step 4: Implement stop-aware processing loop**

Wait for a raw revision newer than the last consumed revision, process exactly that newest value, publish a successful bundle, and categorize processing error or processed replacement. Never drain a historical queue.

- [ ] **Step 5: Run freshness, error, and cancellation tests**

Cover raw replacement before wake, deliberate slow processing, bundle replacement, processor failure, stop while waiting, and pool exhaustion. Assert final processed ID equals the newest input available after each release.

- [ ] **Step 6: Commit processing worker**

```powershell
git add src/processing src/application tests/unit/processing tests/unit/application
git commit -m "feat(app): add newest-frame processing worker"
```

### Task 4: LivePipeline orchestration and Qt presentation integration

**Files:**
- Create: `src/application/include/lumora/application/LivePipeline.hpp`
- Create: `src/application/src/LivePipeline.cpp`
- Create: `src/ui/include/lumora/ui/WorkstationController.hpp`
- Create: `src/ui/src/WorkstationController.cpp`
- Create: `tests/integration/LivePipelineTests.cpp`
- Modify: `src/app/main.cpp`
- Modify: `src/ui/src/MainWindow.cpp`

**Interfaces:**
- Consumes: camera provider, worker dependencies, presentation slot, and `WorkstationView` intents.
- Produces: `LivePipeline::start`, `post`, `pauseViewer`, `resumeViewer`, `shutdown`; `WorkstationController` state translation; simulator-backed production application.

- [ ] **Step 1: Write failing end-to-end lifecycle test**

```cpp
TEST(LivePipeline, PauseKeepsAcquiringAndResumeJumpsToNewest) {
    auto pipeline = makeSimulatedLivePipeline(30.0);
    ASSERT_TRUE(pipeline.start().hasValue());
    connectAndStream(pipeline);
    const auto shown = waitForPresentedFrame(pipeline);
    pipeline.pauseViewer();
    advanceFrames(10);
    EXPECT_GT(pipeline.metrics().acquiredFrames, shown);
    EXPECT_EQ(pipeline.presentedBundle()->sourceFrameId(), shown);
    pipeline.resumeViewer();
    EXPECT_GT(waitForPresentedFrame(pipeline), shown + 5);
}
```

- [ ] **Step 2: Verify orchestration types are missing**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_integration_tests`

Expected: FAIL.

- [ ] **Step 3: Implement ownership and shutdown order**

`LivePipeline` owns pools/slots before workers, starts processing before acquisition, and shuts down acquisition before processing. The controller owns no worker thread and never calls a device directly.

- [ ] **Step 4: Wire simulator composition**

`main.cpp` creates one `SimulatedCameraProvider`, ten raw buffers, required processing/display pools, workers, controller, and main window. Connect/Start/Stop/Disconnect actions post commands. FramePresenter consumes only the bundle slot.

- [ ] **Step 5: Exercise 100 lifecycle cycles**

Run the integration test with first-run confirmation, later-run Resume Live, changed-capability review, repeated connect/start/pause/resume/stop/disconnect, and application shutdown from each state. Stall camera retrieval, processing publication, and UI presentation independently; each must produce `STALE IMAGE / NOT LIVE` within the specified deadline without removing the last contextual frame. Verify thread joins, pool in-use counts return to zero, and no latest slot exceeds capacity one.

- [ ] **Step 6: Commit live integration**

```powershell
git add src/application src/ui src/app/main.cpp tests/integration/LivePipelineTests.cpp
git commit -m "feat(app): connect independent live pipeline"
```

## Milestone 5 acceptance gate

- [ ] Camera/device methods run on exactly one non-UI thread.
- [ ] Slow processing/display causes categorized replacement, never queue growth.
- [ ] Pause continues acquisition and Resume jumps to newest.
- [ ] No startup path streams silently, and Manual Disconnect prevents reconnect until an explicit Connect.
- [ ] Camera, processing, and presentation stalls all produce and then clear the mandatory stale indication correctly.
- [ ] Shutdown succeeds from every camera/viewer state and returns all pool leases.
- [ ] Simulator live view remains responsive during artificial 100 ms processing delay.
