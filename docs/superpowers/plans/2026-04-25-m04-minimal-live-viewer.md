# Milestone 4 Minimal Live Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display immutable format-aware grayscale frames in a responsive Qt workstation viewport with evaluation, paused, and stale safety indications plus fit, 100%, zoom, and pan.

**Architecture:** The UI consumes `FrameBundle` values through a presentation adapter and never knows how a camera or processor produced them. Rendering remains on the Qt UI thread; test harnesses publish prepared display frames from another thread.

**Tech Stack:** C++20, Qt 6 Widgets/Gui, CMake, GoogleTest/CTest. Use the existing Qt `minimal` headless plugin; no Qt Test module or new dependency is needed.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

**Execution progress (2026-09-06):** Task 1 passed independent task/final reviews and was merged in [PR #1](https://github.com/m4bulmagd/Lumora/pull/1) as `a6351e374caa308ce55fc1eef5232de14ead461f`, with Linux/GCC and Windows/MSVC Debug and Release CI passing. Task 2 is implemented locally at `e638caa0f18c2497f955fdb5eaec67e1131305de`, including the final-review rendering regression tests, and passed its independent task review: 16 viewport tests and all 20 CTest entries pass in Linux/GCC Debug and Release. Task 2 Windows/MSVC CI and integration remain pending; Tasks 3–4 are not started. [M3 remains accepted](../../architecture/milestones/m03-camera-api-simulator.md); see the [M4 preflight and execution evidence](../../architecture/milestones/m04-preflight.md). Local task completion is not M4 acceptance.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- UI code receives immutable core frames and has no camera SDK dependency.
- No per-frame queued Qt signal is posted.
- Display buffers remain alive for the complete `QImage`/paint lifetime.
- View transforms never alter source pixels or processing configuration.
- Viewer background is neutral dark and image aspect ratio is preserved.
- Fit uses its exact whole-image scale; `[0.05, 32.0]` constrains manual zoom only. At 100%, one source pixel maps to one logical viewport pixel, independent of device pixel ratio.
- Source IDs are compared only within one source session. Slot revision, accepted frame, and successfully painted frame are separate state.
- Receiving/scheduling a frame does not establish freshness. Retain the last completed bundle separately from a pending paint; Pause must freeze the image actually shown.

## Build and test integration contract

Read `src/core/include/lumora/core/Frame.hpp`, `src/core/include/lumora/core/ImageLayout.hpp`, `src/core/include/lumora/core/Clock.hpp`, `src/core/include/lumora/core/LatestValueSlot.hpp`, `src/CMakeLists.txt`, and `tests/CMakeLists.txt` before coding. Use `DisplayFrame::sourceFrameId`, `FrameBundle::sourceFrameId()`, and `consumeAfter(revision)` returning optional `{revision, value}`; do not invent replacement names or mutable frame constructors. Core factories allow frame ID zero, so absence is represented by `std::optional`, not a sentinel ID.

Each task adds its own sources and focused CTest entry immediately. Until Task 4 extracts a shared Qt test main, `tests/unit/ui/MainWindowSmokeTests.cpp` owns the only `QApplication`/GoogleTest main. Link `GTest::gtest`, not `gtest_main`, for Qt test executables.

Task 1 changes `MainWindowSmoke` to `--gtest_filter=MainWindowSmoke.*`, adds `ViewportTransform` with `--gtest_filter=ViewportTransform.*`, and applies the following properties to both. Extend the same test-name list with `ImageViewport`, `WorkstationView`, and `FramePresenter` as they are added; their filters are the corresponding `Suite.*`. Each filtered command must discover at least one test.

```cmake
set_tests_properties(
  MainWindowSmoke ViewportTransform
  PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>"
)
```

This preserves the configuration-matched plugin path that fixed Windows startup. Do not replace it with a Linux-specific directory, an unqualified Windows `plugins` path, or `offscreen`. Use `QWidget::render` into a `QImage` for deterministic pixel assertions under `minimal`; native Windows visual/DPI checks are a separate manual acceptance item.

After registering each task's files, configure using the [Linux build guide](../../development/build-linux.md), build its test target, confirm the focused test fails for the missing behavior, implement, and rerun. Before committing, run the focused test plus `MainWindowSmoke` and `git diff --check`. Do not accept “No tests were found” as a passing test.

---

### Task 1: Viewport transform model

**Files:**

- Create: `src/ui/include/lumora/ui/ViewportTransform.hpp`
- Create: `src/ui/src/ViewportTransform.cpp`
- Create: `tests/unit/ui/ViewportTransformTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: image dimensions in source pixels; viewport sizes, pointer coordinates, and pan deltas in logical pixels. Geometry is Qt-independent.
- Produces: the `lumora::ui` value types and public operations below; `actualPixels` deliberately has no device-pixel-ratio argument.

- [x] **Step 1: Write failing geometry tests**

```cpp
TEST(ViewportTransform, FitPreservesAspectRatio) {
    auto view = ViewportTransform::fit({2048, 1024}, {1000, 800});
    EXPECT_DOUBLE_EQ(view.scale(), 1000.0 / 2048.0);
    EXPECT_NEAR(view.imageCenterInViewport().y, 400.0, 1e-9);
}

TEST(ViewportTransform, FitCanBeSmallerThanManualMinimum) {
    auto view = ViewportTransform::fit({4096, 2048}, {100, 100});
    EXPECT_DOUBLE_EQ(view.scale(), 100.0 / 4096.0);
    EXPECT_NEAR(view.imageToViewport({4096, 2048}).x, 100.0, 1e-9);
}
```

Add the test source to `lumora_ui_tests`, the model files to `lumora_ui`, and the focused CTest registration described above. A compile failure from the missing model is the first red result; missing dependencies or an unregistered test are not behavioral evidence.

- [x] **Step 2: Verify the missing model fails to compile**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL.

- [x] **Step 3: Implement transform operations**

```cpp
struct Size { double width; double height; };
struct Point { double x; double y; };
struct Vector { double x; double y; };
enum class ViewScaleMode { Fit, Manual };

class ViewportTransform final {
public:
    static ViewportTransform fit(Size image, Size viewport);
    static ViewportTransform actualPixels(Size image, Size viewport);
    void resize(Size image, Size viewport);
    void zoomAt(Point viewportPoint, double factor);
    void panBy(Vector delta);
    double scale() const noexcept;
    Point imageCenterInViewport() const noexcept;
    Point imageToViewport(Point imagePoint) const noexcept;
    ViewScaleMode mode() const noexcept;
    bool drawable() const noexcept;
};
```

Fit sets `scale = min(viewport.width / image.width, viewport.height / image.height)` and centers both axes, without the manual clamp. `actualPixels` selects Manual at exactly `1.0` and centers the image before pan clamping. Store a positive finite scale and the image origin in logical viewport coordinates; map with `origin + imagePoint * scale`.

Manual zoom clamps the requested scale to `[0.05, 32.0]`. Preserve the source point under the cursor using `source = (cursor - origin) / oldScale` and `newOrigin = cursor - source * newScale`, then clamp pan. Pan clamping takes precedence at an image edge: center an axis when the scaled image is smaller than the viewport; otherwise clamp its origin to `[viewportExtent - scaledImageExtent, 0]`.

If Fit is below 0.05, zoom-out is a no-op and zoom-in enters the manual range at the clamped requested scale. If Fit is above 32, zoom-in is a no-op and zoom-out enters the manual range. A factor of 1 is a no-op; never make zoom-out enlarge the image or zoom-in shrink it. Non-positive/non-finite factors or non-finite pointer/delta inputs leave state unchanged.

`resize` recomputes Fit; Manual retains scale and the source point at viewport center before pan clamping. On image-size changes it retains that source coordinate where possible, then clamps to the new image. A zero extent is a valid transient non-drawable state: keep the selected mode, a finite scale, and finite coordinates, and recover when valid sizes return. Negative/non-finite sizes leave an existing transform unchanged; a factory receiving them produces the same finite non-drawable state as an empty image. If intermediate arithmetic would make scale zero/non-finite or produce non-finite coordinates, reject that update (or return the finite non-drawable factory state); cover extreme finite doubles as well as NaN/Inf in tests.

- [x] **Step 4: Add zoom/pan/resize cases**

Cover Fit below 0.05 and above 32, both transitions to manual zoom, repeated zoom in/out, cursor anchoring away from clamped edges, edge precedence, pan bounds, Fit/manual resize, image-size changes, zero extents/recovery, and invalid input. Confirm `ViewportTransform::actualPixels({64, 32}, {128, 128}).scale() == 1.0`; DPR 1.0/1.25/1.5/2.0 belongs to Task 2's Qt rendering tests, not this pure model.

- [x] **Step 5: Run and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure --no-tests=error -R '^(ViewportTransform|MainWindowSmoke)$'`

```powershell
git add src/ui tests/unit/ui/ViewportTransformTests.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): add deterministic viewport transforms"
```

### Task 2: ImageViewport with safe display-buffer lifetime

**Files:**

- Create: `src/ui/include/lumora/ui/ImageViewport.hpp`
- Create: `src/ui/src/ImageViewport.cpp`
- Create: `tests/unit/ui/ImageViewportTests.cpp`
- Create: `tests/support/ViewportTestSupport.hpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `std::shared_ptr<const core::DisplayFrame>` and Task 1's transform; no raw frame, clock, camera, or processing dependency.
- Produces: validated staging, completed-paint identity, a synchronous UI-thread paint observer, and view-only controls. All methods are UI-thread confined.

- [x] **Step 1: Write failing buffer-lifetime and aspect tests**

Add header-only fixtures in `namespace lumora::test` and a private `tests/support` include path for `lumora_ui_tests`. Include `lumora/core/BufferPool.hpp`, `lumora/core/Frame.hpp`, `QImage`, `QWidget`, `<algorithm>`, `<cmath>`, `<cstddef>`, `<cstdint>`, `<memory>`, and `<utility>`.

```cpp
inline std::shared_ptr<const core::DisplayFrame> makeDisplayFrame(
    std::uint32_t width, std::uint32_t height, std::uint64_t id) {
    const auto stride = (static_cast<std::size_t>(width) + 3U) & ~std::size_t{3U};
    const auto layout = core::ImageLayout::create(
        width, height, stride, core::StorageType::UInt8,
        stride * static_cast<std::size_t>(height)).value();
    auto pool = core::BufferPool::create(1U, layout.payloadBytes()).value();
    auto lease = pool->tryAcquire();
    std::ranges::fill(lease->bytes(), std::byte{0x80});
    return core::DisplayFrame::create(
        id, layout, std::move(*lease).seal(), core::DisplayStorage::Gray8,
        core::DisplayMapping{0U, 255U, 255U, 1U},
        core::Orientation{false, false, core::Rotation::Degrees0}).value();
}

inline QImage paintWidget(QWidget& widget, double dpr = 1.0) {
    QImage result(
        static_cast<int>(std::ceil(widget.width() * dpr)),
        static_cast<int>(std::ceil(widget.height() * dpr)),
        QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(dpr);
    result.fill(Qt::transparent);
    widget.render(&result);
    return result;
}
```

These fixtures use small positive dimensions and DPR values; invalid-boundary tests construct their specific inputs separately. Do not modify core constructors to manufacture invalid frames.

```cpp
TEST(ImageViewport, RetainsPixelsUntilReplacement) {
    std::weak_ptr<const DisplayFrame> weak;
    ImageViewport viewport;
    viewport.resize(128, 128);
    {
        auto frame = lumora::test::makeDisplayFrame(64, 32, 7);
        weak = frame;
        ASSERT_TRUE(viewport.present(frame).hasValue());
    }
    lumora::test::paintWidget(viewport);
    EXPECT_EQ(viewport.presentedFrameId(), std::optional<std::uint64_t>{7});
    EXPECT_FALSE(weak.expired());
    ASSERT_TRUE(viewport.present(lumora::test::makeDisplayFrame(64, 32, 8)).hasValue());
    EXPECT_FALSE(weak.expired()); // The previous image is still the painted image.
    lumora::test::paintWidget(viewport);
    EXPECT_TRUE(weak.expired());
}
```

Add `ImageViewport`'s source/header to `lumora_ui`, link `lumora::core` publicly because it appears in the UI API, add the unit test source, and register `ImageViewport` with the shared headless test properties. No resource edit is required unless an actual resource is added.

- [x] **Step 2: Verify missing viewport behavior fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL.

- [x] **Step 3: Implement presentation and paint**

Recheck the core layout/payload contract and Qt's representable dimensions/stride before narrowing to `int`/`qsizetype`. Null input, non-Gray8 storage, or a failed `QImage` wrapper returns a typed error without replacing either the pending or completed image. Core factories already prevent malformed core layouts; tests must not bypass their private constructors.

Evaluation composition wraps Gray8 as a non-owning `QImage::Format_Grayscale8`. Retain the owning shared frame through every implicitly shared `QImage` lifetime, using its cleanup context, and keep at most one pending plus one completed viewport image. On replacement, release each superseded owner only after its last image view is destroyed. `ImageViewport` is the format-aware renderer boundary; keep Qt image details private so a later Gray16/calibrated renderer can replace the implementation without changing frame contracts. Do not add an unused renderer backend.

```cpp
class ImageViewport final : public QWidget {
public:
    using PresentationObserver =
        std::function<void(std::shared_ptr<const core::DisplayFrame>)>;

    explicit ImageViewport(QWidget* parent = nullptr);
    core::Result<void> present(std::shared_ptr<const core::DisplayFrame> frame);
    std::optional<std::uint64_t> presentedFrameId() const noexcept;
    void setPresentationObserver(PresentationObserver observer);
    void discardPendingPresentation();
    void clear();
    void setFitMode();
    void setActualPixels();
    void zoomIn();
    void zoomOut();
    const ViewportTransform& transform() const noexcept;
};
```

`present` validates and stages a frame, then schedules a coalesced paint; it does not claim successful presentation. After completing the image paint, update `presentedFrameId` and invoke the observer directly on the UI thread for that newly painted frame, not for ordinary zoom/resize/expose repaints. Never queue a per-frame Qt signal. The observer must not synchronously repaint; Task 4 uses it only to record completed-bundle state and clock time. Detach it before its owner is destroyed.

`discardPendingPresentation` drops an unpainted replacement while retaining the completed image (needed for Pause). `clear` drops both images, clears completed identity, and returns to empty Fit mode (needed for source replacement). Empty or zero-sized viewports paint only their background and do not report frame completion. Paint letterbox regions with `#16181c`; enable smooth image scaling only when the logical transform scale differs from `1.0`.

- [x] **Step 4: Implement interactions**

Wheel zooms around the logical pointer position; use a factor of `pow(1.2, angleDeltaY / 120.0)`. Buttons use factors `1.2` and `1.0 / 1.2` at viewport center. Left drag pans oversized axes; double-click enters Fit. Task 3 adds keyboard actions. A same-size frame preserves the transform; a size change uses Task 1's `resize` rules. Do not apply `presentationOrientation` again: the incoming display pixels already include it.

- [x] **Step 5: Verify deterministic headless rendering and presentation state**

Render a 2:1 fixture into square, portrait, and wide targets via `paintWidget`; compare image bounds and neutral bars rather than platform-specific text pixels. Repeat at DPR 1.0/1.25/1.5/2.0 with `setActualPixels()` and assert `transform().scale() == 1.0`; the render target's physical bounds change but the logical transform does not. Use padded rows and distinguishable row values to catch stride errors. Test null/Gray16 rejection, no completion before paint, coalesced replacements, observer count on repaints, ID zero, cancellation of a pending image, and complete owner release on `clear`/destruction.

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure --no-tests=error -R '^(ImageViewport|ViewportTransform|MainWindowSmoke)$'`

- [x] **Step 6: Commit viewport**

```powershell
git add src/ui tests/unit/ui/ImageViewportTests.cpp tests/support/ViewportTestSupport.hpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): add safe grayscale image viewport"
```

### Task 3: Workstation shell layout and status model

**Files:**

- Create: `src/ui/include/lumora/ui/WorkstationStatus.hpp`
- Create: `src/ui/include/lumora/ui/WorkstationView.hpp`
- Create: `src/ui/src/WorkstationView.cpp`
- Create: `tests/unit/ui/WorkstationViewTests.cpp`
- Modify: `src/ui/src/MainWindow.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 2's `ImageViewport` and presentation-safe status, never camera state inferred from frame presence.
- Produces: the following `lumora::ui` status/view contracts, a fixed sidebar/right-viewer layout, and stable object names. `ViewerState` remains the cross-milestone viewer-state type; later application code must not introduce a competing enum with reversed semantics.

```cpp
enum class ViewerState { Live, Paused };
enum class FrameFreshness { WaitingForFrame, Current, Stale };
struct WorkstationStatus final {
    ViewerState viewerState{ViewerState::Live};
    FrameFreshness freshness{FrameFreshness::WaitingForFrame};
    std::optional<std::chrono::system_clock::time_point> frameUtc;
    std::chrono::milliseconds frameAge{0};
};

class WorkstationView final : public QWidget {
    Q_OBJECT
public:
    explicit WorkstationView(QWidget* parent = nullptr);
    QWidget* sidebar() const noexcept;
    ImageViewport* imageViewport() const noexcept;
    ViewerState viewerState() const noexcept;
    const WorkstationStatus& status() const noexcept;
    void setStatus(WorkstationStatus status);
signals:
    void pauseRequested();
    void resumeRequested();
};
```

`setStatus` is the state setter; controls emit intent rather than guessing which bundle has finished painting. Task 4 supplies that state. With no bound presenter/frame, show `Waiting for image`, disable Pause and image-only actions, and never fabricate a timestamp or camera connection state.

- [ ] **Step 1: Write a failing widget-structure test**

```cpp
TEST(WorkstationView, ImageAreaDominatesInitialLayout) {
    WorkstationView view;
    view.resize(1280, 800);
    view.show();
    QCoreApplication::processEvents();
    EXPECT_LT(view.sidebar()->width(), view.imageViewport()->width());
    EXPECT_EQ(view.viewerState(), ViewerState::Live);
    EXPECT_EQ(view.status().freshness, FrameFreshness::WaitingForFrame);
}
```

Register the new library/test files and the `WorkstationView` CTest suite with the same Qt plugin properties before running the red test.

- [ ] **Step 2: Verify missing view fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL.

- [ ] **Step 3: Implement clean initial layout**

Create a header, fixed-width left sidebar, expanding viewer, and compact footer. The sidebar identifies Original display and contains Pause/Live; the footer contains Fit, 100%, and zoom controls, matching design §11.1. Enhanced/Compare, processing, capture, orientation editing, diagnostics, and full fullscreen interaction arrive in their numbered milestones. No Record control is present.

Replace `MainWindow`'s placeholder central widget with `WorkstationView`, preserving the `mainWindow` identity and exactly one `releaseClassBanner` carrying `EVALUATION — NOT FOR CLINICAL USE`. Keep the banner and image-local paused/stale overlay in the normal and fullscreen-capable composition. The compile-time evaluation flag must not become a user-dismissable preference. Use `tr()` for English text, accessible names, and stable names `imageViewport`, `sidebar`, `pauseLiveButton`, `fitAction`, `actualPixelsAction`, `zoomInAction`, `zoomOutAction`, and `frameStateOverlay`.

- [ ] **Step 4: Wire viewer controls without camera calls**

Fit/100%/zoom actions invoke the viewport methods. Pause/Live emits the corresponding intent; tests use `setStatus` to exercise each state until Task 4 binds the presenter. Paused state shows persistent high-contrast `PAUSED`, the completed frame's UTC timestamp, and age supplied by the presenter. Stale Live state shows `STALE IMAGE / NOT LIVE`; waiting state has no fabricated image or age. Paused is visibly non-live regardless of freshness and remains distinct from an acquisition Stop command.

Support keyboard activation of all buttons/actions, with viewer-focused shortcuts F (Fit), 1 (100%), Space (Pause/Live), and +/- (zoom). Scope shortcuts to the viewer so future editable controls do not lose ordinary text input. No view includes a camera adapter header or calls `ICameraDevice`.

- [ ] **Step 5: Run UI tests and manual visual check**

Test action routing, keyboard activation, resize, unavailable/no-frame controls, exact banner text, non-dismissible textual paused/stale states, and transitions back to Current. Feed fixed timestamps/ages via `setStatus`; verify text/state rather than platform-dependent glyph pixels. Reparent/show the shell fullscreen in a UI test and check banner/overlay visibility without implementing M9's full fullscreen UX.

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure --no-tests=error -R '^(WorkstationView|ImageViewport|ViewportTransform|MainWindowSmoke)$'`

Before M4 acceptance, manually check a native Windows 11 window at logical client sizes 1280x720 and 1920x1080 and exercise 100%, 125%, 150%, and 200% scaling on a screen large enough for each checked layout. Record physical and effective logical size, screenshot, banner/overlay readability, keyboard controls, and exact 100% semantics; these are not claims that every physical-resolution/scaling combination can fit the same window. Any insufficient-workspace case must be recorded and must not silently clip safety indications. Headless CI is not evidence that this manual check occurred.

- [ ] **Step 6: Commit workstation shell**

```powershell
git add src/ui tests/unit/ui/WorkstationViewTests.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): add minimal workstation layout"
```

### Task 4: Latest-frame presenter and simulated viewer integration test

**Files:**

- Create: `src/ui/include/lumora/ui/FramePresenter.hpp`
- Create: `src/ui/src/FramePresenter.cpp`
- Create: `tests/unit/ui/FramePresenterTests.cpp`
- Create: `tests/integration/SimulatedViewerTests.cpp`
- Create: `tests/support/QtTestMain.cpp`
- Modify: `tests/support/ViewportTestSupport.hpp`
- Modify: `tests/unit/ui/MainWindowSmokeTests.cpp`
- Create: `tools/viewer-harness/main.cpp`
- Create: `tools/viewer-harness/SimulatorFeed.hpp`
- Create: `tools/viewer-harness/SimulatorFeed.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: a session-owned `core::LatestValueSlot<core::FrameBundle>`, Task 3's view, and `core::IClock`. These dependencies outlive the presenter; all presenter calls run on the UI thread.
- Produces: the following presenter API, deterministic unit tests with `ManualClock`, and a non-shipping simulator feed/harness. Construction binds the view/paint observer but leaves the timer stopped, allowing tests to drive `refresh` explicitly.

```cpp
class FramePresenter final {
public:
    FramePresenter(core::LatestValueSlot<core::FrameBundle>& slot,
                   WorkstationView& view, core::IClock& clock);
    ~FramePresenter();
    FramePresenter(const FramePresenter&) = delete;
    FramePresenter& operator=(const FramePresenter&) = delete;
    void start();
    void stop();
    void refresh();
    void pause();
    void resume();
    void resetSource(core::LatestValueSlot<core::FrameBundle>& freshSlot);
    std::uint64_t displayedFrameCount() const noexcept;
    std::shared_ptr<const core::FrameBundle> presentedBundle() const noexcept;
};
```

`start`/`stop` idempotently control the UI timer, not acquisition or viewer Pause. Destruction stops the timer, disconnects control intents, and clears the viewport observer before the presenter becomes inaccessible. `presentedBundle()` is null until a paint completes; counters reset per source session.

- [ ] **Step 1: Write failing newest-frame and pause tests**

Extend `ViewportTestSupport.hpp` with this Original-only factory. Include `lumora/core/Clock.hpp` and `<optional>`; use the existing Mono8 descriptor contract, factory validation, clock domains, and immutable pixels. No enhancement members are created.

```cpp
inline std::shared_ptr<const core::FrameBundle> makeBundle(
    std::uint32_t width, std::uint32_t height, std::uint64_t id,
    core::IClock& clock, double fps = 30.0) {
    auto display = makeDisplayFrame(width, height, id);
    auto settings = core::AcquisitionSettingsSnapshot::create(
        core::CameraIdentity{"Lumora", "Fixture", "SIM-TEST", "Simulator", std::nullopt},
        core::SourcePixelFormat{"Mono8", 0x01080001U, 8U, 255U,
            core::SourcePacking::Unpacked, core::BitAlignment::LeastSignificant,
            core::StorageType::UInt8},
        core::RegionOfInterest{0U, 0U, width, height}, fps, fps,
        std::nullopt, std::nullopt).value();
    auto raw = core::RawFrame::create(
        id, display->layout, display->pixels,
        core::FrameMetadata{std::nullopt, clock.steadyNow(), clock.utcNow(),
            std::nullopt, std::move(settings)}).value();
    return core::FrameBundle::create(raw, display, nullptr, nullptr).value();
}
```

```cpp
TEST(FramePresenter, ResumeShowsNewestBundleNotBacklog) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    ManualClock clock;
    view.resize(1280, 800);
    view.show();
    FramePresenter presenter(slot, view, clock);
    for (auto id : {1U, 2U, 3U}) {
        (void)slot.publish(lumora::test::makeBundle(64, 32, id, clock));
    }
    presenter.refresh();
    lumora::test::paintWidget(*view.imageViewport());
    ASSERT_NE(presenter.presentedBundle(), nullptr);
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 3U);
    presenter.pause();
    for (auto id : {4U, 5U}) {
        (void)slot.publish(lumora::test::makeBundle(64, 32, id, clock));
    }
    presenter.refresh();
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 3U);
    presenter.resume();
    presenter.refresh();
    lumora::test::paintWidget(*view.imageViewport());
    EXPECT_EQ(presenter.presentedBundle()->sourceFrameId(), 5U);
}
```

Register `FramePresenterTests.cpp` in `lumora_ui_tests` and the `FramePresenter` CTest entry with the common Qt plugin environment. Keep these unit tests independent of the simulator worker and real-time sleeps.

- [ ] **Step 2: Verify presenter is missing**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL for the missing presenter contract. After minimal construction exists, observe a failing newest-frame/paint assertion before implementing delivery.

- [ ] **Step 3: Implement polling presentation**

Use a 17 ms `QTimer` with a monotonic cadence guard so early timer delivery cannot exceed 60 Hz. `refresh` performs a non-waiting latest-slot read, stages only a newer source ID within this session, and always updates status/age even when no frame is accepted. It must never wait for publication, process pixels, or access a camera. Read counters through the API; any outward Qt counter notification is low-frequency, not one per frame.

Track the examined slot revision separately from source IDs and retain at most one pending and one completed bundle. Only the viewport's synchronous completed-paint observer promotes a pending bundle to `presentedBundle`, increments displayed count, and records `clock.steadyNow()`. A rejected or coalesced-away frame, or a repaint of the current image, cannot advance freshness. Frame IDs use optional state so zero is accepted as a valid first ID.

Pause discards any unpainted replacement and freezes the last completed bundle, UTC timestamp, and viewport pixels. Acquisition and slot replacement continue. Resume resets the pending-ID watermark to the completed frame and performs a fresh read with `consumeAfter(0)` before returning to normal revision tracking; this can recover a previously consumed but never painted candidate even when no newer publication occurred during Pause. Do not drain a backlog or require an extra camera frame to resume.

Derive the expected period from the completed bundle's `raw->metadata.acquisitionSettings.actualFps`; use the existing positive, finite metadata contract. In Live, stale is true at `>= max(500 ms, 3 / actualFps seconds)` since the last new completed paint, or when the completed frame's `hostReceiptTime` age reaches that deadline. Thus painting a delayed old bundle cannot make it Current. Use `steadyNow` for elapsed time and `acquisitionUtcTime` only for the displayed timestamp; wall-clock changes must not reset age. With no completed frame, show WaitingForFrame. In Paused, always show the paused timestamp/age; Resume reevaluates stale before an old image could be labeled Current.

`resetSource` requires a slot containing only the new session and an old publisher already stopped/quiescent. Clear the viewport, pending/completed owners, slot revision, ID watermarks, freshness timestamps, and session counters; bind the fresh slot in Live/WaitingForFrame. Preserve whether timer polling was enabled. Do not reuse a slot that can still receive old-session values. The controller in M5 owns this handoff; no new session field is added to core frames in M4.

Add focused tests for paint-not-yet-completed, unsupported-frame rejection, repeated/out-of-order IDs, ID zero, pause between staging and paint, resume without another publication, 499/500 ms at 30 FPS, 3 seconds at 1 FPS, delayed-frame Resume, wall-clock jumps, new-session ID 1 after old-session ID 100, and old-owner release. Manually advance `ManualClock` and drive rendering to keep deadline tests deterministic.

- [ ] **Step 4: Build a test-only simulator harness**

Before writing the feed, register a failing `SimulatedViewer.ResponsiveTenSeconds` integration test that requires actual worker publications and completed paints. Move the existing `QApplication`/GoogleTest `main` unchanged from `MainWindowSmokeTests.cpp` into `tests/support/QtTestMain.cpp`; compile it exactly once into each Qt test executable. Create `lumora_integration_tests` with that main and `SimulatedViewerTests.cpp`, linking `lumora::ui`, `GTest::gtest`, and the non-shipping feed support. Neither Qt executable links `gtest_main`.

Implement `SimulatorFeed` in `tools/viewer-harness/SimulatorFeed.{hpp,cpp}` and reuse it from the integration test and `main.cpp`. Its constructor takes `LatestValueSlot<FrameBundle>&` and `IClock&`; `start()` launches one worker, `stop() noexcept` requests stop and joins idempotently, and `result() const -> Result<void>` returns a synchronized snapshot of any startup/retrieval/publication failure. Its destructor stops/joins. Pools, provider, device creation, open/configure/retrieve/stop/close/destruction all occur on that worker, not the UI thread.

Use a generated MovingBar, full-range Mono8, 640x480, 30 FPS, RealTime pacing, a fixed ten-buffer raw pool, and a cancellation-aware 50 ms retrieve timeout. The test-only full-range Mono8 identity display mapping may share the sealed immutable raw buffer; it must not copy/normalize/enhance on the UI thread or pretend to support high-bit-depth rendering. Build Original-only bundles with matching IDs, identity orientation, `DisplayMapping{0, 255, 255, 1}`, and null enhancement members. Failed pool acquisition/publication is observable and never triggers unbounded allocation. Stop the producer before closing its slot; test owners outlive all workers and presenters.

Under `LUMORA_BUILD_TESTS`, add a static `lumora_viewer_harness_support` target for `SimulatorFeed.cpp` with core/camera-simulator linkage and its private-to-tools/tests include path, plus the `lumora_viewer_harness` executable linking UI and that support. Neither target is installed/packaged or linked into `lumora_app`. The manual executable requires explicit `--start` to start its synthetic feed and retains the evaluation banner. Launch with the configuration-matched native Qt platform plugin for visual checks, not `minimal`; startup/retrieval errors must be visible and recorded, not silently ignored. Production application camera commands remain M5 work.

- [ ] **Step 5: Run normal integration and opt-in stress verification**

The integration driver services Qt events while the real producer runs and repeatedly zooms, pans, pauses, resumes, and resizes. Assert nonzero producer and completed-paint counts, continued publication during Pause, resume to a recent frame, bounded retained ownership, observable worker errors, and clean joins. Do not impose the M8 reference-workstation throughput or <100 ms hardware latency gate on shared CI runners.

Separately test stopped producer publication, suppressed prepared-bundle publication despite continued simulated acquisition, and blocked image acceptance/paint while the status heartbeat continues. The M4 harness models these boundaries; actual independent processing-worker fault tests arrive in M5. A received bundle or scheduled paint must not clear stale. For a full Qt event-loop stall, advance the manual clock without servicing UI work, then assert stale on the first recovery refresh; do not claim an overlay can repaint during a blocked event loop.

Use a shared integration driver for `SimulatedViewer.ResponsiveTenSeconds` and `SimulatedViewer.StressTenMinutes`, passing 10 seconds and 600 seconds respectively. Register only the short case by default; a `stress` label alone does not exclude a test from current CI. Add a root CMake option defaulting OFF, then use exact filters and explicit timeouts:

```cmake
option(LUMORA_ENABLE_STRESS_TESTS "Register opt-in stress tests" OFF)

# tests/CMakeLists.txt; lumora_integration_tests has its own QApplication main.
add_test(NAME SimulatedViewer
  COMMAND lumora_integration_tests --gtest_filter=SimulatedViewer.ResponsiveTenSeconds)
set_tests_properties(SimulatedViewer PROPERTIES TIMEOUT 60
  ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")
if(LUMORA_ENABLE_STRESS_TESTS)
  add_test(NAME SimulatedViewer.Stress
    COMMAND lumora_integration_tests --gtest_filter=SimulatedViewer.StressTenMinutes)
  set_tests_properties(SimulatedViewer.Stress PROPERTIES LABELS stress TIMEOUT 750
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")
endif()
```

Run the red/green unit and integration targets, then the full Linux Debug/Release simulator suites and matching Windows CI. For the long run, configure a Release simulator build with `-DLUMORA_ENABLE_STRESS_TESTS=ON`, build, and run `ctest --preset linux-gcc-release-sim --output-on-failure --no-tests=error -L stress`; repeat the opt-in stress run with the Windows Release preset. Restore the option to OFF afterward so ordinary local runs stay short. Record stress duration, platform, test results, and manual Windows checks in the eventual M4 acceptance record; no stress or visual result is implied by this plan.

- [ ] **Step 6: Commit presentation boundary**

```powershell
git add src/ui tests/unit/ui tests/integration/SimulatedViewerTests.cpp tests/support tools/viewer-harness CMakeLists.txt src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): present latest simulated frame without event backlog"
```

## Milestone 4 acceptance gate

- [ ] Static and simulated display frames render with preserved aspect ratio.
- [ ] Pause freezes the shown frame and Resume jumps directly to newest.
- [ ] Evaluation, PAUSED timestamp/age, and STALE IMAGE / NOT LIVE indications remain persistent in normal and fullscreen-capable layouts.
- [ ] Fit, 100%, zoom, pan, resize, and buffer-lifetime tests pass.
- [ ] No UI header imports a pylon type or performs frame processing.
- [ ] Completed-paint freshness, delayed Resume, source-session reset, and staged-versus-painted Pause regressions pass.
- [ ] Full Linux/GCC and Windows/MSVC Debug/Release simulator suites pass at the recorded source SHA, with the supported Qt platform plugin and no pylon dependency.
- [ ] Normal 10-second CI integration and explicit 10-minute Release stress runs on both platforms pass; the long tests remain opt-in.
- [ ] Native Windows 11 visual/DPI checks are recorded, and the manual synthetic viewer remains responsive at a configured 30 FPS.
- [ ] The harness is excluded from install/package targets; `lumora_app` remains without production live-pipeline composition until M5.
- [ ] Update requirements traceability with actual test names/results and commit a separate M4 acceptance record before starting M5. M13 Windows installation, M14 hardware acceptance, and the separate future clinical-release boundary remain unchanged.
