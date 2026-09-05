# Linux development build

Linux x86-64 with GCC is Lumora's daily development and simulator-test environment. It is not the platform used to produce official Windows artifacts.

## Prerequisites

- A current x86-64 Linux distribution; Ubuntu 24.04 LTS or newer is the reference family.
- GCC 12 or newer with C++20 support.
- CMake 3.28 or newer and Ninja.
- Git, curl, zip, unzip, tar, `pkg-config`, autoconf, autoconf-archive, automake, and libtool for vcpkg ports.

On Debian/Ubuntu, install the toolchain with:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build git curl zip unzip tar pkg-config autoconf autoconf-archive automake libtool
```

## Bootstrap the pinned vcpkg baseline

The committed `vcpkg.json` baseline and overrides are the dependency lock. Do not develop against arbitrary system Qt, OpenCV, GoogleTest, or spdlog installations.

From the repository root:

```bash
git clone https://github.com/microsoft/vcpkg.git .tools/vcpkg
git -C .tools/vcpkg checkout 04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4
.tools/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

`.tools/` is ignored. CI checks out the same vcpkg commit. Binary packages use vcpkg's supported `files` cache under `out/vcpkg-cache`; the workflows restore/save that directory with a pinned `actions/cache` action. They do not use the removed `x-gha` backend. A developer may opt into the same local cache before configuring:

```bash
export VCPKG_BINARY_SOURCES="clear;files,$PWD/out/vcpkg-cache,readwrite"
mkdir -p out/vcpkg-cache
```

CI cache keys separate operating systems and architectures. Each run saves a new archive snapshot, including completed packages if a later build or test step fails. vcpkg still checks package ABI compatibility after restoration. Caches contain dependency packages only, not installed application data or captures; cache-service failures do not suppress build/test failures. See [vcpkg binary caching](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching) and [GitHub cache actions](https://github.com/actions/cache).

## Configure, build, and test

Debug simulator build:

```bash
cmake --preset linux-gcc-debug-sim --fresh \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset linux-gcc-debug-sim --parallel
ctest --preset linux-gcc-debug-sim --output-on-failure -LE hardware
```

Release simulator build:

```bash
cmake --preset linux-gcc-release-sim --fresh \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset linux-gcc-release-sim --parallel
ctest --preset linux-gcc-release-sim --output-on-failure -LE hardware
```

The simulator presets force `LUMORA_ENABLE_BASLER=OFF`; pylon is neither searched for nor linked. The test presets set `QT_QPA_PLATFORM=minimal`, allowing the Qt window smoke test to run without a display server. The reduced Qt build intentionally supplies the `minimal` plugin rather than the separate `offscreen` plugin.

To launch the shell in a graphical Linux session:

```bash
out/build/linux-gcc-debug-sim/src/lumora_app
```

## Clean generated builds

This removes only generated CMake build trees. It does not remove source, the vcpkg checkout, installed dependencies, logs, configuration, or captures:

```bash
cmake -E remove_directory out/build
```

To preserve other presets, replace `out/build` with one explicit preset directory.
