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

The full normal protocol, final branch review and Linux/Windows CI remain required before integration. Raw evidence and commands are retained under `out/qa/m08-window-identity/` in the preserved M8 worktree. The 33.3 ms/30 FPS target and designated Windows acceptance remain open.
