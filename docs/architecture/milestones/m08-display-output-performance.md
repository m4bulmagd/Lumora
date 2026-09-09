# M8 display-output performance continuation

**Date:** 2026-09-09. **Status:** Implementation and evidence approved; integrated through PR #15.

This bounded continuation targets measured display-output cost without changing image values or interfaces. Clean baseline `405c97b721a2699463626d0219cf2b345b7ae30c` repeatedly called immutable layout getters inside the mapper pixel loop and orientation row/tile loops. Clean candidate `e801ecea0c99e7a8f3a2659b8db8b1408e031733` hoists those getter results into local immutable values in two implementation files. The historical `perf/m08-display-output` branch was local during implementation and measurement; its later integration is recorded below.

## Retained implementation

`DisplayMapper.cpp` caches the already-validated source width and height plus destination stride and active row bytes after all existing pre-write validation and overlap checks. Its byte-safe U16 loads and exact Gray8 expression remain:

```text
(uint32(value) + 128) / 257
```

`OrientationTransform.cpp` caches source and destination dimensions, strides, and active row bytes before the same-axis and swapped-axis loops. All compile-time reversals, source-coordinate formulas, 32×32 transpose tiling, active-row `memcpy` calls, and byte-safe per-pixel copies are unchanged. No public API, validation/error order, output metadata, buffer ownership, resource accounting, engine hook or timing boundary, thread policy, schema, source format, or reference output changed.

The normal SIM-LIVE path still supplies Mono12 samples with 12 valid bits (0–4095) unpacked into a UInt16 application buffer. Normalization expands them across the full 0–65535 `CanonicalU16` working range; processing remains high depth, and Original and Enhanced terminal frames remain Gray8. Scaling the working range does not add captured sensor information. Physical-camera format and transport packing remain unknown pending the M6 hardware profile.

## Frozen normal baseline

The authenticated clean `405c97b` baseline uses the strict Mono12 v3 full Standard workload: 512, 1024, and 2048 square inputs, identity then horizontal-flip/clockwise-90° orientation, 100 warm-ups and 500 measured frames per row. It is a new run against the already integrated Mono12 implementation, not a replacement for the historical `1148036`/`717acc8` comparison.

| Size | Orientation | Median / P95 (ms) | FPS |
| --- | --- | ---: | ---: |
| 512×512 | Identity | 7.529 / 10.260 | 125.518 |
| 512×512 | Flip / 90° | 11.283 / 12.251 | 94.228 |
| 1024×1024 | Identity | 19.043 / 25.524 | 48.234 |
| 1024×1024 | Flip / 90° | 27.499 / 35.675 | 34.130 |
| 2048×2048 | Identity | 70.351 / 96.831 | 13.364 |
| 2048×2048 | Flip / 90° | 103.979 / 116.029 | 9.479 |

All six rows completed with zero processing errors, drops, or measured C++ allocation calls/bytes/releases. The associated portable and glibc allocation captures and the independent before verifier passed. These baseline results vary from the earlier clean `717acc8` run and are kept as the matched baseline for this experiment; they do not revise the earlier record.

## Supplementary stage profile

The frozen stage helper runs the actual 2048×2048 Mono12 Session with five warm-ups and 20 measured calls for identity and flip/90°. It uses the same descriptor, input, resources, stage order, and composite output hashes as the corresponding normal baseline rows.

| Profile | Original map | Enhanced map | Original orientation | Enhanced orientation | CLAHE |
| --- | ---: | ---: | ---: | ---: | ---: |
| Identity median (ms) | 7.173 | 7.247 | Not applied | Not applied | 22.893 |
| Flip / 90° median (ms) | 7.036 | 7.327 | 16.584 | 16.981 | 23.612 |

The two identity mappings total about 14.4 ms. In the rotated profile the two mappings plus two orientations total about 47.9 ms, while CLAHE is about 23.6 ms. These figures justify investigating getter hoisting but remain supplementary: the helper measures five warm-ups and 20 calls, excludes returned-frame destruction, does not hash every measured output, and is not an allocation proof, normal 100/500 throughput result, publication result, or acceptance result.

## Characterization and source verification

The new `MapsUnalignedVectorBoundaryWidthsAcrossPaddedRows` characterization passed against the unchanged baseline in Debug and Release before production edits. It covers widths 7, 8, 9, 15, 16, 17, 31, 32, and 33 over three rows with unaligned starts and unequal odd strides; an independent scalar oracle checks every active sample, boundary values, source immutability, destination padding/canaries, and returned mapping metadata.

At the candidate:

- focused Debug and Release mapper/orientation/engine checks pass 3/3 in 0.66/0.14 seconds;
- full Debug and Release pass 54/54 in 59.67/25.04 seconds;
- native X11 passes 1/1 in Debug and Release in 0.11/0.06 seconds;
- targeted ASan/UBSan with leak detection passes 3/3 in 1.79 seconds; and
- independent spec and quality review approves the source with no findings.

The first sandbox X11 attempt could not connect to the display before application startup. The first sandbox sanitizer run completed all assertions but LeakSanitizer could not operate under ptrace. Matching host-permission retries passed without source or dependency changes; these environmental failures are retained rather than relabeled as source failures.

Release disassembly places cached getter calls before the hot loops. The mapper loop is auto-vectorized in 16-pixel blocks with scalar tails and no layout getter call in the processing loop. Inspected same-axis and swapped-axis orientation specializations likewise contain no layout getter call in their row/tile/pixel loops. This establishes the intended mechanism; the matched measurements below decide whether it is retained.

## Candidate evidence

The clean candidate `e801ecea0c99e7a8f3a2659b8db8b1408e031733` has freeze SHA-256 `96e4ac468a31f8d1269325a8c8712f9bf91f875f539e87550d946460b24ea621`; the before freeze is `9a27787c54ceef703f812176617dbf7fbb9f85cb04629700af093d71e0a7bfe7`.

| Size | Orientation | Before → after median (ms) | Before → after P95 (ms) | Before → after FPS |
| --- | --- | ---: | ---: | ---: |
| 512×512 | Identity | 7.529 → 6.825 | 10.260 → 9.861 | 125.518 → 136.214 |
| 512×512 | Flip / 90° | 11.283 → 7.204 | 12.251 → 9.615 | 94.228 → 131.662 |
| 1024×1024 | Identity | 19.043 → 16.330 | 25.524 → 20.889 | 48.234 → 59.722 |
| 1024×1024 | Flip / 90° | 27.499 → 19.220 | 35.675 → 24.557 | 34.130 → 50.447 |
| 2048×2048 | Identity | 70.351 → 59.203 | 96.831 → 71.442 | 13.364 → 16.438 |
| 2048×2048 | Flip / 90° | 103.979 → 66.513 | 116.029 → 75.686 | 9.479 → 14.780 |

All six normal profiles improve in median, P95, and observed FPS. At 2048, throughput rises **23.0%** for identity and **55.9%** for flip/90°. Both remain below the 30 FPS target.

The alternating AB/BA/AB stage experiment forms each output-path sample by summing the corresponding Original and Enhanced mapping samples within one frame and, for flip/90°, the corresponding two orientation samples. It then takes each run's median and the median of the three run medians. This is not a sum of separately reported stage medians. The paired output path falls from **14.508 to 4.143 ms** for identity, a **71.44%** reduction, and from **48.484 to 10.793 ms** for flip/90°, a **77.74%** reduction. Candidate mapping medians are 2.000/2.149 ms for identity and 2.000/2.114 ms for flip/90°; rotated Original/Enhanced orientation medians are 3.262/3.453 ms.

Unchanged candidate controls remain about 23.070/23.149 ms for CLAHE, 10.497/10.432 ms for sharpen, 7.221/7.246 ms for denoise, and 6.667/6.598 ms for normalization. Their before/after movements are mixed: flip CLAHE, both normalization profiles, and both sharpen profiles are slightly slower, while shared Window/Level and some neutral tone timings move lower despite unchanged code. The reused ancillary normal kernel control likewise has a nearly flat maximum-4095 median, 6.425→6.439 ms, but P95 rises 6.517→13.765 ms; generic 4094/4096 medians stay near 23.84 ms with mixed tails. This is not a normalization change or primary speedup result. Scheduling, clock, cache, and thermal variation therefore remain; the full-pipeline gain is an observed matched-run result and is not attributed entirely to the output-path reduction or to a sum of stage medians.

The getter-hoist implementation is retained. The strict final normal verifier passes: all complete outputs, measured aggregate FNV fingerprints, resource plans, errors, drops, and measured C++ allocation counts match, with zero errors, drops, calls, bytes, or releases. Both 64×48 normal portable and separately controlled glibc allocation profiles pass at 100 warm-ups/1,000 measured cycles. All 26 reference PGM files are byte-identical with matching hashes. Independent final evidence audit is approved with no required corrections.

The subsequent local [CLAHE interpolation continuation](m08-clahe-interpolation-performance.md) profiles tile-LUT generation against row interpolation and retains a bounded callback-cache change after independent evidence audit. M9 presets, controls, and synchronized views remain later work.

## Evidence, integration, and open gates

Baseline, stage-profile, validation, disassembly, and candidate artifacts are retained under `out/qa/m08-display-output/` in the preserved feature worktree. The performance figures remain bound to clean baseline `405c97b721a2699463626d0219cf2b345b7ae30c` and measured clean candidate `e801ecea0c99e7a8f3a2659b8db8b1408e031733`.

[PR #15](https://github.com/m4bulmagd/Lumora/pull/15) merged at 2026-09-09T09:54:34Z as `c4f7fed7b16822de0b81e079b2dd68b18861c580`. Its tree `e3dbb3eba1226090a9908c2818ad6d546da588db` equals verified PR-head `c03251013c38de4e0d7a3f244200c65bbacbb9ba` tree. Fresh local verification at that head passed 54/54 in Debug (60.03 s) and Release (25.33 s). [Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34336142497) passed 54/54 in Debug (73.48 s) and Release (23.90 s), plus native X11 1/1 in Debug (0.43 s) and Release (0.04 s). [Windows/MSVC PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34336142419) passed 54/54 in Debug (85.00 s) and Release (39.41 s). Matching [Linux](https://github.com/m4bulmagd/Lumora/actions/runs/34336138823) and [Windows](https://github.com/m4bulmagd/Lumora/actions/runs/34336138794) branch-push runs also passed. The PR head and merge commit are integration revisions, not measurement revisions.

Post-merge main checkpoint `7bd8122b54af892514aede81ec7aaf7b1599cf86` passed [Linux main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34337808735) and [Windows main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34337808708). Those workflows verify the display-output integration through PR #15; they do not cover the later local CLAHE source `afe5de8`.

The 2048 33.3 ms/30 FPS goal, designated Windows reference/workstation/freshness and performance acceptance, native Windows 11 visual/DPI and packaging validation, deferred M4/M5 acceptance, and the M6 camera/NIC profile all remain open. No simulator or Linux result establishes the physical Basler format or designated Windows performance.
