# Lumora requirements and implementation index

**Approved clarification baseline:** 2026-09-04

This directory is the implementation authority for Lumora. The numbered milestones build an open-source engineering/evaluation release only. It is **not for clinical use**, must not acquire or store real patient data, and does not claim Egyptian Drug Authority registration or diagnostic validation.

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

M1-M5 may proceed without a selected physical camera. Simulator implementation may continue if a later hardware gate is unresolved, but the gated milestone cannot be accepted.
