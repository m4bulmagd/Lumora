# Lumora

Lumora is an open-source real-time X-ray camera imaging workstation. The repository includes the M1 native C++/Qt foundation, M2 immutable frames and bounded buffers, and M3 camera API and simulator alongside the reviewed product requirements, architecture, roadmap, and implementation plans.

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

Linux Debug and Release verification is recorded in the [M3 verification record](docs/architecture/milestones/m03-camera-api-simulator.md). Windows/MSVC verification remains pending, so M3 is implemented but not formally accepted. The application still opens the foundation shell; the simulated live viewer is M4, not yet implemented. See the [M4 preflight](docs/architecture/milestones/m04-preflight.md) for the remaining entry checks.

The software is still an engineering/evaluation build. It is **not for clinical use**, must not be used for diagnosis, and must not acquire or store real patient data.

## Build

Use the platform-specific contract:

- [Linux development build](docs/development/build-linux.md)
- [Windows 11 compatibility build](docs/development/build-windows.md)
- [Requirements traceability](docs/architecture/requirements-traceability.md)

Both build guides pin vcpkg to the repository's manifest baseline. Basler hardware support is intentionally absent from simulator builds and tests.
