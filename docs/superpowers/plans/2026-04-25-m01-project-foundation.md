# Milestone 1 Project Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish reproducible Linux/GCC and Windows/MSVC C++20 builds, a test runner, localization-ready Qt application shell, version/release-class source, and logging bootstrap without requiring pylon.

**Architecture:** CMake owns small library targets and one executable. A pinned vcpkg manifest supplies shared open-source dependencies on Linux and Windows. Simulator presets configure without pylon; Windows production presets opt into the isolated external Basler target later.

**Tech Stack:** Ubuntu Linux x64/GCC, Windows 11 x64/Visual Studio 2022 MSVC, C++20, CMake, Ninja, vcpkg manifest mode/binary cache, Qt 6 Widgets, GoogleTest/CTest, and spdlog.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Use C++20 with GCC and MSVC and Qt 6 Widgets; warnings are errors for Lumora-owned code.
- Keep dependencies minimal and target-scoped; do not link Qt or pylon into `lumora_core`.
- `LUMORA_ENABLE_BASLER=OFF` must configure without a pylon installation.
- Use no mutable global singleton or service locator.
- All normal tests run without camera hardware.

---

### Task 1: CMake target and preset skeleton

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `vcpkg.json`
- Create: `vcpkg-configuration.json`
- Create: `LICENSE`
- Create: `NOTICE`
- Create: `.gitignore`
- Create: `cmake/Dependencies.cmake`
- Create: `cmake/Pylon.cmake`
- Create: `cmake/Warnings.cmake`
- Create: `cmake/Version.cmake`
- Create: `src/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: a pinned vcpkg baseline for Qt 6, OpenCV, GoogleTest, and spdlog; optional pylon only in later Basler presets.
- Produces: presets `linux-gcc-debug-sim`, `linux-gcc-release-sim`, `windows-msvc-debug-sim`, `windows-msvc-release-sim`, `windows-msvc-debug-basler`, and `windows-msvc-release-basler`; option `LUMORA_ENABLE_BASLER`; helper `lumora_enable_warnings(target)`.

- [ ] **Step 1: Add a configure-contract test script**

Create `tests/cmake/VerifyTargets.cmake` with assertions that `lumora_core`, `lumora_diagnostics`, `lumora_ui`, and `lumora_app` are defined after their milestone introduces them, and that `lumora_camera_basler` exists only when `LUMORA_ENABLE_BASLER` is true.

```cmake
function(require_target name)
  if(NOT TARGET ${name})
    message(FATAL_ERROR "Required target missing: ${name}")
  endif()
endfunction()
```

- [ ] **Step 2: Verify the empty repository cannot configure**

Run: `cmake --preset linux-gcc-debug-sim`

Expected: FAIL because `CMakePresets.json` and the top-level project do not yet exist.

- [ ] **Step 3: Define project options and target-scoped dependency discovery**

The top-level file must set `CMAKE_CXX_STANDARD 20`, disable compiler extensions, enable CTest, include the four helper modules, and add `src` and `tests`. `Dependencies.cmake` must use `find_package` for manifest-provided Qt6 Widgets, OpenCV core/imgproc/imgcodecs, GTest, and spdlog. Pin the registry baseline and dependency versions/features in the manifest; configure an authenticated binary cache in CI. `Pylon.cmake` must return immediately when Basler support is off. `.gitignore` must exclude `out/`, `CMakeUserPresets.json`, runtime logs/configuration/captures, local hardware profiles, and test artifacts without ignoring source fixtures.

```cmake
option(LUMORA_ENABLE_BASLER "Build the Basler pylon camera adapter" OFF)
option(LUMORA_BUILD_TESTS "Build automated tests" ON)
option(LUMORA_BUILD_BENCHMARKS "Build benchmark executables" OFF)
```

- [ ] **Step 4: Define cross-platform presets**

Use separate binary directories under `out/build/<preset-name>`, Ninja on Linux, x64 MSVC on Windows, and explicit Basler on/off cache variables. Test presets must set `outputOnFailure` true. Never place local pylon or vcpkg paths in committed presets; document them in ignored `CMakeUserPresets.json`.

- [ ] **Step 5: Configure both Linux simulator presets**

Run:

```bash
cmake --preset linux-gcc-debug-sim
cmake --preset linux-gcc-release-sim
```

Expected: PASS without querying for pylon. The matching Windows simulator presets run in CI.

- [ ] **Step 6: Commit the build skeleton**

```powershell
git add CMakeLists.txt CMakePresets.json vcpkg.json vcpkg-configuration.json LICENSE NOTICE .gitignore cmake src/CMakeLists.txt tests/CMakeLists.txt tests/cmake
git commit -m "build: add cross-platform CMake foundation"
```

### Task 2: Qt application shell

**Files:**
- Create: `src/app/main.cpp`
- Create: `src/ui/include/lumora/ui/MainWindow.hpp`
- Create: `src/ui/src/MainWindow.cpp`
- Create: `src/ui/resources/lumora.qrc`
- Create: `tests/unit/ui/MainWindowSmokeTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Qt6::Widgets` and generated Qt MOC/RCC support.
- Produces: `lumora::ui::MainWindow`, target `lumora_ui`, executable `lumora_app`, and test target `lumora_ui_tests`.

- [ ] **Step 1: Write the failing shell smoke test**

```cpp
TEST(MainWindowSmoke, HasStableIdentityAndCanClose) {
    lumora::ui::MainWindow window;
    EXPECT_EQ(window.objectName(), QStringLiteral("mainWindow"));
    EXPECT_FALSE(window.windowTitle().isEmpty());
    window.show();
    QCoreApplication::processEvents();
    window.close();
    EXPECT_FALSE(window.isVisible());
}
```

- [ ] **Step 2: Build the test to verify the missing interface fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL because `MainWindow` and its target do not exist.

- [ ] **Step 3: Add the smallest Qt Widgets shell**

`MainWindow` is a final `QMainWindow` with stable object name, title `Lumora`, a neutral dark central widget, and no camera or processing dependency. All user-visible English strings use Qt translation functions from the start. A compile-time evaluation release-class source cannot be disabled by runtime configuration. `main.cpp` creates `QApplication`, constructs the window, shows it, and returns `app.exec()`.

```cpp
namespace lumora::ui {
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};
}
```

- [ ] **Step 4: Run the shell test headlessly**

Run with `QT_QPA_PLATFORM=offscreen`: `ctest --preset linux-gcc-debug-sim --output-on-failure -R MainWindowSmoke`

Expected: PASS and process exits without leaked top-level widgets.

- [ ] **Step 5: Launch the application manually**

Run the built `lumora_app.exe`, verify one resizable dark window opens, then close it with the window close button.

- [ ] **Step 6: Commit the shell**

```powershell
git add src/app src/ui tests/unit/ui src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add Qt workstation shell"
```

### Task 3: Application version and logging bootstrap

**Files:**
- Create: `src/core/include/lumora/core/Error.hpp`
- Create: `src/core/include/lumora/core/Result.hpp`
- Create: `src/core/include/lumora/core/AppVersion.hpp.in`
- Create: `src/diagnostics/include/lumora/diagnostics/Logging.hpp`
- Create: `src/diagnostics/src/Logging.cpp`
- Create: `tests/unit/diagnostics/LoggingTests.cpp`
- Create: `tests/unit/core/ResultSmokeTests.cpp`
- Modify: `src/app/main.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: spdlog, generated semantic version values, and a caller-provided log directory.
- Produces: `ErrorCategory`, `Error`, `Result<T, Error>`, `AppVersion::string()`, `Logging::start(const std::filesystem::path&) -> Result<void>`, `Logging::shutdown()`, and startup/shutdown events.

- [ ] **Step 1: Write the failing logging lifecycle test**

```cpp
TEST(Logging, WritesVersionedStartupAndShutdownEvents) {
    TempDirectory temp;
    ASSERT_TRUE(Logging::start(temp.path()).hasValue());
    Logging::shutdown();
    const auto text = readOnlyLogFile(temp.path());
    EXPECT_THAT(text, HasSubstr("application_started"));
    EXPECT_THAT(text, HasSubstr(AppVersion::string()));
    EXPECT_THAT(text, HasSubstr("application_stopped"));
}
```

- [ ] **Step 2: Verify the test fails for missing types**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_diagnostics_tests`

Expected: FAIL because version and logging contracts do not exist.

- [ ] **Step 3: Generate version information and implement a bounded logger bootstrap**

Generate a header from CMake project version. Add the complete `Error` categories and a C++20-compatible `Result<T, Error>` that supports move-only values. Start a temporary synchronous rotating sink in this milestone, with five 10 MiB files and an injectable directory; Milestone 11 converts the sink to bounded asynchronous JSON-lines logging.

```cpp
struct AppVersion final {
    static std::string_view string() noexcept;
};

enum class ErrorCategory {
    CameraDiscovery, CameraConnection, CameraConfiguration, Acquisition,
    InvalidFrame, Processing, Configuration, Encoding, Storage,
    ResourceExhaustion, Cancelled, Internal
};
```

- [ ] **Step 4: Integrate lifecycle order**

`main.cpp` resolves the log directory, starts logging before constructing `MainWindow`, logs the generated version, runs the event loop, destroys UI objects, and shuts logging down last.

- [ ] **Step 5: Run focused and full tests**

Run:

```bash
ctest --preset linux-gcc-debug-sim --output-on-failure -R Logging
ctest --preset linux-gcc-debug-sim --output-on-failure
```

Expected: PASS; one startup and one shutdown event are present.

- [ ] **Step 6: Commit version/logging bootstrap**

```powershell
git add cmake/Version.cmake src/core src/diagnostics src/app/main.cpp tests/unit/core tests/unit/diagnostics
git commit -m "feat: add versioned logging bootstrap"
```

### Task 4: Versioned configuration foundation

**Files:**
- Create: `src/configuration/include/lumora/configuration/ApplicationConfiguration.hpp`
- Create: `src/configuration/include/lumora/configuration/ConfigurationCodec.hpp`
- Create: `src/configuration/include/lumora/configuration/ConfigurationStore.hpp`
- Create: `src/configuration/src/ConfigurationCodec.cpp`
- Create: `src/configuration/src/ConfigurationStore.cpp`
- Create: `tests/unit/configuration/ConfigurationStoreTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Qt Core JSON, `QStandardPaths`, `Result`, and `Error`.
- Produces: schema-1 `ApplicationConfiguration`, `ConfigurationCodec::decode/encode`, `ConfigurationStore::load/save`, and a caller-injectable configuration path for tests.

- [ ] **Step 1: Write failing default/round-trip/corruption tests**

```cpp
TEST(ConfigurationStore, MissingFileReturnsValidatedDefaults) {
    TempDirectory temp;
    ConfigurationStore store(temp.path() / "config.json");
    auto loaded = store.load();
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value().schemaVersion, 1);
    EXPECT_TRUE(loaded.value().usedDefaults);
}
```

- [ ] **Step 2: Verify configuration target is missing**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_configuration_tests`

Expected: FAIL.

- [ ] **Step 3: Implement schema-1 envelope and validation**

```cpp
struct ApplicationConfiguration final {
    static constexpr int CurrentSchemaVersion = 1;
    int schemaVersion{CurrentSchemaVersion};
    QJsonObject application;
    QJsonObject cameraProfiles;
    QJsonObject processing;
    QJsonObject presets;
    QJsonObject capture;
    QJsonObject ui;
    bool usedDefaults{false};
};
```

Reject non-object sections and schema versions newer than the application. Keep domain models outside JSON; later milestones replace individual raw sections with typed codecs while retaining this envelope.

- [ ] **Step 4: Implement atomic save and invalid-file preservation**

Write a complete sibling temporary file, flush/close it, then replace the prior config. On decode failure, rename the original to `config.invalid-<UTC timestamp>.json`, return validated defaults plus a warning result, and never overwrite the invalid copy.

- [ ] **Step 5: Run configuration failure matrix**

Cover missing file, valid round-trip, truncated JSON, wrong root type, missing schema, future schema, unwritable directory, replace failure, and Unicode path. Verify prior valid content survives a failed save.

- [ ] **Step 6: Commit configuration foundation**

```powershell
git add src/configuration tests/unit/configuration src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(config): add versioned atomic JSON storage"
```

### Task 5: Developer build contract and simulator CI

**Files:**
- Create: `docs/development/build-linux.md`
- Create: `docs/development/build-windows.md`
- Create: `docs/architecture/requirements-traceability.md`
- Create: `.github/workflows/windows-simulator.yml`
- Create: `.github/workflows/linux-simulator.yml`
- Create: `README.md`

**Interfaces:**
- Consumes: the six CMake presets and existing test targets.
- Produces: documented Linux daily-development and Windows compatibility paths plus Linux and Windows simulator CI jobs that never request pylon.

- [ ] **Step 1: Document exact prerequisites and commands**

Include supported architectures, GCC and Visual Studio prerequisites, vcpkg bootstrap/baseline/binary-cache policy, CMake configure/build/test commands, Linux headless UI test configuration using the available Qt `minimal` platform plugin, and clean output removal instructions that target only `out/build`. Document that official Windows artifacts are built on Windows rather than cross-compiled.

- [ ] **Step 2: Add Linux and Windows CI workflows**

The workflows must configure the respective `linux-gcc-release-sim` and `windows-msvc-release-sim` presets, build all targets, and run CTest with `-LE hardware`. Dependency acquisition uses the pinned manifest and binary cache. Both jobs are required checks.

- [ ] **Step 3: Seed traceability**

Add rows for design sections 1 through 4 and Milestone 1 tests. Mark implementation status only after their tests pass.

- [ ] **Step 4: Verify commands from a fresh build directory**

Run locally on Linux:

```bash
cmake --preset linux-gcc-release-sim --fresh
cmake --build --preset linux-gcc-release-sim --parallel
ctest --preset linux-gcc-release-sim --output-on-failure -LE hardware
```

Expected: PASS locally, followed by PASS for both required CI jobs.

- [ ] **Step 5: Commit documentation and CI**

```powershell
git add README.md docs/development docs/architecture .github/workflows/linux-simulator.yml .github/workflows/windows-simulator.yml
git commit -m "docs: define Linux and Windows build contract"
```

## Milestone 1 acceptance gate

- [ ] Linux/GCC and Windows/MSVC Debug and Release simulator presets configure, build, and pass tests.
- [ ] `LUMORA_ENABLE_BASLER=OFF` performs no pylon discovery.
- [ ] The application opens and closes cleanly.
- [ ] Startup and shutdown logs contain the application version.
- [ ] Build instructions succeed in a clean output directory.
- [ ] Dependencies are pinned, user-visible strings are translation-ready English, and the evaluation release class cannot be disabled at runtime.
