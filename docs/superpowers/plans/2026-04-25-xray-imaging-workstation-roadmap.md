# Lumora X-Ray Imaging Workstation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver an open-source Lumora engineering/evaluation workstation that is explicitly not for clinical use, develops and tests on Linux, installs on Windows 11, acquires Basler or simulated continuous monochrome video, preserves original sensor values, enhances it through a bounded low-latency pipeline, and captures traceable snapshots.

**Architecture:** Build a modular C++20 monolith from independently tested libraries. Frames cross camera, processing, UI, and capture boundaries only through immutable values and fixed-capacity exchanges; Qt and pylon remain isolated adapters.

**Tech Stack:** Ubuntu Linux x64/GCC for daily development, Windows 11 x64/MSVC for production, C++20, CMake, pinned vcpkg manifests, dynamically linked LGPL-compatible Qt 6 Widgets modules, optional external Basler pylon SDK/runtime, OpenCV C++, GoogleTest/CTest, spdlog, and JSON configuration.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- Preserve original 8/10/12/16-bit monochrome sensor values; packed 10/12-bit input is stored losslessly in unsigned 16-bit form.
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

Execute the milestone plans in numeric order. A later milestone begins only after the preceding milestone acceptance gate is recorded as passing.

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
