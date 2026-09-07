# Windows 11 compatibility build

Windows 11 x64 with MSVC is Lumora's production, packaging, and final Basler hardware-acceptance platform. Build official Windows artifacts natively on Windows; Linux-to-Windows cross-compilation is not supported.

## Prerequisites

- Windows 11 x64.
- Visual Studio 2022 17.8 or newer with the **Desktop development with C++** workload, the current Windows 11 SDK, and CMake tools.
- CMake 3.28 or newer.
- Git and PowerShell 7.

Run the commands below in PowerShell from a Visual Studio x64 developer environment.

## Bootstrap the pinned vcpkg baseline

```powershell
git clone https://github.com/microsoft/vcpkg.git .tools/vcpkg
git -C .tools/vcpkg checkout 04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4
.tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
$LumoraVcpkgRoot = (Resolve-Path .tools\vcpkg).Path
```

The committed manifest baseline and overrides pin Qt, OpenCV, GoogleTest, and spdlog. `.tools/` is ignored. CI uses the same vcpkg commit and the supported `files` binary cache under `out/vcpkg-cache`, restored/saved by a pinned `actions/cache` action rather than the removed `x-gha` backend. An optional local cache can be enabled for the current PowerShell session:

```powershell
$env:VCPKG_BINARY_SOURCES = "clear;files,$PWD\out\vcpkg-cache,readwrite"
New-Item -ItemType Directory -Force out\vcpkg-cache | Out-Null
```

CI cache keys separate operating systems and architectures; vcpkg checks package ABI compatibility before reuse. Completed dependency packages are saved even if a later build/test step fails, unless the run is cancelled. Cache-service failures do not suppress build/test failures. See [vcpkg binary caching](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching) and [GitHub cache actions](https://github.com/actions/cache).

## Configure, build, and test

Debug simulator build:

```powershell
cmake --preset windows-msvc-debug-sim --fresh "-DCMAKE_TOOLCHAIN_FILE=$LumoraVcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmake --build --preset windows-msvc-debug-sim --parallel
ctest --preset windows-msvc-debug-sim --output-on-failure -LE hardware
```

Release simulator build:

```powershell
cmake --preset windows-msvc-release-sim --fresh "-DCMAKE_TOOLCHAIN_FILE=$LumoraVcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmake --build --preset windows-msvc-release-sim --parallel
ctest --preset windows-msvc-release-sim --output-on-failure -LE hardware
```

These presets force `LUMORA_ENABLE_BASLER=OFF`, so a pylon installation is not required. They use the dynamic `x64-windows` triplet to preserve the approved Qt LGPL linking boundary. The Qt smoke test uses the `minimal` platform plugin and does not open an interactive desktop window.

CTest sets the smoke test's `QT_QPA_PLATFORM_PLUGIN_PATH` from the imported `Qt6::QMinimalIntegrationPlugin` target, selecting the installed plugin directory for the active Debug or Release configuration. Copying Qt DLLs beside the executable alone does not provide this plugin path. This test-only environment does not replace deploying the Qt runtime and the `windows` platform plugin with the future Windows installer.

Basler presets are reserved for the later camera-adapter milestone. Machine-specific pylon paths belong in ignored `CMakeUserPresets.json`, never in the shared presets.

## Launch the desktop application

After the Release simulator build, launch the normal application from the repository root:

```powershell
cmake --build --preset windows-msvc-release-sim --target lumora_app --parallel
cmake -E env QT_QPA_PLATFORM=windows `
  "QT_QPA_PLATFORM_PLUGIN_PATH=$PWD/out/vcpkg_installed/x64-windows/Qt6/plugins/platforms" `
  out/build/windows-msvc-release-sim/src/Release/lumora_app.exe
```

For Debug, use `windows-msvc-debug-sim`, `src/Debug/`, and `x64-windows/debug/Qt6/plugins/platforms`. Use the matching installed plugin directory if the dependency prefix differs. These are developer-build commands; native Windows execution and appearance remain subject to the deferred validation below.

The application starts in **Waiting for image**. Select **SIM-LIVE**, click **Connect**, click **Apply** and review the settings, then **Confirm** and **Start**. After M7 integration this streams synthetic Mono12 data in U16 storage through the production acquisition, processing and presentation pipeline, with Gray8 conversion only at the display boundary. The evaluation banner remains visible. No physical camera is connected by this composition.

Viewer **Pause / Live** freezes/resumes presentation while acquisition continues. Camera **Stop** stops acquisition while retaining the connected device; **Disconnect** closes it. If saved identity and requested settings match, the application may offer **Resume Live** on a later launch; it still requires an explicit click. Check these controls and the paused/stale indications in the normal application when collecting M4/M5 Windows acceptance evidence. See the [Linux launch guide](build-linux.md#launch-the-desktop-application) for the shared startup, saved-preference and source-reset behavior.

An older M5 Mono8 saved request differs from M7 and leaves startup disconnected. Explicitly select **SIM-LIVE**, **Connect**, **Apply** and review, then **Confirm** and **Start**. The [M7 record](../architecture/milestones/m07-preflight.md) records passing Linux/Windows simulator CI separately from the still-required native Windows checks.

## M4 synthetic viewer and remaining Windows checks

Test-enabled builds also produce a separate non-shipping synthetic viewer harness. It supplements the normal application checks above. From the repository root, after the Release build:

```powershell
cmake --build --preset windows-msvc-release-sim --target lumora_viewer_harness --parallel
cmake -E env QT_QPA_PLATFORM=windows `
  "QT_QPA_PLATFORM_PLUGIN_PATH=$PWD/out/vcpkg_installed/x64-windows/Qt6/plugins/platforms" `
  out/build/windows-msvc-release-sim/src/Release/lumora_viewer_harness.exe --start
```

For Debug, use `windows-msvc-debug-sim`, `src/Debug/`, and `x64-windows/debug/Qt6/plugins/platforms`. Use the matching installed plugin directory if the dependency prefix differs. These are developer-build launch instructions, not installer deployment. They require native Windows verification; Linux runs cannot establish Windows appearance or compatibility.

The harness starts a synthetic 640x480 Mono8 moving bar only with `--start`. Without it the view waits. Close the window to stop/join the worker. Exercise Pause/Live, Fit, 100%, zoom, pan, and resize, and confirm the persistent evaluation banner and paused/stale indications. Recoverable 50 ms retrieval timeouts are visibly reported and counted; configured 30 FPS is not a measured throughput guarantee. No real patient data or physical camera is permitted in this evaluation harness.

The separate 600-second Release stress gate is opt-in:

```powershell
cmake --preset windows-msvc-release-sim -DLUMORA_ENABLE_STRESS_TESTS=ON
cmake --build --preset windows-msvc-release-sim --parallel
ctest --preset windows-msvc-release-sim --output-on-failure --no-tests=error -L stress
cmake --preset windows-msvc-release-sim -DLUMORA_ENABLE_STRESS_TESTS=OFF
```

Restore OFF even after failure/interruption. Save the source SHA, duration, platform, result, publication/paint counts, and timeout count from `out/build/windows-msvc-release-sim/Testing/Temporary/LastTest.log`. Normal CI does not implicitly run this stress case.

### Run Windows stress from Linux through GitHub Actions

The **Windows Simulator** workflow adds the 600-second Release stress test only on an explicit manual dispatch. Push and pull-request runs keep their short Debug/Release suites; `LUMORA_ENABLE_STRESS_TESTS` still defaults OFF. A manual run first passes those same suites, enables stress, builds the integration target, and invokes `cmake/RunStress.cmake`. Cleanup attempts to restore OFF even after a failed stress run. Cancellation or runner loss can prevent cleanup/upload; a later run uses a fresh hosted workspace.

After the workflow change is merged into `main`, run from Linux with GitHub CLI repository write access:

```bash
gh workflow run windows-simulator.yml --repo m4bulmagd/Lumora --ref main
gh run list --repo m4bulmagd/Lumora --workflow windows-simulator.yml --event workflow_dispatch --limit 5
```

Select the new run ID from that list, inspect its source SHA, and download its evidence after completion (replace `RUN_ID` below with that ID):

```bash
gh run view RUN_ID --repo m4bulmagd/Lumora
gh run download RUN_ID --repo m4bulmagd/Lumora --dir out/windows-stress-RUN_ID
```

The workflow must exist on the default branch before it can be manually dispatched; see [GitHub's manual workflow instructions](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow). Manual runs have a separate concurrency group, so routine push/PR runs do not cancel them. A second manual run on the same ref supersedes the first.

The artifact `windows-stress-<SHA>-<run ID>-<attempt>` is retained for 30 days and includes:

- `metadata.txt`: checked-out source SHA, test preset, host OS, CMake/runner image versions, run/attempt IDs, UTC start/end, and CTest exit code.
- `ctest.log`: verbose CTest output, including publication/paint counts, maximum retained bundles, timeout count, and elapsed time. The viewer prints accumulated counts before its final assertions, so those assertion failures retain the counts too.
- `results.xml`: JUnit test identities, outcomes, output, and durations.

Failure still fails the job; available evidence uploads even on failure. An early configure/build failure may have no stress artifact, and process termination before the summary may leave no counts: inspect the job log and do not treat it as a completed stress run. Missing stress registration is an error, never a pass. The local collector requires a new output directory so an earlier evidence set cannot be overwritten. Preserve downloaded acceptance evidence before the artifact expires.

The hosted runner is `windows-2022` (Windows Server), not a Windows 11 desktop. This workflow can supply the Windows/MSVC automated stress evidence, but **does not satisfy native Windows 11 visual/DPI, installer, hardware, or clinical acceptance**. Creating the workflow is not evidence that its Windows stress run passed.

### Native Windows 11 visual checks

Before M4 acceptance, record native Windows 11 visual checks at logical client sizes 1280x720 and 1920x1080 with 100%, 125%, 150%, and 200% display scaling on a sufficiently large screen. Record physical and effective logical geometry, screenshots, keyboard controls, exact logical-pixel 100% behavior, and safety-text visibility. Record insufficient-workspace cases without silently clipping safety indications. The headless tests and stress run do not replace these manual checks. Installation/upgrade validation remains M13; hardware acceptance remains M14.

## Clean generated builds

This command removes only generated CMake build trees:

```powershell
cmake -E remove_directory out/build
```

To preserve other presets, replace `out/build` with one explicit preset directory. Do not delete per-user Lumora configuration, logs, or captures as part of a build clean.
