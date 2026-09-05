# Requirements traceability

This matrix starts the evidence chain for the approved evaluation architecture. “Verified on Linux” records local milestone results; the Windows column remains pending until the committed required CI job completes. Later milestones extend these rows instead of replacing them.

Current checkpoint: M1–M3 implementation is merged locally. The [M3 verification record](milestones/m03-camera-api-simulator.md) records the tested baseline and evidence; **M3 acceptance is pending Windows/MSVC CI**. An implemented subset below does not imply that its complete design section or a later milestone is accepted.

| ID | Design source | Implementation/evidence | Linux/GCC | Windows/MSVC | Status |
|---|---|---|---|---|---|
| DES-1 | §1 Purpose and evaluation boundary | Compile-time evaluation release class, `MainWindowSmoke`, `Logging` | Verified | CI pending | M1 baseline implemented |
| DES-2 | §2 Scope and success criteria | Simulator-only presets, pylon-off configure path, evaluation banner; broader product scope maps to M2–M14 | Verified for M1 | CI pending | M1 subset implemented; later scope planned |
| DES-3 | §3 Architectural approach | Qt-free core, camera API and simulator targets; focused configuration/diagnostics/UI targets; typed errors and bounded exchanges | Verified for implemented subset | CI pending | M1–M3 foundation implemented |
| DES-4 | §4 Repository and build structure | CMake target contract, pinned vcpkg manifest, Linux and Windows Debug/Release simulator presets | Verified | CI pending | Linux implemented; Windows verification pending |
| DES-5 | §5 Core frame and error model | Capability-derived pixel descriptors, checked layouts, immutable paired frames, reusable aligned buffers, typed errors, separate steady/UTC timestamps; transactional failure-result construction | Verified | CI pending | M2 foundation and M3 extensions implemented |
| DES-6 | §6 Camera abstraction | `CameraConfiguration`, `CameraContract`, `SimulatedCamera`, `SequenceSource`, `FaultScript`, `SimulatedCamera.PatternAllocation` | Verified for API/simulator | CI pending | M3 implemented; Basler adapter remains M6 |
| DES-7 | §7 Camera and viewer state machines | Simulator lifecycle and persistent-disconnect tests; application/viewer states remain M4–M5/M12 | Verified for device subset | CI pending | Partial |
| DES-8 | §8 Threading model | Thread-confined camera port, cancellable clock waits and retrieval; worker orchestration/shutdown remains M5/M12 | Verified for primitives/device | CI pending | Partial |
| DES-9 | §9 Frame memory ownership and bounded buffering | `Core.BufferPool`, `Core.Frame`, `Core.LatestValueSlot`, `Core.BoundedQueue`; camera publication/lease tests | Verified for primitives/device | CI pending | M2–M3 subset; application pool sizing remains later work |
| DES-10 | §10 Processing pipeline | Frame/provenance contracts exist; processing, high-bit-depth mapping and enhancements remain M5/M7/M8 | Not yet verified as pipeline | Not run | Planned |
| DES-11 | §11 Rendering and workstation UI | `MainWindowSmoke`; live rendering and controls remain M4/M9 | Verified for shell only | CI pending | Partial |
| DES-12 | §12 Capture and future recording | M10 capture plan; recording requires its own approved specification | Not run | Not run | Planned/deferred |
| DES-13 | §13 Configuration and presets | `ConfigurationStore`; presets, installation profiles and controls remain M9 | Verified for store only | CI pending | Partial |
| DES-14 | §14 Diagnostics, metrics, and logging | `Logging`, pool/slot counters and simulator pacing-slip tests; full diagnostics remain M11 | Verified for foundation only | CI pending | Partial |
| DES-15 | §15 Reliability and failure policy | M3 timeout, cancellation, fault transaction, sequence and lease regressions; system recovery remains M12 | Verified for device subset | CI pending | Partial |
| DES-16 | §16 Test strategy | M1–M3 verification maps below; integration, reference-image, performance, soak and hardware suites remain later milestones | 18 CTest entries pass in Debug/Release | CI pending | Partial |
| DES-17 | §17 Milestones and acceptance criteria | Per-milestone plans and [M3 acceptance status](milestones/m03-camera-api-simulator.md#acceptance-evidence) | M1–M3 local evidence | CI pending | No cross-platform acceptance claimed |
| DES-18 | §18 Technical risks and mitigations | Bounded storage/retrieval and SDK isolation tested; rendering, throughput, hardware and distribution risks remain in their milestone gates | Partial evidence | Pending | Ongoing |
| DES-19 | §19 Deferred decisions | [Open hard gates](../superpowers/README.md#open-hard-gates) record required camera, workstation and distribution inputs | Not an executable test | Not an executable test | Deferred until the specified gates |
| DES-20 | §20 References informing the design | References remain in the approved specification; they are background, not verification evidence | Not applicable | Not applicable | Reference material |

## Milestone 1 verification map

| Test/check | Requirement covered | Automated evidence |
|---|---|---|
| `MainWindowSmoke` | Resizable shell opens/closes; visible evaluation identity; translation-ready UI strings | `lumora_ui_tests` |
| `ResultSmoke` | Qt-free typed success/failure contract including move-only values | `lumora_core_tests` |
| `Logging` | Writable rotating log; startup/shutdown; version and release class; typed path failure | `lumora_diagnostics_tests` |
| `ConfigurationStore` | Defaults, schema validation, round-trip, corrupt-file preservation, atomic-save failures, Unicode paths | `lumora_configuration_tests` |
| Configure-time target contract | Required M1 targets exist; Basler target cannot appear when disabled | `lumora_verify_milestone_one_targets()` |
| Linux simulator CI | Pinned dependencies, pylon-off Debug and Release configure/build/test | `Linux Simulator / Linux GCC Debug and Release Simulator` |
| Windows simulator CI | Native MSVC Debug and Release configure/build/test without pylon | `Windows Simulator / Windows MSVC Debug and Release Simulator` |

## Milestone 2 verification map

| Test/check | Requirement covered | Automated evidence |
|---|---|---|
| `Core.Result`, `Core.CheckedMath` | Typed errors, move-only results, checked addition/multiplication boundaries | `lumora_core_tests` |
| `Core.ImageLayout`, `Core.FrameMetadata` | Fail-closed format descriptors; zero/stride/payload/overflow layout validation; complete applied acquisition snapshot | `lumora_core_tests` |
| `Core.BufferPool` | Fixed capacity, deterministic exhaustion/high-water metrics, aligned preallocation, immutable sealing, final-reference recycling, safe state lifetime, 100,000 concurrent acquisitions | `lumora_core_tests` |
| `Core.Frame` | Sealed-buffer size checks, native raw/processed storage, pipeline/timing provenance, display mapping/orientation, source-ID and dimension pairing | `lumora_core_tests` |
| `Core.LatestValueSlot` | Capacity-one newest-value delivery, monotonic revision, explicit replacement accounting, close/stop wake within 250 ms, 100,000 publications | `lumora_core_tests` |
| `Core.BoundedQueue` | Constructor-fixed FIFO capacity, full rejection, ordered draining, close/stop wake within 250 ms | `lumora_core_tests` |
| `Core.Clock` | Independent monotonic and UTC time domains plus deterministic manual advancement | `lumora_core_tests` |
| Target dependency review | `lumora_core` has no Qt, OpenCV, spdlog, or pylon link dependency | `src/CMakeLists.txt`; Linux Debug/Release full builds |

## Milestone 3 verification map

| Test/check | Requirement covered | Automated evidence |
|---|---|---|
| `CameraConfiguration` | Complete capability-derived format descriptors, strict ROI increments, in-range numeric requests, reserved-trigger rejection, deterministic ordered violations | `lumora_camera_api_tests` |
| `CameraContract` | Compile-time vendor-neutral provider/device port shape, cancellation-aware immutable-frame retrieval signature, non-throwing cleanup signatures | `lumora_camera_api_tests`; closed-state and thread-confinement requirements are documented in `ICameraDevice.hpp` |
| `SimulatedCamera` | Stable discovery identity, initially closed/idempotent lifecycle, generated pattern determinism, applied-value quantization, streaming write permissions, ROI/format metadata, pool exhaustion, pacing, whole-retrieval budgets and cancellation | `lumora_camera_simulator_tests`, including `ProviderDiscoversOneStableDescriptorAndRejectsWrongId` and `LifecycleIsIdempotentAndCapabilitiesRequireOpen` |
| `SequenceSource` | Lexical P5 replay, exact payloads, big-endian U16 decoding, declared maxima 255/1023/4095/65535/1000, CRLF handling, invalid-file rejection, Loop/Stop/Error | `lumora_camera_simulator_tests`; committed synthetic fixtures |
| `FaultScript` plus `SimulatedCamera` fault/replay cases | Frame/time-indexed deterministic faults, persistent disconnect and explicit restoration, shared configuration validation, replay cursor preservation and transactional fault consumption | `lumora_camera_simulator_tests` |
| `SimulatedCamera.PatternAllocation` | Public pattern generation propagates allocation failure without termination from an incorrect `noexcept` boundary | Isolated `lumora_pattern_allocation_tests` executable |
| `Core.Clock` | Stop-aware steady/manual waits, independent real elapsed bound, extreme/zero/negative timeout and cancellation precedence | `lumora_core_tests`; filter includes `Clock*`, `ManualClock*`, and `SystemClock*` |
| `Core.Result` | Final failure result is constructed before fault state commits; failed copies cannot consume an occurrence | `lumora_core_tests` |
| Target dependency review | Camera API links only core; simulator links only camera API; no Qt/OpenCV/spdlog/pylon dependency in either camera target | `src/CMakeLists.txt`; simulator-only Linux Debug/Release configure/build |

The [M3 record](milestones/m03-camera-api-simulator.md) maps every M3 acceptance criterion to observed evidence, records limitations, and gives the exact outstanding Windows gate. M4–M14 acceptance checklists remain planned in their milestone files; no tests or performance evidence for those milestones are claimed here.

No row in this file represents clinical validation, regulatory evidence, or authorization for diagnostic use.
