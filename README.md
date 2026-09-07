# Lumora

Lumora is an open-source real-time X-ray camera imaging workstation. The repository includes the M1 native C++/Qt foundation, M2 immutable frames and bounded buffers, M3 camera API and simulator, M4 viewer, and M5 independent live pipeline with startup controls and saved preferences, alongside the reviewed product requirements, architecture, roadmap, and implementation plans.

The numbered milestones target an engineering/evaluation release that is **not for clinical use** and must not acquire or store real patient data. A future clinical diagnostic release for Egypt is a separate gated program.

Start with:

- [Current progress and remaining gates](docs/PROGRESS.md)
- [Product requirements](prd.md)
- [Requirements and implementation index](docs/superpowers/README.md)
- [Architecture specification](docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md)
- [Fourteen-milestone roadmap](docs/superpowers/plans/2026-04-25-xray-imaging-workstation-roadmap.md)

Daily simulator development is planned for Linux/GCC with mandatory Linux and Windows/MSVC CI. Windows 11 remains the official installation, packaging, and final Basler hardware-acceptance platform.

## Current implementation

Milestones 1–3 provide:

- C++20 CMake targets with pinned vcpkg dependencies;
- Linux/GCC and Windows/MSVC simulator presets that keep Basler pylon disabled;
- a resizable Qt Widgets shell with a non-removable evaluation banner;
- typed results and errors, versioned rotating logs, and versioned atomic JSON configuration;
- immutable frame bundles, fixed-capacity buffer pools, latest-value exchanges, and separate monotonic/UTC clocks;
- a vendor-neutral camera API, deterministic generated patterns, strict PGM sequence replay, pacing, and scripted faults;
- headless unit and UI smoke tests.

M3 is accepted for engineering/evaluation: Linux/GCC and Windows/MSVC CI each passed all 18 CTest entries in both Debug and Release at the exact merged source commit. Run links and platform details are in the [M3 verification record](docs/architecture/milestones/m03-camera-api-simulator.md).

M4 Tasks 1–4 (viewport geometry, grayscale rendering, workstation layout, controls, safety indications, latest-frame presenter, and separate synthetic moving-video harness) are merged with passing Linux and Windows Debug/Release CI. The separate evaluation harness remains available using the [Linux instructions](docs/development/build-linux.md#m4-synthetic-live-viewer-harness). See the [M4 execution evidence and outstanding Windows gates](docs/architecture/milestones/m04-preflight.md). M4 is not yet accepted; Windows 11 installation and hardware acceptance remain later milestones.

M5 Tasks 1–2 add a Qt-free camera state machine, bounded priority command mailbox and independently owned acquisition worker. They are merged with passing Linux/Windows Debug/Release CI at `7295fb9`; see the [acquisition checkpoint](docs/architecture/milestones/m05-acquisition-worker.md#merged-cross-platform-checkpoint-2026-09-07). [Task 3](docs/architecture/milestones/m05-processing-worker.md) adds Mono8 processing and its newest-frame worker, merged through PR #6 as `2031848` after matching cross-platform CI passed at `ccabaae`. [Task 4](docs/architecture/milestones/m05-startup-preferences.md) adds saved preferences and startup controls. [Task 5](docs/architecture/milestones/m05-live-integration.md) connects the production simulator, workers and viewer, merged with Task 4 through [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) as `f01b408`. All recorded review findings are resolved; local verification and post-merge Linux/Windows Debug/Release CI passed at that merged checkpoint. See the [integrated evidence](docs/architecture/milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07). Native Windows 11 visual/DPI checks and separate full M5 acceptance remain open.

M7 is implemented on the unmerged `feat/m07-high-bit-depth` branch: validated processing contracts, deterministic U16 normalization, window/level, terminal Gray8 mapping and a pooled live engine. SIM-LIVE now exercises Mono12 in U16. All four task reviews passed; local Debug/Release each passed 41/41 checks, and a tests-disabled application build passed. Whole-branch review is pending. See the [M7 execution record](docs/architecture/milestones/m07-preflight.md); Windows verification and milestone acceptance remain pending.

The normal application on `main` starts in `Waiting for image`, with no silent stream. Follow the [desktop launch guide](docs/development/build-linux.md#launch-the-desktop-application): select `SIM-LIVE`, Connect, Apply and review, Confirm, then Start to see synthetic live video. Viewer Pause freezes presentation while acquisition continues. No physical camera is connected by this simulator composition.

The software is still an engineering/evaluation build. It is **not for clinical use**, must not be used for diagnosis, and must not acquire or store real patient data.

## Build

Use the platform-specific contract:

- [Linux development build](docs/development/build-linux.md)
- [Windows 11 compatibility build](docs/development/build-windows.md)
- [Requirements traceability](docs/architecture/requirements-traceability.md)

Both build guides pin vcpkg to the repository's manifest baseline. Basler hardware support is intentionally absent from simulator builds and tests.
