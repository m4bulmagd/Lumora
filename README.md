# Lumora

Lumora is an open-source real-time X-ray camera imaging workstation. The repository includes the M1 native C++/Qt foundation, M2 immutable frames and bounded buffers, M3 camera API and simulator, and the first three M4 viewer tasks alongside the reviewed product requirements, architecture, roadmap, and implementation plans.

The numbered milestones target an engineering/evaluation release that is **not for clinical use** and must not acquire or store real patient data. A future clinical diagnostic release for Egypt is a separate gated program.

Start with:

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

M4 Tasks 1–2 (viewport geometry and grayscale rendering) are merged with passing cross-platform CI. Task 3 adds the workstation layout, image controls, and explicit waiting/paused/stale indications; its full Linux Debug and Release suites pass locally, while its Windows CI and integration are pending. The application now opens this layout in `Waiting for image`: there is no connected simulator or live video yet. Task 4 adds the presenter and a non-shipping simulator harness. See the [M4 preflight and execution evidence](docs/architecture/milestones/m04-preflight.md). M4 acceptance, Windows 11 installation, and hardware acceptance remain outstanding.

The software is still an engineering/evaluation build. It is **not for clinical use**, must not be used for diagnosis, and must not acquire or store real patient data.

## Build

Use the platform-specific contract:

- [Linux development build](docs/development/build-linux.md)
- [Windows 11 compatibility build](docs/development/build-windows.md)
- [Requirements traceability](docs/architecture/requirements-traceability.md)

Both build guides pin vcpkg to the repository's manifest baseline. Basler hardware support is intentionally absent from simulator builds and tests.
