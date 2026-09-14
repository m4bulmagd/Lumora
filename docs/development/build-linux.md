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

The commands below build the current Widgets workstation. An opt-in [QML SIM-LIVE pilot](#optional-qml-sim-live-pilot) and a separate [renderer experiment](#qml-renderer-experiment) are also available. Both frontends reuse shared C++ workstation and presentation policy; see the [migration plan](../superpowers/plans/2026-09-13-qml-stage-one.md). Existing `minimal` smoke tests do not verify Quick rendering.

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

## Optional QML SIM-LIVE pilot

The `-sim-qml` presets enable `LUMORA_BUILD_QML_UI` and the `qml-ui` vcpkg feature. They use separate build and dependency directories (`out/vcpkg_qml_installed`), preserving the original Widgets installation. The feature adds Qt Declarative (QML, Quick, Quick Controls and Quick Test), SVG, Shader Tools and Language Server at matching Qt 6.11.1 versions; it enables Qt's OpenGL feature on Linux. Qt libraries remain dynamically linked.

```bash
cmake --preset linux-gcc-debug-sim-qml \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/.tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset linux-gcc-debug-sim-qml --parallel
cmake --build --preset linux-gcc-debug-sim-qml --target all_qmllint
ctest --preset linux-gcc-debug-sim-qml -R '^Qml\.' --output-on-failure
cmake --build --preset linux-gcc-debug-sim-qml --target run-lumora-qml
```

Use `linux-gcc-release-sim-qml` for Release. `lumora_app` remains the Widgets workstation and is also built by these presets. Option OFF does not search for the declarative modules. A manually supplied Qt prefix must contain matching Core, Widgets, Qml, Quick, QuickControls2 and test modules; mixing an installed Widgets runtime with unrelated Quick libraries is unsupported.

The pilot uses the separate `LumoraQmlPilot` application identity and preference directory. Select SIM-LIVE, **Connect → Apply → review current readback → Confirm → Start Live**. Saved settings may reconnect for inspection, but streaming always requires an explicit Start or eligible **Resume saved Live** action. Stop and Disconnect can cancel pending startup. The simulator still requests 640×480 Mono12 in UInt16 at 30 FPS through the existing pipeline.

The QML frontend provides Original/Enhanced/Compare, Pause/Resume, Fit and logical 100%, pointer zoom/pan, and exact window/level fields and sliders. The processing selector includes Original, Standard, High Contrast, Soft Detail, Custom and loaded saved recipes; Reset processing returns to Original while preserving camera confirmation, paused pixels and viewport state. Image pixels and frame ownership remain in C++. Processing preferences save only after matching successful activation; failed activation restores the accepted selection. See the [preset/reset checkpoint](../architecture/milestones/qml-presets.md). Remaining processing controls, preset save/rename/delete, camera settings and installation editors remain later migration work; camera readback and installation orientation are read-only here. Use the Widgets application for those existing editors.

Click or Tab to the image surface before using Space=Pause/Resume, F=Fit, 1=100%, and +/-=zoom. Those keys do not intercept numeric entry. Evaluation, camera/viewer state, timestamp/age, stale/pause, orientation and errors remain visible around the image.

`Qml.Workstation` drives the compiled scene through real controls and the simulator. `Qml.Runtime` and `Qml.ProcessingAdapter` cover adapter/lifetime and activation/persistence behavior. For native Linux software-rendered captures:

```bash
LUMORA_QML_CAPTURE_DIR="$PWD/out/qa/qml-presets/captures-manual" \
  xvfb-run -a cmake -E env QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  out/build/linux-gcc-debug-sim-qml/tests/lumora_qml_tests
```

Use the matching Qt platform-plugin path if required by the installed prefix. The retained worktree uses the matching official Qt 6.11.1 SDK at `.tools/qt-official`, with non-Qt dependencies from the original vcpkg prefix. Its Debug application links Release Qt libraries. Reconfigure that cache with the recorded `CMAKE_PREFIX_PATH`; do not replace it with an unrelated Qt version. Xvfb/software or llvmpipe passes do not establish physical-display/GPU or native Windows/DPI acceptance.

The Linux-only `QmlPilot` install component stages the executable, scanned QML imports, runtime libraries/plugins and existing notices outside the build tree. See the [live checkpoint record](../architecture/milestones/qml-live.md) for deployment mechanics and the [preset/reset record](../architecture/milestones/qml-presets.md) for the latest fresh stage. It is not an accepted installer or distribution bundle.

## QML renderer experiment

With QML and tests enabled, `Qml.ImageRenderer` exercises the C++ image sink in
an actual offscreen/software Quick window. It is independent of the live composition and
does not attach a camera. Full regressions include all QML test registrations:

```bash
ctest --preset linux-gcc-debug-sim-qml -LE 'hardware|desktop' --output-on-failure
```

For native Linux checks, use the matching Qt platform plugins and run the image
test executable directly. Select a render backend explicitly; the diagnostic
output records the actual graphics implementation:

```bash
xvfb-run -a -s '-screen 0 2560x2160x24' env \
  QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl QSG_RENDER_LOOP=threaded QSG_INFO=1 \
  out/build/linux-gcc-debug-sim-qml/tests/lumora_quick_image_tests
```

Use `QSG_RENDER_LOOP=basic` for the basic OpenGL loop, or replace
`QSG_RHI_BACKEND=opengl` with `QT_QUICK_BACKEND=software` for software rendering.
Use `QT_SCALE_FACTOR=2` for the DPR 2 fixture. Xvfb can select Mesa llvmpipe;
successful OpenGL tests on it do not establish physical GPU or display behavior.

`lumora_quick_renderer_benchmark` prints bounded synthetic-frame measurements as
JSON. Build and run its Release target with the same explicit environment. Set
`LUMORA_RENDERER_CAPTURE_DIR` to capture separate 900×600 and 1280×800 Compare
windows outside the timed samples. The [benchmark description](../../benchmarks/presentation/README.md)
defines the timestamp and storage boundaries; the [checkpoint record](../architecture/milestones/qml-renderer-experiment.md)
records verification, measured costs and remaining limits. Do not interpret this
experiment as live processing throughput or complete session/GPU memory accounting.

## Launch the desktop application

After building, run this from the repository root in a graphical Linux session:

```bash
cmake --build --preset linux-gcc-debug-sim --target run-lumora
```

Use `linux-gcc-release-sim` for Release. This development-only target selects `xcb` and the matching Debug/Release Qt plugin directory for this process; it does not require a global Qt environment setting. It runs until you close the window. Launching the binary directly may require an explicit platform-plugin path with a vcpkg build.

The application displays the mandatory `EVALUATION — NOT FOR CLINICAL USE` banner and starts in `Waiting for image`, with Pause and image controls initially disabled. Mode actions become available when a display bundle is admitted; Pause and geometry controls become available when an image completes painting. Its production composition supplies a synthetic 640x480 Mono12 moving bar configured for 30 FPS. Raw samples use U16 storage; normalization and window/level remain U16 until the terminal Gray8 display mapper. See the [progress summary](../PROGRESS.md) for the integrated implementation and local branch verification status. No physical camera or patient data is involved. A successful launch does not establish milestone acceptance or clinical validation.

For a first run:

1. Wait for discovery, then explicitly select the `SIM-LIVE` simulator in Camera startup.
2. Click **Connect**. The camera opens idle; there is no live video yet.
3. Click **Apply** and review the requested and actual settings.
4. Click **Confirm** to acknowledge the actual settings.
5. Click **Start** to begin live synthetic video.

The viewer's **Pause / Live** button freezes/resumes the displayed image while acquisition continues. Camera **Stop** stops acquisition while retaining the connected device; **Start** can restart its still-confirmed settings. **Disconnect** closes the device and cancels pending startup/Resume intent. The last image can remain as context with the existing paused/stale indication. Explicit **Refresh** after Disconnect binds a fresh waiting source and clears that contextual image; it discovers cameras but does not reconnect. In Error, use Retry only when a desired camera identity is retained, or Disconnect then Refresh to restart discovery.

Preferences are saved in the background after successful confirmation. On a later run, matching saved identity and requested settings allow the application to connect idle for capability checks and potentially offer **Resume Live**. That startup action still needs an explicit click and rechecks Apply/readback before streaming. Capability or actual-setting changes require review, Confirm and Start; a load/save warning is not durable confirmation. **Resume Live** is distinct from the viewer's **Live** button. No startup path silently streams.

The merged M9 Task 2 controls appear under **Processing** after settings finish loading. Choose a preset or enable a stage and adjust its slider/numeric value. Accepted changes apply to subsequent frames and save in the background. **Reset processing** applies the Original processing preset without changing the camera, pause or zoom. A paused image stays frozen while settings change; resume the viewer to see new frames.

The **Original**, **Enhanced** and **Compare** buttons above the sidebar scroll select the display mode. Compare shows Original on the left and Enhanced on the right; Fit, 100%, zoom and pan act on both panes. These buttons change presentation only: **Original mode** retains the active processing settings, while the **Original preset** changes the processing definition. Mode switching also works on the same frozen bundle while paused, preserving its timestamp and increasing age.

A bundle without Enhanced falls back to Original and disables Enhanced/Compare with a visible reason. Recovery re-enables the buttons but leaves Original selected until the operator chooses a mode. A paused pair remains available despite later processing failures; Resume evaluates the newest bundle. The [Task 3 record](../architecture/milestones/m09-compare.md#verification-record) records passing local and hosted Linux/Windows verification, including the retained timeout evidence. Remaining camera configuration and fullscreen are subsequent work.

Upgrading from the M5 Mono8 simulator to M7 Mono12 changes the reported capabilities. An older saved Mono8 request keeps startup disconnected: explicitly select **SIM-LIVE**, **Connect**, **Apply** and review, then **Confirm** and **Start**. It cannot authorize an unchanged-settings Resume. The separate M4 harness below still uses Mono8.

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


### Camera settings

The integrated application provides **Camera settings**. After **Connect**, open it to edit supported exposure/gain modes and values while stopped. If the stream is running, use **Stop**; viewer **Pause** keeps acquiring and does not enable these edits. Choose **Apply settings**, review the separate actual readback, then explicitly **Confirm** and **Start** using the existing panel. Closing without Apply discards the draft. A changed source or external request requires closing and reopening the dialog.

Confirmed settings save in the background and may be eligible for explicit **Resume Live** on the next launch. At the Task 4A checkpoint, FPS, pixel format, full ROI and acquisition mode were read-only. [PR #20 integration evidence](../architecture/milestones/m09-camera-settings.md#pr-20-integration) records the passing Linux/Windows Debug/Release checks. Task 4B subsequently added the FPS controls below.


### Stopped frame-rate settings

The application on `main` also edits **Frame rate (fps)** while stopped. Build and launch with the same instructions above. SIM-LIVE advertises 1–60 FPS. Choose **Stop**, open **Camera settings**, edit FPS, **Apply settings**, review actual readback, then **Confirm** and **Start**. Requested 1.25 FPS is retained while the simulator reads back actual 1 FPS. Ordinary Apply and eligible saved Resume retain these settings. Viewer Pause continues acquisition and does not enable editing.

The low-FPS watchdog correction preserves 250 ms retrieval polling, allowing healthy slow streams to continue. The [Task 4B record](../architecture/milestones/m09-frame-rate.md#pr-21-integration) retains verified source, inspected native layouts and the explicit numeric-widget precision limit. PR #21 merged after Linux/Windows Debug/Release CI passed. At that integrated checkpoint, ROI, format and acquisition mode remained read-only.


### Stopped ROI and pixel-format settings

The application on local `main` includes **Pixel format** and **Image region** controls. Build and launch from the repository root using the simulator presets above. After Connect, use **Stop**, open **Camera settings**, choose an advertised format and edit ROI x/y/width/height, then **Apply settings**. Review the complete actual readback, close the old dialog, **Confirm**, and **Start**. Reopen Camera settings for further edits after a successful source change. Viewer Pause continues acquisition.

Production SIM-LIVE advertises only Mono12, x/y zero and dimensions up to 640×480; smaller ROI requests are supported. Bounds, increments and containment come from capabilities. Unrepresentable unsigned editor ranges disable Apply with an explanation. Format changes are exercised with additional simulator fixtures; no physical-camera format support is implied.

The camera remains open and frame IDs continue. Processing settings and display-mode selection survive; the old image, pause and viewport state reset before new frames arrive. Confirmed full settings use the existing saved Resume workflow. [Task 4C evidence](../architecture/milestones/m09-format-roi.md) records exact local verification, the temporary matching Qt runtime used for native X11 checks, and the remaining scope. This continuation has not yet been published or verified by hosted Windows CI.

### Per-camera preferences and installation orientation

The application on local `main` remembers confirmed acquisition settings by manufacturer/model/serial. The former `codex/m09-camera-controls` branch and `.worktrees/m09-camera-profiles` worktree remain retained with their verification records. Selection is stored independently. Compatible requests return when the camera is discovered; changed identity, capabilities or installation binding require review before Resume.

**Camera installation** shows orientation read-only during normal launch. Editing requires deliberate `--installation` launch plus actual OS administrator authority, a stopped current camera, matching Original/Enhanced previews and explicit consent. **Save installation** persists the machine profile; follow **Apply → review → Confirm → Start** to activate it. Saving does not restart acquisition. Native Original and intermediate pixels remain unchanged, and the status follows the displayed or paused frame.

Linux machine data uses `/etc/lumora/installation-profiles.json`. Startup validates the existing path authority without creating or repairing it. Unsafe ownership/write permissions require administrator correction; invalid data requires explicit preserved-backup repair. The [Task 4D record](../architecture/milestones/m09-camera-profiles.md) details local verification and remaining Windows/platform gates.

The subsequent [Task 4E compact controls](../architecture/milestones/m09-camera-controls.md) show contextual camera actions and expandable requested/actual review, separately from the displayed image's Live/Paused/stale state. Settings remain inspectable while streaming; editing requires Stop. Read-only fields show current camera readback; unavailable fields are identified without blocking other writable fields. Apply opens review when confirmation is required; the workflow remains **Stop → Apply → review → Confirm → Start**. Tasks 4C–4E are [integrated into local main at `e2de210`](../architecture/milestones/m09-camera-controls.md#local-main-integration), with no push, PR or hosted CI result. Their development record still distinguishes full production-source checks from the later test-fixture correction and sanitizer rerun; local integration supplies no native Windows acceptance.
