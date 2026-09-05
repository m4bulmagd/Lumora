# Milestone 3 Camera API and Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a vendor-neutral continuous free-running camera contract and deterministic software camera capable of generated patterns, recorded sequence replay, pacing, and scripted faults.

**Architecture:** Camera interfaces depend only on core domain types. The simulator obeys the same capability, configuration, buffer, and lifecycle rules as a hardware adapter.

**Tech Stack:** C++20 standard library, CMake, GoogleTest/CTest; no Qt or pylon in camera API/simulator.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

**Execution checkpoint (2026-09-05):** Implementation is merged; Linux verification and implementation clarifications are recorded in the [M3 verification record](../../architecture/milestones/m03-camera-api-simulator.md). Windows/MSVC verification is pending, so the milestone is not yet accepted. The record contains observed evidence; the task instructions below remain the original execution checklist.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- UI and application code must not see pylon or GenApi types.
- Every retrieve operation accepts a bounded timeout and writes into the application buffer pool.
- Simulator behavior must be deterministic under `ManualClock`.
- Camera configuration is capability-validated and returns actual applied values.
- Normal tests require neither Ethernet nor physical hardware.

---

### Task 1: Camera descriptors, capabilities, and configuration validation

**Files:**
- Create: `src/camera/api/include/lumora/camera/CameraTypes.hpp`
- Create: `src/camera/api/include/lumora/camera/CameraConfigurationValidator.hpp`
- Create: `src/camera/api/src/CameraConfigurationValidator.cpp`
- Create: `tests/unit/camera/CameraConfigurationValidatorTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SourcePixelFormat`, `RegionOfInterest`, `CameraIdentity`, `Result`, and `Error` from `lumora_core`.
- Produces: `CameraId`, `CameraDescriptor`, `NumericCapability`, `CameraCapabilities`, `CameraConfiguration`, `AppliedCameraConfiguration`, and `validateCameraConfiguration`.

- [ ] **Step 1: Write failing boundary/increment tests**

```cpp
TEST(CameraConfigurationValidator, RejectsRoiThatMissesCameraIncrement) {
    auto capabilities = mono12Capabilities(/* widthIncrement = */ 8);
    auto requested = validConfiguration();
    requested.roi.width = 1025;
    auto result = validateCameraConfiguration(requested, capabilities);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "roi_width_increment");
}
```

- [ ] **Step 2: Verify missing contracts fail**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_camera_api_tests`

Expected: FAIL on missing camera types.

- [ ] **Step 3: Implement strongly typed camera values**

```cpp
struct CameraId { std::string value; auto operator<=>(const CameraId&) const = default; };
struct NumericCapability { double minimum; double maximum; double increment; bool writableWhileStreaming; };
struct CameraConfiguration {
    SourcePixelFormat pixelFormat;
    RegionOfInterest roi;
    std::optional<double> requestedFps;
    ExposureConfiguration exposure;
    GainConfiguration gain;
};
```

Validation checks the complete capability-derived format descriptor, ROI containment/increments, positive FPS, numeric ranges/increments, and rejects all trigger configuration in this release. Continuous free-running acquisition is the only initial mode. It returns all detected violations in deterministic field order so the UI can explain the first and diagnostics can retain the full list.

- [ ] **Step 4: Test actual-value quantization separately from validation**

Requested values inside declared ranges pass validation. The device adapter may return quantized `AppliedCameraConfiguration`; the validator must not silently mutate the request.

- [ ] **Step 5: Run and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R CameraConfiguration`

```powershell
git add src/camera/api tests/unit/camera src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(camera): define vendor-neutral configuration model"
```

### Task 2: Camera provider and device interfaces

**Files:**
- Create: `src/camera/api/include/lumora/camera/ICameraProvider.hpp`
- Create: `src/camera/api/include/lumora/camera/ICameraDevice.hpp`
- Create: `tests/unit/camera/CameraContractTests.cpp`

**Interfaces:**
- Consumes: camera values, `BufferPool`, immutable `RawFrame`, stop tokens, and typed results.
- Produces: pure abstract `ICameraProvider` and `ICameraDevice` contracts.

- [ ] **Step 1: Write a compile-time fake implementing the intended contract**

```cpp
class ContractCamera final : public ICameraDevice {
public:
    Result<void> open() override;
    Result<CameraCapabilities> capabilities() override;
    Result<AppliedCameraConfiguration> applyConfiguration(const CameraConfiguration&) override;
    Result<void> startStream() override;
    Result<std::shared_ptr<const RawFrame>> retrieve(
        std::chrono::milliseconds timeout, BufferPool& destination) override;
    Result<void> stopStream() noexcept override;
    Result<void> close() noexcept override;
};
```

- [ ] **Step 2: Verify compilation fails before interface headers exist**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_camera_api_tests`

Expected: FAIL.

- [ ] **Step 3: Define exact provider/device contracts**

```cpp
class ICameraProvider {
public:
    virtual ~ICameraProvider() = default;
    virtual Result<std::vector<CameraDescriptor>> discover(std::stop_token) = 0;
    virtual Result<std::unique_ptr<ICameraDevice>> create(const CameraId&) = 0;
};
```

Document that a device is thread-confined, starts closed, and all lifecycle operations are idempotent. `retrieve` returns `Cancelled`, `AcquisitionTimeout`, `InvalidFrame`, or an owned frame; it never returns a view into adapter memory.

- [ ] **Step 4: Run contract tests**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R CameraContract`

Expected: PASS and API target has no pylon/Qt link dependency.

- [ ] **Step 5: Commit camera ports**

```powershell
git add src/camera/api tests/unit/camera/CameraContractTests.cpp
git commit -m "feat(camera): add provider and device ports"
```

### Task 3: Generated-pattern simulated camera

**Files:**
- Create: `src/camera/simulator/include/lumora/camera/sim/SimulatedCameraProvider.hpp`
- Create: `src/camera/simulator/include/lumora/camera/sim/SimulatedCameraOptions.hpp`
- Create: `src/camera/simulator/include/lumora/camera/sim/IPatternGenerator.hpp`
- Create: `src/camera/simulator/src/SimulatedCameraProvider.cpp`
- Create: `src/camera/simulator/src/SimulatedCameraDevice.cpp`
- Create: `src/camera/simulator/src/PatternGenerators.cpp`
- Create: `tests/unit/camera/SimulatedCameraTests.cpp`

**Interfaces:**
- Consumes: camera contracts, `BufferPool`, and `IClock`.
- Produces: simulator provider/device, `SimulationPattern`, and configurable generated-frame behavior.

- [ ] **Step 1: Write a failing deterministic-frame test**

```cpp
TEST(SimulatedCamera, RampFramesAreRepeatable) {
    ManualClock clock;
    auto device = makeRampCamera(clock, 8, 4, 12, 30.0);
    ASSERT_TRUE(device->open().hasValue());
    ASSERT_TRUE(device->startStream().hasValue());
    auto first = device->retrieve(100ms, pool()).value();
    EXPECT_EQ(readU16(*first, 7, 0), expectedRampValue(7, 12));
    EXPECT_EQ(first->frameId, 1U);
}
```

- [ ] **Step 2: Verify the test fails for missing simulator**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_camera_simulator_tests`

Expected: FAIL.

- [ ] **Step 3: Implement capabilities and lifecycle**

The provider exposes one stable device per configured simulator. The device requires Open before capability access, validates configuration, fills a writable raw lease, seals it, and increments frame ID only after successful publication.

```cpp
enum class SimulationPattern { Ramp, Gradient, Checkerboard, ImpulseNoise, MovingBar };
struct SimulatedCameraOptions {
    CameraId id;
    CameraCapabilities capabilities;
    SimulationPattern pattern;
    double defaultFps;
    std::uint64_t seed;
};
```

- [ ] **Step 4: Implement manual and real-time pacing**

Manual-clock mode waits on the injected clock/test signal; production simulator mode uses steady deadlines and stop-aware waits. It does not accumulate missed sleeps: after delay it schedules from the current time and increments a pacing-slip metric hook.

- [ ] **Step 5: Add lifecycle/configuration tests**

Cover discover, wrong ID, repeated open/close, repeated start/stop, retrieve while stopped, pool exhaustion, FPS quantization, ROI, Mono8, Mono12-in-U16, and cancellation.

- [ ] **Step 6: Run and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R SimulatedCamera`

```powershell
git add src/camera/simulator tests/unit/camera/SimulatedCameraTests.cpp
git commit -m "feat(camera): add deterministic generated simulator"
```

### Task 4: Recorded PGM sequence replay and fault scripts

**Files:**
- Create: `src/camera/simulator/include/lumora/camera/sim/SequenceSource.hpp`
- Create: `src/camera/simulator/include/lumora/camera/sim/FaultScript.hpp`
- Create: `src/camera/simulator/src/SequenceSource.cpp`
- Create: `src/camera/simulator/src/FaultScript.cpp`
- Create: `tests/fixtures/sequences/ramp16/frame0001.pgm`
- Create: `tests/fixtures/sequences/ramp16/frame0002.pgm`
- Create: `tests/unit/camera/SequenceSourceTests.cpp`
- Create: `tests/unit/camera/FaultScriptTests.cpp`

**Interfaces:**
- Consumes: simulator device, standard PGM P5 files, and `ManualClock`.
- Produces: `SequenceSource::openDirectory`, sequence end policy, and frame/time-indexed `FaultScript` events.

- [ ] **Step 1: Write failing sequence-order and endian tests**

```cpp
TEST(SequenceSource, ReadsSixteenBitPgmInLexicalOrder) {
    auto source = SequenceSource::openDirectory(fixture("sequences/ramp16"), SequenceEnd::Loop).value();
    EXPECT_EQ(source.next().value().firstPixel, 0x0123);
    EXPECT_EQ(source.next().value().firstPixel, 0x0456);
    EXPECT_EQ(source.next().value().firstPixel, 0x0123);
}
```

- [ ] **Step 2: Verify missing sequence source fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_camera_simulator_tests`

Expected: FAIL.

- [ ] **Step 3: Implement strict PGM parsing**

Support binary P5, comments, every valid maximum sample value from 1 through 65535 (including 255, 1023, 4095, and 65535), big-endian samples when the maximum exceeds 255, exact payload length, uniform sequence geometry, and lexical filename ordering. Preserve numeric samples, record the header maximum as `SourcePixelFormat::sampleMaximum`, derive valid bits/storage, and let normalization use that declared maximum. Reject zero/out-of-range maximum values, samples above the declared maximum, trailing or short payloads, mixed geometry, empty directories, and non-files without crashing.

- [ ] **Step 4: Implement explicit fault events**

```cpp
enum class SimulatedFault { Timeout, Disconnect, MalformedFrame, ConfigurationFailure };
struct FaultEvent { std::uint64_t atFrame; SimulatedFault fault; std::uint32_t repeatCount; };
```

Fault scripts are sorted and validated at construction. A Disconnect persists until the test calls `restoreConnection`; malformed-frame faults construct a deliberately invalid candidate that the normal validation boundary rejects.

- [ ] **Step 5: Run unit and integration replay tests**

Verify Loop, Stop, and Error end policies; configurable FPS; repeatable fault locations; recovery; and cancellation while waiting for the next frame.

- [ ] **Step 6: Commit sequence simulation**

```powershell
git add src/camera/simulator tests/fixtures/sequences tests/unit/camera
git commit -m "feat(camera): add sequence replay and scripted faults"
```

## Milestone 3 acceptance gate

- [ ] Simulator-only configuration links no pylon target.
- [ ] Generated and PGM-replayed frames are byte-for-byte deterministic.
- [ ] PGM fixtures cover 255, 1023, 4095, 65535, and a non-power-of-two valid maximum.
- [ ] Capability validation is shared by generated and sequence devices.
- [ ] All lifecycle calls are idempotent and timeout/cancellation paths are tested.
- [ ] Fault scripts reproduce timeout, malformed-frame, disconnect, and configuration failures at exact frame IDs.
