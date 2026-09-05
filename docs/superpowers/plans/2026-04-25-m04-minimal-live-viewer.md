# Milestone 4 Minimal Live Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display immutable format-aware grayscale frames in a responsive Qt workstation viewport with evaluation, paused, and stale safety indications plus fit, 100%, zoom, and pan.

**Architecture:** The UI consumes `FrameBundle` values through a presentation adapter and never knows how a camera or processor produced them. Rendering remains on the Qt UI thread; test harnesses publish prepared display frames from another thread.

**Tech Stack:** C++20, Qt 6 Widgets/Gui/Test, CMake, GoogleTest/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

**Execution readiness (2026-09-05):** Not started; M3 Windows/MSVC acceptance is pending. Read the [M4 preflight](../../architecture/milestones/m04-preflight.md) for merged API/build integration details and the Fit-limit and headless-plugin instructions to resolve before executing the affected tasks.

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

---

### Task 1: Viewport transform model

**Files:**
- Create: `src/ui/include/lumora/ui/ViewportTransform.hpp`
- Create: `src/ui/src/ViewportTransform.cpp`
- Create: `tests/unit/ui/ViewportTransformTests.cpp`

**Interfaces:**
- Consumes: image and viewport sizes expressed as Qt-independent doubles.
- Produces: `ViewportTransform::fit`, `actualPixels`, `zoomAt`, `panBy`, `imageToViewport`, and clamped viewport state.

- [ ] **Step 1: Write failing geometry tests**

```cpp
TEST(ViewportTransform, FitPreservesAspectRatio) {
    auto view = ViewportTransform::fit({2048, 1024}, {1000, 800});
    EXPECT_DOUBLE_EQ(view.scale(), 1000.0 / 2048.0);
    EXPECT_NEAR(view.imageCenterInViewport().y, 400.0, 1e-9);
}
```

- [ ] **Step 2: Verify the missing model fails to compile**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL.

- [ ] **Step 3: Implement transform operations**

```cpp
class ViewportTransform final {
public:
    static ViewportTransform fit(Size image, Size viewport);
    static ViewportTransform actualPixels(Size image, Size viewport, double devicePixelRatio);
    void zoomAt(Point viewportPoint, double factor);
    void panBy(Vector delta);
};
```

Clamp scale to `[0.05, 32.0]`; retain the image point below the cursor during zoom; center axes where the image is smaller than the viewport; avoid NaN/Inf for zero-size transient resize events.

- [ ] **Step 4: Add zoom/pan/resize cases**

Cover repeated zoom in/out, cursor anchoring, pan clamping, Fit after resize, 100% under device pixel ratios 1.0/1.25/1.5/2.0, and zero viewport.

- [ ] **Step 5: Run and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R ViewportTransform`

```powershell
git add src/ui tests/unit/ui/ViewportTransformTests.cpp
git commit -m "feat(ui): add deterministic viewport transforms"
```

### Task 2: ImageViewport with safe display-buffer lifetime

**Files:**
- Create: `src/ui/include/lumora/ui/ImageViewport.hpp`
- Create: `src/ui/src/ImageViewport.cpp`
- Create: `tests/unit/ui/ImageViewportTests.cpp`
- Modify: `src/ui/resources/lumora.qrc`

**Interfaces:**
- Consumes: `std::shared_ptr<const DisplayFrame>` and `ViewportTransform`.
- Produces: `ImageViewport::present`, `setFitMode`, `setActualPixels`, `zoomIn`, `zoomOut`, and Qt mouse/wheel interactions.

- [ ] **Step 1: Write failing buffer-lifetime and aspect tests**

```cpp
TEST(ImageViewport, RetainsPixelsUntilReplacement) {
    std::weak_ptr<const DisplayFrame> weak;
    ImageViewport viewport;
    {
        auto frame = makeDisplayFrame(64, 32, 7);
        weak = frame;
        viewport.present(frame);
    }
    EXPECT_FALSE(weak.expired());
    viewport.present(makeDisplayFrame(64, 32, 8));
    EXPECT_TRUE(weak.expired());
}
```

- [ ] **Step 2: Verify missing viewport behavior fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL.

- [ ] **Step 3: Implement presentation and paint**

Validate the `DisplayFrame` format, width, height, stride, and payload again at the UI boundary. Evaluation composition accepts `Gray8` and constructs a non-owning `QImage::Format_Grayscale8` while retaining the owning frame. Reject unsupported formats explicitly; keep the viewport behind a renderer interface so future `Gray16`/calibrated 10-bit rendering does not change core frame contracts. Paint letterbox regions with `#16181c` and use smooth transformation only when scale is not exactly 1:1.

```cpp
class ImageViewport final : public QWidget {
public:
    Result<void> present(std::shared_ptr<const DisplayFrame> frame);
    std::uint64_t presentedFrameId() const noexcept;
};
```

- [ ] **Step 4: Implement interactions**

Wheel zooms around pointer; left drag pans only while larger than viewport; double-click enters Fit; keyboard actions expose Fit and 100%. A new frame preserves transform unless image dimensions change, in which case Fit recomputes and manual mode clamps.

- [ ] **Step 5: Render deterministic offscreen images**

Render a 2:1 fixture into square, portrait, and wide viewports; compare image bounds and neutral bars rather than platform-specific text pixels. Run with `QT_QPA_PLATFORM=offscreen`.

- [ ] **Step 6: Commit viewport**

```powershell
git add src/ui tests/unit/ui/ImageViewportTests.cpp
git commit -m "feat(ui): add safe grayscale image viewport"
```

### Task 3: Workstation shell layout and status model

**Files:**
- Create: `src/ui/include/lumora/ui/WorkstationStatus.hpp`
- Create: `src/ui/include/lumora/ui/WorkstationView.hpp`
- Create: `src/ui/src/WorkstationView.cpp`
- Create: `tests/unit/ui/WorkstationViewTests.cpp`
- Modify: `src/ui/src/MainWindow.cpp`

**Interfaces:**
- Consumes: presentation-safe status text/enums and `ImageViewport`.
- Produces: fixed sidebar/right-viewer layout, header/footer status controls, `ViewerState { Live, Paused }`, release/staleness presentation state, and stable Qt object names for automated tests.

- [ ] **Step 1: Write a failing widget-structure test**

```cpp
TEST(WorkstationView, ImageAreaDominatesInitialLayout) {
    WorkstationView view;
    view.resize(1280, 800);
    view.show();
    QCoreApplication::processEvents();
    EXPECT_LT(view.sidebar()->width(), view.imageViewport()->width());
    EXPECT_EQ(view.viewerState(), ViewerState::Live);
}
```

- [ ] **Step 2: Verify missing view fails**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_ui_tests`

Expected: FAIL.

- [ ] **Step 3: Implement clean initial layout**

Create header, fixed-width left sidebar, expanding viewer, and compact footer. At this milestone the sidebar contains the display-mode area, Pause/Live, Fit, 100%, and zoom controls; processing/capture controls are added by their numbered milestone plans. Render a persistent compile-time-controlled `EVALUATION — NOT FOR CLINICAL USE` banner in normal and fullscreen-capable layout. Use translation-ready English text, accessible names, and stable object names.

- [ ] **Step 4: Wire viewer controls without camera calls**

Qt actions invoke `ImageViewport` methods or emit presentation intent. Pause changes view state only and adds a persistent high-contrast `PAUSED` overlay with the frozen frame UTC timestamp and increasing age. The view exposes no `ICameraDevice` reference and includes no camera adapter header.

- [ ] **Step 5: Run UI tests and manual visual check**

Test keyboard activation, resize, collapsed unavailable state, and status colors without relying solely on color. Manually check 1280x720, 1920x1080, and Windows scaling 125%.

- [ ] **Step 6: Commit workstation shell**

```powershell
git add src/ui tests/unit/ui/WorkstationViewTests.cpp
git commit -m "feat(ui): add minimal workstation layout"
```

### Task 4: Latest-frame presenter and simulated viewer integration test

**Files:**
- Create: `src/ui/include/lumora/ui/FramePresenter.hpp`
- Create: `src/ui/src/FramePresenter.cpp`
- Create: `tests/integration/SimulatedViewerTests.cpp`
- Create: `tools/viewer-harness/main.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `LatestValueSlot<FrameBundle>`, `WorkstationView`, and UI timer ticks.
- Produces: `FramePresenter::refresh`, pause/resume semantics, displayed frame count, and a non-shipping manual viewer harness.

- [ ] **Step 1: Write failing newest-frame and pause tests**

```cpp
TEST(FramePresenter, ResumeShowsNewestBundleNotBacklog) {
    LatestValueSlot<FrameBundle> slot;
    WorkstationView view;
    FramePresenter presenter(slot, view);
    publishBundles(slot, {1, 2, 3});
    presenter.refresh();
    EXPECT_EQ(view.imageViewport()->presentedFrameId(), 3U);
    presenter.pause();
    publishBundles(slot, {4, 5});
    presenter.refresh();
    EXPECT_EQ(view.imageViewport()->presentedFrameId(), 3U);
    presenter.resume();
    presenter.refresh();
    EXPECT_EQ(view.imageViewport()->presentedFrameId(), 5U);
}
```

- [ ] **Step 2: Verify presenter is missing**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_integration_tests`

Expected: FAIL.

- [ ] **Step 3: Implement polling presentation**

`FramePresenter` owns a `QTimer` interval suitable for at most 60 Hz, tracks the last consumed slot revision, displayed frame ID, presentation timestamp, and expected frame period, and calls `present` only for newer IDs. In Live, lack of a successfully presented frame for `max(500 ms, 3 expected frame periods)` displays persistent `STALE IMAGE / NOT LIVE` over the retained frame until successful presentation. It emits low-frequency counters; it does not receive a signal per frame.

- [ ] **Step 4: Build a test-only simulator harness**

The harness runs a simulator producer on `std::jthread`, converts configured Mono8 frames into display frames without enhancement, publishes bundles, and stops by stop token. It is excluded from install/package targets.

- [ ] **Step 5: Run a 10-minute responsiveness integration test**

Publish at 30 FPS while repeatedly zooming, panning, pausing, resuming, and resizing. Separately stop publication, prevent processing publication, and suppress a UI refresh to prove the stale indication uses successful presentation rather than mere camera activity. Assert paused timestamp/age, final frame freshness, latest-slot capacity one, and clean worker join. Keep the normal CI form to 10 seconds and label the 10-minute form `stress`.

- [ ] **Step 6: Commit presentation boundary**

```powershell
git add src/ui tests/integration/SimulatedViewerTests.cpp tools/viewer-harness src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): present latest simulated frame without event backlog"
```

## Milestone 4 acceptance gate

- [ ] Static and simulated display frames render with preserved aspect ratio.
- [ ] Pause freezes the shown frame and Resume jumps directly to newest.
- [ ] Evaluation, PAUSED timestamp/age, and STALE IMAGE / NOT LIVE indications remain persistent in normal and fullscreen-capable layouts.
- [ ] Fit, 100%, zoom, pan, resize, and buffer-lifetime tests pass.
- [ ] No UI header imports a pylon type or performs frame processing.
- [ ] Manual viewer remains responsive at simulated 30 FPS.
