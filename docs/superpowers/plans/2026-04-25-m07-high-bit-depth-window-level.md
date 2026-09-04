# Milestone 7 High-Bit-Depth and Window-Level Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the canonical unsigned 16-bit processing path, immutable Original path, fixed-order validated pipeline definitions, window/level, and a format-aware display-mapping boundary whose evaluation implementation produces Gray8.

**Architecture:** Raw sensor values remain immutable. Processing stages read typed image views and write separate pooled buffers; window/level operates in U16 and only the display mapper produces U8.

**Tech Stack:** C++20, OpenCV core/imgproc, existing core/application modules, GoogleTest/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Never mutate or alias writable memory from `RawFrame`.
- Packed 10/12-bit values are LSB-aligned numeric samples in U16 before normalization.
- Normalization is based on the declared source sample maximum/valid bits, never per-frame min/max.
- Window/level remains U16; display mapping is the only U16-to-Gray8 boundary in evaluation composition.
- A configuration revision activates only between complete frames.

---

### Task 1: Processing configuration and stage contracts

**Files:**
- Create: `src/processing/include/lumora/processing/ImageView.hpp`
- Create: `src/processing/include/lumora/processing/ProcessingConfiguration.hpp`
- Create: `src/processing/include/lumora/processing/IProcessingStage.hpp`
- Create: `src/processing/include/lumora/processing/PipelineCompiler.hpp`
- Create: `src/processing/src/PipelineCompiler.cpp`
- Create: `tests/unit/processing/PipelineCompilerTests.cpp`

**Interfaces:**
- Consumes: core image layout/result types and U16 storage.
- Produces: `StageId`, all initial parameter structs, `StageParameters`, `StageDefinition`, `PipelineDefinition`, `ImageDomain`, `StageTraits`, `IProcessingStage`, and `PipelineCompiler::compile`.

- [ ] **Step 1: Write failing domain/order/configuration tests**

```cpp
TEST(PipelineCompiler, RejectsDuplicateMandatoryNormalization) {
    PipelineDefinition definition = defaultPipeline();
    definition.stages.insert(definition.stages.begin(), definition.stages.front());
    auto result = PipelineCompiler(stageRegistry()).compile(definition);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "duplicate_normalization");
}
```

- [ ] **Step 2: Verify missing contracts fail**

Run: `cmake --build --preset linux-gcc-debug-sim --target lumora_processing_tests`

Expected: FAIL.

- [ ] **Step 3: Define the complete configuration vocabulary once**

```cpp
enum class StageId { Normalize, WindowLevel, BrightnessContrast, Gamma,
    Clahe, Denoise, Sharpen, Invert };
enum class ImageDomain { SensorU16, CanonicalU16 };
enum class DenoiseMode { Gaussian, Median };
enum class Rotation { Degrees0, Degrees90, Degrees180, Degrees270 };

using StageParameters = std::variant<NormalizationParameters,
    WindowLevelParameters, BrightnessContrastParameters, GammaParameters,
    ClaheParameters, DenoiseParameters, SharpenParameters,
    InvertParameters>;
```

Parameter bounds are explicit: window `[1,65535]`, level `[0,65535]`, brightness `[-1,1]`, contrast `[0,4]`, gamma `[0.1,5]`, CLAHE clip `[0.1,40]` and tiles `[2,32]`, odd denoise kernel `3/5/7`, sharpen amount `[0,5]`, radius `[0.5,5]`, threshold `[0,65535]`.

`StageTraits` also declares dimension changes, required scratch images, bounded history-frame count, calibration-asset requirement, and execution backend. Initial stages declare zero history, no calibration asset, and CPU execution. Dark-frame, flat-field, bad-pixel, temporal, and GPU stages can therefore extend the registry without changing worker or frame-exchange contracts; their algorithms and assets remain out of this release.

- [ ] **Step 4: Implement compile-time/runtime validation**

Every `StageDefinition` parameter variant must match its `StageId`; stage IDs are unique; Normalize is first and mandatory; the only valid order is Normalize -> WindowLevel -> BrightnessContrast -> Gamma -> Clahe -> Denoise -> Sharpen -> Invert. Enabled adjacent domains must match; disabled stages remain serializable but are omitted from execution without changing their canonical positions. Return all validation violations in stable stage order.

- [ ] **Step 5: Test complete parameter boundaries and fixed-order rules**

Cover minimum/maximum accepted values, values immediately outside bounds, wrong variant, duplicate IDs, missing normalization, disabled stages, and rejection of every reordered or invalid-domain definition.

- [ ] **Step 6: Commit contracts**

```powershell
git add src/processing tests/unit/processing/PipelineCompilerTests.cpp
git commit -m "feat(processing): define validated pipeline contracts"
```

### Task 2: Deterministic sensor normalization

**Files:**
- Create: `src/processing/include/lumora/processing/NormalizeStage.hpp`
- Create: `src/processing/src/NormalizeStage.cpp`
- Create: `tests/unit/processing/NormalizeStageTests.cpp`

**Interfaces:**
- Consumes: sensor U8/U16 image view, declared `sampleMaximum`/valid bits, alignment, and a writable canonical U16 view.
- Produces: `NormalizeStage::process` mapping `[0, sampleMaximum]` to `[0,65535]` deterministically.

- [ ] **Step 1: Write exact bit-depth tests**

```cpp
TEST(NormalizeStage, Mono12ScalesDeclaredRangeNotFrameRange) {
    const std::array<std::uint16_t, 4> input{0, 1, 2048, 4095};
    auto output = runNormalize(input, 12);
    EXPECT_EQ(output, (std::array<std::uint16_t, 4>{0, 16, 32776, 65535}));
}
```

Expected values use integer rounding defined as `(value * 65535 + sourceMax / 2) / sourceMax`; do not replace the assertion with a floating tolerance.

- [ ] **Step 2: Verify stage is missing**

Build `lumora_processing_tests`; expect failure.

- [ ] **Step 3: Implement row/stride-aware normalization**

Support declared maxima 1 through 65535 with valid/storage consistency, including camera maxima for 8, 10, 12, and 16 bits. Reject any sample above `sampleMaximum` with `sample_exceeds_source_maximum`; diagnostics identify the first failing coordinate. No production path clamps the malformed sample silently.

- [ ] **Step 4: Test non-contiguous rows and temporal stability**

Use padded input/output strides and two frames with different observed minima/maxima but the same sample at one coordinate; assert that coordinate produces the same normalized value.

- [ ] **Step 5: Run and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R NormalizeStage`

```powershell
git add src/processing tests/unit/processing/NormalizeStageTests.cpp
git commit -m "feat(processing): add deterministic U16 normalization"
```

### Task 3: U16 window/level stage and terminal display mapper

**Files:**
- Create: `src/processing/include/lumora/processing/WindowLevelStage.hpp`
- Create: `src/processing/include/lumora/processing/DisplayMapper.hpp`
- Create: `src/processing/src/WindowLevelStage.cpp`
- Create: `src/processing/src/DisplayMapper.cpp`
- Create: `tests/unit/processing/WindowLevelStageTests.cpp`
- Create: `tests/unit/processing/DisplayMapperTests.cpp`

**Interfaces:**
- Consumes: canonical U16 view, `WindowLevelParameters`, and pooled U16/U8 outputs.
- Produces: exact U16 window mapping and a format-aware display-mapper interface with exact U16-to-Gray8 evaluation mapping.

- [ ] **Step 1: Write failing endpoint tests**

```cpp
TEST(WindowLevelStage, MapsBelowInsideAndAboveWindow) {
    WindowLevelParameters p{.window = 2000.0, .level = 3000.0};
    auto out = runWindowLevel({1999, 2000, 3000, 4000, 4001}, p);
    EXPECT_EQ(out.front(), 0);
    EXPECT_EQ(out[1], 0);
    EXPECT_NEAR(out[2], 32768, 1);
    EXPECT_EQ(out[3], 65535);
    EXPECT_EQ(out.back(), 65535);
}
```

- [ ] **Step 2: Verify mapper/stage tests fail**

Build `lumora_processing_tests`; expect missing types.

- [ ] **Step 3: Implement a documented inclusive mapping**

Define lower=`level-window/2`, upper=`level+window/2`; values at/below lower map to 0, at/above upper map to 65535, and interior values use rounded linear interpolation. Clamp mathematical bounds to the canonical domain without changing configuration values.

- [ ] **Step 4: Implement terminal mapper**

Map U16 `[0,65535]` to U8 `[0,255]` with `(value + 128) / 257`, preserving black, midpoint rounding, and white. Support padded strides and reject aliasing/size mismatch.

- [ ] **Step 5: Add exhaustive scalar equivalence test**

Generate all 65,536 U16 values and compare vectorized/stage output to the scalar reference formula. Repeat for window widths 1, 2, 4096, and 65535 and levels at 0, midpoint, and 65535.

- [ ] **Step 6: Commit window/display mapping**

```powershell
git add src/processing tests/unit/processing/WindowLevelStageTests.cpp tests/unit/processing/DisplayMapperTests.cpp
git commit -m "feat(processing): add U16 window level and display mapping"
```

### Task 4: Processing pipeline executor and original path

**Files:**
- Create: `src/processing/include/lumora/processing/ProcessingPipeline.hpp`
- Create: `src/processing/include/lumora/processing/ProcessingWorkspace.hpp`
- Create: `src/processing/include/lumora/processing/FrameProcessingEngine.hpp`
- Create: `src/processing/src/ProcessingPipeline.cpp`
- Create: `src/processing/src/ProcessingWorkspace.cpp`
- Create: `src/processing/src/FrameProcessingEngine.cpp`
- Create: `tests/unit/processing/FrameProcessingEngineTests.cpp`
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: compiled pipeline, raw/processing/display pools, Normalize, WindowLevel, DisplayMapper, and `IFrameProcessor`.
- Produces: `ProcessingPipeline::activate`, `FrameProcessingEngine::process`, atomic configuration revision, and paired Original/Enhanced bundles.

- [ ] **Step 1: Write failing original-preservation and pairing tests**

```cpp
TEST(FrameProcessingEngine, ProcessingCannotChangeRawSamples) {
    auto raw = makeMono12Frame({0, 100, 2048, 4095});
    const auto before = sha256ForTest(raw->pixels.bytes());
    auto bundle = engine().process(raw).value();
    EXPECT_EQ(sha256ForTest(raw->pixels.bytes()), before);
    EXPECT_EQ(bundle->raw->frameId, bundle->enhanced->sourceFrameId);
    EXPECT_EQ(bundle->raw->frameId, bundle->enhancedDisplay->sourceFrameId);
}
```

- [ ] **Step 2: Verify engine is missing**

Build `lumora_processing_tests`; expect failure.

- [ ] **Step 3: Implement preallocated workspace and pipeline swap**

`ProcessingWorkspace` acquires two U16 leases for ping-pong execution and two Gray8 leases for Original/Enhanced evaluation display. Resolution changes require a stopped-state `prepare(layout)` call. `activate` compiles a complete fixed-order definition then swaps an immutable compiled pipeline under a short mutex/atomic shared pointer between frames.

- [ ] **Step 4: Implement Original and Enhanced routes**

Original executes Normalize, the configured WindowLevel, and DisplayMapper and is described as `Original (display mapped)`. Enhanced executes the full fixed-order enabled pipeline and DisplayMapper. Neither route mutates RawFrame; installation orientation is a shared presentation transform introduced in Milestone 8, not a processing stage.

- [ ] **Step 5: Replace production pass-through processor**

Wire `FrameProcessingEngine` into `LivePipeline`. Retain the Mono8 pass-through only as a focused test fixture, not production composition.

- [ ] **Step 6: Run integration tests at every valid bit depth**

Stream simulator frames at 8/10/12/16 valid bits, assert paired IDs, exact raw hashes, valid U8 displays, configuration revision changes only between frames, and pool counts return after shutdown.

- [ ] **Step 7: Commit high-depth engine**

```powershell
git add src/processing src/app/main.cpp tests/unit/processing/FrameProcessingEngineTests.cpp tests/integration
git commit -m "feat(processing): integrate immutable high-depth frame engine"
```

## Milestone 7 acceptance gate

- [ ] Known 8/10/12/16-bit inputs produce exact canonical values.
- [ ] Raw hashes remain unchanged through Original and Enhanced processing.
- [ ] Window/level and display mappings pass exhaustive scalar comparison.
- [ ] Original and Enhanced displays always share the raw frame ID.
- [ ] No U8 conversion occurs before `DisplayMapper`.
- [ ] Linux/GCC and Windows/MSVC produce exact normalization, window/level, inversion, and Gray8 mapping results; reordered definitions are rejected.
