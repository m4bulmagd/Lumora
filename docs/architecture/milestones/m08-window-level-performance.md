# M8 Window/Level latency optimization

**Date:** 2026-09-08

The owner approved continued latency work after [M8 Tasks 3–5](m08-continuation.md) merged through PR #12. This bounded first slice starts from `7a81e65` and optimizes the existing full-range Window/Level mapping. The larger M8 performance and designated Windows acceptance gates remain open.

## Contract and approach

Once the existing validation derives clipped endpoints of exactly 0 and 65535, every U16 sample maps to itself. The stage can therefore copy each active row into its distinct output. Each view supplies its own stride; byte copying supports unaligned storage and leaves padding untouched. All parameter, storage, domain, extent, full-payload overlap and endpoint checks precede this path. Other windows retain the existing scalar mapping and rounding.

This retains the enabled operation, canonical domain, timing entry, output ownership and resource accounting. Standard parameters, independent expected images and freshness/recovery behavior remain unchanged.

The existing exhaustive identity test already covers all 65,536 inputs. Additional test-first characterization protects unequal padded strides and unaligned backing storage, including nondefault windows clipped to full range. A temporary incorrect copy mutation must demonstrate that the added coverage detects padding corruption. Such characterization is expected to pass on the correct pre-optimization code; it is not described as a newly failing functional requirement.

## Baseline and evidence scope

The clean `7a81e65` Release build passes the four affected processing registrations before changes. A local direct-stage diagnostic uses a 2048×2048 ramp, three repeated rounds of 20 warm-up and 100 measured calls for default identity, clipped-wide identity and nonidentity windows. The same harness, inputs and compiler options are used before and after, with output checksums outside timing. Mean baseline round medians are approximately 26.8 ms, 26.8 ms and 10.4 ms respectively.

A separate existing benchmark CLI run uses an explicitly custom workload: size 2048, five warm-ups and fifteen measured frames for all eleven rows. This provides a quick paired full-Standard comparison, including both orientations, complete output SHA256/fingerprints, requested resources and allocation counters. It is not normal workstation acceptance evidence. The prior full normal M8 results retain their original build provenance.

## Implementation and local verification

Commit `b836334` adds the nine-line row-copy path and two focused tests, and strengthens unchanged-destination assertions for invalid views. Independent task review approves the implementation with no Critical/Important findings. The author reports shell-level stream-fd noise; independent root runs using a clean shell retain no compiler or shell diagnostics.

Direct Window/Level tests pass 11/11 in Debug and Release. A temporary stride-sized copy mutation fails both new canary cases; restoring active-row copying returns both configurations to green. Full application verification passes 53/53 Debug (170.36 s) and Release (23.73 s), plus native X11 1/1 each (0.12/0.07 s).

## Paired diagnostic result

| Direct stage case | Baseline mean of round medians | Optimized mean of round medians |
|---|---:|---:|
| Default full-range identity | 26.805 ms | 1.626 ms |
| Clipped-wide identity | 26.786 ms | 1.650 ms |
| Nonidentity window control | 10.408 ms | 10.345 ms |

The default identity diagnostic improves about 93.9%; this is a standalone-stage result. The existing custom CLI comparison reports full Standard medians of 267.622 to 250.589 ms with identity orientation and 389.191 to 377.598 ms with horizontal flip/90-degree rotation. Corresponding P95 values are 272.061 to 270.851 ms and 401.012 to 391.307 ms. Fifteen measured samples are useful diagnostic evidence but are insufficient to infer a stable P95 improvement or workstation acceptance.

All eleven custom rows retain identical complete output SHA256/fingerprints, input patterns, pipeline definitions, resource plans and sample counts, with zero measured C++ allocation calls/bytes/releases. The before/after direct-stage output checksums also match. The benchmark artifacts retain actual build provenance: baseline clean `7a81e65`, candidate `b836334` with only this untracked documentation record reported dirty. Later clean normal evidence will retain its own build revision.

## Normal Release evidence

The full normal protocol completed from clean build `8877f4637efc1fb7422196dd4b5c40124ed729dd`, with source provenance captured at `2026-09-08T08:52:29Z`. This is GNU 15.2.0 Release (`-O3 -DNDEBUG`) on the Ubuntu 26.04 / i7-10510U laptop, using the same pinned dependencies and FE_TONEAREST contract as the preceding M8 evidence. The binary remained unchanged throughout the sequential run, with no concurrent local builds, tests or profiling.

All 33 cases completed 100 warm-ups and 500 measured frames with zero drops, processing errors or measured C++ allocation calls/bytes/releases. Complete output SHA256/fingerprints, inputs, pipeline definitions, resource plans and sample counts match the prior clean normal `1b85953` artifacts. Independent arithmetic, resource/count, provenance and schema checks pass.

| Standalone Window/Level size | Prior normal median | Current normal median |
|---|---:|---:|
| 512×512 | 1.563 ms | 0.026 ms |
| 1024×1024 | 6.450 ms | 0.098 ms |
| 2048×2048 | 25.479 ms | 1.706 ms |

| Full Standard size / orientation | Median | P95 | Wall-derived FPS |
|---|---:|---:|---:|
| 512×512 / identity | 26.180 ms | 29.027 ms | 37.747 |
| 512×512 / horizontal flip + 90° | 31.245 ms | 34.112 ms | 31.655 |
| 1024×1024 / identity | 72.548 ms | 77.673 ms | 13.644 |
| 1024×1024 / horizontal flip + 90° | 101.237 ms | 127.782 ms | 9.414 |
| 2048×2048 / identity | 245.099 ms | 258.911 ms | 4.057 |
| 2048×2048 / horizontal flip + 90° | 370.323 ms | 388.070 ms | 2.688 |

The standalone identity improvement is consistent with the paired diagnostic. Full-pipeline results are mixed across these separate normal runs: 2048 P95 falls from 282.622/410.989 ms to 258.911/388.070 ms, but 1024 oriented P95 rises from 105.444 to 127.782 ms while its median changes from 101.525 to 101.237 ms and FPS falls from 9.822 to 9.414. Each revision has one normal run; CPU frequency, load and thermal state are not captured. These observations neither establish a general end-to-end improvement nor identify a causal regression. High-resolution 33.3 ms/30 FPS acceptance remains unmet.

Both normal Standard allocation profiles (64×48, identity and horizontal flip/90°) complete 100 warm-ups and 1,000 measured cycles with zero C++ allocation calls/bytes/releases. A separate controlled glibc 2.43 tracing process completes the same profiles with zero measured allocator events; positive and empty controls, trace hashes and trace contents pass independent checks. This is bounded allocation evidence, not a full-resolution latency or Windows heap proof.

The thirteen ordinary candidate cases produce 26 PGM files byte-identical to the preceding candidates. Independent checks verify their hashes, P5 headers and lengths; seven independent exact comparisons remain exact, while six backend cases remain provisional with unapproved thresholds/reviews. Candidate and allocation artifacts retain the same clean build provenance. No reference acceptance is inferred.

Raw evidence, diagnostic executables, commands and verification logs remain under `out/qa/m08-window-identity/` in the preserved M8 worktree; normal outputs are in its `normal/` subdirectory.

## Review, integration and next slice

Independent task and whole-branch source/document review approve the implementation with no Critical/Important findings. [Linux CI at `8877f46`](https://github.com/m4bulmagd/Lumora/actions/runs/34206884738) passes 53/53 Debug and Release checks plus native X11 in both; [Windows CI at the same head](https://github.com/m4bulmagd/Lumora/actions/runs/34206884739) passes 53/53 in both configurations. Final evidence/documentation review and matching integration-head CI remain required before merge.

The recommended next bounded experiment is to combine CLAHE histogram redistribution with its existing cumulative LUT pass while preserving every effective bin count and rounding operation. CLAHE is the largest measured Standard stage at 512/1024 and second-largest at 2048; this local change could remove repeated histogram writes without changing retained storage or ownership. Its speedup must be measured before selection for integration. Sharpen's larger intermediate-storage opportunity is a separate later slice. M9 controls follow the performance work; designated Windows references/workstation/freshness acceptance and the earlier deferred gates remain open.
