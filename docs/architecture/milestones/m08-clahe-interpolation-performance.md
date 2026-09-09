# M8 CLAHE interpolation performance continuation

**Date:** 2026-09-09. **Status:** Implementation and evidence approved; integrated through PR #16.

This bounded continuation profiles the current CLAHE stage and removes repeated loads of array addresses inside the interpolation pixel loop without changing output values. The matched normal baseline is clean `c03251013c38de4e0d7a3f244200c65bbacbb9ba`; the measured candidate is clean `afe5de8d9573ef9995409f8c82318418973fd4f3` on the historical `perf/m08-clahe-interpolation` branch. Intervening changes from the original frozen `e801ece` source through `7bd8122` are documentation-only for the relevant production source. Its later integration is recorded below.

## Coarse phase diagnosis

Three paired off/coarse 2048-square identity runs use five warm-ups and 20 measured calls. The coarse derivative clocks only the two executor dispatches and preserves the tile and row callback source bodies byte-for-byte. Its whole-CLAHE run medians differ by at most 2.409% from the corresponding off runs. The median of three run medians attributes about **5.522 ms** of caller wall to tile LUT generation and **17.968 ms** to row interpolation.

This is supplementary caller-wall diagnosis, not the normal 100/500 benchmark or a machine-code-identity claim. Worker elapsed times overlap and cannot be added as CPU time. The deeper phase-v2 derivative ran about 13% faster than the clean control, demonstrating instrumentation perturbation; its reset/fill/bin measurements are qualitative only. The hardened phase-v3 validator and independent helper review approve this claim separation and all retained exact-output/capture checks.

## Retained source change

Inside the interpolation callback, `ClaheStage.cpp` binds six immutable locals once: five LUT/index/weight bases and the image width. The loop then uses those bases instead of repeatedly loading the array addresses. The callback bodies otherwise keep the same indexing, sample loads, offsets, signed width, loop bounds, float expression grouping, float result, and final `cv::saturate_cast`.

All 65,536 histogram bins, tile geometry, clipping/CDF/LUT generation, resource accounting, execution slots, thread ownership, validation and error behavior, formats, buffers, and public interfaces are unchanged. Prepared vectors are not resized during execution, tile submission completes before interpolation, and the executor waits for all callbacks before the dispatch returns, so the cached read-only bases retain their original lifetime and sharing contract. Independent spec and quality review approves the one-file source change with no findings.

## Verification

- Full Linux Debug passes 54/54 in 60.09 seconds; Release passes 54/54 in 24.92 seconds.
- Native X11 passes 1/1 in Debug/Release in 0.11/0.05 seconds.
- Targeted CLAHE and engine ASan/UBSan checks with leak detection pass 2/2 in 13.72 seconds.
- The strict final normal verifier passes with all complete hashes, measured aggregate fingerprints, resources, errors, drops, and C++ allocation counts equal.
- Both 64×48 normal portable and separately controlled glibc profiles pass at 100 warm-ups/1,000 measured cycles with zero measured allocation events.
- All 26 reference PGM files are byte-identical with matching hashes.

The Debug/Release, native X11, and sanitizer test processes ran from `7bd8122` plus the final one-file source diff, which is identical to the later clean `afe5de8` source. The frozen after normal, paired-profile, and reference binaries use clean `afe5de8`; the record does not relabel the earlier test processes as committed-source runs.

## Matched normal Mono12 result

Both sources use the strict six-row Mono12 v3 full Standard workload: 512, 1024, and 2048 squares, identity then flip/90°, with 100 warm-ups and 500 measured frames per row.

| Size | Orientation | Before → after median (ms) | Before → after P95 (ms) | Before → after FPS |
| --- | --- | ---: | ---: | ---: |
| 512×512 | Identity | 6.774 → 6.644 | 9.117 → 8.945 | 141.433 → 145.010 |
| 512×512 | Flip / 90° | 7.117 → 7.032 | 8.868 → 9.391 | 137.879 → 134.976 |
| 1024×1024 | Identity | 16.177 → 16.090 | 21.486 → 20.761 | 58.382 → 60.620 |
| 1024×1024 | Flip / 90° | 18.892 → 18.729 | 24.417 → 24.047 | 50.873 → 51.796 |
| 2048×2048 | Identity | 58.867 → 57.675 | 73.353 → 72.496 | 16.415 → 16.813 |
| 2048×2048 | Flip / 90° | 66.899 → 64.292 | 88.802 → 72.849 | 14.176 → 15.305 |

All six medians improve and five of six FPS values improve. At 2048, FPS rises **2.42% / 7.96%** for identity/flip90. The 512 flip90 result is mixed: FPS falls **2.105%** and P95 rises **5.902%**, despite its slightly lower median. Both 2048 profiles remain below 30 FPS.

## Supplementary paired CLAHE result

Across AB/BA/AB runs, the median of three run medians falls from **23.841 to 21.706 ms** for identity and **23.350 to 21.322 ms** for flip90. Not every pair improves: the second flip candidate is **25.211 ms**, slower than its matching **23.350 ms** baseline run, and its unchanged sharpening control also rises to about **11.39 ms** from **10.407 ms**. The ancillary unchanged normalization kernels also have mixed tails: maximum 4094 P95 rises from 24.635 to 27.785 ms while its median stays about 23.84 ms. These controls show run variability. The normal full-pipeline gain is not attributed solely to CLAHE, and the supplementary stage result is not converted into FPS.

The implementation is retained for the narrow 2048 and paired-CLAHE effects because the source change is small and exact outputs and resource/allocation contracts are preserved. The disclosed 512 flip90 regression does not require another repeat for those bounded claims. No universal speedup or no-regression claim is made. The independent final evidence audit approves this disposition with no required correction or additional timing run; its ledger is `.superpowers/sdd/2026-09-09-m08-clahe-phases/final-evidence-audit.md`.

## Evidence, integration, and open gates

Frozen normal, allocation, phase, paired-stage, reference, validation, and review evidence is retained under `out/qa/m08-clahe-phases/` in the preserved worktree. The source and helper reviews are in `.superpowers/sdd/2026-09-09-m08-clahe-phases/`.

[PR #16](https://github.com/m4bulmagd/Lumora/pull/16) merged as `ed238d7c49d85ac861d09bd7be60aed3d42fbc6b`, whose commit time is 2026-09-09T14:54:38Z. Its tree `39c66795139944cacf0715be47cdfd609d6f2c01` equals the verified PR-head `aff3e130aeb6a2e292c5a98ee70cd456497c69fe` tree. Fresh local verification at that head passed 54/54 in Debug (71.62 s) and Release (27.15 s), plus native X11 1/1 in Debug (0.12 s) and Release (0.07 s). An initial native run lacked display-socket access; the identical command passed with desktop access and no source change.

[Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34365380855) passed 54/54 in Debug (73.48 s) and Release (23.94 s), plus native X11 1/1 in Debug (0.12 s) and Release (0.04 s). [Windows/MSVC PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34365380852) passed 54/54 in Debug (67.29 s) and Release (38.74 s). Matching [Linux](https://github.com/m4bulmagd/Lumora/actions/runs/34365346561) and [Windows](https://github.com/m4bulmagd/Lumora/actions/runs/34365346571) branch-push workflows also passed.

Performance evidence remains bound to clean baseline `c03251013c38de4e0d7a3f244200c65bbacbb9ba` and measured clean candidate `afe5de8d9573ef9995409f8c82318418973fd4f3`; the PR head and merge commit are integration revisions. Post-merge main-workflow results have not yet been verified; the [Linux](https://github.com/m4bulmagd/Lumora/actions/workflows/linux-simulator.yml?query=branch%3Amain) and [Windows](https://github.com/m4bulmagd/Lumora/actions/workflows/windows-simulator.yml?query=branch%3Amain) workflow pages remain the status authority.

Hosted Windows/MSVC CI establishes source build/test compatibility. It does not close the 2048 33.3 ms/30 FPS target, designated Windows reference/workstation/freshness and performance acceptance, native Windows 11 visual/DPI and packaging validation, deferred M4/M5 acceptance, or the M6 camera/NIC profile. M9 presets, controls, and synchronized views remain later work.
