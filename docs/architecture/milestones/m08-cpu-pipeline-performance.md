# M8 exact CPU pipeline optimization

**Date:** 2026-09-08. **Status:** Local implementation, measurement and independent review complete; unpublished.

The owner approved the [exact CPU design](../../superpowers/specs/2026-09-08-m08-cpu-pipeline-design.md) and its [six-task implementation plan](../../superpowers/plans/2026-09-08-m08-cpu-pipeline.md). Work is local on `perf/m08-cpu-pipeline` in the preserved `.worktrees/m08-tone-stages` checkout. It includes the preceding unpublished [CLAHE/sharpening optimization](m08-clahe-sharpen-performance.md). The integrated main checkout remains at `0df7a98`; this record does not establish Windows or hardware acceptance.

## Retained implementation

Validated full-range U16 Normalize, neutral Brightness/Contrast and neutral Gamma now copy active row bytes. Validation, narrower-input scaling, first-invalid-sample diagnostics, source immutability and padding remain unchanged. Gamma still owns its prepared LUT. The earlier full-range Window/Level identity copy remains in use.

Orientation dispatch selects storage width and mapping outside the pixel loop. Non-swapping transforms copy or reverse rows; axis-swapping transforms use 32×32 tiles. All flip/rotation combinations retain their mapping, unaligned byte access and unequal-stride behavior. Raw and Enhanced U16 remain in native orientation; only the two display products are oriented.

Each prepared engine owns one executor with four execution slots by default: its processing caller and at most three persistent helper threads. Preparation creates the helpers, destruction joins them, and activation reuses the same pool. Fixed storage and synchronous barriers replace any need for a per-frame task queue. All helpers must acknowledge the caller's floating-point control environment before any slot starts a job. Setup failure falls back to one complete caller execution; restoration failure disables later helper dispatch without replaying completed work.

CLAHE distributes tile histograms/LUTs and then interpolation rows across the slots. Each slot owns a full 65,536-bin histogram. Gaussian distributes horizontal rows, waits, then distributes vertical rows. Sharpening gives each stripe its own small row ring and recomputes the required reflected halo. Gaussian and sharpening use a serial path below 65,536 pixels. Public standalone factories remain serial; median remains serial in both paths.

The retained x86-64 SSE2 change packs sharpening's final conversions, thresholding, saturation and U16 output across eight pixels. It preserves double precision and each pixel's operation order, with scalar tails and a portable fallback. Strict floating-point compile flags prohibit contraction or reassociation in affected kernels. Independent binary64 fixtures and U16 boundary tests cover both backends, rounding modes, unaligned rows and lane tails.

The preceding smaller sharpening intermediate and exact rounding simplification remain. The five earlier CLAHE histogram/CDF experiments remain rejected: this branch gains CLAHE parallelism while preserving its original bin count and clipping, redistribution, CDF and interpolation equations.

## Why these changes were retained

Separate paired diagnostics use frozen before/after executables, complete output checks and no concurrent local builds or tests. They identify the effect of individual steps; their timings are not normal throughput or Windows acceptance. The direct-engine figures below are medians of three repeat means, with 20 measured calls per profile in each repeat. The standalone SSE2 figures are medians of three repeat medians, with 100 measured calls per case in each repeat.

- Blocked orientation reduced the two 2048 display rotations from 62.635/62.318 to 17.206/14.410 ms in three paired repeats. The oriented direct-engine median fell from 255.921 to 162.155 ms.
- Prepared parallel stages reduced the identity direct-engine median from 129.864 to 68.204 ms and the oriented median from 161.963 to 99.433 ms. Identity CLAHE fell from 55.519 to 25.935 ms, Gaussian from 14.575 to 7.486 ms, and sharpening from 41.509 to 13.335 ms. One oriented display stage increased from 14.257 to 17.133 ms; no causal explanation was established. The complete engine still improved.
- SSE2 sharpening improved all three paired standalone repeats at 512, 1024 and 2048. The corresponding median reductions were 17.22%, 14.37% and 16.05%. In the engine, sharpening improved about 12%, while the complete-engine median changed only about 1–2%; one identity repeat regressed. It is retained for the repeatable stage benefit, without attributing a large pipeline gain to SIMD.

All corresponding full outputs remained identical. Successive diagnostic checkpoints have different timing conditions and must not be added together or treated as one controlled comparison.

## Storage, ownership and helper bounds

Sizing and construction share the same checked formulas. CLAHE adds one 262,144-byte histogram per additional slot. Sharpening arrays require `slots * width * min(height, kernelSize) * 8 + kernelSize * 8` bytes. Gaussian retains its existing full-image double intermediate. Each private parallel CLAHE, Gaussian and sharpening owner adds one executor pointer on this Linux ABI.

At 2048×2048 with the Standard seven-tap sharpening kernel and four slots, the candidate requirement is **43,656,344 bytes** and actual retained stage storage is **43,651,608 bytes**. Both increase by **1,130,520 bytes** over the pre-plan ring baseline: 786,432 bytes of histograms, 344,064 bytes of stripe rings and 24 bytes of stage owners. The executor occupies 280 bytes, and the engine-state object grows by 40 bytes. Runtime/control storage is unchanged.

Fixed session storage is **151,020,280 bytes** for identity and **155,214,584 bytes** for flip/90° orientation, an increase of 320 bytes in each profile. The admitted three-envelope totals remain within the 512 MiB configured limit. These are requested/accounted storage bounds, not process RSS; thread stacks, TLS, thread-library and OS bookkeeping are explicitly excluded. Activation preserves the active/in-flight/candidate ownership bound and creates no additional pool.

## Final verification and provenance

The final production/test source is clean **`7ae53f8e1b4479908c35d64e2422379787d018d1`**. Full Linux Debug and Release suites pass **54/54** in 55.72 and 24.49 seconds. Native X11 smoke passes **1/1** in both configurations. Targeted executor, CLAHE, detail-stage and engine checks pass **4/4 under ASan/UBSan with leak detection** in 35.18 seconds. No compiler warnings or sanitizer findings were reported in these final checks.

Independent task reviews and the whole-branch source review approve the retained changes with no open source findings. Coverage includes exact complete images, all orientation combinations, independent intermediate-bit and boundary references, mutation sensitivity, helper floating-point setup/failure, concurrent activation while a helper holds an active image job, retirement accounting and prepared reuse.

The Release benchmark, allocation and reference executables and three supplementary QA helpers are frozen in `out/qa/m08-cpu-next/final-binaries/`. `final-provenance.json` identifies the clean revision and hashes 29 bundle files, including exact-version schemas, helper sources/build records and actual kernel compile commands. Helper records also authenticate the compiler, linked archives, compiler dependencies and dynamic libraries on this evidence host. The original baseline manifest remains unchanged; a separate manifest authenticates the preceding reference manifest and all 26 PGM files.

The pre-plan normal baseline is clean `603f49c396ef52f658aa8d0fa596af77a92002aa`. Both runs use the Linux i7-10510U laptop, GCC 15.2, Qt 6.11.1 and OpenCV 4.12. The canonical workload is Mono16 and retains 100 warm-ups and 500 measured frames per case. FPS is measured frames divided by wall time, including the benchmark's pooled output turnover and fingerprint work. Physical camera acquisition and Qt presentation are outside this processing benchmark.

## Normal complete-pipeline results

All 33 cases completed with identical full output SHA-256 and equal-count fingerprints, zero measured C++ allocations/bytes/releases, zero processing errors and zero drops. Independent comparison verifies the unchanged workloads, exact reviewed execution metadata and source-matched resource deltas. Legacy v1 and current v2 artifacts pass their frozen exact-version schemas.

| Size | Orientation | Before median / P95 (ms) | After median / P95 (ms) | Before → after FPS |
| --- | --- | ---: | ---: | ---: |
| 512×512 | Identity | 23.761 / 26.372 | 11.201 / 15.165 | 41.713 → 83.890 |
| 512×512 | Flip / 90° | 28.484 / 30.771 | 13.812 / 17.265 | 34.898 → 69.117 |
| 1024×1024 | Identity | 65.069 / 85.063 | 30.037 / 39.098 | 14.728 → 32.519 |
| 1024×1024 | Flip / 90° | 88.787 / 93.378 | 37.158 / 47.140 | 11.234 → 26.042 |
| 2048×2048 | Identity | 204.605 / 241.188 | 80.897 / 97.502 | 4.852 → 11.969 |
| 2048×2048 | Flip / 90° | 305.358 / 329.322 | 117.933 / 147.178 | 3.243 → 8.127 |

At 2048, median time falls **60.46% / 61.38%**, and measured throughput rises **146.69% / 150.57%**. All six complete profiles improve in median, P95 and FPS in this normal-run comparison. The 1024 identity average exceeds 30 FPS, but its P95 still exceeds 33.3 ms. Both 2048 profiles remain below the target. These successive runs characterize this Linux laptop; they do not establish a universal speedup or designated Windows acceptance.

Standalone results remain mixed. At 2048, Normalize falls from 32.074 to 1.639 ms, neutral Brightness/Contrast from 28.554 to 1.593 ms, Gamma from 8.613 to 1.643 ms, and sharpening from 46.135 to 37.157 ms. Serial CLAHE is approximately unchanged at 66.387 / 66.475 ms; it does not use the engine's parallel factory. Gaussian is 18.752 / 17.854 ms. Median is 1201.979 / 1222.584 ms and is not enabled in Standard. Changes in unmodified control paths are observations, not benefits attributed to the implementation.

The smaller standalone cases also contain regressions and tail variation:

| Size / stage | Before → after median (ms) | Before → after P95 (ms) |
| --- | ---: | ---: |
| 512 / Window/Level | 0.027 → 0.026 | 0.032 → 0.039 |
| 512 / CLAHE | 14.918 → 16.670 | 17.684 → 17.978 |
| 512 / Gaussian | 0.921 → 0.917 | 1.538 → 2.214 |
| 512 / Median | 73.015 → 65.166 | 76.842 → 95.497 |
| 1024 / Window/Level | 0.093 → 0.110 | 0.228 → 0.157 |
| 1024 / CLAHE | 23.922 → 26.434 | 26.006 → 38.693 |
| 1024 / Median | 297.612 → 303.562 | 325.761 → 345.485 |

In particular, 512/1024 standalone CLAHE medians increase about 11.7%/10.5%, and 1024 CLAHE P95 increases about 48.8%. Their cause is not isolated by this single before/after normal run. The complete prepared Standard workload improves despite these results; no claim that every stage became faster is made. The repeated paired experiments above supply narrower evidence for the retained optimizations.

## Allocation, references and artifact checks

Both canonical 64×48 Standard orientation profiles complete 100 warm-ups and 1,000 measured cycles in the portable and separately controlled glibc runs. Measured C++ calls/bytes/releases and raw glibc allocator events are zero. The caller's eight-route positive control reports 8 calls / 150 bytes / 8 releases. Real helper controls report mask 14 and 3 calls / 774 bytes / 3 releases; independently parsed helper traces contain allocations of 257, 258 and 259 bytes and their matching releases. Marker, end-marker and malloc provider identities and hashes agree. Empty and positive trace controls are verified.

Separate final-source 512×512 portable and glibc diagnostics also complete 100 warm-ups and 1,000 measured cycles per orientation. Actual CLAHE tile and row jobs, Gaussian horizontal and vertical jobs, and sharpening row jobs each execute 1,000 callbacks on **every one of the four slots**. Each slot processes 16,000 tile items or 128,000 row items per job kind. Complete PGM outputs and independently reconstructed 1,000-cycle fingerprints match the baseline; measured C++ and controlled glibc counts remain zero. These diagnostic artifacts keep their own scope rather than replacing the canonical 64×48 evidence.

The zero-heap claim covers successful steady-configuration processing after preparation and warm-up, including the observed helper paths. It retains the documented allocator exclusions and does not promise zero releases during concurrent-activation retirement. Thread construction, destruction and platform runtime storage remain outside this measured boundary.

All 13 reference candidates and all 26 PGM files remain byte-identical. Seven exact cases retain zero comparison errors, six provisional cases retain null comparisons, and all review/acceptance thresholds remain null. Candidate equality does not promote provisional cases to accepted Windows references.

Independent validators authenticate the frozen bundle, original and supplemental baselines, complete outputs, execution policy, helper build inputs, reproduced size audit, every resource delta, schema versions, trace paths/providers and actual trace events. Temporary-copy mutations verify rejection of corrupted provenance, resource fields, schema, execution wording, trace binding and large-helper payloads. The Mono12 verifier additionally checks the real raw timing arrays, CSV and all seven payload files. Validation logs are retained in `out/qa/m08-cpu-next/final-validation/`; final helper/source review has no open findings. Final independent evidence and documentation review is approved with no required fixes. It independently checks the frozen inputs, schemas, all normal outputs/resources, reference bytes, raw traces, large-helper job attribution and Mono12 payloads/timing arithmetic.

The production executable hashes are:

| Frozen executable | SHA-256 |
| --- | --- |
| Benchmark | `1efe82bdbb5037347757612c96bfdcd57c9be89790f8da50ac38da4ba1a07103` |
| Allocation probe | `6d3e90df9debb23f8e810b34d68840add3fd839582074a1fcffa8adba0bafcb3` |
| Reference generator | `1a385470ecaceb4cf49d118fe9a5047ce680a2694594db65da5fbf43c7840d1c` |

## Supplementary Mono12 diagnosis and next development

The simulator ships Mono12, while the canonical throughput workload above uses Mono16. A separate 2048 direct-engine diagnostic therefore uses a real 12-bit descriptor (maximum 4095), deriving the upper 12 bits of the same seeded xorshift U16 input. Per profile it performs one constructor sentinel, five warm-ups, one retained exemplar, twenty measured calls and one post-verification call. It retains the sentinel/exemplar during timing. Encoding and hashing occur outside those calls. This is a stage-cost diagnostic, with **no FPS or allocation-proof claim**.

| Operation | Identity mean (ms) | Flip / 90° mean (ms) |
| --- | ---: | ---: |
| Mono12 normalization | 34.639 | 33.913 |
| CLAHE | 32.690 | 32.543 |
| Gaussian | 9.028 | 8.781 |
| Sharpening | 12.237 | 12.328 |
| Original display mapping | 7.545 | 7.328 |
| Enhanced display mapping | 7.679 | 7.758 |
| Original orientation | Not applied | 18.884 |
| Enhanced orientation | Not applied | 19.688 |
| Engine total, including other stages | 109.228 | 147.138 |

Engine medians are 108.610 / 146.290 ms. Public-call means are 109.240 / 147.149 ms and exclude destruction of the returned frame. These twenty-call diagnostics must not be converted into canonical throughput or directly compared as a controlled performance experiment with older diagnostics.

The independently regenerated input PGM hash is `810ab1ab8416914a9ea65b5e179d7ddcbde7ed620fb095d0456e313d3f573bde`. Complete composite PGM hashes remain `51c5ebb22c9d06ddea891f5217932f6f30e2c272bb7ec5f8aea5593e9d2dfa78` for identity and `723604514c4851b5490dd45fff692177c856f5e37ad3d8bf4ef6e9e04688403d` for flip/90°. The manifest records both complete-file and pixel-payload hashes, the exact input derivation, raw samples, CSV, source/binary bindings and supplementary scope.

The next bounded CPU experiment should address **Mono12 normalization**: its current general path performs an integer division for each sample, while the common maximum 4095 permits a constant-denominator specialization. Preserve exhaustive values, rounding, first-invalid-sample behavior, strides and padding, then measure the real Mono12 path. Next inspect generated code and profile display mapping/orientation; the two mappings cost about 15 ms combined, and the two rotated displays add about 39 ms here. CLAHE remains another major cost, so profile its tile and interpolation phases before selecting further work. These are recommendations, not implemented changes or promised gains. Benchmark the designated Windows workstation before deciding whether a larger backend change is necessary for the 2048 target.

## Publication and remaining gates

The branch is local and unpublished. Publication, hosted Linux/Windows CI and merge require explicit authorization for this new branch. The prior automatic approval rejection concerned publishing a different M8 optimization branch under permission that named the M7 branch; no M8 optimization payload was pushed by that rejected action.

Windows/MSVC verification of this branch, designated Windows reference/workstation/freshness acceptance, the owner's deferred M4/M5 Windows 11 validation and M6 camera/NIC profile remain open. Linux results do not close those gates. M8 remains open for further measured latency work and the designated-platform acceptance gates above.
