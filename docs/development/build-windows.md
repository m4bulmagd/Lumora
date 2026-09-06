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

## M4 synthetic viewer and remaining Windows checks

The normal `lumora_app` still opens in `Waiting for image`; production live-pipeline composition remains M5. Test-enabled builds also produce a non-shipping synthetic viewer harness. From the repository root, after the Release build above:

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

Before M4 acceptance, record native Windows 11 visual checks at logical client sizes 1280x720 and 1920x1080 with 100%, 125%, 150%, and 200% display scaling on a sufficiently large screen. Record physical and effective logical geometry, screenshots, keyboard controls, exact logical-pixel 100% behavior, and safety-text visibility. Record insufficient-workspace cases without silently clipping safety indications. The headless tests and stress run do not replace these manual checks. Installation/upgrade validation remains M13; hardware acceptance remains M14.

## Clean generated builds

This command removes only generated CMake build trees:

```powershell
cmake -E remove_directory out/build
```

To preserve other presets, replace `out/build` with one explicit preset directory. Do not delete per-user Lumora configuration, logs, or captures as part of a build clean.
