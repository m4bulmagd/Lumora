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

Normal builds produce the sole `lumora_app` Qt Quick/QML workstation. Qt Widgets is not required unless the separate legacy regression option is enabled. These instructions describe the intended native Windows build; the local Linux default switch does not establish Windows compilation, graphics, visual/DPI, installer or hardware acceptance.
Debug simulator build:

```powershell
cmake --preset windows-msvc-debug-sim --fresh "-DCMAKE_TOOLCHAIN_FILE=$LumoraVcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmake --build --preset windows-msvc-debug-sim --parallel 3
ctest --preset windows-msvc-debug-sim --output-on-failure -LE hardware
```

Release simulator build:

```powershell
cmake --preset windows-msvc-release-sim --fresh "-DCMAKE_TOOLCHAIN_FILE=$LumoraVcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmake --build --preset windows-msvc-release-sim --parallel 3
ctest --preset windows-msvc-release-sim --output-on-failure -LE hardware
```

These presets force `LUMORA_ENABLE_BASLER=OFF`, so a pylon installation is not required. They use the dynamic `x64-windows` triplet to preserve the approved Qt LGPL linking boundary. Quick tests use the `offscreen` platform plugin and software rendering without opening an interactive desktop window.

CTest sets each Quick test's `QT_QPA_PLATFORM_PLUGIN_PATH` from the imported `Qt6::QMinimalIntegrationPlugin` target, selecting the installed plugin directory for the active Debug or Release configuration. Copying Qt DLLs beside the executable alone does not provide this plugin path. This test-only environment does not replace deploying the Qt runtime and the `windows` platform plugin with the future Windows installer.

Basler presets are reserved for the later camera-adapter milestone. Machine-specific pylon paths belong in ignored `CMakeUserPresets.json`, never in the shared presets.

## QML dependencies and compatibility names

Every normal preset enables `qml-ui`, the default manifest feature, using `out/vcpkg_qml_installed`. Qt Core, Gui, Qml, Quick, QuickControls2, Multimedia and test modules must all match 6.11.1 and remain dynamically linked. `LUMORA_BUILD_QML_UI=OFF` is rejected; there is no Widgets application fallback.

The old `windows-msvc-{debug,release}-sim-qml` presets and build-only `lumora_qml_app` target remain compatibility names. They produce the same `lumora_app`, not a second executable. Run QML lint with:

```powershell
cmake --build --preset windows-msvc-debug-sim --target all_qmllint --parallel 3
ctest --preset windows-msvc-debug-sim -R '^Qml\.' --output-on-failure
```

The application uses the production `Lumora` organization/application identity and existing production preferences. Former `LumoraQmlPilot` settings remain untouched in their separate directory; there is no automatic merge or import. Preserve both sets of files. Any selected pilot migration is a later explicit opt-in procedure.

Camera and installation controls are QML. Installation editing requires `--installation` and actual OS administrator authority; the flag alone grants none. Saved-preset creation/rename/deletion remains later work. The shared QML source adds fullscreen (F11/Escape), collapsible panels, optional frame Details and saved window layout; native Windows validation of these controls remains pending.

Native Windows graphics and 100%/125%/150%/200% display scaling require separate recorded results. Linux SDK/Xvfb/llvmpipe evidence does not establish them. The Linux `QmlPilot` staging component retains its compatibility name; it is not a Windows installer or M13 acceptance.

## Multimedia sources and runtime deployment

The existing `sim` preset names still disable pylon; the normal application now also includes the [Qt local-camera/manual RTSP adapter](video-sources.md). The manifest pins `qtmultimedia[ffmpeg]` to Qt 6.11.1, with default features disabled. Configure checks the exact module version and requires `Qt6::QFFmpegMediaPlugin`. The pinned FFmpeg port selects native Schannel for Windows TLS when OpenSSL is not requested.

A Windows deployment needs the matching Qt Multimedia DLL, FFmpeg media plugin and its dependent FFmpeg DLLs in addition to the existing Qt Quick/platform runtime. The Linux `QmlPilot` staging component is not a Windows deployment procedure. Windows compilation, camera-driver access/privacy permissions, RTSP/RTSPS playback and installer/runtime deployment for this extension remain unverified. The Linux SDK probe used bundled FFmpeg 7.1.3; the pinned vcpkg baseline selects 9.0.1 and has not been freshly built for this extension. Keep those provenance claims separate; see [dependency notices](../../THIRD-PARTY-LICENSES/FFmpeg.txt).

## Optional legacy regression tests

The `windows-msvc-{debug,release}-sim-legacy-tests` presets enable `LUMORA_BUILD_LEGACY_WIDGETS_TESTS=ON` and manifest feature `legacy-widgets-tests`, with dependencies isolated in `out/vcpkg_legacy_tests_installed`. This adds unique legacy Widgets unit/mixed integration coverage and the non-shipping harness, not a second workstation. The normal build retains all 30 Qt-free ProcessingConfiguration, InstallationPipeline and CameraReconfiguration cases in `lumora_backend_integration_tests`.

```powershell
cmake --preset windows-msvc-debug-sim-legacy-tests --fresh "-DCMAKE_TOOLCHAIN_FILE=$LumoraVcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmake --build --preset windows-msvc-debug-sim-legacy-tests --parallel 3
ctest --preset windows-msvc-debug-sim-legacy-tests -L legacy-widgets --output-on-failure
```

## Launch the desktop application

After the Release simulator build, launch the normal application from the repository root:

```powershell
cmake --build --preset windows-msvc-release-sim --target lumora_app --parallel 3
cmake -E env QT_QPA_PLATFORM=windows `
  "QT_QPA_PLATFORM_PLUGIN_PATH=$PWD/out/vcpkg_qml_installed/x64-windows/Qt6/plugins/platforms" `
  out/build/windows-msvc-release-sim/src/qml/Release/lumora_app.exe
```

For Debug, use `windows-msvc-debug-sim`, `src/qml/Debug/`, and `x64-windows/debug/Qt6/plugins/platforms`. Use the matching installed plugin directory if the dependency prefix differs. These are developer-build commands; native Windows execution and appearance remain subject to the deferred validation below.

The application starts in **Waiting for image**. Select **SIM-LIVE**, click **Connect**, click **Apply** and review the settings, then **Confirm** and **Start** for the existing Mono12 simulator. The [video-source guide](video-sources.md) covers OS cameras and manual RTSP/RTSPS registration, Mono8 conversion, supported fixed modes up to 1920×1080, session-only credentials and real-source installation profiles. An administrator installation launch needs both `--installation` and an elevated administrator token. Qt RTSP metadata probing can issue PLAY during Connect; the application delivers no viewer frames before Start. These source paths still require native Windows verification. The evaluation banner remains visible.

Viewer **Pause / Live** freezes/resumes presentation while acquisition continues. Camera **Stop** stops acquisition while retaining the connected device; **Disconnect** closes it. If saved identity and requested settings match, the application may offer **Resume Live** on a later launch; it still requires an explicit click. Check these controls and the paused/stale indications in the normal application when collecting M4/M5 Windows acceptance evidence. See the [Linux launch guide](build-linux.md#launch-the-desktop-application) for the shared startup, saved-preference and source-reset behavior.

An older M5 Mono8 saved request differs from M7 and leaves startup disconnected. Explicitly select **SIM-LIVE**, **Connect**, **Apply** and review, then **Confirm** and **Start**. The [M7 record](../architecture/milestones/m07-preflight.md) records passing Linux/Windows simulator CI separately from the still-required native Windows checks.

## M4 synthetic viewer and remaining Windows checks

The optional legacy regression builds produce a separate non-shipping synthetic viewer harness. It supplements the normal application checks above. From the repository root, after the Release build:

```powershell
cmake --build --preset windows-msvc-release-sim-legacy-tests --target lumora_viewer_harness --parallel 3
cmake -E env QT_QPA_PLATFORM=windows `
  "QT_QPA_PLATFORM_PLUGIN_PATH=$PWD/out/vcpkg_legacy_tests_installed/x64-windows/Qt6/plugins/platforms" `
  out/build/windows-msvc-release-sim-legacy-tests/src/Release/lumora_viewer_harness.exe --start
```

For Debug, use `windows-msvc-debug-sim-legacy-tests`, `src/Debug/`, and `x64-windows/debug/Qt6/plugins/platforms`. Use the matching installed plugin directory if the dependency prefix differs. These are developer-build launch instructions, not installer deployment. They require native Windows verification; Linux runs cannot establish Windows appearance or compatibility.

The harness starts a synthetic 640x480 Mono8 moving bar only with `--start`. Without it the view waits. Close the window to stop/join the worker. Exercise Pause/Live, Fit, 100%, zoom, pan, and resize, and confirm the persistent evaluation banner and paused/stale indications. Recoverable 50 ms retrieval timeouts are visibly reported and counted; configured 30 FPS is not a measured throughput guarantee. No real patient data or physical camera is permitted in this evaluation harness.

The separate 600-second Release stress gate is opt-in:

```powershell
cmake --preset windows-msvc-release-sim-legacy-tests -DLUMORA_ENABLE_STRESS_TESTS=ON
cmake --build --preset windows-msvc-release-sim-legacy-tests --parallel 3
ctest --preset windows-msvc-release-sim-legacy-tests --output-on-failure --no-tests=error -L stress
cmake --preset windows-msvc-release-sim-legacy-tests -DLUMORA_ENABLE_STRESS_TESTS=OFF
```

Restore OFF even after failure/interruption. Save the source SHA, duration, platform, result, publication/paint counts, and timeout count from `out/build/windows-msvc-release-sim-legacy-tests/Testing/Temporary/LastTest.log`. Normal CI does not implicitly run this stress case.

### Run Windows stress from Linux through GitHub Actions

The **Windows Simulator** workflow adds the 600-second Release stress test only on an explicit manual dispatch. Push and pull-request runs keep their short Debug/Release suites; `LUMORA_ENABLE_STRESS_TESTS` still defaults OFF. A manual run first passes those same QML suites, configures the separate legacy regression preset with stress enabled, builds its integration target, and invokes `cmake/RunStress.cmake`. Cleanup attempts to restore OFF even after a failed stress run. Cancellation or runner loss can prevent cleanup/upload; a later run uses a fresh hosted workspace.

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
