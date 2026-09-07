# Lumora requirements and implementation index

**Approved clarification baseline:** 2026-09-04

This directory is the implementation authority for Lumora. The numbered milestones build an open-source engineering/evaluation release only. It is **not for clinical use**, must not acquire or store real patient data, and does not claim Egyptian Drug Authority registration or diagnostic validation.

See the [current progress summary](../PROGRESS.md) for merged implementation, verified CI, repository cleanup and remaining gates. It summarizes evidence without changing the document authority below.

## Document authority

All documents must be satisfied together:

1. `prd.md` defines product intent, scope, and release boundaries.
2. `specs/2026-04-25-xray-imaging-workstation-design.md` defines architecture and system-level acceptance.
3. `plans/2026-04-25-xray-imaging-workstation-roadmap.md` defines the authoritative fourteen-milestone order and stable cross-milestone contracts.
4. The fourteen `plans/2026-04-25-m*.md` files define test-first implementation tasks.

The PRD's ten product delivery phases are not implementation milestone IDs. If a future edit creates a conflict, do not infer a compromise: stop, update the PRD/spec decision first, then propagate it to the roadmap and affected milestone plans.

## Approved implementation baseline

| Area | Binding decision |
|---|---|
| Release | Every numbered milestone produces `EVALUATION — NOT FOR CLINICAL USE`; use only phantoms, test objects, synthetic data, or properly anonymized sequences. |
| Future clinical use | Clinical diagnosis in Egypt is a separate release program requiring qualified Egyptian regulatory input and explicit EDA, intended-purpose/classification, QMS, risk, clinical/usability, cybersecurity, patient-data, diagnostic-display, traceability, and release-authorization decisions. |
| Platforms | Daily development and simulator testing use Ubuntu Linux/GCC. Linux/GCC and Windows/MSVC simulator CI are required at every milestone. Official packaging and final camera/NIC acceptance run on Windows 11; official Windows artifacts are built on Windows, not cross-compiled. |
| Dependencies | Pin Qt/OpenCV/GTest/spdlog with vcpkg manifest mode and CI binary caching. Keep pylon optional/external; store machine paths only in ignored `CMakeUserPresets.json`. |
| Acquisition | Continuous free-running live video is the initial mode. Triggering is reserved. Exact camera/sensor/firmware/NIC/formats/mode are a hard gate before M6. |
| Pixel contract | Preserve immutable numeric sensor samples. `SourcePixelFormat` records stable encoding/name, valid bits, declared sample maximum, packing, alignment, and U8/U16 application storage. Camera maxima normally equal `2^validBits-1`; PGM replay uses its header maximum. `DisplayFrame` is format-aware; evaluation rendering uses Gray8. |
| Processing | Fixed versioned order: Normalize -> Window/Level -> Brightness/Contrast -> Gamma -> CLAHE -> Denoise -> Sharpen -> Invert. Operators may enable/disable permitted stages and adjust values but cannot reorder them. |
| Original and orientation | `Original (display mapped)` uses deterministic normalization, active window/level, and the shared installation orientation for viewing. Flip/rotation are administrator-managed, confirmed stopped-state camera-profile settings applied equally to Original and Enhanced presentation. Stored Original and enhanced U16 remain native-orientation. |
| Live-state safety | PAUSED always shows a persistent overlay, frozen-frame timestamp, and increasing age. Live frames older than `max(500 ms, 3 expected frame periods)` show persistent `STALE IMAGE / NOT LIVE`. Both remain visible in Compare and fullscreen. |
| Capture | Capture the exact displayed/paused bundle. Original Mono8 uses lossless 8-bit PNG; Mono10/12/16 numeric samples use lossless 16-bit PNG with format metadata. Enhanced U16 is native-orientation; preview U8 matches the oriented screen. |
| Startup | First run requires explicit camera selection, configuration confirmation, and Start. Later runs may offer one-click Resume Live only for unchanged identity/capabilities; never auto-stream. Manual Disconnect disables reconnect until explicit Connect. |
| UI | Product name is Lumora. UI/operator documentation are English only but all user-visible strings are localization-ready. Record UI is absent in v1. |
| Data paths | Windows operator preferences/logs: `%LOCALAPPDATA%\Lumora\Config` and `Logs`; captures: `%USERPROFILE%\Pictures\Lumora\Captures`. Installation camera identity/orientation is machine-wide under `%PROGRAMDATA%\Lumora\Config`, writable by administrators and readable by operators. Linux uses corresponding XDG/user and injected system-profile paths. |
| Distribution | Apache-2.0 Lumora code; dynamically linked LGPL-compatible Qt only; include LICENSE, NOTICE, source/relink information, dependency inventory, and SBOM. Admin-managed per-machine WiX MSI plus primary `Lumora-Setup.exe` Burn bootstrapper for the official VC++ x64 redistributable. Pylon Runtime remains a separately managed vendor prerequisite. |
| Signing | Unsigned internal evaluation artifacts must be unmistakably labeled and hashed. External evaluation and all future clinical artifacts must be Authenticode signed and timestamped. |

## Open hard gates

These are intentional deferred inputs, not contradictions and not permission to guess:

| Gate | Due | Required record |
|---|---|---|
| Camera integration | Before M6 starts | Exact Basler model/sensor/firmware, NIC/driver/link, capability-reported source formats, and feasible continuous free-running ROI/FPS/exposure/gain mode. |
| Performance acceptance | Before M8 acceptance | Designated Windows workstation CPU/GPU/RAM/display/driver profile and approved benchmark conditions. |
| Pylon distribution | Before an external Basler package | Approved pylon Runtime version, detection method, deployment responsibility, and written redistribution decision. Default is a separate vendor prerequisite. |
| Open-source distribution | Before any external package | Dependency-license review, LGPL relink/source materials, third-party notices, and SBOM approval. |
| External signing | Before any external package | Organization signing identity/certificate custody, timestamp service, protected pipeline, and release authority. |
| Clinical release | Before enabling a clinical build class | Separately approved Egypt-specific regulatory and clinical program with traceability and release authority. |

M1-M5 may proceed without a selected physical camera. The [2026-09-07 continuation](../architecture/milestones/m07-preflight.md) permits simulator-only M7 implementation while M4/M5 native Windows checks and M6 remain pending. The owner's subsequent approval extends development to [M8 Task 1](../architecture/milestones/m08-tone-stages.md). These exceptions do not authorize M6 implementation without its profile or acceptance of the deferred milestones.

## M4 verification and scoped deferral (2026-09-07)

The [M3 acceptance record](../architecture/milestones/m03-camera-api-simulator.md) records passing Linux/GCC and Windows/MSVC Debug/Release CI at the exact merged source commit. M4 Tasks 1–4 are merged with matching cross-platform CI, including Task 4's latest-frame presenter and non-shipping simulator harness. Windows Release stress and matching Linux/Windows Debug/Release CI passed at `6c054a7`; native Windows 11 visual/DPI checks and full M4 acceptance remain pending. The [scoped deferral record](../architecture/milestones/m04-deferred-windows-validation.md) permits M5 development with those manual checks still required before Windows release acceptance. The [Windows guide](../development/build-windows.md#run-windows-stress-from-linux-through-github-actions) documents opt-in automated stress execution from Linux. Normal application live-pipeline composition remains M5.

The [M4 preflight](../architecture/milestones/m04-preflight.md) and [M4 plan](plans/2026-04-25-m04-minimal-live-viewer.md) carry forward the reviewed implementation details: exact whole-image Fit outside manual zoom limits, logical-pixel 100%, the installed `minimal` Qt headless plugin, source-session reset, and paint-completion-based freshness. Product semantics are recorded first in PRD §19.7–19.8 and design §7/§8.2/§11.2. The recorded M4-to-M5 exception and subsequent [simulator-only M7 continuation](../architecture/milestones/m07-preflight.md) change the normal development entry sequence; Windows visual, installer, hardware, and other release acceptance requirements are not waived.

## M5 documentation preflight (2026-09-06)

The [M5 preflight](../architecture/milestones/m05-preflight.md) and [M5 plan](plans/2026-04-25-m05-independent-live-pipeline.md) clarify the priority mailbox, camera error/state table, session and frame lifetime, minimal explicit startup with saved preferences, deterministic slow-processing tests, and CMake/test registration. M9 extends the startup interface/preferences; M12 adds scheduled recovery to M5's failure classification and interim operator-only retry. The design's M5 section records this staging; the product release scope and final recovery requirements are unchanged.

The user approved M5 development on 2026-09-07 under the recorded M4 manual-validation deferral; [Task 1 (state machine and command mailbox)](../architecture/milestones/m05-camera-state-mailbox.md) retains its initial local implementation/review evidence and links to subsequent merged verification. M4 is not fully accepted. The preflight is a contract, not M5 test evidence; matching Windows CI and affected native UI checks remain required for acceptance.

The subsequent Task 2 approval adds the [dedicated acquisition worker checkpoint](../architecture/milestones/m05-acquisition-worker.md): implementation and independent task/Standards/Spec reviews are complete. Tasks 1–2 were merged and pushed by separate authorization, with matching Linux/Windows Debug/Release CI at `7295fb9`; see the [exact-source evidence](../architecture/milestones/m05-acquisition-worker.md#merged-cross-platform-checkpoint-2026-09-07).

The owner subsequently approved [Task 3: frame processor and processing worker](../architecture/milestones/m05-processing-worker.md), whose final review fixes passed local verification and exact-head Linux/Windows Debug/Release CI at `ccabaae`. [PR #6](https://github.com/m4bulmagd/Lumora/pull/6) is merged as `2031848`. The separately authorized [Task 4](../architecture/milestones/m05-startup-preferences.md) and [Task 5](../architecture/milestones/m05-live-integration.md) continuation supplies saved preferences, explicit startup controls and visible production simulator live composition. All recorded task and final Standards/Spec review findings are resolved; [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) merged as `f01b408` after its own exact-head checks passed. The [merged integration checkpoint](../architecture/milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) records passing post-merge local and Linux/Windows Debug/Release CI, plus subsequent branch cleanup. Native Windows 11 checks and full M5 acceptance remain open. The [launch guide](../development/build-linux.md#launch-the-desktop-application) describes the normal application's synthetic-video workflow.


## M7 simulator continuation (2026-09-07)

The [M7 execution record](../architecture/milestones/m07-preflight.md) records the owner-authorized simulator continuation while M4/M5 native Windows checks and M6 hardware work remain pending. Tasks 1–4 are implemented and reviewed, then merged through [PR #8](https://github.com/m4bulmagd/Lumora/pull/8) as `d191097`: configuration validation, U16 normalization, window/level, terminal Gray8 mapping and the pooled live engine. Local and CI Linux Debug/Release each passed 41/41 checks; Windows/MSVC passed 40/40 per configuration at the exact PR head. The tests-OFF/Basler-OFF Release app and whole-branch review passed. Preceding deferred gates and separate acceptance remain open.

## M8 Task 1 simulator continuation (2026-09-07)

The owner approved the [bounded tone-stage continuation](../architecture/milestones/m08-tone-stages.md) after M7: U16 brightness/contrast, immutable cached gamma and inversion with exhaustive tests. Implementation `246a73a` passed independent task review, Linux Debug/Release 42/42 application checks and a tests-disabled Release build. Final review remains pending. Live activation/composition, cross-activation cache retention, remaining algorithms, whole-frame allocation and Windows performance evidence belong to later work; no milestone acceptance is claimed.
