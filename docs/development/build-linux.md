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

## Launch the desktop application

After building, run this from the repository root in a graphical Linux session:

```bash
cmake --build --preset linux-gcc-debug-sim --target run-lumora
```

Use `linux-gcc-release-sim` for Release. This development-only target selects `xcb` and the matching Debug/Release Qt plugin directory for this process; it does not require a global Qt environment setting. It runs until you close the window. Launching the binary directly may require an explicit platform-plugin path with a vcpkg build.

The M5 development application displays the mandatory `EVALUATION — NOT FOR CLINICAL USE` banner and starts in `Waiting for image`, with Pause and image controls disabled until a frame is actually presented. Its production composition supplies a synthetic 640x480 full-range Mono8 moving bar at 30 FPS; no physical camera or patient data is involved. See the [Task 5 checkpoint](../architecture/milestones/m05-live-integration.md) for current verification/review status. A successful launch does not establish milestone acceptance or clinical validation.

For a first run:

1. Wait for discovery, then explicitly select the `SIM-LIVE` simulator in Camera startup.
2. Click **Connect**. The camera opens idle; there is no live video yet.
3. Click **Apply** and review the requested and actual settings.
4. Click **Confirm** to acknowledge the actual settings.
5. Click **Start** to begin live synthetic video.

The viewer's **Pause / Live** button freezes/resumes the displayed image while acquisition continues. Camera **Stop** stops acquisition while retaining the connected device; **Start** can restart its still-confirmed settings. **Disconnect** closes the device and cancels pending startup/Resume intent. The last image can remain as context with the existing paused/stale indication. Explicit **Refresh** after Disconnect binds a fresh waiting source and clears that contextual image; it discovers cameras but does not reconnect. In Error, use Retry only when a desired camera identity is retained, or Disconnect then Refresh to restart discovery.

Preferences are saved in the background after successful confirmation. On a later run, an unchanged saved simulator identity may connect idle for capability checks and offer **Resume Live**. That startup action still needs an explicit click and rechecks Apply/readback before streaming. Capability or actual-setting changes require review, Confirm and Start; a load/save warning is not durable confirmation. **Resume Live** is distinct from the viewer's **Live** button. No startup path silently streams.

The sidebar scrolls when needed, keeping the evaluation banner and viewer Pause/Live control outside the startup scroll area. Use a graphical session to assess actual appearance; the M4 harness below remains a separate non-shipping test tool.

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

## M4 synthetic live-viewer harness

With the normal test-enabled build, launch the non-shipping harness from the repository root in a graphical session:

```bash
cmake --build --preset linux-gcc-debug-sim --target lumora_viewer_harness --parallel 3
cmake -E env QT_QPA_PLATFORM=xcb \
  "QT_QPA_PLATFORM_PLUGIN_PATH=$PWD/out/vcpkg_installed/x64-linux-dynamic/debug/Qt6/plugins/platforms" \
  out/build/linux-gcc-debug-sim/src/lumora_viewer_harness --start
```

For Release, use `linux-gcc-release-sim` and remove `debug/` from the plugin path. If reusing an existing pinned installation through `CMAKE_PREFIX_PATH`, use that installation's matching plugin directory instead. Omitting `--start` leaves the viewer waiting; no acquisition begins automatically. Close the window to stop and join the simulator worker.

The feed is a synthetic 640x480 Mono8 moving bar, configured for 30 FPS. Try Pause/Live, Fit, 100%, zoom, pan, and resize. Pause freezes the displayed image while acquisition continues. The evaluation banner must stay visible. This harness is built only with `LUMORA_BUILD_TESTS=ON`, is not installed, and is not linked into `lumora_app`. It does not access a physical X-ray system or patient data.

The 50 ms whole-retrieval budget can produce recoverable timeouts under load, especially in Debug. Those exact typed timeouts are retained and counted, reported in the title and stderr, and retried; terminal errors remain failures. A timeout count is not evidence of achieving 30 FPS or the later latency/performance targets.

## Opt-in M4 Release stress test

Normal CTest runs include the short integration case, not the ten-minute run. After configuring the Release build above:

```bash
cmake --preset linux-gcc-release-sim -DLUMORA_ENABLE_STRESS_TESTS=ON
cmake --build --preset linux-gcc-release-sim --parallel 3
ctest --preset linux-gcc-release-sim --output-on-failure --no-tests=error -L stress
cmake --preset linux-gcc-release-sim -DLUMORA_ENABLE_STRESS_TESTS=OFF
```

Restore the option to OFF even if the test fails or is interrupted. Record the source SHA, platform, duration, publication/paint counts, timeout count, and result from `out/build/linux-gcc-release-sim/Testing/Temporary/LastTest.log`. The test exercises 600 seconds of simulator/viewer interaction under `minimal`; it does not verify physical scan-out, Windows DPI, hardware throughput, or clinical suitability. M4 also requires matching Windows CI, Windows Release stress, and native Windows 11 visual/scaling evidence.

## Clean generated builds

This removes only generated CMake build trees. It does not remove source, the vcpkg checkout, installed dependencies, logs, configuration, or captures:

```bash
cmake -E remove_directory out/build
```

To preserve other presets, replace `out/build` with one explicit preset directory.
