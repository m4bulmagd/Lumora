# Lumora

Lumora is an open-source real-time X-ray camera imaging workstation. The repository includes the implemented native C++/Qt foundation, immutable frames and bounded buffers, camera API and simulator, viewer, independent live pipeline, and the reviewed M7/M8 processing work, alongside the reviewed product requirements, architecture, roadmap, and implementation plans.

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

M7 is merged through [PR #8](https://github.com/m4bulmagd/Lumora/pull/8) as `d191097`: validated processing contracts, deterministic U16 normalization, window/level, terminal Gray8 mapping and a pooled live engine. SIM-LIVE exercises Mono12 in U16. All task/final reviews passed; local and CI Linux Debug/Release each passed 41/41 checks, and Windows/MSVC passed 40/40 per configuration at the PR head. A tests-disabled application build passed. See the [M7 execution record](docs/architecture/milestones/m07-preflight.md); native Windows checks, preceding deferred gates and separate milestone acceptance remain open.

M8 Task 1 is reviewed and merged through [PR #10](https://github.com/m4bulmagd/Lumora/pull/10) as `d6f94e1`: standalone U16 brightness/contrast, gamma and inversion, with cached gamma tables and exhaustive references. Linux Debug/Release each passed 42/42 checks, Windows/MSVC each passed 41/41, and the tests-disabled Release app built successfully. The [tone-stage record](docs/architecture/milestones/m08-tone-stages.md) retains evidence and decisions. The later composition work is recorded separately below. [Task 2 CLAHE](docs/architecture/milestones/m08-clahe.md) merged through [PR #11](https://github.com/m4bulmagd/Lumora/pull/11) as `c0c4102` after task/final review and Linux/Windows Debug/Release CI passed. [Tasks 3–5](docs/architecture/milestones/m08-continuation.md) merged through [PR #12](https://github.com/m4bulmagd/Lumora/pull/12) as `17d1ede`: denoise/sharpen, shared orientation, complete prepared execution, resource admission, and Original-only recovery with Retry. Shared Standard, exact references and optional benchmark/reference/allocation tools are source-reviewed and verified, including normal allocation proofs and Linux/Windows Debug/Release CI. The first [Window/Level latency optimization](docs/architecture/milestones/m08-window-level-performance.md) is merged through [PR #13](https://github.com/m4bulmagd/Lumora/pull/13) as `fd4c604`, with passing review, normal equivalence/allocation evidence and Linux/Windows Debug/Release PR CI. The subsequent [CLAHE/sharpening performance experiments](docs/architecture/milestones/m08-clahe-sharpen-performance.md) retain exact sharpening/Gaussian optimizations and a smaller sharpening intermediate; CLAHE trials showed no reliable gain. Clean `603f49c` measures 4.85/3.24 FPS at 2048 for identity/flip-and-rotation. The subsequent [exact CPU pipeline optimization](docs/architecture/milestones/m08-cpu-pipeline-performance.md) adds validated identity copies, blocked orientation, prepared parallel CLAHE/filters and exact SSE2 sharpening. On the same Linux host, clean `7ae53f8` measures **11.97/8.13 FPS** with **97.5/147.2 ms P95** for the complete 2048 Standard Mono16 v2 workload, about 2.5× the preceding throughput. The subsequent [exact Mono12 normalization](docs/architecture/milestones/m08-mono12-normalization-performance.md) measures **13.92/9.56 FPS** with **85.4/115.6 ms P95** for the full 2048 Mono12 v3 pipeline at clean `717acc8`, up from its matched clean `1148036` **8.71/6.54 FPS** baseline. Alternating kernel repeats show a **4.70×** normalization speedup with identical pixels. These Mono12 and Mono16 workloads have separate baselines and row sets. The combined performance series merged through [PR #14](https://github.com/m4bulmagd/Lumora/pull/14) as `5de569f1341f9b2d63c4a4656e4a90fbec5bf9ac`; [Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34286991145) passed 54/54 checks and native X11 1/1 in Debug and Release at verified head `3393a9d`, and [Windows/MSVC PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34286990978) passed 54/54 in Debug and 54/54 in Release at that head. The final head adds only a GCC 13 test const-reference correction; production source is unchanged from `717acc8`. Hosted CI verifies integrated source compatibility, while the 30 FPS target, designated Windows reference/workstation/freshness and performance evidence, native Windows 11 visual/DPI and packaging checks, and the M6 camera/NIC profile remain open. Next profile display mapping/orientation and CLAHE against the faster Mono12 baseline.

The normal application on `main` starts in `Waiting for image`, with no silent stream. Follow the [desktop launch guide](docs/development/build-linux.md#launch-the-desktop-application): select `SIM-LIVE`, Connect, Apply and review, Confirm, then Start to see synthetic live video. Viewer Pause freezes presentation while acquisition continues. No physical camera is connected by this simulator composition.

SIM-LIVE produces Mono12 samples with 12 valid bits (0–4095), unpacked into a UInt16 application buffer. Lumora normalizes them across the 0–65535 `CanonicalU16` working range, retains high-depth raw and processed data, and maps Original and Enhanced presentation frames to Gray8; scaling the working range does not add captured sensor information. The evidence benchmark instead defaults to synthetic Mono16 schema v2 with eleven rows per size. Explicit `--source-format mono12` selects schema v3 and the two full Standard orientation rows per size. The recorded 2048 results do not describe the configured 640×480 simulator rate, and the physical-camera format and transport packing remain unknown pending the M6 hardware profile.

The software is still an engineering/evaluation build. It is **not for clinical use**, must not be used for diagnosis, and must not acquire or store real patient data.

## Build

Use the platform-specific contract:

- [Linux development build](docs/development/build-linux.md)
- [Windows 11 compatibility build](docs/development/build-windows.md)
- [Requirements traceability](docs/architecture/requirements-traceability.md)

Both build guides pin vcpkg to the repository's manifest baseline. Basler hardware support is intentionally absent from simulator builds and tests.
