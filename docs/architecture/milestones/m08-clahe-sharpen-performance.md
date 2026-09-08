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

Final-source full local application tests pass **53/53 in Debug (171.69 s)** and **53/53 in Release (24.52 s)**; native X11 passes **1/1** in each configuration (0.16/0.07 s). The targeted CLAHE, detail-stage and engine tests pass **3/3 under ASan/UBSan with leak detection (29.86 s)**. No compiler warnings or sanitizer findings were reported. The completed normal 33-row evidence and separate allocation/candidate checks are recorded below. Stage medians are not added or relabeled as complete-pipeline FPS.

The independent whole-branch source review is approved with no findings at `603f49c`, whose production and test source equals `eea5136`. Raw evidence and commands are retained under `out/qa/m08-clahe-sharpen/`.

## Normal complete-pipeline measurement

The final executables were frozen from clean **`603f49c396ef52f658aa8d0fa596af77a92002aa`**, with provenance captured at `2026-09-08T11:32:35Z`. The comparison uses the preceding clean `8877f4637efc1fb7422196dd4b5c40124ed729dd` normal run; its production source is identical to the `0df7a98` starting point. Both use the same Linux i7-10510U host, Release build flags, toolchain, dependencies, input definitions and normal 100-warm-up/500-measured-frame protocol. Later documentation edits do not change the frozen executables or their source provenance.

Full-frame measurements include Standard enhancement, Original and Enhanced display mapping, orientation, pooled publication/consumption and retained-output turnover. FPS uses measured frame count divided by measured wall time; it is not the reciprocal of the median or a sum of stage medians. Physical-camera acquisition and Qt presentation are outside this local processing benchmark. Complete output SHA256 verification runs outside timing, while the fixed per-cycle fingerprint is included.

All 33 cases complete with unchanged full output SHA256 and equal-count fingerprints, zero measured C++ allocation calls/bytes/releases, zero processing errors and zero drops. Complete definitions, inputs, orientations and non-source build/host provenance match the previous normal run. The exact intended sharpening resource delta is independently derived and checked for every size and both complete-pipeline orientations.

| Size | Orientation | Before median / P95 (ms) | After median / P95 (ms) | Before → after FPS |
| --- | --- | ---: | ---: | ---: |
| 512×512 | Identity | 26.180 / 29.027 | 23.761 / 26.372 | 37.747 → 41.713 |
| 512×512 | Flip / 90° | 31.245 / 34.112 | 28.484 / 30.771 | 31.655 → 34.898 |
| 1024×1024 | Identity | 72.548 / 77.673 | 65.069 / 85.063 | 13.644 → 14.728 |
| 1024×1024 | Flip / 90° | 101.237 / 127.782 | 88.787 / 93.378 | 9.414 → 11.234 |
| 2048×2048 | Identity | 245.099 / 258.911 | 204.605 / 241.188 | 4.057 → 4.852 |
| 2048×2048 | Flip / 90° | 370.323 / 388.070 | 305.358 / 329.322 | 2.688 → 3.243 |

At 2048, complete-pipeline median time falls 16.52% / 17.54%, and measured FPS rises 19.61% / 20.67%. P95 falls 6.85% / 15.14%. The results remain mixed at the tail: 1024 identity P95 rises 9.51% despite its lower median and higher throughput. These are observations from successive normal runs on this Linux laptop, not a guarantee of universal improvement or designated Windows acceptance.

The normal standalone 2048 sharpening median falls from 75.870 to 46.135 ms; Gaussian falls from 27.077 to 18.752 ms. Unchanged CLAHE moves from 60.677 to 66.387 ms, and Gaussian P95 is approximately unchanged at 29.451 / 29.531 ms. These control/tail movements reinforce the limits of a single normal-run comparison. The repeated isolated rounding comparisons provide the narrower evidence for the latency benefit; no CLAHE speedup is claimed.

Both Standard 64×48 orientation profiles complete 100 warm-ups and 1,000 measured allocation cycles with all eight C++ positive controls (8 calls / 150 bytes / 8 releases) and zero measured calls/bytes/releases. Separately preloaded glibc runs also complete both 1,000-cycle profiles with zero measured allocator events, verified empty controls and 26-event positive controls. Independent checks rehash all three frozen executables, actual libc marker/end-marker/malloc providers and trace files, and parse the positive-control operations and 297-byte total. These proofs retain their explicit allocator scope and exclusions.

All 13 reference candidates and all 26 PGM files remain byte-identical to the preceding artifacts. Seven exact cases keep zero comparison errors; six provisional cases keep null comparisons and all cases retain null review/acceptance thresholds. Artifact schemas, timing arithmetic, source provenance and resource accounting pass independent checks. The helper also rejects a deliberately corrupted output checksum in its self-check.

The complete 33.3 ms/30 FPS target is still unmet at 2048 on this host. M8 remains open pending further measured latency work and designated Windows reference/workstation/freshness acceptance. The next performance investigation should profile the remaining CLAHE and orientation/display work before selecting another implementation change. Deferred M4/M5 Windows 11 and M6 hardware gates are unchanged.

Final independent evidence/documentation review is approved with no findings. It confirms the documented figures and variability, exact candidate bytes, derived resource deltas and the additional independent binary/provider/control-trace audits. Local implementation and remeasurement are complete.

## Publication status

This optimization branch has not been published. Automatic approval review rejected the initial push before execution because the earlier publication permission named the M7 branch, not this new branch and its source history. Explicit owner authorization is required before publishing `perf/m08-clahe-sharpen-cost`, running hosted Linux/Windows CI and merging after both platforms pass. Local verification does not substitute for those pending platform checks.
