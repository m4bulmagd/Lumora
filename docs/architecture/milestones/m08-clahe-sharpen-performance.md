# M8 CLAHE and sharpening performance experiments

**Date:** 2026-09-08

The owner authorized CLAHE histogram optimization, reduced sharpening cost and complete-pipeline remeasurement after the [Window/Level optimization](m08-window-level-performance.md). Work starts from `0df7a98` on `perf/m08-clahe-sharpen-cost` in the preserved M8 worktree. Designated Windows reference/workstation/freshness acceptance and the previously deferred gates remain open.

## Retained changes and numerical contract

Sharpening now retains `width * min(height, kernelSize)` horizontal doubles plus kernel coefficients. A local producer starts at zero on every call, produces each source row once in increasing order, and reuses slots only after the row leaves the vertical neighborhood. Gaussian coefficients, horizontal and vertical tap order, REFLECT_101 borders, rounded U16 blur, signed detail, strict threshold, saturation and final rounding are preserved. Odd strides and unaligned starts keep byte-safe access.

Gaussian denoise retains its full intermediate. Sharpen's exact scratch estimate, allocation and reported bytes change together; `scratchImages = 4` remains a conservative shape-independent upper bound. At 2048×2048/radius 1, sharpening arrays require **114,744 bytes instead of 33,554,488**. The compiled stage-owner size remains 80 bytes on this Linux ABI. These are requested storage bytes, not process RSS. The default session still reserves three activation envelopes from its configured limit; candidate and actual retained-stage requirements fall, while its declared total remains bounded by the same limit.

The shared private U16 rounding helper retains the clamp and `+0.5` but removes the redundant floor: truncating the resulting finite value in `[0.5, 65535.5]` produces the identical representable integer. The independent scalar oracle retains its floor expression. Gaussian and sharpening exactness are both checked. No FP reassociation, coefficient quantization, fast-math, new threads, dependencies or parameter changes are included.

**CLAHE production code is unchanged from `0df7a98`.** Five histogram/CDF candidates were implemented, tested, reviewed and measured, but none established a reliable improvement. They were removed rather than retained as unsupported optimizations. A new independent analytical residual-bin test remains: uniform 21,845, 4×4/grid 2/clip 2 produces 49,151. The experiment commits, frozen executables, complete output files and reports are preserved.

## Isolated measurements

The diagnostic harness uses deterministic full-range U16 noise at 512, 1024 and 2048, with 20 warm-ups and 100 measured calls per case. Immutable executables use the same Release flags and dependencies. Each timing sequence runs without concurrent local builds or tests. Complete raw output bytes, checksums, counts and exact scratch requirements are checked before interpreting timing. These stage diagnostics are separate from the normal pipeline protocol and Windows acceptance.

The baseline executable comes from clean `0df7a98`; ring-only comes from `215c3ad` plus the preserved uncommitted ring patch; ring plus rounding corresponds to `3e0027f`. The latter two share the same intermediate CLAHE implementation, so their CLAHE control is unchanged. Final source `eea5136` restores the original CLAHE before whole-pipeline verification. Binary and patch hashes are retained with the raw diagnostics.

The first sequence runs ring-only, ring plus rounding, then the original baseline. Medians in milliseconds:

| Size | Stage | Original baseline | Ring only | Ring plus rounding |
| --- | --- | ---: | ---: | ---: |
| 512 | Sharpen | 4.832 | 4.963 | 2.863 |
| 1024 | Sharpen | 19.688 | 19.644 | 11.874 |
| 2048 | Sharpen | 77.859 | 78.412 | 46.328 |
| 512 | Gaussian denoise | 1.562 | 1.453 | 0.913 |
| 1024 | Gaussian denoise | 6.481 | 6.530 | 3.980 |
| 2048 | Gaussian denoise | 26.886 | 26.615 | 17.660 |

A reverse-order repeat compares ring plus rounding with ring-only. At 2048, sharpening falls from **78.351 to 45.482 ms (41.95%)**, and Gaussian denoise from **26.705 to 17.221 ms (35.51%)**. The unchanged CLAHE and Window/Level controls remain approximately stable. All 12 stage outputs are byte-identical. The ring alone has no demonstrated latency benefit; its value is the smaller storage requirement and successful processing within the lower admitted budget. The shared rounding change supplies the measured stage latency benefit.

The CLAHE experiments retain the same exact pixels and resources:

| Candidate | Measured disposition |
| --- | --- |
| Fuse batch/residual redistribution, CDF and reset | Mixed small changes across two comparisons; no reliable gain |
| Keep sparse updates separate; fuse batch/CDF/reset | Approximately neutral |
| Cache the invariant clipping limit | Mixed results; no reliable gain |
| Add direct byte-row reads for interior tile spans | No reliable gain; additional branch path removed |
| Group four exact integer CDF prefixes | Slower in the paired run: 20.56% / 22.80% / 6.33% at 512 / 1024 / 2048 |

The last comparison also contains some movement in control stages; it supports rejecting that candidate, not a universal regression percentage. No CLAHE speedup is claimed. Reducing histogram passes or instructions did not reliably reduce runtime on this host.

## Verification and complete-pipeline follow-up

The retained sharpening change is independently approved with no findings. Tests establish a real lower-budget failure before implementation, then successful processing, one-byte-short rejection and failed larger-radius activation preserving the prior configuration. They also cover short images, ring wrap, reflected borders, scalar-reference pixels, variable-stride A–B–A calls, padding and source canaries. Final focused Debug/Release each pass 3/3 registrations, including 1,000 alternating allocation-free calls per stage with eight positive controls. CLAHE candidates each pass the exact matrix and allocation checks before measurement; the final production restore is byte-equal to the baseline source.

Final-source full local application tests pass **53/53 in Debug (171.69 s)** and **53/53 in Release (24.52 s)**; native X11 passes **1/1** in each configuration (0.16/0.07 s). The targeted CLAHE, detail-stage and engine tests pass **3/3 under ASan/UBSan with leak detection (29.86 s)**. No compiler warnings or sanitizer findings were reported. The normal 33-row protocol will use clean fixed binaries, 100 warm-ups and 500 measured frames, followed by portable and separately controlled glibc allocation proofs and candidate-image/schema checks. Intended sharpening resource deltas will be verified explicitly. Stage medians must not be added or relabeled as complete-pipeline FPS.

Raw evidence and commands are retained under `out/qa/m08-clahe-sharpen/`. Whole-branch review and matching Linux/Windows CI precede integration. The complete-pipeline result, 33.3 ms/30 FPS gate and designated Windows acceptance remain pending.
