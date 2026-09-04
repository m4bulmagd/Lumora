# Milestone 14 Basler Hardware Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Validate only the non-clinical evaluation release candidate on the approved Basler camera, reference Windows workstation, and Ethernet adapter with documented continuous free-running payload, latency, image integrity, lifecycle, recovery, and soak evidence.

**Architecture:** Hardware configuration is supplied through an uncommitted local profile matching a committed schema/example. Test executables consume public application contracts and write immutable JSON reports; they do not introduce production-only test hooks.

**Tech Stack:** Windows 11 x64, Basler pylon Runtime/SDK, production camera/NIC/workstation, CTest hardware suite, diagnostics metrics, PowerShell orchestration.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Final GigE validation occurs on the production workstation/NIC and physical camera, not a virtual machine.
- Hardware tests are opt-in and carry the `hardware` CTest label.
- Do not change NIC, firewall, camera firmware, or camera user sets automatically.
- Hardware mode must fit measured Ethernet payload capacity; no test claims 2K Mono12/30 over standard 1 GigE when infeasible.
- P95 acquisition-to-presentation latency target is below 100 ms at the approved feasible mode.
- Hardware acceptance includes 100 lifecycle cycles and at least four hours continuous streaming.
- Tests use phantoms, test objects, or other non-patient sources only; passing this plan never authorizes clinical diagnosis.

---

### Task 1: Hardware profile schema and payload budget calculator

**Files:**
- Create: `tests/hardware/hardware-profile.schema.json`
- Create: `tests/hardware/hardware-profile.example.json`
- Create: `tests/hardware/HardwareProfile.cpp`
- Create: `tests/hardware/HardwareProfileTests.cpp`
- Create: `tools/hardware/payload-budget.cpp`
- Modify: `.gitignore`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: local `LUMORA_HARDWARE_PROFILE` path containing expected camera identity, image mode, link speed, and acceptance targets.
- Produces: validated `HardwareProfile`, calculated raw/payload bandwidth, utilization warning, and committed example without real deployment secrets.

- [ ] **Step 1: Define and test schema**

Require camera serial/model/sensor/firmware, source-format stable encoding/name/packing/alignment/valid bits/sample maximum, continuous free-running acquisition mode, ROI/offset, requested FPS, exposure/gain mode/value, NIC name/driver/link Mbps, packet size if configured externally, workstation/display identifier, latency target, soak duration, and capture root. Reject missing or future-schema fields clearly. This profile must match the hard gate approved before Milestone 6.

- [ ] **Step 2: Implement exact bandwidth calculations**

Calculate unpacked memory bandwidth and wire payload from pixels, bits per pixel, FPS, line/frame overhead when known, and configured link rate. Report payload Mbps, utilization percentage, and headroom; warn above 80% and reject a requested mode above theoretical link payload.

- [ ] **Step 3: Verify the nominal constraint**

Add a test proving 2048x2048x12x30 exceeds 1,000,000,000 bits/s before protocol overhead. Add passing examples using reduced ROI/FPS/bit depth.

- [ ] **Step 4: Protect local identity/profile**

Ignore `tests/hardware/hardware-profile.local.json` and artifact directories. The example uses unmistakably synthetic serial/path values.

- [ ] **Step 5: Commit hardware profile tooling**

```powershell
git add tests/hardware tools/hardware .gitignore src/CMakeLists.txt
git commit -m "test(hardware): define camera profile and payload budget"
```

### Task 2: Capability and frame-integrity qualification

**Files:**
- Create: `tests/hardware/BaslerQualificationTests.cpp`
- Create: `tools/hardware/run-qualification.ps1`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `HardwareProfile`, Basler provider/device contracts, raw pool, and diagnostics recorder.
- Produces: discovery/capability/applied-setting/frame-integrity JSON evidence.

- [ ] **Step 1: Add identity/capability assertions**

Discover exact serial, verify expected manufacturer/model/transport, record firmware, and compare requested format/ROI/FPS/exposure/gain with advertised ranges/increments.

- [ ] **Step 2: Apply and read back the approved mode**

Open, apply complete continuous free-running configuration with triggering disabled, record requested and actual values, start, and acquire at least 3,000 frames. Fail if the adapter silently substitutes an unsupported mode, source format, or identity.

- [ ] **Step 3: Validate every acquired frame**

Check dimensions, stride, payload, storage type, valid bits, monotonic host/frame IDs, sample range, and optional device IDs/timestamps. Record camera-reported skips, grab failures, and timeouts.

- [ ] **Step 4: Qualify each approved pixel format/ROI profile**

Run a named profile for every mode intended for operators. Stop between mode changes, rebuild pools after checked memory preflight, and record actual throughput.

- [ ] **Step 5: Emit signed-off qualification report**

Write application/pylon/firmware versions, profile, capabilities, results, metrics, and logs reference to `artifacts/hardware/<UTC>/qualification.json`; preserve artifacts outside Git.

- [ ] **Step 6: Commit qualification suite**

```powershell
git add tests/hardware/BaslerQualificationTests.cpp tools/hardware/run-qualification.ps1 tests/CMakeLists.txt
git commit -m "test(hardware): qualify Basler modes and frame integrity"
```

### Task 3: End-to-end performance and freshness test

**Files:**
- Create: `tests/hardware/LivePerformanceTests.cpp`
- Create: `tools/hardware/run-performance.ps1`
- Create: `docs/operations/gige-performance-checklist.md`

**Interfaces:**
- Consumes: full release pipeline, approved profile, Standard preset, metrics/process health, and UI presentation acknowledgements.
- Produces: acquisition/processing/display FPS, processing median/P95, latency current/P95, drops by boundary, CPU/memory, and queue/pool high-water evidence.

- [ ] **Step 1: Document passive network checks**

Record NIC model/driver/link speed, camera link identity, OS power plan, packet-size setting, competing traffic, and pylon transport statistics. The application reports recommendations but does not change them.

- [ ] **Step 2: Run 15-minute warm-up and 30-minute measurement**

Use the approved hardware mode and Standard preset. Exercise periodic zoom/pan, Original/Enhanced/Compare switches, slider changes, pause/resume, and snapshot capture while measuring actual unique presented frames. Confirm the evaluation banner, active orientation, and PAUSED timestamp/age remain visible throughout applicable normal/fullscreen/Compare cases.

- [ ] **Step 3: Assert sustainable-mode gates**

Acquisition and display meet agreed profile FPS within 5%; Standard processing P95 fits the frame period; live latency P95 is below 100 ms; latest-slot depths remain one; no pool exhaustions occur in the sustainable run; capture does not create a growing latency trend.

- [ ] **Step 4: Run deliberate overload case**

Use the simulator at 2048x2048 U16 60 FPS or an expensive pipeline configuration. Assert drops are categorized, queues do not grow, and the UI remains responsive. If no new frame is successfully presented within the deadline, `STALE IMAGE / NOT LIVE` must appear and clear on recovery; never weaken this or bounded freshness to satisfy timing. This overload is not a hardware link throughput claim.

- [ ] **Step 5: Preserve performance report and commit tooling**

```powershell
git add tests/hardware/LivePerformanceTests.cpp tools/hardware/run-performance.ps1 docs/operations/gige-performance-checklist.md
git commit -m "test(hardware): measure end-to-end live performance"
```

### Task 4: Repeated lifecycle and physical recovery qualification

**Files:**
- Create: `tests/hardware/BaslerLifecycleTests.cpp`
- Create: `tools/hardware/run-recovery-protocol.ps1`
- Create: `docs/operations/camera-recovery-test-protocol.md`

**Interfaces:**
- Consumes: Basler live pipeline, reconnect policy, human-confirmed cable/power actions, and hardware profile.
- Produces: 100-cycle lifecycle evidence and timestamped recovery state/metric/log sequences.

- [ ] **Step 1: Run 100 automated software lifecycle cycles**

Each cycle discovers the exact camera, opens, applies settings, starts, receives at least 30 valid frames, stops, closes, and verifies the device can be immediately reopened. Record handle/thread/pool counts every cycle and fail on a sustained growth trend.

- [ ] **Step 2: Execute cable-removal protocol 20 times**

The script prompts the tester to disconnect/reconnect at randomized streaming times, records confirmation timestamps, and verifies removal detection, UI responsiveness, persistent stale/disconnected indication, expected reconnect attempts, exact identity restoration, settings/orientation restoration, and clearing only after resumed fresh presentation.

- [ ] **Step 3: Execute camera power-cycle protocol**

Perform at least five controlled power cycles. Verify the device is destroyed after removal, no resource remains locked, reconnect targets the same serial, and manual Disconnect cancels an in-progress retry.

- [ ] **Step 4: Exercise network/timeouts without unsafe automation**

Use only approved lab procedures to interrupt Ethernet or camera power. Do not disable production adapters or firewall rules from the test executable. Record acquisition timeout thresholds and error-to-reconnect timing.

- [ ] **Step 5: Review recovery evidence**

Every event must include state sequence, attempt number/delay, error/native code, camera identity, time to next valid displayed frame, and final resource counts. Any freeze, wrong-camera connection, or exhausted retry without actionable Error fails acceptance.

- [ ] **Step 6: Commit recovery protocol/suite**

```powershell
git add tests/hardware/BaslerLifecycleTests.cpp tools/hardware/run-recovery-protocol.ps1 docs/operations/camera-recovery-test-protocol.md
git commit -m "test(hardware): qualify camera lifecycle and recovery"
```

### Task 5: Four-hour hardware soak and acceptance report

**Files:**
- Create: `tools/hardware/run-hardware-soak.ps1`
- Create: `docs/release/hardware-acceptance-template.md`
- Create: `docs/release/release-readiness-checklist.md`

**Interfaces:**
- Consumes: release candidate, validated dependency/version/signature-status manifest, approved hardware profile, soak runner, performance and recovery reports.
- Produces: final hardware acceptance record with pass/fail disposition and traceability to all Milestone 14 gates.

- [ ] **Step 1: Configure the four-hour scenario**

Run approved camera mode and Standard preset continuously; capture Both every 15 minutes; switch display modes every 10 minutes; pause/resume hourly; adjust and restore window/level twice; record health every 30 seconds.

- [ ] **Step 2: Execute and archive evidence**

Archive profile, release manifest, command line, JSON metrics, structured logs, captures with manifests, process-health samples, and tester notes. Every capture/report states `EVALUATION — NOT FOR CLINICAL USE`. Do not place imaging artifacts or real serial data in Git.

- [ ] **Step 3: Evaluate fixed acceptance criteria**

Require no crash/deadlock/UI freeze, no continuously increasing queue or memory/handle trend after warm-up, valid original capture round-trips, no unexplained acquisition failures, P95 latency below target, expected FPS, and clean shutdown with zero pool leases.

- [ ] **Step 4: Complete release readiness review**

Cross-reference simulator soak, clean-machine Setup/MSI lifecycle, dependency/license/SBOM record, camera qualification, performance, recovery, and hardware soak. Each failure has an owner and blocks the evaluation release until fixed and rerun. The review explicitly states that it provides no EDA registration, clinical validation, or patient-use authorization.

- [ ] **Step 5: Commit templates and orchestration**

```powershell
git add tools/hardware/run-hardware-soak.ps1 docs/release/hardware-acceptance-template.md docs/release/release-readiness-checklist.md
git commit -m "docs(release): define hardware acceptance evidence"
```

## Milestone 14 acceptance gate

- [ ] Approved camera mode fits measured link capacity and all acquired frames validate.
- [ ] Sustainable mode meets FPS and P95 latency targets with fixed queue depths.
- [ ] One hundred lifecycle cycles and physical cable/power recovery protocols pass.
- [ ] Four-hour hardware soak shows no crash, freeze, resource leak trend, or accumulating latency.
- [ ] Final report references exact application, dependency, pylon, camera firmware, workstation, and NIC versions.
- [ ] Evaluation status appears in UI/captures/reports, all testing uses non-patient sources, and the report disclaims clinical authorization.
