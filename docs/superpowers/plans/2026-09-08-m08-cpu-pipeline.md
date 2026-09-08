# M8 Exact CPU Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce complete Standard pipeline latency using exact identity copies, blocked orientation, and measured prepared parallel/SIMD processing.

**Architecture:** Keep stage invocation and output contracts. One engine-owned prepared executor serves CLAHE and detail stages through private factory interfaces; public standalone factories remain serial. Admit all stage scratch before allocating and retain only measured optimizations.

**Tech Stack:** C++20, pinned Qt/OpenCV, CMake/Ninja, GoogleTest, GCC/MSVC, existing processing evidence tools.

**Spec:** [2026-09-08-m08-cpu-pipeline-design.md](../specs/2026-09-08-m08-cpu-pipeline-design.md)

## Global Constraints

- Preserve exact U16 and Gray8/Gray16 output, stage order, validation precedence, first-invalid-sample diagnostics, source immutability, and destination padding.
- Preserve supported unaligned byte views, unequal valid strides, rectangular extents, and all existing orientation combinations.
- Preserve zero measured per-frame C++ allocations/deallocations and controlled glibc allocator events after preparation and warm-up, including helper threads.
- Preserve bounded active/in-flight/candidate stage ownership, resource admission before allocation, fallback/retry behavior, and prepared stage reuse.
- Keep public standalone stage factories serial and source-compatible. Engine execution may use the private prepared executor.
- Use C++20 with the pinned Qt/OpenCV dependencies and supported GCC/MSVC configurations; add no dependency, global thread-pool setting, fast-math flag, unconditional AVX instruction, or GPU backend.
- Keep the existing 65,536 CLAHE histogram bins, clipping/redistribution/CDF/interpolation equations and per-pixel floating-point operation order.
- Thread creation and destruction occur during stopped engine preparation/destruction. Activation creates no additional worker pool. A session has at most four execution slots, including its calling processing thread.
- Keep raw and Enhanced U16 pixels in native orientation; apply orientation to the two display products as before.

## Execution and evidence rules

Worktree: `.worktrees/m08-tone-stages`; local branch `perf/m08-cpu-pipeline` starts at `e753fd0`. The previous branch and all ignored QA are preserved. The controller owns Git, builds used for timings, final verification and documentation; one implementation author at a time. Authors may run the focused checks requested by the controller. Reviews are read-only and do not duplicate unchanged tests.

For exact optimizations, establish the current performance failure before implementation and add any missing independent behavior coverage first. Correct pre-optimization outputs are expected to pass these characterization tests; do not fabricate a functional failure. Demonstrate meaningful mutation sensitivity for newly protected boundaries. New executor behavior uses a genuine failing test before its implementation. Timing thresholds belong to paired diagnostics, not flaky CI tests.

Cached build directories: `out/build/linux-gcc-debug-sim`, `out/build/linux-gcc-release-sim`, `out/build/linux-gcc-debug-memory-sim`. Use `cmake --build --preset linux-gcc-<configuration>-sim --parallel 3` and focused CTest registrations; for the sanitizer cache use `cmake --build out/build/linux-gcc-debug-memory-sim --parallel 3` because it has no preset. Do not install dependencies or clean caches unnecessarily. No concurrent builds/tests during controller timing runs. Store new QA in `out/qa/m08-cpu-next/`.

### Task 1: Validated identity copies

**Files:** Modify `src/processing/src/NormalizeStage.cpp`, `BrightnessContrastStage.cpp`, `GammaStage.cpp`, and the existing private `ToneStageSupport.hpp`; create private `ImageRowCopy.hpp` for the shared active-row copy. Test `tests/unit/processing/NormalizeStageTests.cpp` and `ToneStageTests.cpp`.

**Interfaces:** Public stage interfaces, gamma LUT ownership, timings and resource sizes remain unchanged.

- [x] Extend tests before source changes: all 65,536 Mono16 values copy exactly; neutral BC/Gamma retain unaligned/padded source and destination canaries across multiple rows; identity-eligible inputs still reject overlap/domain/storage/extent failures; Mono12 still scales and reports its first out-of-range sample. Reuse existing exhaustive gamma/BC tests instead of duplicating them.
- [x] Run focused Release `Processing.NormalizeStage` and `Processing.ToneStages`; record existing passing behavior and the preserved slow baseline. Demonstrate a realistic canary/guard mutation failure in the new coverage, then restore it.
- [x] After existing validation, copy active row bytes for validated U16 maximum 65535, exact BC 0/1 and Gamma 1. Leave all general paths and gamma construction unchanged.
- [x] Build Debug/Release `lumora_processing_tests`; run the two focused registrations. Controller measures a frozen before/after stage diagnostic and complete engine outputs with no concurrent build.
- [x] Controller commits source/tests and obtains independent task spec/quality review before completion.

### Task 2: Specialized blocked orientation

**Files:** Modify `src/processing/src/OrientationTransform.cpp`; test `tests/unit/processing/OrientationTransformTests.cpp`.

**Interfaces:** Keep `OrientationTransform::outputLayout/apply` signatures and error order. No resource-plan change or additional heap storage.

- [ ] Add independent mapping tests spanning all flip/rotation combinations for Gray8 and Gray16, including unaligned source/destination, unequal strides, 1-wide/1-high images, odd rectangles and tile boundaries at 31/32/33 and 63/64/65. Verify every active pixel and all padding/source canaries. Use direct mathematical expected mapping, not production coordinate helpers.
- [ ] Run current focused test and record baseline diagnostic; demonstrate a wrong-tile-edge or row-stride mutation is detected, then restore.
- [ ] Dispatch storage/orientation outside the hot loop. Implement compile-time fixed-size copies; use fixed 32-by-32 tiles for axis-swapping transforms and row copy/reversal for non-swapping transforms. Preserve all validation and avoid signed/unsigned underflow at boundaries.
- [ ] Run Debug/Release `Processing.OrientationTransform` and `Processing.FrameProcessingEngine`. Controller measures rotations and whole-frame checksums; retain the blocked implementation only with useful repeatable improvement.
- [ ] Controller commits and obtains independent task spec/quality review.

### Task 3: Prepared executor and engine lifecycle

**Files:** Create private `src/processing/src/PreparedCpuExecutor.hpp/.cpp`; modify `src/CMakeLists.txt`, `src/processing/src/PreparedRuntime.hpp`, `ProcessingPreparation.cpp`, `FrameProcessingEngine.cpp`, `src/processing/include/lumora/processing/ProcessingPreparation.hpp`; create `tests/unit/processing/PreparedCpuExecutorTests.cpp` and register it in `tests/CMakeLists.txt`; extend `PreparedRuntimeTests.cpp` and `FrameEngineAllocationTests.cpp` where lifecycle/admission behavior is consumed. Update resource serialization/validation/schema files under `benchmarks/processing/` and `tests/unit/processing/EvidenceArtifactTests.cpp` for new execution metadata. Include `FrameEngineTestAccess.hpp/.cpp`, `EvidenceWorkload.hpp/.cpp`, `EvidenceMeasurement.hpp/.cpp`, `EvidenceHeap.hpp/.cpp` and `ProcessingAllocationProbe.cpp` for real helper controls.

**Interfaces:** Private executor constructor accepts `std::size_t slots` in [1,4]; `slots() const noexcept`; synchronous `run(std::size_t itemCount, void* context, void (*work)(void*, std::size_t slot, std::size_t begin, std::size_t end) noexcept)`. Choose `void run(...) noexcept`: the design's full serial fallback handles floating-environment capture/setup failure before any callback. Restore failure after callbacks disables future helper dispatch and must not replay completed work. Slot zero waits for the same prepare/commit decision as helpers. Publish the chosen exact signature in the task report for Task 4. `ProcessingPreparationOptions::cpuExecutionSlots` defaults to 4 and is frozen by the plan. Add dimensionless `ProcessingResources::cpuExecutionSlots` and `cpuHelperThreads`, and byte count `cpuExecutorBytes`. Own the executor through `EngineState`'s unique pointer declared before runtime/stage fields: report its allocated object bytes separately from `engineStateBytes` and include both exactly once in fixed storage. Document thread runtime/stack exclusions.

- [ ] Write failing executor tests for disjoint complete range coverage (including zero/fewer items than slots), synchronous completion, repeated different jobs, real helper execution, one-slot operation, caller rounding-mode propagation/restoration, and destruction joining persistent workers. Avoid wall-clock race synchronization; use latches/condition variables and observable outputs.
- [ ] Implement the fixed-storage executor with at most three threads, no dynamic queue/function wrapper, startup readiness, partial-construction cleanup, context publication/completion barriers and helper environment propagation. Verify partial-start cleanup and floating-environment capture/setup/restore failures with narrow private injectable seams. No callback starts before all environment acknowledgements; aborted helpers become quiescent before the single serial fallback. Keep test utilities private.
- [ ] Test and implement preparation option validation, executor fixed-storage admission, engine ownership/destruction order and typed startup errors. Create workers only during stopped construction; activation and resources() never dispatch. Preserve the old stage execution path until Task 4.
- [ ] Add a real worker callback allocation positive control to the process-global allocation test; a subsequent repeated arithmetic dispatch must report zero calls/bytes/releases. Keep thread creation/destruction outside the measured region. Compile executor floating-environment operations with `-ffp-contract=off -frounding-math` on GCC/Clang and `/fp:strict` on MSVC; record generated command lines. Test the worker control independently rather than assuming the existing caller control proves it. Arm/reset/end the process-global tracker on the caller outside the synchronous dispatch; do not call the existing self-arming `allocationControls()` concurrently from helpers.
- [ ] Implement the versioned artifact and helper-control contract in `.superpowers/sdd/2026-09-08-m08-cpu-pipeline/evidence-preflight.md`: emit v2 benchmark/allocation artifacts while accepting exact legacy v1 shapes; preserve v1 schema definitions and reference/workstation formats. Put slots/helper counts at prepared resource root and executor bytes in bounded byte statistics; enforce exact sums and `thread_stacks_TLS_thread_library_and_OS_bookkeeping` exclusion. Add private synchronous engine dispatch forwarding and evidence-only helper control: slot zero does not allocate; helper slot s makes one non-elidable allocation of `256+s` bytes, then frees it. For four slots verify mask14 and 3 calls/774 bytes/3 releases. Preserve the existing caller eight-route control. Add a distinct positive glibc helper trace result and strict v2 helperControl member; do not reuse zero-event result polarity. Preserve normal64x48 geometry. Legacy compatibility tests must use tracked representative fixtures, not ignored developer QA paths. New parallel stage work is still deferred to Task4; metadata must say so accurately.
- [ ] Run Debug/Release executor, runtime/engine, allocation and evidence registrations. Run targeted sanitizer checks for the new executor. Controller commits and obtains an independent concurrency-capable task review.

### Task 4: Parallel CLAHE and filters with admitted private scratch

**Files:** Modify `src/processing/src/StageStorage.hpp`, `PreparedStageFactory.cpp`, `PreparedRuntime.hpp`, `ProcessingPreparation.cpp`, `FrameProcessingEngine.cpp`, `FrameEngineTestAccess.hpp/.cpp`, `ClaheStage.cpp`, `DenoiseStage.cpp`, `SharpenStage.cpp`, `DetailStageSupport.hpp/.cpp`; add private factory declarations through existing friends in stage public headers as needed. Extend `ClaheStageTests.cpp`, `DetailStageBatchingTests.cpp`, `DetailStageAllocationTests.cpp`, `FrameEngineAllocationTests.cpp`, `PreparedRuntimeTests.cpp`, `FrameProcessingEngineTests.cpp` and evidence tests only where contracts change. Update `benchmarks/processing/EvidenceProvenance.cpp` when stage jobs become parallel. Reuse Task3 helper-control interfaces and strict v2 artifacts; retain normal workload geometries. A separate ignored large-image diagnostic exercises the same session/helper hooks without relabeling it as a normal artifact.

**Interfaces:** Consume Task 3 executor. Keep public standalone creation/sizing serial. Add private `StageStorage` sizing/construction functions taking admitted slots/executor and forward both public/private construction through one formula/implementation. Extend `prepareDefinition` to consume frozen slots and `makeStage`/`EngineHooks::prepare` to consume the executor. A stage's non-owning executor pointer is valid through its engine-owned lifetime; test hooks must exercise the production parallel factory.

- [ ] Before implementations, add behavior coverage comparing serial and parallel complete pixels against independent current references for representative grids/kernels, padded/unaligned/odd/tiny images, repeated different inputs and worker stripe boundaries. Include Gaussian and sharpening cases above 65,536 pixels and CLAHE small/large cases. Add budget rejection and exact typed-array delta tests that fail while the private factories still return serial stages.
- [ ] Apply the same explicit FP compiler policy to affected CLAHE/detail floating kernels, recording command lines and verifying complete output equality; helper exception flags are not merged into the caller. CLAHE: size one int32 histogram per slot with checked arithmetic, dispatch disjoint tile ranges, barrier, dispatch disjoint interpolation rows. Preserve bin count, clipping/CDF loop order and FE_TONEAREST validation before mutation.
- [ ] Gaussian: dispatch independent horizontal rows to existing double scratch, barrier, then vertical rows. Keep median serial. Below 65,536 pixels use the serial callback path without changing admitted scratch.
- [ ] Sharpen: allocate checked `slots * width * min(height,kernelSize) * sizeof(double) + kernelSize*sizeof(double)` payload. Give each stripe its own ring and recompute the necessary reflected halo rows. Never write outside its output stripe. Preserve coefficient and per-pixel operation order, saturation and threshold behavior. Below 65,536 pixels use one stripe.
- [ ] Wire frozen slots/executor through planning, creation, activation and test hooks. Test concurrent control-thread activation while an old runtime processes, without creating another pool or sharing candidate scratch. Validate actualRetainedStageBytes and candidate/envelope accounting across reuse and retirement.
- [ ] Exercise real helper paths under C++ allocation tracking and controlled glibc tracing, with helper-side positive controls and both small and 512-or-larger complete sessions. Run focused Debug/Release and targeted sanitizer checks; controller measures paired full-pipeline/stage diagnostics, ensuring exact checksums.
- [ ] Controller commits and obtains independent task spec/quality/concurrency review before SIMD work.

### Task 5: Measured exact SIMD experiment

**Files:** Modify `src/processing/src/DetailStageSupport.cpp/.hpp`; optionally add a private `DetailStageSimd.hpp/.cpp` and register in `src/CMakeLists.txt` if separation improves guarded platform compilation. Extend `tests/unit/processing/DetailStageBatchingTests.cpp` only for newly exposed lane/border gaps.

**Interfaces:** Existing row helpers remain the stage boundary; update `benchmarks/processing/EvidenceProvenance.cpp` if a retained SIMD implementation changes the execution description; scalar fallback remains available on non-x86-64. No public execution-backend change.

- [ ] Freeze a post-parallel baseline binary and record Gaussian/sharpen/full-pipeline output hashes and paired timing. Inspect generated loops before deciding which costs need explicit vectorization.
- [ ] Verify affected floating kernels retain `-ffp-contract=off -frounding-math` (GCC/Clang) or `/fp:strict` (MSVC). Add independent bitwise intermediate-double and final-U16 coverage around SIMD lane tails, tiny borders, threshold ties, saturation, maximum kernel radius and unaligned rows. Demonstrate the coverage detects a meaningful rounding or tail mutation.
- [ ] Implement a guarded baseline SSE2 candidate across independent pixels, retaining double precision and each pixel's operation order. Use unaligned-safe loads/stores; no reassociation, FMA, fast-math or unsupported instruction requirement. Keep a portable fallback and exercise it through an existing private implementation seam or separate test build.
- [ ] Compare all exact outputs and repeat paired timings without concurrent work. Keep a candidate only if it improves useful stage/full-pipeline cost without material regression; otherwise restore source and preserve the experiment evidence with an explicit no-gain conclusion.
- [ ] Run affected Debug/Release/sanitizer checks for retained code. Controller commits and obtains independent task review of retained changes and experiment conclusions.

### Task 6: Complete verification, evidence and development handoff

**Files:** Update `README.md`, `docs/PROGRESS.md`; create `docs/architecture/milestones/m08-cpu-pipeline-performance.md`; complete this plan's checkboxes and preserve its ignored execution ledger. Evidence scripts/artifacts remain under `out/qa/m08-cpu-next/` unless a reusable tool correction is necessary and separately reviewed.

**Interfaces:** Strict processing benchmark/reference/allocation artifacts identify actual clean source and binary hashes. Prior normal artifacts remain immutable. The previous before-full-image/after-ring comparison script is not valid unchanged for this plan's ring/parallel resource deltas.

- [ ] Run complete local Debug and Release suites, native X11 smoke in both, and targeted ASan/UBSan/leak checks for executor, stages and engine. Resolve failures before claiming completion.
- [ ] Obtain whole-branch source review, including retained prior optimization commits, plan rulings, task reports and source/resource boundaries. Fix load-bearing findings through a reviewed author dispatch.
- [ ] Freeze clean final benchmark/allocation/reference tools with source/build/provider provenance. Run the normal 33-case benchmark (512/1024/2048, 100 warm-ups, 500 measured frames), both normal 1,000-cycle allocation profiles, controlled glibc traces with helper-side controls and a large-image helper-path probe, and reference candidates.
- [ ] Independently validate complete output hashes, exact/provisional candidate semantics, resource arithmetic/deltas, zero measured allocations/errors/drops, schema validity, all binary/provider hashes and actual trace event parsing. Compare with the preserved clean pre-plan baseline; report regressions and variance as well as improvements. Internal stage timing remains diagnostic, separate from normal throughput.
- [ ] Record actual FPS and median/P95 per complete profile, accepted/reverted optimizations, added scratch/thread-count bounds and remaining bottlenecks. Keep Windows/native/hardware/clinical acceptance gates unchanged. Obtain final evidence review.
- [ ] Commit local documentation and report the complete result. Publication remains pending explicit authorization for this new branch; do not push, create a PR, run hosted CI or merge based only on this plan's local implementation authorization.
