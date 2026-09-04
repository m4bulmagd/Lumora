# Milestone 6 Basler pylon Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement optional Basler discovery, capability mapping, safe configuration, acquisition, metadata, and device-removal handling behind the existing camera contracts.

**Architecture:** `lumora_camera_basler` is the only target that includes pylon/GenApi headers. One RAII runtime outlives all providers/devices, and the application camera worker remains the exclusive normal caller.

**Tech Stack:** C++20, Basler pylon C++ SDK, CMake, GoogleTest/CTest, existing camera/core/application modules.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Hard entry gate:** Do not begin this milestone until an approved hardware profile records the exact Basler model/sensor/firmware, production NIC/driver/link, capability-reported pixel formats, and intended continuous free-running ROI/FPS/exposure/gain mode. If hardware is unavailable, continue simulator milestones but leave M6 unaccepted.

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- `LUMORA_ENABLE_BASLER=OFF` must remain completely pylon-free.
- A pylon grab result never crosses the Basler target boundary.
- Retrieve timeout is bounded at 250 ms or less in the worker loop.
- Device-removal callbacks only signal; teardown occurs on the camera worker.
- Only selected safe camera settings are mapped; arbitrary GenApi nodes are not exposed.
- Hardware tests use the `hardware` CTest label.

---

### Task 1: Optional pylon discovery and runtime RAII

**Files:**
- Modify: `cmake/Pylon.cmake`
- Modify: `src/CMakeLists.txt`
- Create: `src/camera/basler/include/lumora/camera/basler/PylonRuntime.hpp`
- Create: `src/camera/basler/src/PylonRuntime.cpp`
- Create: `tests/unit/camera/basler/PylonRuntimeTests.cpp`

**Interfaces:**
- Consumes: pylon SDK installation only when enabled.
- Produces: imported pylon target, `PylonRuntime::create() -> Result<shared_ptr<PylonRuntime>>`, and deterministic `PylonInitialize`/`PylonTerminate` ownership.

- [ ] **Step 1: Add configure tests for both feature states**

Configure simulator with an intentionally invalid `PYLON_ROOT` and expect success. Configure Basler with the same invalid root and expect a clear configure failure naming the required SDK/runtime.

- [ ] **Step 2: Write the failing runtime lifecycle test**

```cpp
TEST(PylonRuntime, SharedRuntimeTerminatesAfterLastAdapterOwner) {
    auto runtime = PylonRuntime::create();
    ASSERT_TRUE(runtime.hasValue());
    auto secondOwner = runtime.value();
    runtime.value().reset();
    EXPECT_TRUE(secondOwner->initialized());
}
```

- [ ] **Step 3: Implement exact SDK target discovery and RAII**

Use the SDK-provided CMake configuration if present; otherwise create one narrowly scoped imported target from validated pylon include/library/runtime paths. Do not add pylon include directories globally. `PylonRuntime` is noncopyable; callers share it through `shared_ptr`.

- [ ] **Step 4: Build Basler on/off matrices**

Run simulator presets and a Basler Debug preset. Inspect link dependencies to confirm only `lumora_camera_basler` and the final executable in Basler composition reference pylon.

- [ ] **Step 5: Commit runtime integration**

```powershell
git add cmake/Pylon.cmake src/camera/basler src/CMakeLists.txt tests/unit/camera/basler
git commit -m "build(camera): add optional pylon runtime"
```

### Task 2: Basler provider discovery and identity

**Files:**
- Create: `src/camera/basler/include/lumora/camera/basler/BaslerCameraProvider.hpp`
- Create: `src/camera/basler/src/BaslerCameraProvider.cpp`
- Create: `src/camera/basler/src/BaslerIdentity.cpp`
- Create: `tests/unit/camera/basler/BaslerIdentityTests.cpp`
- Create: `tests/hardware/BaslerDiscoveryTests.cpp`

**Interfaces:**
- Consumes: `PylonRuntime`, `ICameraProvider`, and pylon transport-layer factory.
- Produces: stable IDs `basler:<device-class>:<serial>`, translated descriptors, and `BaslerCameraProvider::create`.

- [ ] **Step 1: Write identity translation tests using captured descriptor fields**

```cpp
TEST(BaslerIdentity, StableIdUsesDeviceClassAndSerial) {
    BaslerIdentityFields fields{"Basler", "ace2", "40212345", "BaslerGigE", "1.2.3"};
    auto descriptor = toCameraDescriptor(fields);
    EXPECT_EQ(descriptor.id.value, "basler:BaslerGigE:40212345");
}
```

- [ ] **Step 2: Verify provider types are missing**

Build `lumora_camera_basler_tests`; expect failure.

- [ ] **Step 3: Implement enumeration without opening devices**

Translate manufacturer, model, serial, device class/transport, and availability. Sort descriptors by stable ID for deterministic UI/tests. Catch pylon exceptions and return `CameraDiscovery` errors with vendor detail but an operator-safe summary.

- [ ] **Step 4: Add opt-in hardware discovery test**

Read expected serial from `LUMORA_TEST_CAMERA_SERIAL`; skip with an explicit reason when absent. When set, assert exactly one matching stable ID and nonempty manufacturer/model/transport.

- [ ] **Step 5: Run non-hardware tests and commit**

```powershell
ctest --preset windows-msvc-debug-basler --output-on-failure -LE hardware -R Basler
git add src/camera/basler tests/unit/camera/basler tests/hardware/BaslerDiscoveryTests.cpp
git commit -m "feat(camera): add Basler discovery adapter"
```

### Task 3: Capability reader and safe parameter mapper

**Files:**
- Create: `src/camera/basler/include/lumora/camera/basler/IGenApiNodeAccess.hpp`
- Create: `src/camera/basler/src/PylonGenApiNodeAccess.cpp`
- Create: `src/camera/basler/src/BaslerCapabilityReader.cpp`
- Create: `src/camera/basler/src/BaslerConfigurationMapper.cpp`
- Create: `tests/unit/camera/basler/BaslerCapabilityReaderTests.cpp`
- Create: `tests/unit/camera/basler/BaslerConfigurationMapperTests.cpp`

**Interfaces:**
- Consumes: internal node-access facade and vendor-neutral camera settings.
- Produces: `readCapabilities(IGenApiNodeAccess&)` including complete `SourcePixelFormat` descriptors and `applyConfiguration(IGenApiNodeAccess&, const CameraConfiguration&) -> Result<AppliedCameraConfiguration>`.

- [ ] **Step 1: Write missing-node and increment tests**

```cpp
TEST(BaslerCapabilities, UnavailableGainIsReportedAsUnsupported) {
    FakeNodeAccess nodes = monoCameraNodes();
    nodes.remove("Gain");
    auto capabilities = readCapabilities(nodes).value();
    EXPECT_FALSE(capabilities.gain.has_value());
}
```

- [ ] **Step 2: Verify mapper test fails**

Build `lumora_camera_basler_tests`; expect missing reader/mapper functions.

- [ ] **Step 3: Implement capability access by availability/writability checks**

Read Width, Height, OffsetX/Y, PixelFormat, AcquisitionMode, AcquisitionFrameRateEnable/AcquisitionFrameRate, ExposureAuto/ExposureTime, GainAuto/Gain using alternate standard names only within the adapter. Translate every supported monochrome format into stable encoding/name, valid bits, declared sample maximum, packing, alignment, and application storage. Every read/write checks node existence, access mode, type, range, and increment. Configure Continuous/free-run and reject trigger enablement in this release.

- [ ] **Step 4: Implement stopped-state configuration transaction**

Apply offsets safely around size changes, pixel format, FPS, exposure, and gain; read all values back and return actual settings. If a write fails, apply the captured preceding settings in reverse dependency order and return an error that reports whether rollback succeeded.

- [ ] **Step 5: Test model variations**

Cover missing optional nodes, read-only FPS, quantized ROI, unsupported format, automatic exposure/gain, write failure at each step, and rollback failure. No test exposes node names outside the Basler target.

- [ ] **Step 6: Commit capability/configuration mapping**

```powershell
git add src/camera/basler tests/unit/camera/basler
git commit -m "feat(camera): map safe Basler capabilities and settings"
```

### Task 4: Basler device acquisition and application-owned frames

**Files:**
- Create: `src/camera/basler/include/lumora/camera/basler/BaslerCameraDevice.hpp`
- Create: `src/camera/basler/src/BaslerCameraDevice.cpp`
- Create: `src/camera/basler/src/BaslerFrameConverter.cpp`
- Create: `tests/unit/camera/basler/BaslerFrameConverterTests.cpp`
- Create: `tests/hardware/BaslerAcquisitionTests.cpp`

**Interfaces:**
- Consumes: `ICameraDevice`, pylon `CInstantCamera`, `BufferPool`, and applied settings.
- Produces: full device lifecycle, freshness-oriented continuous grabbing, exact conversion for every approved capability-reported source format, and frame metadata.

- [ ] **Step 1: Write conversion tests with known packed sample vectors**

Use pylon image objects or converter-compatible buffers containing minimum, midpoint, and maximum values for every format in the approved hardware profile. Assert output application samples equal numeric sensor values, Mono8 remains U8, higher-depth samples are LSB-aligned in U16 storage, and padded source rows are preserved.

- [ ] **Step 2: Verify converter tests fail before implementation**

Build `lumora_camera_basler_tests`; expect missing converter.

- [ ] **Step 3: Implement device lifecycle**

Open attaches the selected device, capabilities/configuration require Open, Start uses continuous acquisition with a latest-image strategy and eight SDK buffers, Retrieve uses caller-provided timeout, Stop is idempotent, and Close stops then destroys the device.

- [ ] **Step 4: Implement immediate copy/unpack and release**

For approved Mono8/compatible unpacked formats, copy validated rows directly into their canonical storage. For approved packed formats configure `CPixelFormatConverter` for Mono16 with LSB output alignment, convert into an acquired application lease, validate output dimensions/stride, seal the lease, create `RawFrame` with its complete source descriptor, and release `CGrabResultPtr` before returning. Any capability-reported but unapproved format fails closed with a stable error.

- [ ] **Step 5: Categorize grab outcomes**

Translate timeout, unsuccessful grab result, skipped-image count, unsupported component/format, short payload, removed device, pylon exception, and application pool exhaustion to stable errors/metrics.

- [ ] **Step 6: Run hardware acquisition smoke test**

With `LUMORA_TEST_CAMERA_SERIAL`, acquire 300 frames, assert monotonic IDs, stable layout, valid sample range, no retained pylon buffers, and clean stop/close. Label the test `hardware`.

- [ ] **Step 7: Commit Basler acquisition**

```powershell
git add src/camera/basler tests/unit/camera/basler/BaslerFrameConverterTests.cpp tests/hardware/BaslerAcquisitionTests.cpp
git commit -m "feat(camera): acquire Basler frames into bounded core buffers"
```

### Task 5: Device removal notification and composition selection

**Files:**
- Create: `src/camera/basler/src/BaslerRemovalHandler.cpp`
- Create: `tests/hardware/BaslerRemovalTests.cpp`
- Modify: `src/app/main.cpp`
- Modify: `docs/development/build-windows.md`

**Interfaces:**
- Consumes: pylon configuration event callback, acquisition worker, and selected provider setting.
- Produces: thread-safe removal flag, `device_removed` acquisition result, and runtime selection between simulator and Basler providers.

- [ ] **Step 1: Write the callback isolation test**

Invoke the removal handler from a foreign test thread and assert it performs only an atomic notification; device stop/destroy calls remain recorded on the acquisition worker thread.

- [ ] **Step 2: Implement notification bridge**

The callback stores removal state and wakes the worker. `retrieve` or the worker observation path returns a stable removal error; the worker stops and destroys the device. Reconnection scheduling remains deferred to Milestone 12.

- [ ] **Step 3: Select provider at composition root**

Use an explicit command-line/developer configuration value `--camera-provider=simulator|basler`. A build without Basler rejects `basler` with a clear startup error and never attempts dynamic loading.

- [ ] **Step 4: Perform cable-removal hardware test**

Start streaming, remove the cable, assert UI state reaches Reconnecting/Error without blocking, reconnect cable, use manual Retry for this milestone, reacquire frames, then exit without a locked device.

- [ ] **Step 5: Commit removal/composition behavior**

```powershell
git add src/camera/basler src/app/main.cpp tests/hardware/BaslerRemovalTests.cpp docs/development/build-windows.md
git commit -m "feat(camera): handle Basler removal without UI blocking"
```

## Milestone 6 acceptance gate

- [ ] Simulator builds and tests remain pylon-free.
- [ ] Basler identity and safe settings are translated without leaking GenApi nodes.
- [ ] Known packed samples convert to exact LSB-aligned numeric values.
- [ ] Grab results are released before frames leave the adapter.
- [ ] Real discovery/acquisition/removal tests pass when the hardware profile is supplied.
- [ ] The approved camera/firmware/NIC/mode gate is recorded, acquisition is continuous free-running, and every enabled source format has exact conversion evidence.
