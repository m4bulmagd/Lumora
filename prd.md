# PRD — Real-Time X-Ray Camera Imaging Workstation

**Product name:** Lumora
**Clarification baseline:** 2026-09-04

This PRD defines two distinct release classes:

- **Evaluation release:** an engineering and hardware-evaluation product that is explicitly not for clinical use and must not acquire or store real patient data.
- **Future clinical release:** a separately approved product for clinical diagnosis in Egypt. It may be released only after the applicable Egyptian Drug Authority pathway, intended purpose, risk management, clinical validation, cybersecurity, usability, and regulated lifecycle obligations have been established and satisfied with qualified regulatory input.

The evaluation release is the scope of the numbered implementation milestones. Clinical use is a future release gate, not an implied property of completing those milestones.

The documentation authority, approved decision summary, and unresolved hard gates are indexed in `docs/superpowers/README.md`.

## 1. Product overview

Lumora is a native Windows desktop application that acquires a live monochrome image stream from a modern GigE/GenICam camera attached to an existing X-ray imaging system.

The system is intended to replace or modernize an older analog video chain in which a camera sends video through BNC to a frame-grabber/computer.

The new system uses a digital industrial camera connected directly to the Windows workstation through Ethernet.

The application receives the camera frames, performs configurable real-time image enhancement and displays the resulting image with minimal latency.


---

# 2. Problem

Older X-ray imaging systems may still use analog CCD cameras and BNC video capture hardware.

These systems can suffer from:

- limited image resolution
- analog signal degradation
- limited dynamic range
- outdated frame-grabber hardware
- obsolete Windows drivers
- poor image-processing capabilities
- difficulty replacing obsolete components
- limited ability to enhance the live image

A modern GigE camera can provide higher-quality digital images but requires new software capable of acquiring and displaying the image stream.

---

# 3. Product objective

Create a reliable Windows application capable of:

**GigE Camera → Digital Acquisition → Real-Time Processing → Low-Latency Display**

The system should provide better control over image quality while preserving the original captured frame.

---

# 4. Primary user

The primary evaluation-release user is a trained engineer, service specialist, or clinical evaluator viewing non-patient test output from an X-ray imaging system. A future clinical release is intended for trained operators or clinicians, subject to its separate clinical-release gate.

The application should therefore prioritize:

- extremely simple operation
- large image display
- low latency
- predictable controls
- reliability
- clear connection/status indicators
- minimal UI clutter

Technical configuration should not dominate the main imaging interface.

---

# 5. Target platform

Production operating system:

**Windows 11 x64**

Development and continuous-integration platforms:

- Ubuntu Linux x64 with GCC for daily simulator development and automated tests
- Windows x64 with Visual Studio 2022/MSVC for mandatory compatibility tests

The simulator application and normal automated tests must remain first-class on both platforms. Official packaging, installation, and final Basler hardware acceptance remain Windows-only. Official Windows artifacts are built with MSVC on Windows; Linux-to-Windows cross-compilation is not a release path.

Initial camera target:

**Basler GigE Vision / GenICam camera**

Core technology direction:

- C++20
- Qt 6
- Basler pylon C++ SDK
- OpenCV
- CMake
- MSVC
- automated C++ testing framework
- pinned vcpkg manifest for Qt, OpenCV, GoogleTest, and spdlog

Basler pylon remains an optional external vendor SDK and must not be required by simulator-only builds.

The exact Basler model, sensor, firmware, NIC, supported pixel formats, and free-running acquisition mode must be selected and recorded before physical-camera implementation begins. Milestones before that gate remain hardware-independent.

---

# 6. High-level system

The system consists of six principal subsystems.

### Camera acquisition

Discovers and communicates with the physical camera.

### Frame pipeline

Receives frames and transports them through the application without unnecessary copying or latency.

### Image processing

Applies deterministic image enhancement.

### Viewer

Displays original or enhanced images.

### Capture/recording

Stores snapshots and, later, frame sequences/video.

### Diagnostics

Tracks the health and performance of the imaging pipeline.

---

# 7. Functional requirements

## FR-1 Camera discovery

The system shall detect supported cameras available to the workstation.

The user shall be able to see basic information including:

- camera manufacturer
- model
- serial number
- connection state

---

## FR-2 Camera connection

The system shall allow the user to connect to and disconnect from a selected camera.

Camera communication failures must not freeze the application.

On first run, the application shall require explicit camera selection, configuration review, and Start. On later runs, it may rediscover the last stable camera identity and offer a one-click **Resume Live**, but it must not begin streaming silently. An identity or capability change requires review before Start. Manual Disconnect disables automatic reconnection until the user explicitly reconnects.

---

## FR-3 Live acquisition

The application shall continuously acquire image frames from the connected camera.

The initial acquisition mode is continuous free-running video. Hardware and software trigger modes are reserved for a later approved requirement.

The acquisition engine must preserve the camera's native image depth where practical.

For example, 10-bit or 12-bit images should remain represented at higher precision rather than immediately being converted to 8-bit.

---

## FR-4 Live display

The current image shall be displayed in a large central viewer.

The viewer should support:

- live image
- pause
- fit to screen
- fullscreen
- zoom
- pan
- reset view

---

## FR-5 Original image

The application must maintain access to the original acquired frame.

Image processing must never irreversibly modify the source frame.

The operator shall be able to switch between:

- Original
- Enhanced

A synchronized side-by-side Compare view shall be provided in the evaluation release.

---

# 8. Image processing requirements

The processing architecture must consist of independent configurable processing stages.

Initial processing capabilities:

- intensity normalization
- window/level
- brightness
- contrast
- gamma
- CLAHE/local contrast enhancement
- denoising
- sharpening
- grayscale inversion
- horizontal flip
- vertical flip
- rotation

Each processing operation should be individually configurable. The evaluation release uses one fixed, documented, versioned stage order. Operators may enable or disable permitted stages and change validated parameters, but may not reorder stages.

Future processing operations may include:

- dark-frame correction
- flat-field correction
- bad-pixel correction
- temporal noise reduction
- detector/camera calibration
- GPU processing

AI-generated image enhancement is excluded from the initial product.

---

# 9. Window and level

Window/level shall be treated as a core image-display capability.

High-bit-depth camera data should be mapped to the display range using configurable window and level values.

The original high-bit-depth data should remain unchanged.

---

# 10. Processing presets

Users shall be able to apply processing presets.

Initial examples:

- Original
- Standard
- High Contrast
- Soft Detail
- Custom

A preset contains a set of processing parameters.

Presets must not be presented as clinically validated diagnostic modes unless they have gone through appropriate validation in the future.

---

# 11. Snapshot capture

The user shall be able to capture the exact currently displayed or paused frame bundle.

Capture modes shall support:

- Original
- Processed
- Original + Processed

Each captured image should include associated metadata.

Original capture preserves effective source storage depth: Mono8 is stored losslessly as 8-bit PNG, while unpacked Mono10/Mono12/Mono16 numeric samples are stored losslessly in a 16-bit PNG with valid-bit, alignment, packing, and source-format metadata. Original capture is never normalized, windowed, enhanced, flipped, or rotated. Processed capture stores the enhanced unsigned 16-bit result and a separate screen-equivalent preview.

Metadata should include:

- capture timestamp
- camera model
- camera serial
- image dimensions
- pixel format
- image bit depth
- declared source sample maximum, packing, and alignment
- camera exposure where available
- camera gain where available
- processing parameters
- selected preset
- application version
- evaluation/clinical release class and safety statement
- live/paused state and frozen-frame timestamp where applicable
- installation orientation and native-versus-presentation orientation

---

# 12. Recording

The architecture shall allow future recording of live sequences.

Recording must run independently from the live display pipeline.

Slow disk IO must not create increasing display latency.

Snapshot capture may be implemented before video recording.

---

# 13. Real-time behavior

The application shall optimize for **freshness of the displayed frame**, rather than guaranteeing display of every acquired frame.

If processing cannot keep up with camera acquisition, stale display frames may be discarded.

The system must use bounded buffering.

Unbounded frame queues are forbidden.

---

# 14. Performance metrics

The application should expose diagnostic metrics including:

- acquisition FPS
- processing FPS
- display FPS
- dropped frames
- frame processing duration
- estimated live latency
- camera connection state
- acquisition errors

Metrics may initially be displayed through a diagnostic panel rather than the primary user interface.

---

# 15. Camera abstraction

The rest of the application must not depend directly on Basler-specific APIs.

A camera abstraction shall allow multiple camera implementations.

Initial implementations:

### Simulated camera

Used for development and automated testing.

### Basler camera

Uses Basler pylon.

Possible future implementations:

- generic GenTL camera
- FLIR
- Allied Vision
- Teledyne
- IDS
- recorded frame sequence

Changing camera vendors should not require rewriting the UI or processing pipeline.

---

# 16. Simulated camera

A software camera implementation is a mandatory early feature.

It shall replay recorded frame sequences as though they came from a physical camera.

The simulator should support configurable frame rates.

This allows development and testing without access to the X-ray hardware.

---

# 17. Threading model

Camera acquisition, image processing and user-interface rendering must operate independently.

No camera SDK calls or heavy image-processing operation should block the GUI thread.

The architecture should approximately separate:

- acquisition worker
- processing worker
- UI/rendering
- storage worker when required

Thread shutdown and ownership must be deterministic.

---

# 18. Frame buffering

Frame queues shall be bounded.

The live processing/display pipeline should favor the newest available frame.

The application must avoid accumulating seconds of delayed frames.

Memory should preferably be reused rather than repeatedly allocated for every frame.

---

# 19. Main User Interface

The application shall use a workstation-style layout with:

- a fixed control sidebar on the left
- a large live X-ray image viewer occupying the main area on the right
- a compact status/header area for camera and acquisition state

The live image must remain the primary visual focus of the application.

## 19.1 Main layout

The interface should approximately follow this structure:

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ Lumora                  ● LIVE      Camera: Connected      FPS: 30          │
├──────────────────────┬───────────────────────────────────────────────────────┤
│                      │                                                       │
│ DISPLAY              │                                                       │
│                      │                                                       │
│ [ Original ]         │                                                       │
│ [ Enhanced ]         │                                                       │
│ [ Compare ]          │                                                       │
│                      │                                                       │
│──────────────────────│                                                       │
│                      │                                                       │
│ IMAGE CONTROLS       │                                                       │
│                      │                                                       │
│ Window               │                                                       │
│ ─────────●────────   │                                                       │
│                      │                  LIVE X-RAY IMAGE                     │
│ Level                │                                                       │
│ ───────●──────────   │                                                       │
│                      │                                                       │
│ Brightness           │                                                       │
│ ─────────●────────   │                                                       │
│                      │                                                       │
│ Contrast             │                                                       │
│ ───────────●──────   │                                                       │
│                      │                                                       │
│ Sharpness            │                                                       │
│ ───────●──────────   │                                                       │
│                      │                                                       │
│ Denoise              │                                                       │
│ ─────●────────────   │                                                       │
│                      │                                                       │
│ Gamma                │                                                       │
│ ───────●──────────   │                                                       │
│                      │                                                       │
│──────────────────────│                                                       │
│                      │                                                       │
│ PRESET               │                                                       │
│ [ Standard ▼ ]       │                                                       │
│                      │                                                       │
│──────────────────────│                                                       │
│                      │                                                       │
│ [ Capture ]          │                                                       │
│                      │                                                       │
│ [ Reset ]            │                                                       │
│                      │                                                       │
├──────────────────────┴───────────────────────────────────────────────────────┤
│ [ Fit ] [ 100% ] [ Zoom - ] [ Zoom + ] [ Fullscreen ]    Latency: 42 ms   │
└──────────────────────────────────────────────────────────────────────────────┘
```

The exact visual design may evolve, but the left-sidebar / right-viewer structure should remain consistent.

---

## 19.2 Left control sidebar

The left sidebar shall contain the primary image-processing controls.

The sidebar should be logically divided into sections.

### Display mode

Provide:

- Original
- Enhanced
- Compare

Only one primary display mode should be active at a time.

### Image-processing controls

Provide controls for:

- Window
- Level
- Brightness
- Contrast
- Gamma
- Denoise
- Sharpening

Installation-level camera-profile settings may include:

- horizontal flip
- vertical flip
- rotation

Flip and rotation are not live operator processing controls. They may be changed only in a confirmed, stopped-state installation workflow, are applied consistently to Original and Enhanced presentation, and are always visible in status and capture metadata. The immutable Original capture remains in native sensor orientation.

Controls should provide immediate real-time visual feedback while being adjusted.

---

## 19.3 Original / Enhanced / Compare modes

### Original

Display the camera frame with only deterministic bit-depth normalization, the active window/level mapping, and the shared installation orientation required for presentation. Operator help and metadata describe this as **Original (display mapped)**.

No enhancement pipeline should modify the source data.

### Enhanced

Display the result of the fixed-order configured image-processing pipeline using the same source frame, window/level basis, and presentation orientation as Original.

### Compare

Allow the operator to visually compare the original and enhanced result.

Possible implementations include:

- side-by-side display
- draggable before/after divider
- instant toggle

The first implementation may use side-by-side comparison.

The architecture should allow alternative comparison modes later.

---

## 19.4 Processing presets

The sidebar shall provide a preset selector.

Initial presets may include:

- Original
- Standard
- High Contrast
- Soft Detail
- Custom

Selecting a preset shall update the processing controls to reflect the preset values.

If the operator manually changes a preset parameter, the current preset may become Custom.

Preset names must not imply clinically validated diagnostic behavior unless formally validated in the future.

---

## 19.5 Capture controls

The sidebar shall provide a prominent Capture control.

Capture should be available directly without navigating to another screen.

The Record control must remain hidden in production and evaluation builds until recording has a separately approved specification and verified implementation.

---

## 19.6 Reset control

Provide a Reset action that restores image-processing parameters to a known default configuration.

Reset must not change camera connection or acquisition state.

---

## 19.7 Main image viewer

The right-hand body of the application shall be dedicated primarily to the live X-ray image.

It should occupy as much screen area as possible.

The viewer shall support:

- live display
- zoom
- pan
- fit to screen
- 100% pixel view
- fullscreen
- paused-frame display

Paused presentation must include a persistent high-contrast **PAUSED** overlay, the frozen frame timestamp, and continuously increasing frame age. The indication remains visible in fullscreen and Compare modes.

While in Live mode, if no new frame is successfully presented within `max(500 ms, 3 expected frame periods)`, the viewer must show a persistent **STALE IMAGE / NOT LIVE** overlay. The last image may remain visible for context but must be visibly invalidated until a fresh frame is presented.

The viewer should use the available area efficiently while preserving the image aspect ratio.

The image should never be stretched or geometrically distorted merely to fill the screen.

Unused viewer space should use a neutral dark background suitable for medical/scientific image viewing.

The evaluation renderer uses 8-bit grayscale through a format-aware renderer boundary and must not be described as diagnostic-grade. A future clinical release must validate the renderer, monitor, calibration, ambient light, and viewing conditions.

---

## 19.8 Viewer interactions

The user should be able to:

- zoom using mouse wheel
- pan while zoomed
- double-click or use a control to fit image to screen
- return to 100% scale
- enter fullscreen mode

Image-processing controls must continue functioning while zoomed or panned.

Changing processing parameters must not reset the current viewport unnecessarily.

---

## 19.9 Header/status bar

The top area shall display important acquisition status without occupying significant vertical space.

Show at least:

- application/product name
- camera connection state
- acquisition state
- LIVE / PAUSED indication
- acquisition/display FPS

Example:

`Lumora    ● LIVE    Camera: Connected    FPS: 30`

Connection and acquisition states should be visually distinguishable.

Examples:

- Connected
- Disconnected
- Connecting
- Reconnecting
- Live
- Paused
- Error

---

## 19.10 Performance/status footer

A compact footer or optional diagnostic overlay may display:

- displayed FPS
- acquisition FPS
- dropped frames
- estimated current latency
- processing duration

Example:

`FPS: 30 | Processing: 11 ms | Latency: 42 ms | Dropped: 0`

The normal operator view should remain uncluttered.

Advanced metrics may therefore be hidden unless diagnostics mode is enabled.

---

## 19.11 Sidebar behavior

The sidebar should have a predictable fixed width.

It may be collapsible to maximize image viewing space.

When collapsed, the live image should automatically expand into the available space.

The application should remember the user's sidebar state between sessions.

---

## 19.12 Fullscreen imaging mode

Fullscreen mode should prioritize the X-ray image.

In fullscreen mode:

- the image should occupy nearly the entire display
- controls may automatically hide
- moving the mouse may temporarily reveal essential controls
- acquisition state should remain visible
- the user must be able to exit fullscreen easily

The application must never hide critical error or camera-disconnection information.

Evaluation builds must also display **EVALUATION — NOT FOR CLINICAL USE** in the normal and fullscreen views. This release status is compile-time controlled and cannot be removed through an ordinary runtime setting.

---

## 19.13 UI responsiveness

Image processing and camera communication must never run directly on the Qt UI thread.

Manipulating controls should remain responsive while the camera is continuously acquiring frames.

The user must be able to interact with:

- sliders
- presets
- viewer
- capture
- fullscreen
- camera controls

without interrupting acquisition.

---

## 19.14 UI design principle

The main design principle is:

**Controls on the left. Image on the right. The live X-ray remains the dominant element of the application.**

The UI should resemble a dedicated imaging workstation rather than a general-purpose desktop application or developer camera utility.
---

# 20. Error handling

The application must gracefully handle:

- camera disconnection
- Ethernet interruption
- camera timeout
- invalid frame
- unsupported pixel format
- processing failure
- missing configuration
- disk full
- unwritable destination
- repeated connect/disconnect
- repeated start/stop
- application shutdown while streaming

Failures must not leave camera resources locked.

---

# 21. Logging

The system shall generate useful local diagnostic logs.

Log events should include:

- application startup
- application shutdown
- software version
- camera discovered
- camera connected
- camera disconnected
- acquisition started
- acquisition stopped
- camera error
- processing error
- dropped frames where meaningful
- capture failure
- recording failure
- important configuration changes

High-frequency per-frame logs should be avoided during normal operation.

Diagnostic events must use stable typed identifiers and versioned fields so future regulated traceability can build on them. Evaluation logs are support evidence, not a claimed compliant clinical audit trail.

---

# 22. Configuration

The application shall persist useful configuration including:

- last selected camera
- display preferences
- processing parameters
- presets
- save location
- installation-level camera orientation

Configuration should be versioned so future releases can migrate old settings.

Platform paths are resolved through a tested path adapter using `QStandardPaths` where appropriate. On Windows, per-user preferences and logs live beneath `%LOCALAPPDATA%\Lumora\Config` and `%LOCALAPPDATA%\Lumora\Logs`; captures default to `%USERPROFILE%\Pictures\Lumora\Captures`. Installation camera identity/orientation lives beneath `%PROGRAMDATA%\Lumora\Config`, writable by administrators and read-only to ordinary operators. Linux development uses corresponding XDG user paths and an injectable system-profile path for tests. Capture storage remains configurable behind a storage adapter.

---

# 23. Testing requirements

The application must be designed for automated testing.

Required test categories:

### Unit tests

Individual processing operations.

### Reference-image tests

Known input frame + processing configuration should produce reproducible expected output.

Unpacking, normalization, window/level, inversion, right-angle orientation, display mapping, and Original capture require exact pixel equality on Linux/GCC and Windows/MSVC. CLAHE, Gaussian/median denoise, and sharpening use documented, reviewed per-algorithm tolerances because supported OpenCV builds may differ. Reference outputs are also recorded on the designated Windows release build; tolerance changes require explicit review.

### Pipeline tests

Frames flow correctly through acquisition, processing and display layers.

### Simulated camera tests

Run without physical hardware.

The PGM P5 sequence reader shall accept maximum sample values from 1 through 65535, including common 255, 1023, 4095, and 65535 values. It preserves numeric samples, records the header maximum as the declared source maximum, derives the required storage/valid bits, and normalizes against that declared maximum rather than observed per-frame extrema.

### Configuration tests

Persistence and migration.

### Lifecycle tests

Repeated:

- connect
- disconnect
- start
- stop
- restart acquisition

### Error tests

Simulated camera/network/storage failure.

### Performance tests

Measure:

- processing duration
- maximum sustainable FPS
- memory usage
- frame latency

Ordinary CI runs functional benchmark smoke tests without workstation-dependent timing gates. Sustained timing acceptance is performed on the designated Windows reference workstation, selected before enhancement-performance acceptance.

### Stability tests

Continuous execution over extended periods.

---

# 24. Hardware testing

Separate hardware-in-the-loop testing shall be performed using:

- target Windows workstation
- production Ethernet adapter
- actual Basler camera
- expected camera resolution
- expected pixel depth
- expected frame rate

Testing through a virtual machine is not considered sufficient for final GigE performance validation.

---

# 25. Non-functional requirements

## Reliability

The program must be suitable for long-running workstation operation.

## Responsiveness

The GUI must remain responsive even during camera or storage failures.

## Maintainability

Subsystems should communicate through clear interfaces.

## Extensibility

New cameras and processing stages should be addable without rewriting unrelated components.

## Observability

Problems experienced in deployment should be diagnosable using logs and system metrics.

## Determinism

Processing should produce consistent output for the same frame and configuration.

## Language and localization

The evaluation release UI and operator documentation are English only. User-visible strings must nevertheless be externalized through Qt translation facilities from the foundation milestone so future localization does not require UI rewrites.

---

# 26. Security

The evaluation release is local-only and must not acquire or store real patient data. Only phantoms, test objects, synthetic frames, and properly anonymized sequences are permitted.

It requires:

- no cloud account
- no remote server
- no remote camera access
- no internet dependency during normal operation

Networking should initially be restricted to camera communication.

Lumora-owned source code is licensed under Apache-2.0. Distributed evaluation builds dynamically link only Qt modules available under LGPL-compatible terms and include the required license texts, notices, source/relink information, dependency inventory, and software bill of materials. A qualified license review is required before external distribution.

---

# 27. Out of scope — v1

The following are intentionally excluded:

- PACS
- DICOM networking
- patient management
- modality worklists
- cloud storage
- remote access
- accounts/authentication
- automatic/self-update
- web application
- mobile application
- AI diagnosis
- AI image generation
- automatic anomaly detection
- image segmentation
- measurements
- annotations
- X-ray generator control
- exposure control
- safety interlocks
- hardware movement control

The exclusions above apply to the evaluation release. They must be revisited and approved before any clinical release.

---

# 28. Future capabilities

Potential future releases may introduce:

- DICOM image creation
- DCMTK
- PACS integration
- modality worklist
- patient/study workflow
- detector calibration
- flat-field calibration wizard
- GPU processing
- additional cameras
- multi-monitor workstation layouts
- audit trail
- user roles
- regulated software lifecycle controls

A future clinical release additionally requires an Egypt-specific regulatory strategy confirmed by qualified counsel, a defined legal manufacturer, risk management, clinical and usability validation, cybersecurity lifecycle controls, and a documented clinical-release authorization gate.

These features should influence architecture where appropriate but must not add unnecessary complexity to the evaluation implementation.

---

# 29. Product delivery phases

These product-level phases communicate scope only. The authoritative execution sequence and acceptance identifiers are the fourteen plans in `docs/superpowers/plans/` and the architecture specification; these phase numbers must not be used as implementation milestone IDs.

## Phase 1 — Foundation

Deliver:

- repository structure
- CMake project
- Qt application shell
- logging
- configuration foundation
- automated tests
- Linux developer build process
- Windows/MSVC CI build process

Success:

Application launches reliably and automated tests run.

---

## Phase 2 — Simulated acquisition

Deliver:

- generic camera abstraction
- simulated camera
- frame representation
- acquisition worker
- bounded buffering

Success:

Recorded frames can be replayed continuously without physical hardware.

---

## Phase 3 — Live viewer

Deliver:

- image rendering
- fit-to-screen
- zoom
- pan
- FPS display
- start/pause

Success:

Simulated frames display smoothly at target FPS.

---

## Phase 4 — Basler camera

Deliver:

- pylon adapter
- camera discovery
- connect/disconnect
- acquisition
- error handling
- basic camera metadata

Success:

Real Basler GigE camera produces stable live images on Windows.

---

## Phase 5 — High-bit-depth pipeline

Deliver:

- native high-bit-depth handling
- window/level
- display conversion

Success:

12/16-bit source images remain high precision until display mapping.

---

## Phase 6 — Enhancement pipeline

Deliver:

- contrast
- brightness
- gamma
- CLAHE
- denoise
- sharpen
- inversion
- processing pipeline architecture

Success:

Processing can run continuously at target FPS without unacceptable latency.

---

## Phase 7 — Workstation UI

Deliver:

- clean imaging interface
- Original / Enhanced
- image controls
- presets
- fullscreen
- comparison workflow

Success:

Application can be comfortably operated without developer knowledge.

---

## Phase 8 — Capture

Deliver:

- original snapshot
- enhanced snapshot
- metadata
- configurable destination

Success:

Captured frames and processing configuration can be recovered reliably.

---

## Phase 9 — Reliability

Deliver:

- camera disconnect recovery
- acquisition timeout handling
- disk errors
- lifecycle stress tests
- performance instrumentation
- memory/stability testing

Success:

Application can run continuously for extended periods without crashes, increasing latency or significant memory growth.

---

## Phase 10 — Windows distribution

Deliver:

- release build
- per-machine WiX v4 MSI for administrator-managed installation under Program Files
- signed `Lumora-Setup.exe` WiX Burn bootstrapper that checks and installs the official VC++ x64 redistributable before the MSI
- dependency packaging
- separately managed official pylon Runtime prerequisite until its redistribution/version policy is approved
- release configuration
- version information
- Apache-2.0 `LICENSE`, third-party `NOTICE`, source/relink information for dynamically linked LGPL-compatible Qt modules, and an SBOM
- signed external-distribution artifacts and checksums

Success:

An administrator can install, repair, upgrade, and uninstall Lumora on a clean Windows 11 x64 workstation, while a non-administrator operator can run it and write only per-user data. The MSI remains separately available from the bootstrapper and uninstall preserves user captures, configuration, and logs.

Developer and internal evaluation builds may be unsigned and must be clearly identified. Any artifact distributed outside the development team, and every future clinical release, must be Authenticode-signed and timestamped. Signing identity availability is a release gate, not a prerequisite for core development.

---

# 30. Initial performance targets

Initial targets should be validated against actual hardware.

Starting objectives:

- ≥30 FPS camera acquisition where supported by hardware
- ≥30 FPS live display
- no continuously growing frame queues
- live-display latency preferably below 100 ms
- responsive UI throughout acquisition
- stable operation for multi-hour sessions
- no significant long-term memory growth

Exact performance requirements should be adjusted after testing the selected camera, resolution and workstation.

---

# 31. Product principle

The central engineering rule for the project is:

**Preserve the original image, process non-destructively, and prioritize a fresh low-latency frame over displaying every frame.**

The application should behave like reliable imaging equipment, not like a generic camera demo.
