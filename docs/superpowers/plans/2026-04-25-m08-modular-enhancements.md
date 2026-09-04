# Milestone 8 Modular Enhancement Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement, independently test, compose in one fixed order, and benchmark all initial CPU enhancement stages while preserving U16 precision, plus an exact shared installation-orientation presentation transform.

**Architecture:** Each stage implements `IProcessingStage`, accepts typed U16 views, writes a separate pooled U16 view, and declares traits used by `PipelineCompiler`. Reference formulas and images guard deterministic behavior.

**Tech Stack:** C++20, OpenCV core/imgproc, GoogleTest/CTest, custom benchmark executable.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Keep all enhancement output U16 until terminal display mapping.
- Do not mutate raw input or allocate per frame after workspace warm-up.
- Disabled stages perform no pixel traversal.
- Stage errors affect one frame and include the stable stage ID.
- Do not add GPU, AI, arbitrary-angle rotation, or temporal history.
- The designated Windows reference workstation must be selected and recorded before the milestone performance gate can pass; CI timing is informational only.

---

### Task 1: Brightness, contrast, gamma, and inversion stages

**Files:**
- Create: `src/processing/include/lumora/processing/ToneStages.hpp`
- Create: `src/processing/src/BrightnessContrastStage.cpp`
- Create: `src/processing/src/GammaStage.cpp`
- Create: `src/processing/src/InvertStage.cpp`
- Create: `tests/unit/processing/ToneStageTests.cpp`

**Interfaces:**
- Consumes: `BrightnessContrastParameters`, `GammaParameters`, `InvertParameters`, U16 image views, and workspace LUT storage.
- Produces: three `IProcessingStage` implementations registered under their canonical `StageId` values.

- [ ] **Step 1: Write scalar-reference tests**

```cpp
TEST(ToneStages, GammaPreservesEndpointsAndUsesCachedRevision) {
    GammaStage stage;
    auto first = run(stage, {0, 16384, 32768, 65535}, GammaParameters{2.0});
    EXPECT_EQ(first.front(), 0);
    EXPECT_EQ(first.back(), 65535);
    const auto builds = stage.lutBuildCountForTest();
    run(stage, {1, 2, 3}, GammaParameters{2.0});
    EXPECT_EQ(stage.lutBuildCountForTest(), builds);
}
```

- [ ] **Step 2: Verify stages are absent**

Build `lumora_processing_tests`; expect failure.

- [ ] **Step 3: Implement exact formulas**

Brightness adds `round(offsetNormalized * 65535)`. Contrast applies `(value-32767.5)*factor+32767.5`. Gamma uses `round(pow(value/65535.0, 1/gamma)*65535)`. All outputs saturate to U16. Invert maps `v` to `65535-v`.

- [ ] **Step 4: Cache the gamma LUT by parameter revision**

Build all 65,536 entries only when gamma changes. Store the LUT in the processing workspace/stage instance; no per-frame heap allocation.

- [ ] **Step 5: Test full-domain equivalence and strides**

Compare all possible input values to scalar formulas at boundary/mid parameter values, including padded rows and in/out non-aliasing.

- [ ] **Step 6: Commit tone stages**

```powershell
git add src/processing tests/unit/processing/ToneStageTests.cpp
git commit -m "feat(processing): add deterministic U16 tone stages"
```

### Task 2: U16 CLAHE stage

**Files:**
- Create: `src/processing/include/lumora/processing/ClaheStage.hpp`
- Create: `src/processing/src/ClaheStage.cpp`
- Create: `tests/unit/processing/ClaheStageTests.cpp`
- Create: `tests/reference/processing/clahe-reference.json`

**Interfaces:**
- Consumes: U16 single-channel views and `ClaheParameters`.
- Produces: OpenCV-backed `ClaheStage` with reusable `cv::CLAHE` object and bounded tile configuration.

- [ ] **Step 1: Write identity-shape and reference tests**

Generate a fixed U16 gradient-plus-impulse input, apply clip 2.0 with 8x8 tiles, and compare output against the designated Windows reference output plus the documented Linux/Windows per-algorithm tolerance for the pinned OpenCV versions. Do not assume byte-identical OpenCV output across toolchains.

- [ ] **Step 2: Verify CLAHE stage is missing**

Build `lumora_processing_tests`; expect failure.

- [ ] **Step 3: Implement row-view wrapping and object reuse**

Wrap input/output with `CV_16UC1` matrices using explicit byte strides. Reconfigure clip limit and tile grid only when parameters change. Call `collectGarbage` only during resolution change or stage destruction, never per frame.

- [ ] **Step 4: Test tiles and small-image rejection**

Cover 2x2, 8x8, and 32x32 grids; dimensions not divisible by tiles; uniform input; full dynamic range; and images too small for the requested grid. Reject invalid definitions before execution.

- [ ] **Step 5: Commit CLAHE**

```powershell
git add src/processing tests/unit/processing/ClaheStageTests.cpp tests/reference/processing/clahe-reference.json
git commit -m "feat(processing): add reusable U16 CLAHE stage"
```

### Task 3: Configurable denoise and unsharp sharpening

**Files:**
- Create: `src/processing/include/lumora/processing/DenoiseStage.hpp`
- Create: `src/processing/include/lumora/processing/SharpenStage.hpp`
- Create: `src/processing/src/DenoiseStage.cpp`
- Create: `src/processing/src/SharpenStage.cpp`
- Create: `tests/unit/processing/DenoiseStageTests.cpp`
- Create: `tests/unit/processing/SharpenStageTests.cpp`

**Interfaces:**
- Consumes: U16 views, denoise mode/kernel/sigma, sharpen radius/amount/threshold, and preallocated scratch images.
- Produces: Gaussian/median denoise and thresholded unsharp mask stages.

- [ ] **Step 1: Write impulse and edge-response tests**

```cpp
TEST(DenoiseStage, MedianRemovesSinglePixelImpulse) {
    auto input = constantImage(7, 7, 1000);
    input(3, 3) = 65535;
    auto output = runMedian(input, 3);
    EXPECT_EQ(output(3, 3), 1000);
}
```

For sharpen, assert a step edge gains the expected signed detail contribution, flat fields remain unchanged, threshold suppresses small differences, and saturation never wraps.

- [ ] **Step 2: Verify missing stages fail**

Build `lumora_processing_tests`; expect failure.

- [ ] **Step 3: Implement denoise modes**

Use OpenCV Gaussian blur for configured odd kernel/sigma and median blur for supported odd kernels. Validate OpenCV's U16/kernel restrictions in `PipelineCompiler` and return a stable error rather than relying on an OpenCV assertion.

- [ ] **Step 4: Implement unsharp mask**

Blur into scratch, calculate signed difference at sufficient precision, apply threshold and amount, saturate to U16, and write output. Scratch comes from `ProcessingWorkspace` and is resized only while stopped.

- [ ] **Step 5: Test borders, strides, full range, and reuse**

Fix border mode to `BORDER_REFLECT_101` and assert it in reference cases. Count workspace allocations across 1,000 same-size frames and expect zero after warm-up.

- [ ] **Step 6: Commit detail stages**

```powershell
git add src/processing tests/unit/processing/DenoiseStageTests.cpp tests/unit/processing/SharpenStageTests.cpp
git commit -m "feat(processing): add bounded U16 detail stages"
```

### Task 4: Shared installation-orientation presentation transform

**Files:**
- Create: `src/processing/include/lumora/processing/OrientationTransform.hpp`
- Create: `src/processing/src/OrientationTransform.cpp`
- Create: `tests/unit/processing/OrientationTransformTests.cpp`

**Interfaces:**
- Consumes: administrator-managed `Orientation { flipHorizontal, flipVertical, rotation }` and format-aware Original/Enhanced display views.
- Produces: exact shared flips and 0/90/180/270-degree rotations with output-layout reporting; it never changes RawFrame or native-orientation Enhanced U16.

- [ ] **Step 1: Write coordinate-mapping tests**

Use paired Original/Enhanced 2x3 images containing unique values and assert exact matrices for every rotation, each flip, and flip-plus-rotation order. Define order as horizontal flip, vertical flip, then clockwise rotation, and assert both presentation paths receive exactly the same transform.

- [ ] **Step 2: Verify stage is missing**

Build `lumora_processing_tests`; expect failure.

- [ ] **Step 3: Implement exact integer transforms**

Use an exact integer mapping with no interpolation for each supported `DisplayStorage`. Report swapped output dimensions for 90/270 degrees and request appropriately shaped pool leases before presentation.

- [ ] **Step 4: Test non-square, odd, padded, and identity cases**

Assert no sample changes, no aspect distortion, and input remains unchanged. Reject an output pool block too small for rotated stride.

- [ ] **Step 5: Commit geometry**

```powershell
git add src/processing tests/unit/processing/OrientationTransformTests.cpp
git commit -m "feat(processing): add shared installation orientation"
```

### Task 5: Full pipeline composition, reference presets, and benchmarks

**Files:**
- Create: `tests/reference/processing/generate-reference-fixtures.cpp`
- Create: `tests/reference/processing/PipelineReferenceTests.cpp`
- Create: `src/processing/include/lumora/processing/ProcessingDefaults.hpp`
- Create: `src/processing/src/ProcessingDefaults.cpp`
- Create: `benchmarks/processing/ProcessingBenchmark.cpp`
- Create: `benchmarks/processing/reference-workstation.json`
- Modify: `src/processing/src/ProcessingPipeline.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: every initial stage, compiler, workspace, simulator fixtures, and benchmark configuration.
- Produces: complete fixed-order registry, enable/disable execution, `makeStandardPipelineDefinition()`, reviewed reference results/tolerances, and JSON benchmark output.

- [ ] **Step 1: Write failing complete-pipeline tests**

Test the canonical ordered definition and multiple reordered definitions; each reorder must be rejected without replacing the active revision. Disable each permitted stage in turn and assert its invocation counter remains zero while neighboring stages still execute.

- [ ] **Step 2: Register every canonical stage**

The default registry contains exactly Normalize, WindowLevel, BrightnessContrast, Gamma, Clahe, Denoise, Sharpen, and Invert in that order. Orientation is separate shared presentation-profile data. Unknown IDs or reordered IDs from serialized configuration are rejected without changing the active pipeline.

- [ ] **Step 3: Define the canonical Standard pipeline**

`makeStandardPipelineDefinition()` returns the exact stage order and values used by benchmarks. Milestone 9's Standard preset must deserialize to an equal normalized definition; this prevents UI preset drift from the performance baseline.

- [ ] **Step 4: Generate and review reference fixtures**

The generator creates deterministic 16-bit ramp, gradient, edge, noise, and non-square orientation PGM files plus expected parameters. Exact stages commit hashes; CLAHE, Gaussian/median denoise, and sharpen commit designated-Windows outputs with reviewed maximum-absolute-error and image-difference thresholds for Linux/Windows. Tests never regenerate expected outputs automatically, and changing a threshold requires review.

- [ ] **Step 5: Implement benchmark output**

Measure each stage and full Standard definition at 512x512, 1024x1024, and 2048x2048 after 100 warm-up frames and at least 500 measured frames. Emit median/P95 milliseconds, FPS, allocation count after warm-up, and working-set samples as JSON.

- [ ] **Step 6: Run functional and performance gates**

Run all processing tests in Linux Debug and require matching Windows CI, then run the Release benchmark on the recorded Windows reference workstation. Full Standard P95 must remain within the 33.3 ms frame budget at 2048x2048; if not, profile and optimize before accepting the milestone. CI benchmark smoke tests carry no workstation-dependent threshold. Never relax bounded freshness or paused/stale indications to meet timing.

- [ ] **Step 7: Commit pipeline evidence**

```powershell
git add src/processing tests/reference benchmarks src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "perf(processing): verify complete U16 enhancement pipeline"
```

## Milestone 8 acceptance gate

- [ ] Every initial stage passes identity, boundary, stride, and reference tests.
- [ ] Stages can be enabled/disabled only in the canonical order; reordered definitions are rejected without mutating raw input or active configuration.
- [ ] Same-size processing performs zero heap allocations after workspace warm-up.
- [ ] Exact-stage hashes and reviewed OpenCV tolerances pass on Linux/GCC and Windows/MSVC.
- [ ] Standard processing meets the 2048x2048 30 FPS reference-workstation gate.
- [ ] Original and Enhanced use the same installation orientation while raw and enhanced U16 storage remain native-orientation.
