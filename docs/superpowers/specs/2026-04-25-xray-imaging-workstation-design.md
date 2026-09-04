# Lumora Real-Time X-Ray Imaging Workstation Design

**Status:** Approved evaluation architecture baseline; not approved for clinical use
**Original date:** 2026-04-25
**Clarification baseline:** 2026-09-04
**Source requirements:** `prd.md` and the approved design discussion
**Target:** Linux/GCC development and simulator tests; Windows 11 x64/MSVC production, packaging, and hardware acceptance; C++20, CMake, Qt 6, OpenCV, Basler pylon, GoogleTest, and spdlog

## 1. Purpose

Lumora is an open-source native Windows workstation application for acquiring monochrome images from a Basler GigE/GenICam camera, applying deterministic real-time enhancement, and displaying the freshest available image with low latency. It replaces the imaging/video portion of an older analog BNC and frame-grabber chain. It does not control the X-ray generator, exposure, mechanical motion, or safety interlocks.

The numbered milestones deliver an **evaluation release only**. That release is not for clinical use and must not acquire or store real patient data. The architecture preserves evidence and extension seams useful to a future clinical diagnostic release in Egypt, but no milestone completion implies Egyptian Drug Authority registration, clinical validation, or authorization for diagnosis.

The central engineering rule is:

> Preserve the original sensor samples, process non-destructively, and favor a fresh displayed frame over displaying every acquired frame.

The evaluation increment includes a simulator, a Basler adapter, high-bit-depth image handling, a modular enhancement pipeline, an operator-oriented viewer, presets, snapshots, diagnostics, reliability hardening, packaging, and real-hardware validation. Full sequence recording is architecturally supported but follows a separately approved specification.

## 2. Scope and success criteria

### 2.1 In scope

- Camera discovery, selection, connection, disconnection, configuration, streaming, timeout detection, controlled reconnection, and diagnostic identity.
- A vendor-neutral camera API with Basler and simulated implementations.
- Generated-pattern and recorded-sequence simulation at configurable frame rates, including injected faults.
- Immutable original frames stored as unsigned 8-bit or canonical unsigned 16-bit samples as appropriate.
- A bounded, freshness-oriented live pipeline with independent acquisition, processing, rendering, and capture workers.
- Deterministic CPU processing using OpenCV.
- Window/level, brightness, contrast, gamma, CLAHE, denoising, sharpening, inversion, flips, and right-angle rotation.
- Original, Enhanced, and synchronized side-by-side Compare presentation.
- Versioned processing presets with non-clinical names.
- Original, processed, or paired snapshot capture with a versioned metadata manifest.
- Versioned JSON configuration, structured local logging, health metrics, stress tests, soak tests, and Windows packaging.
- Mandatory Linux/GCC simulator builds and tests plus mandatory Windows/MSVC simulator CI throughout development.
- Persistent evaluation, paused-frame, and stale-image safety indications.
- Apache-2.0 licensing for Lumora-owned code, dynamically linked LGPL-compatible Qt modules, third-party notices, and an SBOM.

### 2.2 Explicitly out of scope

- DICOM, PACS, patient or worklist data, cloud connectivity, accounts, remote access, REST or web services.
- AI enhancement, diagnosis, segmentation, object detection, measurements, or annotations.
- X-ray generator control, exposure control, safety interlocks, or hardware movement.
- Multi-user operation, automatic updates, audit trails, or medical-device certification functionality.
- Full sequence recording in the initial stable snapshot release.
- Real patient data or clinical diagnostic use in the evaluation release.
- EDA registration, patient workflows, clinical validation, or a claim that the evaluation renderer is diagnostic-grade.

### 2.3 Initial measurable targets

- The CPU processing benchmark uses 2048 x 2048 unsigned 16-bit frames at 30 FPS; a 60 FPS simulator run is the overload stress case.
- Hardware acquisition uses a mode that is feasible for the selected camera and Ethernet link. A standard 1 GigE connection cannot carry 2048 x 2048 Mono12 at 30 FPS without reducing the payload, ROI, bit depth, or frame rate.
- On the designated Windows reference workstation, selected before Milestone 8 acceptance, the Standard preset sustains 30 FPS at the CPU benchmark size with documented headroom.
- P95 acquisition-to-presentation latency is below 100 ms at the hardware acceptance mode.
- Queue depths remain fixed under overload. Stale frames are replaced and counted.
- After a 15-minute warm-up, an eight-hour simulator soak shows no continuing memory-growth trend.
- The GUI remains responsive while streaming, processing is overloaded, the camera disappears, or capture fails.
- Normal automated tests do not require pylon or physical hardware.

These values are acceptance baselines, not guarantees for every camera mode. The application validates each requested camera configuration against discovered device capabilities.

CI runs functional benchmark smoke tests without workstation-dependent timing thresholds. The exact camera model, firmware, NIC, pixel formats, and acquisition mode are a hard decision gate before Milestone 6. The Windows reference workstation is a hard decision gate before Milestone 8 acceptance.

### 2.4 Future clinical-release gate

A future clinical release is a separate program. Before its build flag can be enabled, qualified Egyptian regulatory input must establish the intended purpose, device classification, legal manufacturer, EDA route, quality system, risk-management process, clinical and usability validation, cybersecurity lifecycle, post-market obligations, patient-data controls, diagnostic display requirements, and release authority. Requirements, hazards, mitigations, verification evidence, residual-risk decisions, and released binaries must be traceable.

Evaluation builds display `EVALUATION — NOT FOR CLINICAL USE` in normal and fullscreen views and record the release class in captures and reports. The clinical release class is compile-time controlled and cannot be enabled by an ordinary runtime setting.

## 3. Architectural approach

Lumora is a modular native application with a framework-independent C++ core and a thin Qt Widgets shell. The executable composition root creates concrete adapters and connects them to application services. Camera, processing, display, and storage paths communicate using explicit immutable data and bounded exchanges.

The selected approach is preferred over a Qt-centric frame pipeline because queued signal delivery can obscure backpressure and can accumulate stale frame events. It is preferred over an initial lock-free/GPU graph because that design adds complexity before profiling shows it is needed. Qt signals remain appropriate for low-frequency state notifications and commands; live frames use bounded latest-value slots.

Architectural invariants are:

1. Only the Basler adapter includes pylon headers or exposes pylon concepts.
2. Only the UI module owns or manipulates widgets.
3. No camera SDK call, image algorithm, encoder, or file operation executes on the UI thread.
4. A `RawFrame` is immutable after publication.
5. A processed representation never aliases mutable original pixels.
6. Every cross-thread frame path is bounded.
7. Every camera and worker lifecycle has one explicit owner and deterministic shutdown.
8. Invalid configuration is rejected atomically; the last valid configuration remains active.
9. Metrics may be per-frame counters, but normal logging is not per-frame.
10. Recording can never reuse or change the live-view buffering policy.
11. Evaluation artifacts and UI always identify the release as `EVALUATION — NOT FOR CLINICAL USE`; the numbered milestones never authorize patient use.
12. A presented paused or stale image is always visibly invalidated as non-live in normal, Compare, and fullscreen views.
13. The fixed processing order and installation orientation are versioned data; operators can adjust allowed values but cannot reorder stages or change orientation while streaming.

## 4. Repository and build structure

```text
Lumora/
|-- CMakeLists.txt
|-- CMakePresets.json
|-- vcpkg.json
|-- vcpkg-configuration.json
|-- LICENSE
|-- NOTICE
|-- cmake/
|   |-- Dependencies.cmake
|   |-- Pylon.cmake
|   |-- Warnings.cmake
|   `-- Packaging.cmake
|-- config/
|   `-- default-presets.json
|-- resources/
|   |-- icons/
|   `-- sample-sequences/
|-- src/
|   |-- core/
|   |-- camera/
|   |   |-- api/
|   |   |-- simulator/
|   |   `-- basler/
|   |-- processing/
|   |-- capture/
|   |-- configuration/
|   |-- diagnostics/
|   |-- application/
|   `-- ui/
|-- tests/
|   |-- unit/
|   |-- integration/
|   |-- reference/
|   |-- lifecycle/
|   `-- hardware/
|-- benchmarks/
|-- tools/
|   `-- sequence-generator/
|-- docs/
|   |-- architecture/
|   |-- operations/
|   `-- superpowers/
|-- packaging/
|   |-- wix/
|   `-- manifests/
`-- .github/workflows/
```

### 4.1 CMake targets

The source directories correspond to focused CMake library targets:

- `lumora_core`: frame/value types, result/error types, memory pools, latest-value exchange, clocks, and bounded queues. It has no Qt, OpenCV, or pylon dependency.
- `lumora_camera_api`: vendor-neutral descriptors, capabilities, configurations, device/provider contracts, and camera state.
- `lumora_camera_simulator`: synthetic and sequence-backed cameras plus fault injection.
- `lumora_camera_basler`: pylon runtime, discovery, device lifecycle, format conversion, and parameter mapping.
- `lumora_processing`: stage contracts, pipeline validation, CPU stages, workspaces, and display mapping.
- `lumora_capture`: capture jobs, encoders, manifests, storage transaction handling, and storage results.
- `lumora_configuration`: JSON parsing, validation, migrations, defaults, and atomic persistence.
- `lumora_diagnostics`: structured log events, sinks, counters, rolling timing windows, and metric snapshots.
- `lumora_application`: use cases, state machines, workers, reconnection policy, and subsystem coordination.
- `lumora_ui`: Qt Widgets, presentation controllers, viewport, dialogs, and resource integration.
- `lumora_app`: the executable and composition root.

`LUMORA_ENABLE_BASLER` defaults on for Windows production presets and off for simulator-only developer and CI presets. The Basler target is not configured or linked when the option is off. Tests link only the smallest target needed by the behavior under test.

Committed presets cover Linux/GCC and Windows/MSVC simulator Debug and Release builds plus Windows/MSVC Basler builds. Machine-specific SDK paths belong only in ignored `CMakeUserPresets.json`. A pinned vcpkg manifest supplies Qt, OpenCV, GoogleTest, and spdlog consistently on both operating systems, with an authenticated binary cache in CI. Pylon is an optional external SDK and runtime, not a vcpkg dependency. Every milestone runs Linux simulator tests locally and both Linux/GCC and Windows/MSVC simulator CI; packaging and final hardware acceptance are Windows-only.

Dependencies flow toward contracts and domain types:

```text
Qt UI --> Application --> Core contracts
                       |-> Processing
                       |-> Capture
                       |-> Configuration
                       `-> Diagnostics

Simulator --> Camera API + Core
Basler ----> Camera API + Core + pylon
```

No module obtains dependencies through a global service locator. Runtime dependencies are supplied through constructors by the composition root. `PylonRuntime` is an RAII object owned by the composition root and outlives all pylon-backed providers and devices.

## 5. Core frame and error model

### 5.1 Pixel representation

`SourcePixelFormat` distinguishes the camera/transport layout from the application storage layout. It is a capability-derived descriptor containing a stable canonical name/encoding, valid sensor bits, declared sample maximum, source packing, bit alignment, and resulting `U8` or `U16` application storage. Camera formats normally declare `2^validBits-1`; PGM replay uses its header maximum so non-power-of-two ranges remain unambiguous. The design supports common Mono8, Mono10/Mono12 packed or unpacked variants, and Mono16 when the selected camera and pylon converter expose them. The exact accepted set is frozen only after the camera model, sensor, firmware, NIC, and acquisition mode pass the Milestone 6 gate; unknown formats fail closed with a typed error.

`RawFrame` records:

- Immutable shared ownership of its application buffer.
- Width, height, row stride, payload size, and storage type.
- Source pixel format, valid sensor bits, bit alignment, and source packing.
- Monotonic frame ID and optional camera frame/block ID.
- Host receipt timestamp from `steady_clock` and UTC acquisition timestamp.
- Optional device timestamp retained in its native clock domain.
- The actual camera settings snapshot associated with the frame.

Packed camera input is unpacked into canonical unsigned 16-bit sample values before publication. This preserves sensor values rather than the exact wire-packed byte sequence. Exact transport payload capture is a distinct future encoder and does not redefine `RawFrame`.

`ProcessedFrame` records immutable unsigned 16-bit enhanced pixels in native sensor orientation, the source frame ID, the complete pipeline configuration version, and stage/total timings. `DisplayFrame` is format-aware and records an explicit grayscale storage format, immutable pixels, dimensions/stride, the source frame ID, display mapping, and applied presentation orientation. Evaluation composition publishes `Gray8`; the renderer boundary permits a later calibrated `Gray16` or 10-bit/OpenGL implementation without changing camera, processing, or capture contracts.

`FrameBundle` groups one `RawFrame`, the matching original-display image, optional `ProcessedFrame`, and matching enhanced-display image. All members carry the same source frame ID. The UI and capture subsystem consume `FrameBundle`, ensuring Original, Enhanced, Compare, and Capture Both never mix different acquisitions.

### 5.2 Validation

Before publishing a frame or creating an OpenCV view, the acquisition boundary validates:

- Nonzero supported dimensions.
- Width, height, and stride arithmetic without integer overflow.
- Stride sufficient for the declared width and storage format.
- Payload size sufficient for all declared rows.
- Supported monochrome pixel format and valid-bit count.
- ROI and dimensions compatible with the applied camera configuration.

Malformed input produces a typed `InvalidFrame` result, increments acquisition failure metrics, and is discarded without reaching processing.

### 5.3 Results and errors

The project uses a small C++20-compatible `Result<Value, Error>` value type rather than exceptions for expected cross-module failures. Errors include category, stable code, operator-safe summary, diagnostic detail, recoverability, and optional underlying vendor code.

Error categories include camera discovery, connection, configuration, acquisition, invalid frame, processing, configuration persistence, encoding, storage, resource exhaustion, cancellation, and internal invariant violation. Exceptions are caught at worker boundaries and converted into typed errors; no exception crosses a thread boundary or enters the Qt event loop unexpectedly.

## 6. Camera abstraction

### 6.1 Contracts

The camera API defines the following conceptual operations without exposing SDK-specific types:

| Contract | Responsibility |
|---|---|
| `ICameraProvider::discover` | Return current `CameraDescriptor` values using a bounded operation. |
| `ICameraProvider::create` | Create a closed device for a stable `CameraId`. |
| `ICameraDevice::open` | Claim and open the device. |
| `ICameraDevice::capabilities` | Return supported formats and numeric ranges/increments. |
| `ICameraDevice::applyConfiguration` | Validate and apply safe settings; return actual applied values. |
| `ICameraDevice::startStream` | Begin continuous acquisition. |
| `ICameraDevice::retrieve` | Return an application-owned immutable frame or typed failure within a bounded timeout. |
| `ICameraDevice::stopStream` | Stop acquisition idempotently. |
| `ICameraDevice::close` | Release all device resources idempotently. |

`CameraDescriptor` contains stable identity, manufacturer, model, serial, transport, and availability. `CameraCapabilities` contains supported pixel formats, ROI ranges and increments, frame-rate range, exposure range/modes, gain range/modes, and flags describing whether each value is writable while streaming.

`CameraConfiguration` contains only safe application concepts:

- Pixel format.
- ROI width, height, and offsets.
- Requested acquisition FPS or device-controlled maximum.
- Exposure mode and exposure time.
- Gain mode and gain value.
- Free-run mode initially; trigger configuration is reserved for a later release.

The adapter returns `AppliedCameraConfiguration` because cameras may quantize a value to a supported increment. The UI displays requested and actual values when they differ. Invalid combinations are rejected with an explanation rather than silently accepted.

### 6.2 Thread affinity and Basler behavior

The camera worker exclusively owns the device and performs all normal pylon calls. Retrieval uses a bounded timeout so stop requests and shutdown cannot wait indefinitely. Pylon grab results are copied once into an application buffer and released immediately; retaining pylon grab pointers across the pipeline is prohibited because it prevents SDK buffer reuse.

The pylon removal callback may execute on an SDK thread. It performs no teardown and calls no UI. It only sets a thread-safe removal notification. The camera worker observes that notification, stops retrieval, destroys the removed pylon device, publishes state, and begins the configured reconnection policy.

Pylon's `MaxNumBuffer` starts at eight. The adapter uses a freshness-oriented grab strategy where supported. Camera-side skipped-image counts and vendor error codes are translated into diagnostics metrics.

### 6.3 Simulated camera

The simulator implements the same provider/device contracts and supports:

- Deterministic gradients, ramps, checkerboards, impulse noise, and moving patterns.
- Replay of ordered 8-bit and 16-bit monochrome image sequences.
- Strict PGM P5 replay for maximum sample values 1 through 65535, including 255, 1023, 4095, and 65535, with declared sample-range metadata and big-endian decoding above 255.
- Configurable width, height, valid bit depth, and FPS.
- Real-time pacing, unlimited fastest-possible mode, and manual-clock deterministic mode.
- End-of-sequence loop, stop, or error behavior.
- Injected timeout, malformed frame, disconnect, reconnect, and configuration failure after a specified frame or duration.
- Stable metadata and frame IDs for repeatable assertions.

Simulator capability ranges follow the same validation path as Basler capabilities. Tests can therefore exercise application camera behavior without conditional code.

## 7. Camera and viewer state machines

Camera state is explicit:

```text
Disconnected -> Discovering -> Connecting -> ConnectedIdle -> Streaming
      ^                              |              |             |
      |                              |              |             v
      `------------------------------+-------- Reconnecting <- UnexpectedFailure
                                                     |
                                                     v
                                                   Error
```

Allowed commands depend on state. Invalid commands return a typed state error and do not partially mutate the session. Connect, disconnect, start, stop, and shutdown are idempotent.

Manual Disconnect cancels any pending reconnect and moves to `Disconnected`. Unexpected removal or three consecutive acquisition timeouts moves to `Reconnecting`. Reconnection targets the same stable camera identity and uses five attempts with approximate delays of 0.5, 1, 2, 5, and 5 seconds. Discovery between attempts verifies identity. Exhaustion moves to `Error`, preserves diagnostics, and exposes a manual Retry action.

First run performs discovery but requires explicit camera selection, configuration review, and Start. Later runs may rediscover the last stable identity and offer one-click `Resume Live` only when the identity and capability fingerprint are unchanged; streaming never starts silently. Identity/capability drift returns to review-required state. Manual Disconnect disables automatic reconnect until a later explicit Connect.

Viewer state is independent from camera state:

- `Live` replaces the presented bundle with the newest bundle.
- `Paused` retains the currently displayed bundle while camera acquisition and latest-slot replacement continue. A persistent high-contrast `PAUSED` overlay, frozen-frame timestamp, and increasing age remain visible over Original, Enhanced, Compare, and fullscreen presentation.
- `Original`, `Enhanced`, and `Compare` affect presentation only.
- Stop Stream is a separate acquisition command and is not represented as Pause.

Resuming consumes the freshest available bundle. While `Live`, if no new frame is successfully presented within `max(500 ms, 3 expected frame periods)`, the last frame may remain for context but a persistent `STALE IMAGE / NOT LIVE` overlay visibly invalidates it until a fresh frame is presented. Tests separately stall the camera, processing worker, and UI presentation path.

This separation allows instant fresh resume without coupling viewport or presentation actions to camera ownership.

## 8. Thread ownership and communication

### 8.1 Workers

- **UI thread:** owns all widgets, viewport transforms, the currently presented bundle, and presentation controllers. It sends commands and consumes immutable state/metric snapshots.
- **Camera worker:** a dedicated `std::jthread` that owns the current camera device, camera command mailbox, retrieval loop, timeout policy, configuration transaction, and reconnection.
- **Processing worker:** a dedicated `std::jthread` that owns the active pipeline instance, OpenCV stage objects, lookup tables, and processing workspace.
- **Capture worker:** a dedicated `std::jthread` that owns encoding, metadata serialization, temporary capture directories, final rename, and storage result reporting.

Workers use stop tokens and condition variables rather than busy polling. Low-frequency state is forwarded to Qt using queued signals from presentation adapters. Per-frame Qt signal posting is prohibited.

### 8.2 Live frame flow

```text
Camera worker
  -> LatestFrameSlot<RawFrame> [capacity 1]
  -> Processing worker
  -> LatestFrameSlot<FrameBundle> [capacity 1]
  -> UI refresh/presentation

Current displayed FrameBundle
  -> CaptureQueue [capacity 4]
  -> Capture worker
  -> Filesystem
```

Publishing to a full latest slot atomically replaces and releases the older value. The replacement increments the corresponding drop counter. The processing worker wakes for a newer sequence number and consumes the newest available raw frame. The UI checks for a new bundle at a maximum 60 Hz presentation cadence and paints only a new frame ID.

The camera command queue is bounded to 32 low-frequency commands. Commands that supersede earlier pending values, such as repeated configuration changes, are coalesced. Stop, Disconnect, and Shutdown take precedence over ordinary reconfiguration.

Capture uses a four-job bounded FIFO because user-initiated captures must preserve order and report individual outcomes. When full, a new request returns `CaptureBusy`; it does not allocate another queue node or block the UI.

### 8.3 Shutdown

Shutdown proceeds in this order:

1. Disable new UI commands and capture submissions.
2. Cancel discovery timers and reconnection delays.
3. Request camera stop; stop retrieval, stop the stream, close/destroy the device, and join the camera worker.
4. Request processing stop, release unpublished frames, and join the processing worker.
5. Allow the active capture transaction to complete, cancel queued jobs with explicit outcomes, and join the capture worker.
6. Persist the final valid configuration snapshot.
7. Flush and stop logging.

No thread is detached, forcibly terminated, or allowed to outlive a dependency it uses.

## 9. Frame memory ownership and bounded buffering

### 9.1 Buffer pools

Application frame storage uses fixed-capacity pools after the active resolution is known. Initial capacities are ten raw buffers, nine unsigned 16-bit processing buffers, sixteen unsigned 8-bit display buffers, and four capture jobs. These counts cover the worst documented steady-state leases: one raw slot, one frame under processing, one published bundle, one UI-held bundle, four capture-held bundles, two processing ping-pong buffers, paired Original/Enhanced display buffers, and transition reserve.

Pool creation performs checked size arithmetic and reports the memory requirement before streaming starts. The initial capacities consume approximately 216 MiB of pixel storage at 2048 x 2048, excluding pylon buffers and stage-specific scratch memory. A mode that exceeds the configured safe memory budget is rejected before acquisition. Pools may resize only during a controlled stopped-state resolution change.

A pool lease returns its buffer to the owning pool when the final reference is released. Public frame objects expose constant pixel views. OpenCV `cv::Mat` values are temporary non-owning views whose lifetime cannot exceed the lease.

If the camera worker cannot acquire a raw application buffer immediately, it releases the SDK result, increments `droppedNoRawBuffer`, and continues. It does not wait, heap-allocate a fallback frame, or slow the pylon retrieval loop. The same fail-fast rule applies to processing and display publication under resource exhaustion.

### 9.2 Copy policy

The initial Basler path performs one necessary copy/unpack from SDK-owned memory into core-owned raw storage. This clean boundary prevents SDK buffer starvation and pylon types from leaking into the application. The simulator writes directly into an application pool lease.

Processing never copies the original merely to preserve it; it reads the immutable original and writes separate buffers. Stages alternate between two reusable work buffers. The final enhanced buffer and display buffers retain leases while published or displayed. Stage-specific scratch memory is allocated or resized on configuration/resolution changes, not for every frame.

User-provided pylon buffers are a measured future optimization inside the Basler adapter. Adopting them must not change `RawFrame`, camera contracts, or downstream ownership semantics.

## 10. Processing pipeline

### 10.1 Image domains

```text
Camera payload
  -> unpack and align
Sensor-native unsigned samples
  -> deterministic normalization
Canonical U16
  -> fixed-order enhancement stages
Enhanced U16
  -> format-aware display mapping
Display Gray8 (evaluation composition)
```

Normalization is based on the descriptor's declared source sample maximum, bit depth, and alignment. It never calculates independent per-frame min/max values. This avoids brightness pumping and preserves temporal intensity meaning.

Window/level is a 16-bit tone-mapping stage that maps the configured input window into the full unsigned 16-bit output range. The mandatory terminal display mapper then converts unsigned 16-bit grayscale into the selected display format. The evaluation release selects unsigned 8-bit grayscale. Stage placement is fixed and versioned rather than operator-defined.

### 10.2 Stage contract

Each `IProcessingStage` declares:

- Stable stage ID and human-readable non-clinical name.
- Accepted input and produced output image domains.
- Parameter schema, numeric ranges, defaults, and validation.
- Whether it changes dimensions.
- Required scratch-memory shape and bounded temporal history, if any.
- Deterministic processing operation and timing result.

`PipelineDefinition` is a fixed-order set of enabled stage configurations with a schema version, order version, and configuration revision. `ProcessingPipeline` validates all stages, parameters, and image domains before activation. A new immutable definition is swapped at a frame boundary. An invalid definition leaves the preceding pipeline active. Operators can enable/disable permitted stages and adjust parameters; they cannot reorder them.

The first release order is:

- Sensor normalization.
- Window and level.
- Brightness and contrast using saturating unsigned 16-bit arithmetic.
- Gamma using a cached 65,536-entry lookup table rebuilt only when gamma changes.
- CLAHE on unsigned 16-bit grayscale.
- Gaussian and median denoising options.
- Unsharp-mask sharpening.
- Optional grayscale inversion.

After Original and Enhanced display mapping, the same installation-profile orientation (horizontal/vertical flip and 0/90/180/270-degree rotation without interpolation) is applied to both presentation paths. Orientation is not an enhancement stage or a live operator control. It changes only through an administrator-managed, confirmed stopped-state camera-profile workflow and is always shown in status and capture metadata.

Disabled stages perform no buffer allocation or pixel traversal. The pipeline remains single-frame and sequential initially; OpenCV's measured CPU behavior determines whether its internal thread count should be limited on the target workstation.

### 10.3 Original, Enhanced, and failure behavior

The Original path is raw frame -> sensor normalization -> active window/level -> display mapping -> shared presentation orientation. It is labeled `Original (display mapped)` in operator help. No display or enhancement operation mutates or replaces raw samples.

The Enhanced path is raw frame -> fixed-order validated pipeline -> display mapping -> the same shared presentation orientation. Compare computes both from the same raw frame and places the results in one bundle. Stored Original and Enhanced U16 artifacts remain in native sensor orientation; only the screen-equivalent preview includes presentation orientation.

A stage failure discards the affected processed frame, records the stage and error, and leaves acquisition alive. After three consecutive processing failures, the application switches presentation to Original, shows a non-modal warning, and requires a valid configuration or explicit retry before returning to Enhanced. One malformed configuration cannot partially update the pipeline.

Extension stages for dark-frame subtraction, flat-field correction, bad-pixel correction, temporal denoising, or GPU execution implement the same contract. Calibration assets and temporal history will have explicit version/lifetime owners when those features enter scope.

## 11. Rendering and workstation UI

### 11.1 UI composition

The Qt Widgets interface contains:

- A compact header with product name, camera connection state, acquisition state, selected camera, and essential FPS.
- A fixed, collapsible left sidebar with display mode, processing controls, preset selector, capture mode/action, and reset.
- A dominant dark-background image viewport.
- A compact footer with Fit, 100%, zoom, fullscreen, and optional health information.
- Separate camera configuration and diagnostics dialogs/panels for less frequent operations.

`MainWindow` owns layout. `WorkstationController` translates UI intent to application commands and exposes immutable presentation state. `ImageViewport` owns painting and viewport transforms. `ProcessingPanel` edits complete pipeline definitions. `CameraPanel` consumes only camera descriptors, capabilities, and application configurations.

### 11.2 Viewer behavior

- Original, Enhanced, and Compare are mutually exclusive presentation modes.
- Compare begins as synchronized side-by-side images with identical zoom and pan.
- Fit preserves aspect ratio and responds to window resizing.
- 100% maps one source pixel to one logical image pixel, subject to documented Windows display scaling.
- Mouse-wheel zoom is centered at the pointer; drag pans only when the image exceeds the viewport.
- Double-click returns to Fit.
- Processing changes do not reset viewport state.
- Paused retains both images of the current bundle and shows the mandatory `PAUSED`, frozen timestamp, and increasing-age indication.
- Live presentation applies the mandatory stale-frame deadline and `STALE IMAGE / NOT LIVE` overlay.
- Fullscreen hides nonessential controls but retains evaluation, paused/stale, orientation, and live/error state; Escape exits.

The initial `ImageViewport` uses `QPainter` with `QImage::Format_Grayscale8`. Each `QImage` external-memory view retains the owning display-buffer lease for the full paint lifetime. The renderer is behind a format-aware interface so a future calibrated 10-bit/OpenGL replacement does not affect application or processing code. The evaluation renderer is not represented as diagnostic-grade; a future clinical release must validate the monitor, renderer, calibration, ambient light, and viewing conditions.

### 11.3 Controls and presets

Sliders display numeric values and support keyboard control. Slider drags update local labels immediately and coalesce processing definitions to no more than 30 updates per second; release publishes the exact final value.

Initial presets are Original, Standard, High Contrast, Soft Detail, and Custom. Built-in names and descriptions explicitly state that they are visual configuration bundles and not clinically validated tissue modes. Selecting a preset changes all stage settings atomically. Manual modification changes the displayed preset to Custom. Reset restores the documented processing default only; it does not change the camera, stream, viewport, or capture configuration.

Capture mode is Original, Processed, or Both. Successful capture feedback names the final capture directory. Failures state the actionable cause without freezing or stopping live display.

No Record control/action is present in v1 evaluation or production composition. Recording experiments use non-shipping harnesses until a separate recording specification and acceptance plan are approved.

## 12. Capture and future recording

### 12.1 Snapshot transaction

The capture request retains the exact currently displayed `FrameBundle`. The capture worker creates a unique temporary directory under the configured destination, writes all required artifacts, writes the manifest last, closes every file, and renames the directory to its final name on the same volume. An incomplete transaction never appears under a final capture name.

The initial layout is:

```text
20260425_143052_381_<serial>_<frame-id>/
|-- original.png         # Original or Both; stored depth is declared in metadata
|-- enhanced_u16.png     # Processed or Both
|-- preview_u8.png       # Screen-equivalent preview for Processed or Both
`-- metadata.json
```

The Original PNG stores immutable native-orientation sensor samples without normalization, window/level, enhancement, flip, or rotation: Mono8 is lossless 8-bit PNG; Mono10/Mono12/Mono16 numeric samples are lossless 16-bit PNG. The enhanced PNG stores the unsigned 16-bit fixed-pipeline result in native sensor orientation. The preview is the exact final unsigned 8-bit screen presentation, including active window/level and installation orientation. PNG is lossless and available without adding TIFF or DICOM dependencies. The encoding contract remains replaceable.

`metadata.json` has an independent schema version and includes:

- UTC capture timestamp, source frame ID, and optional device frame ID.
- Application version and capture schema version.
- Camera manufacturer, model, serial, firmware when available, and transport.
- Dimensions, stride, source pixel format, source packing, valid bit depth, and stored format.
- Source alignment, active installation orientation, release class, and whether the captured bundle was paused.
- Applied ROI, FPS, exposure, gain, and other safe camera settings.
- Selected preset and complete ordered processing definition.
- Stage and total processing durations plus measured presentation latency.
- Artifact filenames, intended capture mode, and completed outcomes.

Disk full, destination loss, permission denial, encoder failure, and rename failure produce distinct typed results. Temporary artifacts are retained with a clearly incomplete suffix only when retaining them aids recovery; otherwise they are removed on the next safe cleanup pass. No cleanup targets paths outside the configured capture root.

### 12.2 Recording boundary

A future `ISequenceRecorder` consumes a separate bounded branch from acquisition or processing. It owns its own memory pool, queue, writer thread, manifest, and backpressure policy. A requirement for lossless recording must be validated against sustained camera payload and disk bandwidth; it cannot be achieved by allowing an unbounded queue. When recording cannot sustain input, policy must explicitly stop with an error, reduce the configured acquisition rate, or record counted gaps. It must never delay the live path.

Container, codec, original-versus-processed content, loss policy, and audio are not selected in this design.

## 13. Configuration and presets

Per-user configuration is stored as versioned JSON beneath the local application-data directory resolved by `QStandardPaths`: `%LOCALAPPDATA%\Lumora\Config` on Windows and the corresponding XDG configuration location on Linux. Logs use `%LOCALAPPDATA%\Lumora\Logs` on Windows and the corresponding XDG state/data location on Linux. Captures default to `%USERPROFILE%\Pictures\Lumora\Captures` on Windows and the corresponding XDG Pictures directory on Linux, and remain configurable through a storage adapter.

Installation camera identity and orientation are separate machine-wide versioned data under `%PROGRAMDATA%\Lumora\Config` on Windows. Installer ACLs allow administrators to modify and ordinary operators to read, not write. Linux simulator tests inject an equivalent system-profile root rather than requiring writes to `/etc`. The main application never silently elevates itself. Per-user configuration contains:

- `schemaVersion`.
- Application preferences and diagnostic visibility.
- User camera preferences keyed by vendor, model, and serial.
- A reference/fingerprint for the read-only active installation camera profile and orientation.
- Last selected camera identity.
- Active processing definition and built-in/custom preset references.
- Capture destination and capture mode.
- UI geometry, fullscreen state, and sidebar collapsed state where useful.

The machine profile uses the same versioned, validated, atomic-write rules but can be written only by an explicit administrator workflow while streaming is stopped. A simulator may use a documented identity orientation when no machine profile exists. A Basler Start is blocked with an actionable administrator-required error if the profile is absent, invalid, or does not match the selected camera identity/capability fingerprint.

Configuration models are plain C++ values. Only the serializer/path adapter depends on Qt Core JSON and `QStandardPaths`, avoiding an additional JSON library. Each schema change supplies a tested sequential migration. Loading schema 1 into an application at schema 3 applies 1->2 then 2->3, producing the same normalized result as a schema-3 file.

Saving writes a complete temporary file, flushes and closes it, and replaces the prior file in the same directory. Validation occurs before replacement. If loading fails, the original is renamed with an `.invalid-<UTC timestamp>` suffix, factory defaults are used, and a non-blocking warning identifies the preserved file.

Built-in presets ship in version-controlled resources and are immutable. Custom presets are user configuration and include their own schema/revision. Presets never store or change camera connection settings.

SQLite is not justified for the initial configuration volume and is not included.

## 14. Diagnostics, metrics, and logging

### 14.1 Metrics

The diagnostics service records atomic counters and bounded rolling timing samples. It publishes an immutable `MetricsSnapshot` approximately twice per second containing:

- Camera, processing, and displayed FPS.
- Latest, median, and P95 processing time.
- Current and P95 host-clock acquisition-to-presentation latency.
- Camera-reported skipped frames.
- Grab timeouts and grab failures.
- Frames replaced before processing.
- Frames replaced before display.
- Frames dropped for raw, processing, or display buffer exhaustion.
- Rejected capture jobs and capture failures.
- Current pool usage/high-water marks.
- Camera state, processing health, and active pipeline revision.

Device timestamps are diagnostic metadata unless they are explicitly synchronized to the host. Live latency uses the host `steady_clock` from receipt to successful presentation so it never mixes clock domains.

### 14.2 Structured logs

spdlog writes asynchronous JSON-lines events to five rotating 10 MiB files. Debug builds may additionally use a console sink. The asynchronous queue is bounded; overflow is counted and exposed in metrics. Error and lifecycle events are flushed at controlled boundaries.

Logged events include application version/startup/shutdown, configuration migration or important change, discovery summary, camera identity, connect/disconnect, stream start/stop, reconnect attempts/outcome, acquisition failures, processing failures, capture outcome, storage errors, and shutdown problems.

Events use stable typed IDs and versioned fields so they can support later traceability and an independently specified clinical audit design. Evaluation diagnostic logs are not claimed to be a compliant audit trail.

Frame payloads are never logged. Normal operation does not emit a line for every frame. Drop counters are summarized at most once per ten seconds while nonzero and immediately on a relevant state transition. Logs contain no patient data because patient workflows are out of scope.

## 15. Reliability and failure policy

- A single retrieval timeout increments metrics. Three consecutive timeouts cause controlled stream teardown and reconnection.
- Successful frame retrieval resets the consecutive-timeout count.
- Camera removal destroys the removed device before reconnection.
- Reconnection stops after five failed attempts and waits in Error for operator Retry.
- Manual disconnect always cancels automatic reconnection.
- Unsupported or malformed frames are discarded before processing.
- A processing failure affects one frame; three consecutive failures force Original presentation.
- Pool exhaustion drops the newest incoming work at that boundary and records the reason; it never grows memory or blocks acquisition.
- Capture failure reports the job outcome and leaves acquisition/processing unchanged.
- Configuration-save failure leaves the prior valid file intact.
- UI commands that arrive during shutdown are rejected as cancelled.
- Repeated lifecycle operations are idempotent and validated in stress tests.
- Logging failure degrades diagnostics but does not terminate imaging; the UI exposes logging health.
- Paused and stale images remain visibly invalidated in every presentation mode; this visual safety state cannot be dismissed while its condition persists.

The application makes no attempt to modify NIC settings, firewall rules, generator state, or exposure safety behavior automatically.

## 16. Test strategy

GoogleTest is the single unit/integration framework and CTest provides labels and orchestration. Test-only fakes include a manual clock, deterministic scheduler hooks at exchange boundaries, failing capture encoder, failing storage transaction, and scripted camera provider.

### 16.1 Unit tests

- Checked image-size arithmetic and frame validation for all supported storage layouts.
- Pool lease/recycle behavior, high-water metrics, exhaustion, and destruction ordering.
- Latest-slot replacement and sequence semantics.
- Bounded command/capture queues, coalescing, cancellation, and priority shutdown.
- Every state transition and invalid command for camera and viewer state machines.
- Camera capability and configuration range/increment validation.
- Every processing stage for disabled, identity, nominal, boundary, and invalid parameters.
- Fixed pipeline-order/domain validation, rejection of reordered definitions, and atomic revision activation.
- Preset application and Custom transition.
- Configuration round-trip, corrupt input, future-version rejection, and every migration path.
- Metadata schema contents and capture naming.
- Metrics rates, rolling percentiles, clock behavior, and counter categorization.

### 16.2 Reference-image tests

The repository stores small synthetic unsigned 16-bit patterns for exact algorithm assertions and properly anonymized images for regression coverage. Each fixture records valid bit depth and expected pipeline revision. Unpacking, normalization, window/level, inversion, right-angle orientation, display mapping, and Original capture use exact pixel comparison on Linux/GCC and Windows/MSVC. OpenCV CLAHE, Gaussian/median denoise, and sharpening use reviewed per-algorithm maximum absolute error and image-difference thresholds across the two platforms. Repeatability/reference outputs are also recorded on the designated Windows release build. Dependency locking prevents accidental algorithm drift; any tolerance change requires explicit review.

Reference tests cover Original preservation, window endpoints, gamma endpoints, CLAHE tile boundaries, denoise impulse response, sharpening saturation, inversion, flips, every rotation, complete presets, and original/enhanced frame-ID pairing.

### 16.3 Integration and lifecycle tests

- Simulator discovery through UI-facing state snapshots.
- Connect, configure, start, publish, pause, resume, stop, and disconnect.
- Acquisition overload proving raw slot depth remains one.
- Processing overload proving display slot depth remains one and newest frame wins.
- Slider/preset configuration replacement during streaming.
- Capture Original, Processed, and Both from live and paused bundles.
- Camera, processing, and UI-presentation stalls proving stale indication timing; paused indication proving timestamp/age behavior in normal, Compare, and fullscreen views.
- Timeout, malformed frame, disconnect, reconnect success, reconnect exhaustion, and manual cancellation.
- Storage full, unavailable path, permission failure, encoder failure, and shutdown during capture.
- At least 1,000 simulator lifecycle cycles in the scheduled stress suite.
- Shutdown initiated from each camera state and while each worker is active.

### 16.4 Performance and soak tests

A dedicated benchmark executable reports JSON results without adding a separate benchmark dependency. Release-mode runs measure stage and whole-pipeline throughput for 512 x 512, 1024 x 1024, and 2048 x 2048 unsigned 16-bit inputs. It records median/P95 time, frame throughput, allocations after warm-up, and working-set trend.

The 30 FPS run is a sustain gate on the designated Windows reference workstation. The 60 FPS run verifies controlled drops, current-frame freshness, and fixed queue depths; it does not require every frame to display. CI runs functional benchmark smoke tests with no workstation timing threshold. Performance pressure may reduce expensive processing work or the configured camera mode, but must never weaken bounded freshness or the paused/stale indications.

The simulator soak lasts at least eight hours. Hardware soak lasts at least four hours at the agreed feasible camera mode. Both record memory, handle count, queue depths, latency percentiles, error counts, and reconnect events.

### 16.5 Hardware-in-the-loop suite

Hardware tests are labeled `hardware` and require an explicit camera identity/configuration file. They cover discovery metadata, every supported selected pixel format, ROI increments, applied FPS/exposure/gain, stable acquisition, frame integrity, cable removal, camera power cycle, reconnection, 100 lifecycle cycles, and four-hour streaming. Final GigE validation occurs on the production Windows workstation and NIC, not a virtual machine.

## 17. Milestones and acceptance criteria

Because the product spans several independently reviewable subsystems, implementation is decomposed into fourteen milestone plans rather than one monolithic execution checklist. Each plan must leave the repository buildable, run the tests introduced by that milestone, and preserve all earlier acceptance gates. A short master roadmap records ordering and cross-milestone interfaces; the executable work stays within the milestone plans below.

### Milestone 1: Project foundation

Deliver CMake targets/presets, warnings policy, dependency discovery, Qt application shell, GoogleTest/CTest integration, spdlog bootstrap, version generation, and developer build documentation.

Acceptance: simulator-only Debug and Release configurations build and test on Linux/GCC and Windows/MSVC; the application opens and exits cleanly; Basler-off configuration needs no pylon installation; dependency versions are pinned.

### Milestone 2: Core frames and bounded primitives

Deliver frame/value types, checked validation, clocks, fixed buffer pools, latest-value slots, bounded queues, and typed results/errors.

Acceptance: original buffers are immutable; pool reuse and exhaustion are deterministic; latest slots never exceed capacity one; concurrency tests pass under sustained producer/consumer imbalance.

### Milestone 3: Camera abstraction and simulator

Deliver camera contracts, capability/configuration models, generated patterns, sequence replay, pacing, manual time, and scripted failures.

Acceptance: a test can discover, configure, connect, stream, stop, and disconnect a simulated camera; sequence timing and failure points are repeatable; no physical SDK is loaded.

### Milestone 4: Minimal live viewer

Deliver workstation shell, basic state controls, `ImageViewport`, Original display mapping, pause, Fit, 100%, zoom, and pan using simulated frames.

Acceptance: generated frames display smoothly; pause freezes a stable bundle with the mandatory overlay/timestamp/age while acquisition continues; stale camera, processing, and presentation paths are visibly invalidated; viewport actions do not change camera or image data; UI-thread responsiveness tests pass.

### Milestone 5: Independent live pipeline

Deliver camera/processing workers, latest-frame exchanges, command handling, frame bundles, presentation cadence, stop-token shutdown, and base flow metrics.

Acceptance: forced processing delay causes counted replacement rather than latency growth; no per-frame Qt events accumulate; shutdown succeeds from every active state.

### Milestone 6: Basler adapter

After the camera/firmware/NIC/pixel-format/acquisition-mode gate is approved, deliver optional pylon discovery, runtime/device RAII, safe parameter mapping, bounded free-running retrieval, capability-derived format metadata, removal notification, and lifecycle error translation.

Acceptance: the selected Basler camera is discovered by identity, configured within capabilities, and streamed repeatedly; unplugging never freezes the UI or leaves the device locked; simulator-only builds remain unchanged.

### Milestone 7: High-bit-depth and window/level

Deliver packed-format unpacking, canonical unsigned 16-bit frames, deterministic normalization, Original preservation tests, 16-bit window/level, and final 8-bit display mapping.

Acceptance: known Mono10/Mono12/Mono16 samples produce exact canonical values; raw hashes do not change after processing; window/level boundary and terminal display-mapping reference tests pass.

### Milestone 8: Modular enhancements

Deliver stage contracts/registry, pipeline validation, brightness, contrast, gamma, CLAHE, denoise, sharpen, inversion, flips, rotation, workspace reuse, and benchmarks.

Acceptance: stages can be enabled, disabled, and adjusted only in the fixed versioned order; reordered or invalid pipelines do not replace the active revision; exact and tolerance-governed cross-platform reference outputs pass; Standard sustains the target CPU benchmark on the designated reference workstation before advancing.

### Milestone 9: Presets and workstation controls

Deliver full processing sidebar, built-in/custom presets, coalesced controls, Original/Enhanced/Compare, synchronized comparison, installation-profile orientation workflow/status, evaluation and paused/stale indications, diagnostics access, collapsible sidebar, and fullscreen behavior. Keep Record hidden.

Acceptance: preset selection is atomic; manual changes select Custom; Compare frame IDs always match; image controls preserve viewport state; critical camera errors remain visible in fullscreen.

### Milestone 10: Snapshot capture

Deliver capture queue/worker, PNG encoders, capture transaction, metadata schema, configured destination, feedback, and injected storage failures.

Acceptance: all three modes create the required complete artifact set at the declared source depth and orientation; artifacts and metadata share a frame ID; metadata records release class, pause state, source format/alignment, and active orientation; failures leave no apparently complete capture and do not disrupt live viewing.

### Milestone 11: Diagnostics and supportability

Deliver structured rotating logs, complete metric categories, rolling timing snapshots, health UI, rate-limited summaries, and operations documentation for locating logs/configuration.

Acceptance: required lifecycle/error events contain application and camera identity; metrics distinguish every drop boundary; normal streaming does not generate per-frame logs; log-sink failure is visible but nonfatal.

### Milestone 12: Reliability hardening

Deliver timeout policy, controlled reconnection, processing fallback, platform-neutral process-health sampling with Linux and Windows implementations, lifecycle idempotence, stale-indication fault matrix, 1,000-cycle stress run, and eight-hour simulator soak.

Acceptance: every injected fault has a deterministic state/result; UI remains responsive; queues and pools remain bounded; no continuing memory or handle growth is observed after warm-up.

### Milestone 13: Windows distribution

Deliver production CMake preset, deployed dynamic LGPL-compatible Qt plugins, OpenCV/spdlog runtime packaging, license/notice/SBOM artifacts, application/version metadata, a WiX v4 per-machine MSI, and a signing-capable WiX Burn `Lumora-Setup.exe` bootstrapper that checks/installs the official VC++ x64 redistributable. Treat pylon Runtime as a separately managed vendor prerequisite until its exact redistribution/version policy is approved.

Acceptance: an administrator can install/repair/upgrade/uninstall and a non-administrator operator can run Lumora on a clean Windows 11 x64 machine; the release starts, streams the simulator, writes per-user configuration/logs/captures, and has no developer paths. Internal evaluation artifacts may be unsigned only when clearly labeled and accompanied by hashes. External evaluation and future clinical artifacts must be Authenticode signed and timestamped; the signing identity is a release gate.

### Milestone 14: Hardware performance and acceptance

Deliver documented production camera/NIC mode, measured payload budget, network tuning guidance, latency/throughput report, cable/power recovery evidence, 100-cycle camera lifecycle run, and four-hour hardware soak.

Acceptance: the feasible free-running camera mode meets the agreed acquisition/display and P95 latency target; failures recover or reach a clear actionable Error state; no camera resource remains locked; the report records any reduced ROI/FPS required by link bandwidth. Passing Milestone 14 qualifies only the evaluation release and does not authorize clinical or patient use.

## 18. Technical risks and mitigations

| Risk | Consequence | Mitigation |
|---|---|---|
| 1 GigE payload limit | Requested resolution/FPS cannot be transported | Calculate payload during configuration, expose feasible ranges, validate on target NIC, and use ROI/FPS/bit-depth tradeoffs. |
| GenICam node variation | Configuration works on one model only | Map capabilities in the adapter, query availability/writability, and test against the selected camera. |
| Packed-format mistakes | Corrupted intensity values | Use explicit format adapters and exact bit-pattern reference tests. |
| Retained pylon results | SDK buffer starvation | Copy/unpack at acquisition boundary and release the result immediately. |
| Expensive CLAHE/denoise | Processing misses frame budget | Benchmark each stage, reuse memory, drop stale frames, and add GPU work only after measurement. |
| Hidden allocation/copy | Latency jitter and memory growth | Instrument allocation points, preallocate workspaces, and keep buffer lifetime explicit. |
| Queued UI frame events | Increasing display latency | Use one latest bundle and timed/coalesced presentation. |
| Capture retains live buffers | Acquisition pool exhaustion | Bound capture jobs, size pools for documented leases, reject excess requests, and record pool high-water marks. |
| SDK or disk delays shutdown | Hung application exit | Bound camera retrieval, use stop tokens, define shutdown order, and isolate storage. |
| Plugin/runtime deployment | Clean machine cannot start | Exercise packaging on a clean Windows image and avoid developer-path discovery. |
| Enhancement interpretation | Users infer clinical validation | Use neutral preset names and explicit non-validation language. |
| Display scaling differences | 100% view is misunderstood | Define logical-pixel behavior and document Windows scaling. |

## 19. Deferred decisions

The architecture reserves boundaries but does not select:

- Exact Basler model, sensor resolution, firmware, NIC, supported pixel formats, and production free-running acquisition mode; resolve before Milestone 6.
- Reference workstation CPU/GPU and display; resolve before Milestone 8 acceptance.
- Hardware or software triggering; free-running continuous acquisition is the initial mode.
- OpenGL renderer, CUDA, OpenCL, or another GPU processing API.
- Sequence recording container, codec, content, and loss policy.
- Exact camera transport-payload preservation.
- DICOM/DCMTK, PACS, patient/worklist integration, or calibration workflows.
- Organization signing identity/process; resolve before external distribution. WiX v4 MSI plus Burn bootstrapper is selected, and automatic updating is out of scope.
- Crash dumps, audit logs, user roles, and regulated lifecycle controls.
- Arbitrary-angle rotation, multiple active cameras, or multiple-monitor workflows.

These decisions require hardware evidence, deployment policy, or new product requirements. They do not require changing the approved module boundaries.

## 20. References informing the design

- Basler pylon C++ Programmer's Guide: grab-result smart pointers control buffer reuse and retained results can cause buffer underrun. <https://docs.baslerweb.com/pylonapi/cpp/pylon_programmingguide>
- Basler pylon Advanced Topics: device removal can be observed by callback/query and the removed device must be destroyed. <https://docs.baslerweb.com/pylonapi/cpp/pylon_advanced_topics>
- Qt QThread documentation: worker objects and thread-affinity rules. <https://doc.qt.io/qt-6/qthread.html>
- Qt Threads and QObjects: GUI objects remain on the main thread and cross-thread QObject access requires care. <https://doc.qt.io/qt-6/threads-qobject.html>
- Qt QImage documentation: 16-bit and 8-bit grayscale image formats. <https://doc.qt.io/qt-6/qimage.html>
- OpenCV CLAHE documentation: CLAHE accepts `CV_8UC1` and `CV_16UC1`. <https://docs.opencv.org/4.x/d6/db6/classcv_1_1CLAHE.html>
- Egyptian Drug Authority medical-device regulatory guidelines. <https://edaegypt.gov.eg/en/the-regulatory-reference-of-the-egyptian-drug-authority-eda/regulatory-guidelines/ca-of-medical-device/>
- IEC 62304 medical-device software lifecycle processes. <https://webstore.iec.ch/en/publication/22794>
- ISO 14971 medical-device risk management. <https://www.iso.org/standard/72704.html>
- vcpkg manifest mode. <https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode>
- Qt open-source licensing and LGPL obligations. <https://doc.qt.io/qt-6/licensing.html>
- Basler pylon deployment guide. <https://docs.baslerweb.com/pylonapi/pylon-deployment-guide>
