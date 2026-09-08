# M8 CLAHE and sharpening cost reduction

**Date:** 2026-09-08

The owner authorized CLAHE histogram optimization, reduced sharpening cost and complete-pipeline remeasurement after the [Window/Level optimization](m08-window-level-performance.md). Development starts from `0df7a98` on `perf/m08-clahe-sharpen-cost` in the preserved M8 worktree. This is continued engineering work; designated Windows reference/workstation/freshness acceptance and the previously deferred gates remain open.

## Numerical and ownership contract

CLAHE keeps the prepared histogram, LUT and interpolation/reflection arrays. Clipping remains first. Whole-bin and sparse residual redistribution can be combined with cumulative LUT construction, and each consumed histogram bin can be cleared for the next tile. The effective count at every bin, residual count/locations, ascending cumulative order, float scaling and pinned saturation must remain identical. The existing initial-zero, complete-validation and sequential-owner invariants make histogram reuse safe. Scratch requirements and ownership remain unchanged.

Sharpening keeps the same Gaussian coefficients, horizontal and vertical tap order, REFLECT_101 borders, rounded U16 blur, signed detail, strict threshold, saturation and final rounding. It can retain only `width * min(height, kernelSize)` horizontal doubles plus coefficients, producing rows once in increasing source-row order and reusing slots after they leave the vertical neighborhood. Each call starts its own producer cursor; every consumed row belongs to the current input. Source and destination remain distinct, and valid odd strides/unaligned starts retain byte-safe access.

Gaussian denoise continues to retain its full intermediate. Sharpen's exact scratch estimate, allocation and reported bytes change together; stage/registry `scratchImages = 4` remains a conservative shape-independent upper bound. Engine admission consumes exact bytes and continues to account for fixed owners and active/candidate/retired envelopes. At 2048×2048/radius 1 the proposed sharpening arrays require 114,744 bytes instead of 33,554,488. These are requested array bytes, not process RSS or measured speed.

Removing an explicit floor before bounded U16 conversion is a separate small candidate: after the existing clamp and `+0.5`, truncation produces the same representable integer. The independent scalar oracle retains its original floor expression. Shared-helper changes require Gaussian as well as sharpening exactness checks. No reassociation, coefficient quantization, fast-math, new threads, dependency changes or parameter changes are included.

## Verification and measurement

Test-first work covers a hand-derived nonzero CLAHE residual-bin fixture, histogram reset/reuse, sharpening ring wrap and reflected boundaries, complete scalar-reference output, variable-stride A–B–A calls, padding/source canaries and real processing within a smaller admitted budget. The lower sharpening budget supplies a new failing resource requirement on the baseline. Existing correct pixel behavior can pass new characterization tests before optimization; meaningful temporary mutations must prove the relevant coverage without fabricating a functional failure.

The clean baseline Release build and five affected CTest registrations pass. Immutable baseline stage and CLI executables, hashes and complete diagnostic outputs are retained under `out/qa/m08-clahe-sharpen/`. Paired diagnostics use unchanged inputs, flags and counts without concurrent local builds/tests. Each retained optimization needs measured benefit; reducing scratch alone does not establish lower latency.

After task review, full local Debug/Release and native checks, the normal 33-row protocol will be rerun from clean fixed binaries, followed by portable and separately controlled glibc allocation proofs and candidate-image/schema checks. Exact output comparisons remain mandatory; intended sharpening resource changes will be checked explicitly. Final review and matching Linux/Windows CI precede integration. Results remain pending and do not establish the 33.3 ms/30 FPS acceptance gate.
