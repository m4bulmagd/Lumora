# Linux development build

Linux x86-64 with GCC is Lumora's daily development and simulator-test environment. It is not the platform used to produce official Windows artifacts.

## Prerequisites

- A current x86-64 Linux distribution; Ubuntu 24.04 LTS or newer is the reference family.
- GCC 12 or newer with C++20 support.
- CMake 3.28 or newer and Ninja.
- Git, curl, zip, unzip, tar, `pkg-config`, autoconf, autoconf-archive, automake, and libtool for vcpkg ports.
- X11/XCB and XKB development packages for Qt's `xcb` desktop plugin, plus an installed font family.
- A graphical X11 session, or a Wayland session with XWayland and `DISPLAY` set, for interactive launching. Native Wayland is not enabled in this build.

On Debian/Ubuntu, install the toolchain with:

```bash
sudo apt-get update
sudo apt-get install --yes --no-install-recommends \
  build-essential cmake ninja-build git curl zip unzip tar pkg-config \
  autoconf autoconf-archive automake libtool \
  libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev \
  libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxrender-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-image0-dev \
  libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render0-dev \
  libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
  libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev libxcb-xinput-dev libxcb-xkb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev xvfb xauth fonts-dejavu-core
```

`xvfb` and `xauth` provide the virtual X11 display used by the optional desktop smoke test and Linux CI; they are not needed to launch in an existing desktop session. Install your distribution's `xwayland` package if your Wayland session does not already provide XWayland. See [Qt's Linux requirements](https://doc.qt.io/qt-6/linux-requirements.html).

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

The simulator presets force `LUMORA_ENABLE_BASLER=OFF`; pylon is neither searched for nor linked. The test presets set `QT_QPA_PLATFORM=minimal`, allowing the existing Qt tests to run without a display server. The reduced Qt build supplies `minimal` for these tests and, on Linux only, `xcb` for desktop windows. Linux also enables Fontconfig for system font discovery. These platform-qualified features do not change the Windows dependency selection or the pinned Qt version.

CTest also selects the smoke test's plugin directory from `Qt6::QMinimalIntegrationPlugin` for the active Debug or Release configuration, without requiring a machine-wide Qt plugin-path setting.

## Launch the desktop shell

After building, run this from the repository root in a graphical Linux session:

```bash
cmake --build --preset linux-gcc-debug-sim --target run-lumora
```

Use `linux-gcc-release-sim` for Release. This development-only target selects `xcb` and the matching Debug/Release Qt plugin directory for this process; it does not require a global Qt environment setting. It runs until you close the window. Launching the binary directly may require an explicit platform-plugin path with a vcpkg build.

At this stage the workstation layout displays the mandatory `EVALUATION — NOT FOR CLINICAL USE` banner and starts in `Waiting for image`, with Pause and image controls disabled until a presenter supplies a frame. Moving simulated video is the next M4 task; a successful desktop launch does not mean the live viewer is complete or clinically validated.

If Qt reports that `xcb` cannot be found, reconfigure with the pinned vcpkg toolchain after installing the prerequisites above. Existing `widgets`-only dependency installations must be rebuilt; pointing `CMAKE_PREFIX_PATH` at an older headless Qt installation is not sufficient. If `xcb` is found but cannot connect to a display, run inside your graphical session and check `DISPLAY` and XWayland availability. Do not use `QT_QPA_PLATFORM=minimal` to assess desktop visibility.

## Optional desktop smoke test

The desktop test exercises the real `xcb` backend and waits for the window to be exposed by the display server before checking the evaluation banner and closing. It is separate from the existing headless tests and is registered only when explicitly enabled:

```bash
cmake --preset linux-gcc-debug-sim --fresh \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.tools/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DLUMORA_TEST_LINUX_DESKTOP=ON
cmake --build --preset linux-gcc-debug-sim --parallel
ctest --preset linux-gcc-debug-sim --output-on-failure -LE 'hardware|desktop'
xvfb-run -a ctest --preset linux-gcc-debug-sim --output-on-failure -L desktop --no-tests=error
```

Repeat with `linux-gcc-release-sim` for Release. Linux CI performs both checks in each configuration. Xvfb verifies desktop-plugin loading and window exposure, not appearance on a physical monitor; use `run-lumora` for the manual visual check.

## Clean generated builds

This removes only generated CMake build trees. It does not remove source, the vcpkg checkout, installed dependencies, logs, configuration, or captures:

```bash
cmake -E remove_directory out/build
```

To preserve other presets, replace `out/build` with one explicit preset directory.
