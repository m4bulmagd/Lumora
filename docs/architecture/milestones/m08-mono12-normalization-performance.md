# M8 exact Mono12 normalization performance

**Date:** 2026-09-08. **Status:** Historical local measurement and review complete; integrated through PR #14.

Mono12 normalization is about **4.70× faster** in three alternating before/after kernel repeats, with unchanged pixels. The full 2048×2048 Standard Mono12 pipeline measures **13.92 FPS** for identity and **9.56 FPS** for horizontal flip/clockwise 90° rotation, versus **8.71 / 6.54 FPS** in the matched normal baseline. Both remain below 30 FPS.

The owner approved the [design](../../superpowers/specs/2026-09-08-m08-mono12-normalization-design.md) and [implementation plan](../../superpowers/plans/2026-09-08-m08-mono12-normalization.md). The historical measurement branch `perf/m08-mono12-normalization` started at `c7cee62314c95eeba3191e07e8fec35ec12e37f3` and included the preceding then-unpublished [CPU optimizations](m08-cpu-pipeline-performance.md). The preserved worktree is `.worktrees/m08-tone-stages`; main was at `0df7a98` when this checkpoint began. Publication and hosted CI are recorded in the integration addendum below and do not change this measurement scope.

## Exact implementation and arithmetic decision

The existing validated Mono16 active-row copy remains first. A private templated pixel loop selects the new arithmetic only for validated UInt16 storage with `sampleMaximum == 4095`; descriptor name and validBits alone do not select it. All other maxima/storage retain the generic formula. Validation precedence, unaligned byte access, unequal strides, source immutability, padding and row-major first-invalid diagnostics remain unchanged. Valid prefix pixels are written before the first invalid sample; the invalid pixel and suffix remain untouched.

The mathematical contract is the original rounded integer result:

```text
floor((65535*v + 2047) / 4095)
  = 16*v + floor((15*v + 2047) / 4095),  0 <= v <= 4095
```

GCC 15 at the actual Release `-O3` configuration kept a recurring hardware DIV for direct constant-division forms, including the equivalent `16*v + (v+136)/273`. The retained implementation therefore evaluates the exact correction through a bounded quotient:

```text
m = 15*v + 2048
correction = (m + (m >> 12)) >> 12
output = 16*v + correction
```

This computes `floor((m-1)/4095)`, not `floor(m/4095)`. To see why, write `m = 4096*a+b`. The validated range gives `2048 <= m <= 63473`, `0 <= a <= 15`, `0 <= b <= 4095` and `1 <= a+b <= 4110`. Both the shift expression and division expression equal `a`, plus one exactly when `a+b >= 4096`. Uint64 arithmetic has ample headroom. All 4,096 values are compared against an independent direct-division oracle. The original proposed constant-expression implementation is replaced by this mathematically equivalent bounded evaluation; this is an explicit implementation decision supported by code generation and measurement.

Tests pin `136→2176`, `137→2193`, `2048→32776` and `4095→65535`, exercise the complete domain across odd, unaligned, padded rows, and cover multiple first-invalid positions. Neighboring maxima 4094/4096 and validBits16/max4095 protect dispatch. Deliberate four-bit-shift and omitted-range-check mutations fail, then the restored implementation passes. Generated Release code has no division in the fixed valid-pixel loop; the generic loop retains runtime division. No LUT, SIMD path, dependency, allocator state, worker, interface or resource-plan change was added.

## Real Mono12 evidence and compatibility

The normal SIM-LIVE composition requests 640×480 at 30 FPS and produces Mono12 samples with 12 valid bits (0–4095), unpacked and least-significant in a UInt16 application buffer. Lumora normalizes them across the 0–65535 `CanonicalU16` working range, retains high-depth raw and processed data, and maps Original and Enhanced presentation frames to Gray8. Scaling the working range does not add captured sensor information. Physical-camera format and transport packing remain unknown pending the M6 hardware profile, and the 2048 evidence below does not describe the configured simulator rate.

Evidence-only `--source-format mono12` changes the actual Session acquisition descriptor and input, and emits strict v3 artifacts for full Standard rows only. Input derives the upper twelve bits from the existing uint32 xorshift sequence: `uint16(state >> 16) >> 4`. It is serialized with the existing big-endian U16 PGM encoding and maximum 65535. Source format and derivation are required metadata; unsupported standalone Mono12 measurement calls reject with code 2.

Default and explicit Mono16 retain v2, eleven benchmark rows per size and all prior schema bytes. Reference/workstation artifacts remain v1; the canonical workstation attachment contract still accepts only Mono16 v1/v2 evidence. New Mono12 measurements do not silently replace that acceptance workload. The reference generator does not accept the new selector.

The normal Mono12 benchmark covers 512, 1024 and 2048 squares, identity then flip/90°, with **100 warm-ups and 500 measured cycles per row**. FPS is measured frames divided by wall time, including pooled output turnover and fingerprint work. Physical camera acquisition and Qt presentation are outside this processing benchmark. Exact image hashing and schema checks run outside the measured loop. The older twenty-call Mono12 diagnosis and canonical Mono16 results are distinct workloads and are not substituted for this baseline.

## Normal complete-pipeline comparison

The evidence-only baseline is clean `1148036ced51d7df1e6ee0aab29c6ed9347a221a`; the optimized source is clean **`717acc89b95d309dd629e025f0fc0db17af9e2a3`**. Both use the same i7-10510U Linux laptop, Ubuntu 26.04, GCC 15.2, Qt 6.11.1, OpenCV 4.12.0 port 9, Release flags and prepared four-slot execution. No other local builds, tests or measurements ran concurrently with the timed captures.

| Size | Orientation | Before median / P95 (ms) | After median / P95 (ms) | Before → after FPS |
| --- | --- | ---: | ---: | ---: |
| 512×512 | Identity | 16.318 / 18.314 | 7.552 / 9.852 | 63.418 → 127.102 |
| 512×512 | Flip / 90° | 16.641 / 18.003 | 11.540 / 12.700 | 60.232 → 87.622 |
| 1024×1024 | Identity | 33.410 / 43.358 | 19.279 / 24.530 | 28.788 → 49.155 |
| 1024×1024 | Flip / 90° | 43.312 / 50.178 | 27.816 / 33.637 | 22.436 → 34.485 |
| 2048×2048 | Identity | 110.785 / 134.802 | 69.334 / 85.424 | 8.706 → 13.918 |
| 2048×2048 | Flip / 90° | 150.831 / 166.992 | 102.875 / 115.593 | 6.537 → 9.558 |

All six profiles improve in median, P95 and observed FPS in these normal runs. At 2048, median falls **37.42% / 31.80%** and FPS increases **59.86% / 46.22%**. The 1024 identity profile meets both 30 FPS and 33.3 ms P95 on this host; the rotated 1024 profile exceeds 30 average FPS but its P95 is **33.64 ms**, slightly above the latency target. Both 2048 profiles still miss the target.

These are observed before/after runs on a laptop, not a universal speedup or an isolated attribution of every full-pipeline millisecond to normalization. Control timings changed between the initial baseline and later repeated captures, so scheduling/clock/thermal variability is not eliminated. The alternating kernel experiment below supplies the narrower repeated evidence for retaining the arithmetic change.

## Alternating kernel experiment and controls

Frozen before/after kernels run in **AB / BA / AB** order. Each invocation uses 2048×2048, 20 warm-ups and 100 measured samples for each maximum. The harness validates full source immutability and every output sample with an independent quotient/remainder oracle outside timing. Its seeded modulo(maximum+1) input with pinned endpoints is explicitly different from the full-pipeline upper-twelve-bit pattern. Per-profile complete input/output PGM hashes and equal-count fingerprints agree across all six invocations. Medians and nearest-rank P95 are reconstructed from the raw chronological samples.

Each cell lists the three separate repeats in pair order; times are milliseconds.

| Maximum | Before medians | After medians | Before P95 | After P95 |
| --- | ---: | ---: | ---: | ---: |
| 4095 | 25.966, 25.973, 25.950 | 5.482, 5.523, 5.613 | 28.175, 27.684, 29.021 | 5.579, 6.449, 6.441 |
| 4094 | 25.967, 26.043, 26.080 | 24.229, 24.367, 24.492 | 27.119, 38.247, 31.424 | 25.586, 27.400, 26.604 |
| 4096 | 26.052, 25.898, 25.972 | 24.217, 24.408, 24.637 | 32.903, 26.724, 28.570 | 26.546, 24.659, 25.056 |
| 65535 | 1.120, 1.116, 1.161 | 1.177, 1.102, 1.158 | 1.155, 1.186, 1.342 | 1.904, 1.147, 1.514 |

For maximum 4095, the median of the three per-run medians falls from **25.966 to 5.523 ms**, a **78.73%** reduction, or **4.70×** speedup. Every pair improves strongly. Generic 4094/4096 controls also improve modestly in these repeats; no material generic regression is observed. The untouched 65535 copy has mixed medians (about 1.1–1.2 ms) and noisier tails, including 1.155→1.904 ms P95 in the first pair. It is not claimed as an optimization benefit.

The earlier normal-capture kernel measured 34.060→6.419 ms for 4095, while its generic controls shifted from about 34 to 24 ms. The alternating repeats are used for the narrower normalization speedup claim; those two separate timing conditions must not be combined into a stronger causal claim.

## Allocation, resources and references

All six normal benchmark rows have identical complete composite output SHA-256 and equal-count fingerprints, zero processing errors/drops, and zero measured C++ allocation calls/bytes/releases. The independent verifier regenerates all input hashes from the specified xorshift derivation and checks exact schema versions, descriptors, row products, timing arithmetic and unchanged resource/execution fields.

Both **64×48** normal allocation profiles complete 100 warm-ups and 1,000 measured cycles in portable and separately controlled glibc captures, with zero measured C++ counts and zero raw allocator events. Empty, caller-positive and real-helper positive controls pass, with matching loader/marker/malloc providers. Allocation artifacts carry no output checksum: output equality is established by the benchmark and kernel evidence. This run does not establish large-image glibc coverage or per-job helper participation; the larger benchmark rows establish C++ counters only. Steady-state success after preparation/warm-up is the measured boundary, with existing allocator/platform exclusions retained.

All resource plans are unchanged. At 2048, candidate requirement is 43,656,344 bytes, actual retained stage storage 43,651,608, executor 280 and engine state 5,712 bytes. Fixed accounted session storage remains 151,020,280 bytes for identity and 155,214,584 for flip/90°. These are accounted bounds, not process RSS; the configured limit remains 512 MiB, with existing thread-stack/TLS/runtime exclusions.

A fresh reference generator run at clean 717acc8 produces all **26 candidate/input PGM files byte-identical** to the parent bundle. This preserves existing references and their acceptance status; it does not approve provisional Windows candidates.

## Verification, provenance and reproduction

Full Linux Debug and Release each pass **54/54** checks (69.93 / 26.76 seconds), including default v2 and new v3 smoke. Native X11 passes **1/1** in each configuration. Targeted NormalizeStage, FrameProcessingEngine and FrameEngineAllocation checks pass **3/3 under ASan/UBSan with leak detection** (19.41 seconds), and the tests-OFF/tools-ON build passes. Initial sandbox native-display and LeakSanitizer-ptrace failures are retained; the successful native and leak-enabled checks ran untraced, without disabling leak detection or installing dependencies.

Task and whole-branch source reviews approve the implementation. The source review's design-wording Minor is addressed by the explicit arithmetic decision above and in the design. A suggested expansion of the compact source comment is covered by the tracked proof here; the source comment itself is unchanged. The baseline verifier's deprecated resolver was replaced with an offline-only `referencing.Registry`, and its clean rerun has no warning. The direct standalone guard has a passing Release-linked ignored QA probe; that probe remains a manual check outside CTest/CI. Final independent evidence/documentation review is approved with no findings; the paired raw-data audit and reference-file audit pass.

Evidence is retained under `out/qa/m08-mono12-normalization/` in the preserved worktree. `before-v2/` and `after/` each bind 30 frozen files, including executables, schemas, static archives, source/header/build records and actual kernel compile command/disassembly. Compiler/linker and dynamically loaded providers are resolved and hashed. Immutable invocation/completion records authenticate commands, environment, source, output and log hashes; failed attempts are preserved. `before/` is an earlier runtime preflight only, never the final baseline.

Freeze manifest SHA-256:

- Before-v2: `13ded5b15ec38276b09921da8adbddc87be16f02b1f105a57bd360be05a66887`
- After: `8847554aa92d0dcb1a6ce428c01c6739b8cfb007cf4d82c2a4b38563a7eedd94`

The frozen optimized executable hashes are:

| Executable | SHA-256 |
| --- | --- |
| `lumora_processing_benchmark` | `0e6c91a118fbce1a787afed777eced8522d51f8e1fd81207075923bd874fcfbb` |
| `lumora_processing_allocation_probe` | `a7a9514e4e27bb3e4bcaec35e8f27ca354748b07105d2bc96cf844d6a9aa2ace` |
| `lumora_processing_reference_generator` | `7f46697479d37b9d985a4f9485a3623c0aaadbb9e1a398dd6995adb69a076770` |
| `normalize-timings` | `0a309476520a8bee7409d5de4be50aa1dd615789e12eaa854d4919d46e5204f8` |

Each `normal/capture.json` records the kernel, benchmark, portable allocation and glibc allocation steps. `paired/` records all six alternating invocations and their raw samples, matching build/toolchain/loader facts, normal-capture prerequisites and the successful independent verification log. The verifier authenticates frozen files/providers, schemas, inputs, full outputs, resources, raw trace controls and timing arithmetic. `validation/` retains local checks, `paired-controller-audit.json` and `reference-comparison.json`; `task-2/` retains characterization, deliberate mutation, restored-green and generated-code evidence. Reports/rulings are in `.superpowers/sdd/2026-09-08-m08-mono12-normalization/`.

To recheck retained normal evidence without rerunning timings:

```sh
python3 out/qa/m08-mono12-normalization/verify-evidence.py \
  --before out/qa/m08-mono12-normalization/before-v2 \
  --after out/qa/m08-mono12-normalization/after
```

`freeze.py`, `run-normal.py` and `paired-kernel.py` record the capture commands and refuse overwriting prior labels. Any future measurement should use a fresh label and matching clean build; preserve these bundles.

## Next development and open gates

The subsequent bounded [display-output continuation](m08-display-output-performance.md) profiles and hoists invariant layout getters from display mapping and orientation while preserving this Mono12 result as historical evidence. The diagnosis here identified roughly 15 ms for two display mappings, roughly 39 ms for two rotated displays and roughly 33 ms for CLAHE at 2048. Those are historical stage diagnostics, not measurements from the continuation; its fresh baseline, protocol and results are recorded separately. Further work should retain complete pixel equality and measure the full pipeline before deciding whether a larger backend change is justified.

## Integration status

Clean `1148036` and `717acc8` remain the matched before/after measurement sources. [PR #14](https://github.com/m4bulmagd/Lumora/pull/14) merged the combined performance series as `5de569f1341f9b2d63c4a4656e4a90fbec5bf9ac`. At verified PR head `3393a9d`, [Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34286991145) passed 54/54 checks in Debug (60.53 s) and Release (22.50 s), plus native X11 1/1 in Debug (0.35 s) and Release (0.03 s). [Windows/MSVC PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34286990978) passed 54/54 in Debug (87.37 s) and 54/54 in Release (39.56 s). The final head adds only the GCC 13 test const-reference correction `3393a9d`; production source is unchanged from measured clean `717acc8`. The PR head and merge commit are integration revisions, not measurement revisions.

Hosted Windows CI verifies MSVC build/test compatibility. Designated Windows reference/workstation/freshness and performance acceptance, native Windows 11 visual/DPI and packaging checks, the owner's deferred M4/M5 acceptance, the M6 camera/NIC profile, and the 33.3 ms/30 FPS gate remain open. No acceptance gate is closed by the hosted workflows. M9 processing controls and synchronized views remain later development after the agreed performance work. Subsequent merged-main verification is tracked by the [Linux main workflow](https://github.com/m4bulmagd/Lumora/actions/workflows/linux-simulator.yml?query=branch%3Amain) and [Windows main workflow](https://github.com/m4bulmagd/Lumora/actions/workflows/windows-simulator.yml?query=branch%3Amain); no main-workflow result is claimed here.
