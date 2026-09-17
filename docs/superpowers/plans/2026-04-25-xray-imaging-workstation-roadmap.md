# Lumora X-Ray Imaging Workstation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver an open-source Lumora engineering/evaluation workstation that is explicitly not for clinical use, develops and tests on Linux, installs on Windows 11, acquires Basler or simulated continuous monochrome video, preserves original sensor values, enhances it through a bounded low-latency pipeline, and captures traceable snapshots.

**Architecture:** Build a modular C++20 monolith from independently tested libraries. Frames cross camera, processing, UI, and capture boundaries only through immutable values and fixed-capacity exchanges; Qt and pylon remain isolated adapters.

**Tech Stack:** Ubuntu Linux x64/GCC for daily development, Windows 11 x64/MSVC for production, C++20, CMake, pinned vcpkg manifests, dynamically linked LGPL-compatible Qt 6.11.1 (Qt Quick/QML workstation and Multimedia adapters), optional external Basler pylon SDK/runtime, OpenCV C++, GoogleTest/CTest, spdlog, and JSON configuration.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

**Historical UI direction checkpoint (2026-09-13):** [ADR 0001](../../adr/0001-qt-quick-qml-frontend.md) records Qt Quick/QML as the selected frontend. The [migration proposal](../specs/2026-09-13-qt-quick-qml-migration-design.md) and [handoff](../../PROGRESS.md#next-session-qt-quickqml) continue from [local main at `e2de210`](../../architecture/milestones/m09-camera-controls.md#local-main-integration), which includes completed M9 Task 4E. The integration has not been pushed and has no PR or hosted CI result. The next session should plan and verify the initial QML integration before continuing M9 Task 5 fullscreen/sidebar/preferences on that frontend. The proposal's targets and stages are unimplemented; existing build commands still describe Widgets. Milestone order and external acceptance gates remain unchanged.

**Current UI and input update (2026-09-16):** the [QML migration](../../architecture/milestones/qml-only-workstation.md) and [layout work](../../architecture/milestones/qml-layout.md) supersede the historical UI checkpoint above. The owner also approved the [flexible video-source design](../specs/2026-09-16-video-sources-design.md) and [execution plan](2026-09-16-video-sources.md). This scoped extension reuses one active acquisition/processing/viewer session for SIM-LIVE, Qt OS cameras and explicit RTSP/RTSPS addresses. Media input is decoded to Mono8 with reported fixed modes up to 1920×1080; it does not claim native high-bit-depth sensor preservation for consumer color video. Source discovery, per-session authentication and the catalog are bounded additions to existing Apply/Confirm/Start and installation policy. See the [operator guide](../../development/video-sources.md) and [verification record](../../architecture/milestones/video-sources.md). Basler/pylon M6 and physical Windows M14 gates remain unchanged; ONVIF, proprietary BNC SDKs, full color and simultaneous feeds remain separate.

## Global Constraints

- Preserve original 8/10/12/16-bit monochrome sensor values; packed 10/12-bit input is stored losslessly in unsigned 16-bit form. The separately approved media adapter explicitly converts decoded color video to Mono8 and does not label it as native high-depth sensor data.
- Keep `DisplayFrame` format-aware; evaluation composition selects unsigned 8-bit at the terminal display-mapping boundary.
- Keep acquisition, processing, rendering, and capture independent; no SDK, processing, encoding, or disk work may execute on the Qt UI thread.
- Live raw and processed exchanges have capacity one and replace stale frames.
- Initial pools are 10 raw, 9 unsigned 16-bit processing, and 16 unsigned 8-bit display buffers; capture FIFO capacity is 4.
- Basler support is optional at configure time through `LUMORA_ENABLE_BASLER`; simulator-only builds require no pylon installation.
- Every milestone builds/tests the simulator on Linux/GCC and Windows/MSVC. Packaging and final camera/NIC acceptance are Windows-only; official Windows artifacts are built on Windows, not cross-compiled.
- A pinned vcpkg manifest supplies Qt/OpenCV/GTest/spdlog on both platforms; pylon remains external. Machine paths belong only in ignored `CMakeUserPresets.json`.
- CPU/OpenCV processing comes first; do not add CUDA, OpenCL, AI/ML, DICOM, PACS, real patient data, cloud, or generator controls.
- Numbered milestones produce only `EVALUATION — NOT FOR CLINICAL USE` builds. A future Egypt clinical release requires a separately approved EDA/regulatory, risk, clinical, usability, cybersecurity, diagnostic-display, and release-evidence program.
- Use one fixed, documented, versioned processing order. Operators may enable/disable permitted stages and adjust parameters but cannot reorder stages.
- Installation orientation is an administrator-managed stopped-state camera-profile setting applied equally to Original and Enhanced presentation; stored Original remains in native sensor orientation.
- Paused and stale images retain mandatory non-dismissible overlays in normal, Compare, and fullscreen views. The live stale deadline is `max(500 ms, 3 expected frame periods)`.
- Evaluation builds use only phantoms, test objects, synthetic images, or properly anonymized sequences.
- Lumora-owned code is Apache-2.0; external distribution includes license/notice/source-relink/SBOM evidence and only dynamically linked LGPL-compatible Qt modules.
- Use explicit ownership, RAII, stop-token shutdown, typed results, and no mutable global singleton or service locator.
- Normal tests require no camera; hardware tests carry the CTest label `hardware`.
- Standard processing must sustain the 2048 x 2048 unsigned 16-bit 30 FPS reference workload on the designated workstation; overload must remain bounded.
- Hardware P95 acquisition-to-presentation latency target is below 100 ms at a link-feasible camera mode.
- No Record UI ships in v1. Full sequence recording remains behind non-shipping harnesses until a separate recording specification and plan are approved and verified.

---

## 1. Plan set and dependency order

### C-arm planning extension (2026-09-17)

The owner requested plans for a Live/Reference workstation, durable stills, saved-image processing, cine and polished UI/UX. The [C-arm delivery plan](2026-09-17-carm-workstation-roadmap.md) defines CR-1–CR-8 alongside this roadmap. The [shared design](../specs/2026-09-17-carm-workstation-design.md) describes the interaction and ownership contracts; [capture/review](2026-09-17-carm-capture-review.md) and [UI/UX](2026-09-17-carm-workstation-ui.md) supply focused tasks.

M10 retains exact-frame capture ownership, with its planned UI integration updated for QML. The session catalog follows M10 as CR-1 work; independent reference, saved-image editing and dual-monitor layout follow as CR-2–CR-4. CR-5 recording and CR-6 event integration have separate specification/resource/hardware gates; CR-7 optional tools and CR-8 clinical interoperability remain separately scoped. CR identifiers do not renumber M1–M14. Preparing these documents does not authorize bypassing the prerequisite/acceptance rules below; an implementation kickoff must record its applicable entry conditions.

### Numbered milestone sequence

Execute the milestone plans in numeric order. Normally a later milestone begins only after the preceding milestone acceptance gate is recorded as passing. Recorded exceptions are [M4-to-M5 development with deferred native Windows 11 validation](../../architecture/milestones/m04-deferred-windows-validation.md) and the subsequent [simulator-only M7 development continuation](../../architecture/milestones/m07-preflight.md), both dated 2026-09-07. The subsequent [M8 implementation continuation](../../architecture/milestones/m08-continuation.md) permits its bounded simulator work. The owner approved the [M9 Task 1 preset continuation](../../architecture/milestones/m09-presets.md) on 2026-09-09 and the [bounded live-video extension](2026-09-16-video-sources.md) on 2026-09-16. These exceptions do not record milestone acceptance, waive release validation, or permit skipping other milestone gates.

Documentation-only preflight preparation may precede that gate; implementation requires acceptance or an explicit scoped exception above. The [M5 preflight](../../architecture/milestones/m05-preflight.md) refines the existing plan against M2–M4 without recording acceptance. M5 supplies minimal startup controls/persistence and failure classification; M9 expands the UI/preferences, and M12 adds timed automatic recovery to the same application contracts. M4 Windows stress passed at `6c054a7`; the native Windows 11 checks remain pending and mandatory before Windows release acceptance.

| Milestone | Plan | Depends on | Deliverable |
|---|---|---|---|
| 1 | `2026-04-25-m01-project-foundation.md` | Approved design | Reproducible Linux/Windows build, shell, tests, logging bootstrap |
| 2 | `2026-04-25-m02-core-frames-buffers.md` | M1 | Immutable frames, validation, result types, bounded memory/exchanges |
| 3 | `2026-04-25-m03-camera-api-simulator.md` | M2 | Vendor-neutral camera API and deterministic simulator |
| 4 | `2026-04-25-m04-minimal-live-viewer.md` | M3 | Responsive Qt viewer driven by simulated display frames |
| 5 | `2026-04-25-m05-independent-live-pipeline.md` | M4 | Acquisition/processing/presentation workers and freshness semantics |
| 6 | `2026-04-25-m06-basler-adapter.md` | M5 | Optional pylon adapter and physical camera streaming |
| 7 | `2026-04-25-m07-high-bit-depth-window-level.md` | M6 | Canonical high-depth path, original preservation, window/level |
| 8 | `2026-04-25-m08-modular-enhancements.md` | M7 | Complete CPU enhancement stages and reference benchmarks |
| 9 | `2026-04-25-m09-presets-workstation-ui.md` | M8 | Production workstation controls, presets, compare, fullscreen |
| 10 | `2026-04-25-m10-snapshot-capture.md` | M9 | Transactional Original/Processed/Both capture and metadata |
| 11 | `2026-04-25-m11-diagnostics-supportability.md` | M10 | Structured logs, full metrics, health UI, support documentation |
| 12 | `2026-04-25-m12-reliability-hardening.md` | M11 | Reconnection, fault matrix, lifecycle stress, simulator soak |
| 13 | `2026-04-25-m13-windows-distribution.md` | M12 | Reproducible deployable release and clean-machine installer |
| 14 | `2026-04-25-m14-hardware-acceptance.md` | M13 | Real-camera performance, recovery, and stability evidence |

## 2. Canonical configure, build, and test commands

Daily development commands from the repository root on Linux:

```bash
cmake --preset linux-gcc-debug-sim
cmake --build --preset linux-gcc-debug-sim --parallel
ctest --preset linux-gcc-debug-sim --output-on-failure
```

Linux Release simulator:

```bash
cmake --preset linux-gcc-release-sim
cmake --build --preset linux-gcc-release-sim --parallel
ctest --preset linux-gcc-release-sim --output-on-failure
```

Mandatory compatibility commands in a Visual Studio x64 developer shell or GitHub Actions Windows runner:

```powershell
cmake --preset windows-msvc-debug-sim
cmake --build --preset windows-msvc-debug-sim --parallel
ctest --preset windows-msvc-debug-sim --output-on-failure
```

Release simulator build:

```powershell
cmake --preset windows-msvc-release-sim
cmake --build --preset windows-msvc-release-sim --parallel
ctest --preset windows-msvc-release-sim --output-on-failure
```

Basler-enabled build after the pylon SDK is installed:

```powershell
cmake --preset windows-msvc-release-basler
cmake --build --preset windows-msvc-release-basler --parallel
ctest --preset windows-msvc-release-basler --output-on-failure -LE hardware
```

Hardware tests are always opt-in:

```powershell
ctest --preset windows-msvc-release-basler --output-on-failure -L hardware
```

## 3. Stable cross-milestone contracts

These names form the integration ledger. A milestone may extend a type but must not silently rename or reverse an established contract.

### Core

```cpp
template<class T, class E = Error> class Result;
enum class StorageType { UInt8, UInt16 };
enum class DisplayStorage { Gray8, Gray16 };
struct SourcePixelFormat; // stable encoding/name, valid bits, sample maximum, packing, alignment
struct ImageLayout;
class WritableBufferLease;
class SharedBuffer;
class BufferPool;
struct RawFrame;
struct ProcessedFrame;
struct DisplayFrame;
struct FrameBundle;
template<class T> class LatestValueSlot;
template<class T> class BoundedQueue;
class IClock;
```

### Camera

```cpp
struct CameraId;
struct CameraDescriptor;
struct CameraCapabilities;
struct CameraConfiguration;
struct AppliedCameraConfiguration;
class ICameraDevice;
class ICameraProvider;
```

### Processing

```cpp
enum class StageId;
using StageParameters = std::variant<NormalizationParameters,
    WindowLevelParameters, BrightnessContrastParameters, GammaParameters,
    ClaheParameters, DenoiseParameters, SharpenParameters,
    InvertParameters>;
struct Orientation;
struct StageDefinition;
struct PipelineDefinition;
class IProcessingStage;
class ProcessingPipeline;
class IFrameProcessor;
```

### Application and capture

```cpp
enum class CameraSessionState;
enum class ViewerState;
struct CameraCommand;
struct CameraStatusSnapshot;
struct StartupPreferences;
class CameraCommandMailbox;
class CameraSessionStateMachine;
class AcquisitionWorker;
class ProcessingWorker;
class LivePipeline;
enum class CaptureMode { Original, Processed, Both };
struct CaptureJob;
struct CaptureResult;
class ICaptureEncoder;
class ICaptureStore;
```

`CameraCommand` owns a variant payload and request ID; it is not a variant alias. Mailbox/worker/pipeline `post(CameraCommand)` returns `Result<void>` for admission (`camera_mailbox_full` or `cancelled` on rejection), while immutable status reports execution outcome. Session generation guards camera commands but is not a core frame ID. The application owns camera/processing lifecycle; `FramePresenter` owns viewer Pause/Resume and completed-paint freshness. `LivePipeline` must not acquire Qt or duplicate presenter state to implement viewer actions. The [M5 contracts](../../architecture/milestones/m05-preflight.md#resolved-implementation-contracts) define priority, revision guards, source-context acknowledgement, and shutdown ownership.

## 4. Repository-wide review gates

Every task ends with a focused test and commit. Every milestone ends with:

- [ ] Run the complete Linux/GCC and Windows/MSVC simulator CI presets.
- [ ] Run `cmake --build` for Debug and Release Linux simulator presets locally; require the Windows CI result before acceptance.
- [ ] Confirm `LUMORA_ENABLE_BASLER=OFF` does not search for or load pylon.
- [ ] Confirm evaluation UI/artifacts retain release, pause/stale, and orientation semantics affected by the milestone.
- [ ] Inspect `git diff --check` and the branch review diff.
- [ ] Update `docs/architecture/requirements-traceability.md` with implemented design sections and test names.
- [ ] Record measured timing or memory evidence when the milestone has a performance gate.
- [ ] Commit the milestone acceptance record separately from feature commits.

## 5. Scope control

The approved [video-source extension](2026-09-16-video-sources.md) permits explicit RTSP/RTSPS video ingestion through Qt Multimedia and OS-exposed capture devices. It does not permit remote workstation control, network-wide discovery, recording, multiple active feeds or proprietary capture-card SDK work. A BNC connector requires an appropriate digitizer; the generic adapter accepts it only when its driver exposes a supported OS camera.

If implementation discovers a requirement for DICOM, patient data, clinical diagnosis, X-ray generator interaction, remote access, AI, full recording, dark-frame/flat-field/bad-pixel calibration, temporal algorithms, compliant audit logging, automatic updating, multiple active cameras/monitors, or a GPU algorithm, stop that work and create a new approved design. Do not hide such behavior inside a camera, processing, or capture adapter.

If the target camera cannot meet the nominal payload, record the feasible combination of ROI, pixel format, packet settings, and FPS. Do not weaken bounded-latency or original-preservation requirements to compensate.

Before Milestone 6, record the exact camera model/sensor/firmware, NIC, supported formats, and free-running acquisition mode. Before Milestone 8 performance acceptance, record the designated Windows workstation. Before external distribution, record the signing identity and approve all license obligations.

## Task 1: Approve the ordered execution baseline

**Files:**
- Read: `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`
- Read: all fourteen milestone plan files listed above
- Create during execution: `docs/architecture/requirements-traceability.md`

**Interfaces:**
- Consumes: the approved design and the stable contract ledger in this roadmap.
- Produces: a reviewed execution order and a traceability table mapping each design section to milestone, task, and test.

- [ ] **Step 1: Initialize source control because the inspected directory has no Git repository**

```powershell
git init -b main
git add prd.md docs
git commit -m "docs: establish Lumora requirements and implementation design"
```

Expected: `git status --short --branch` reports branch `main` with no uncommitted documentation changes before application work begins.

- [ ] **Step 2: Create the traceability table header**

```markdown
| Design section | Requirement | Milestone/task | Verification | Status |
|---|---|---|---|---|
```

- [ ] **Step 3: Add one row for every numbered design section and each milestone acceptance criterion**

Expected: sections 1 through 20 are represented, and all 14 milestone acceptance blocks have a named verification command or test.

- [ ] **Step 4: Review cross-milestone contract names**

Run:

```powershell
rg -n "Result<|RawFrame|FrameBundle|ICameraDevice|PipelineDefinition|CaptureJob" docs/superpowers/plans
```

Expected: no conflicting spelling or alternate name for the same contract.

- [ ] **Step 5: Commit the roadmap baseline**

```powershell
git add docs/architecture/requirements-traceability.md
git commit -m "docs: add architecture traceability baseline"
```
