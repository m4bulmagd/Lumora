# M8 exact CPU pipeline optimization

Status: owner authorized planning and execution on 2026-09-08 ("ok plan that and do it"). This continues the measured CPU optimization recommendations: identity fast paths, blocked orientation, and prepared parallel/SIMD processing. It does not authorize publication of this new branch.

## Problem and evidence

The preceding clean Linux Release measurement, source-equivalent to `e753fd0`, gives Standard 2048 median/P95 of 204.605/241.188 ms for identity and 305.358/329.322 ms for horizontal flip/90-degree rotation (4.852/3.243 measured FPS). A separate short engine diagnostic attributes about 67 ms to full-range Normalize, neutral Brightness/Contrast and Gamma, and 108 ms to the two display rotations. CLAHE, Gaussian and sharpening remain expensive. See the [preceding record](../../architecture/milestones/m08-clahe-sharpen-performance.md) and preserved `out/qa/m08-bottleneck-readout/` evidence.

The target is lower complete-pipeline latency with identical pixels. A 30 FPS result is an acceptance target, not a promised outcome on this Linux laptop. Windows 11 remains the official acceptance platform; its deferred checks remain open.

## Binding requirements

- Preserve exact U16 and Gray8/Gray16 output, stage order, validation precedence, first-invalid-sample diagnostics, source immutability, and destination padding.
- Preserve supported unaligned byte views, unequal valid strides, rectangular extents, and all existing orientation combinations.
- Preserve zero measured per-frame C++ allocations/deallocations and controlled glibc allocator events after preparation and warm-up, including helper threads.
- Preserve bounded active/in-flight/candidate stage ownership, resource admission before allocation, fallback/retry behavior, and prepared stage reuse.
- Keep public standalone stage factories serial and source-compatible. Engine execution may use the private prepared executor.
- Use C++20 with the pinned Qt/OpenCV dependencies and supported GCC/MSVC configurations; add no dependency, global thread-pool setting, fast-math flag, unconditional AVX instruction, or GPU backend.
- Keep the existing 65,536 CLAHE histogram bins, clipping/redistribution/CDF/interpolation equations and per-pixel floating-point operation order.
- Thread creation and destruction occur during stopped engine preparation/destruction. Activation creates no additional worker pool. A session has at most four execution slots, including its calling processing thread.
- Keep raw and Enhanced U16 pixels in native orientation; apply orientation to the two display products as before.

## Design

### Exact identity copies

After all current checks, Normalize copies active U16 rows when the validated source maximum is 65535; narrower sources keep scaling and first-invalid-sample checking. Brightness/Contrast copies only for exact brightness 0 and contrast 1. Gamma copies only for exact gamma 1. Copies retain stage invocations and timing IDs. Gamma's prepared LUT and resource accounting remain unchanged. The existing full-range Window/Level row copy is the precedent.

### Blocked orientation

Retain output-layout and input validation. Dispatch storage width and the orientation mapping once outside the pixel loop. Use a small fixed tile (initially 32 by 32) for axis-swapping transforms, with compile-time one-byte/two-byte copies so both source and destination cache lines are reused. Non-swapping transforms use contiguous row copies or specialized reversal. No full-image temporary is added. Tile edges and tiny images use the same exact mapping.

### One prepared executor per engine

Introduce private `detail::PreparedCpuExecutor`, owned by `EngineState` and declared before its runtime/stage fields, so it is destroyed after those fields. Its fixed arrays hold at most three persistent helper threads; the processing caller supplies slot zero. A synchronous dispatch accepts a plain function pointer, stack-owned context, and an item count, partitions disjoint ranges deterministically, and returns only after every range finishes. No `std::function`, dynamic task queue, per-frame future, thread creation, or detached work is permitted. Serial dispatch uses the same callback with slot zero.

Add stopped preparation option `cpuExecutionSlots` (default 4, valid 1 through 4). Freeze it into the admitted plan; activation uses that value. An executor with one slot creates no helper thread. Do not derive storage requirements from changing hardware-concurrency values. Worker startup must complete before preparation succeeds; partial startup failure joins all created workers before returning a typed preparation error. Destruction wakes and joins all helpers while their synchronization storage remains alive.

Propagate the caller's floating environment to helpers for each dispatch; CLAHE continues rejecting a non-FE_TONEAREST caller before mutation. Use two phases: all helpers acknowledge environment setup while slot zero also waits, then commit callbacks only if every setup succeeded. On capture/setup failure, abort and wait for all helpers to become quiescent before executing the entire range exactly once on the caller. Restore helper environments after callbacks and preserve the caller's mode. A restore failure after work must never replay that work; disable future helper dispatch for that executor and keep subsequent calls serial. Test capture/setup/restore failure paths through a narrow private injection seam. Capture the supported floating environment, including guarded x86 SIMD control state where required, rather than only the rounding-mode integer. The observable floating contract is exact pixels/intermediate arithmetic and preserved caller control modes; helper exception-status flags are not merged into the caller. Fixed publication/completion synchronization protects context lifetime and LUT phase boundaries. Concurrent calls to one executor are outside its one-processing-caller contract; control-thread preparation must never dispatch work.

Account executor object storage once in fixed engine storage. Expose admitted execution slots/helper count in evidence. Runtime-managed thread stacks, TLS and thread-library bookkeeping are explicitly excluded from the existing requested-storage accounting (which is already not RSS); document the fixed helper-count bound and this exclusion. Do not claim `sizeof(std::thread)` accounts for a stack. Stage typed-array scratch remains exactly sized and checked, including every worker histogram/ring. No silent scalar fallback may mask allocation or thread-start failure during preparation.

### Parallel stage execution

Use the existing private `StageStorage` friend seam for executor-aware sizing and construction, sharing the underlying sizing logic with public serial factories. Each prepared stage holds a non-owning executor reference whose engine owner outlives it. `prepareDefinition`, `makeStage`, activation, and the private engine test hook carry the admitted slots/executor consistently.

- CLAHE: one 65,536-entry int32 histogram per slot, one shared LUT with disjoint tile writes. Complete tile jobs before dispatching interpolation rows. Keep each tile's arithmetic unchanged.
- Gaussian denoise: disjoint horizontal rows into the existing full-image double intermediate, then a barrier and disjoint vertical output rows. Median remains serial in this scope.
- Sharpen: one ring per slot. Each stripe recomputes its required reflected halo rows using the same horizontal function, then writes only its own destination rows. Ring payload is `slots * width * min(height, kernelSize) * sizeof(double)`, plus one shared coefficient array. Never parallelize the existing shared ring in place.

For Gaussian and sharpening, use serial execution below 65,536 pixels to avoid dispatch overhead; scratch admission remains independent of this execution cutoff. CLAHE can dispatch even on small images because its LUT work is substantial. The exact cutoff/slot policy can be adjusted only by a recorded measurement and matching documentation, without changing output or admission guarantees.

### SIMD selection

After parallel correctness, measure explicit baseline SSE2 operations across independent pixels in Gaussian/sharpen loops on x86-64, with portable scalar fallback elsewhere. Preserve each pixel's tap sequence, double precision, intermediate rounding, saturation and threshold comparison. Do not reassociate reductions or introduce fused multiply-add. Compile affected floating-kernel and executor translation units with `-ffp-contract=off -frounding-math` on GCC/Clang and `/fp:strict` on MSVC; record the actual command lines. Inspect generated code and pair timings against a frozen pre-SIMD binary. Test bitwise intermediate doubles as well as final U16 values. Retain a SIMD candidate only if it produces exact outputs and repeatable useful latency reduction. Recording and reverting an unsuccessful measured candidate completes the experiment; it is not a SIMD speedup claim.

## Verification and scope

The zero-heap requirement keeps the existing successful steady-configuration measurement scope. Concurrent activation retirement may already release old stage ownership on the processing caller; this plan does not introduce a new reclamation contract or claim allocation-free live replacement.

Tests cover exhaustive identity values, invalid narrower samples, padded/unaligned views, tile boundaries and orientations; real executor range coverage/reuse/join/environment behavior; serial/parallel exact stage outputs; active/in-flight activation and budget enforcement; and allocation controls that actually execute on helpers. Performance characterization uses frozen binaries, complete output hashes and full Standard runs, with internal stage diagnostics reported separately. Run Debug/Release application suites, native Linux smoke and targeted sanitizers after retained changes. Preserve old QA artifacts.

GPU, reduced histogram precision, tone-stage elimination/fusion, display-map/orientation fusion, UI controls and M9 remain outside this plan. Local source/test/evidence work is authorized. Publishing, hosted CI and merging this new branch require the explicit publication permission already requested following automatic approval review's rejection of the earlier new-branch push.
