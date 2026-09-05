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

Basler presets are reserved for the later camera-adapter milestone. Machine-specific pylon paths belong in ignored `CMakeUserPresets.json`, never in the shared presets.

## Clean generated builds

This command removes only generated CMake build trees:

```powershell
cmake -E remove_directory out/build
```

To preserve other presets, replace `out/build` with one explicit preset directory. Do not delete per-user Lumora configuration, logs, or captures as part of a build clean.
