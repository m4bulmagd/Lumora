# M8 Exact Mono12 Normalization Design

The user approved establishing a normal Mono12 baseline, specializing normalization, and measuring the complete pipeline again. The local implementation starts at `c7cee62314c95eeba3191e07e8fec35ec12e37f3` on `perf/m08-mono12-normalization` in the existing isolated `.worktrees/m08-tone-stages` worktree. Main and earlier branches/evidence remain preserved. Publication is outside this approval.

## Outcome and boundaries

Reduce Mono12 normalization cost without changing any pixel, failure behavior, resource accounting, or measurement boundary. The prior twenty-call diagnostic identified runtime division in normalization as a substantial cost but is not a controlled full throughput baseline. Establish the baseline with the new evidence-only tooling before changing production normalization.

Preserve exact U16 and display output, validation precedence, first-invalid-sample diagnostics, source immutability, destination padding, unaligned byte views, unequal valid strides, and partial writes before the first invalid sample. Preserve the Mono16 identity copy and the generic path for all other validated maxima/storage. Add no dependency, public application API, heap storage, worker, approximate arithmetic, fast-math option, or resource-plan change. Windows acceptance and the canonical Mono16 workstation contract remain unchanged.

The selected specialization is validated UInt16 storage with `sampleMaximum == 4095`, independently of descriptor name, packing provenance, or validBits alone. A private compile-time choice shares the existing pixel loop and makes 4095 a compiler-visible constant. Keep the exact existing uint64 numerator `(value * 65535 + maximum / 2) / maximum`, including the ordered range check before each write. Verify all 4096 valid values with an independent integer oracle, boundary/error/canary behavior, generated Release arithmetic, useful repeated kernel gain, and full-pipeline hashes.

Other candidates are a lookup table and a SIMD rewrite. Both add complexity unnecessary to remove a runtime denominator; prefer the constant specialization first. Display/orientation and further CLAHE optimization are later work, outside this implementation.

## Evidence contract

## Recommended bounded extension

Add `--source-format mono12|mono16` to the benchmark and allocation probe only. Omitted or explicit `mono16` preserves the current v2 artifact shape, input, eleven benchmark rows per size, all measurement boundaries, and existing allocation geometry. `mono12` emits v3 artifacts and runs only the two full Standard benchmark rows per size. Do not introduce a second workload selector for this task.

There is **no current `--workload` CLI option**. `Options.workload` in `EvidenceSupport.hpp` is the internal `standard`/`smoke`/`custom` size-and-count profile kind. `EvidencePrimitives.cpp:24–53` accepts benchmark custom counts only as the full `--sizes`/`--warm-up`/`--measured` triplet, and explicit custom counts remain `custom` even if equal to defaults. Leave that behavior unchanged; use defaults for normal baseline and after runs.

Mono12 full-only has a concrete semantic reason: `Standalone` in `EvidenceWorkload.cpp:45` treats only normalize input as sensor-native; all subsequent isolated stages consume canonical U16, with no normalization step executed. Feeding their current input buffers 0–4095 and merely marking them Mono12 would change their canonical intensity distribution and confuse stage interpretation. Correctly expanding standalone semantics would be separate work. The user-requested full pipeline already exercises normalization, both display mappings, orientation, pool turnover, and the prepared stages.

The new normal benchmark has six ordered rows: sizes 512, 1024, 2048, each identity then Hflip + clockwise90, 100 warm-ups and 500 measured cycles per row. Smoke has four rows at 64 and 128, 2/5 counts. Existing complete custom triplets can select custom sizes/counts with the same two-row product.

Matching allocation means the same Mono12 derivation and real descriptor in both existing normal allocation profiles: **64×48, identity then Hflip + clockwise90, 100 warm-ups and 1,000 measured cycles per row**. It does not mean silently replacing canonical allocation geometry with 512/1024/2048. Benchmark rows already collect and require zero C++ allocations/releases at those larger sizes. Smoke allocation remains 64×48 and 2/20, explicitly nonproof.

## Minimal interfaces and files

1. `benchmarks/processing/EvidenceSupport.hpp`: add `enum class SourceFormat { Mono16, Mono12 };` and `Options::sourceFormat = SourceFormat::Mono16`. Keep `workload` meaning unchanged. Use a single shared measurement-input helper rather than duplicate derivation loops in both executables.
2. `EvidencePrimitives.cpp`: parse the new closed, case-sensitive enum for `Tool::Benchmark` and `Tool::Allocation`; reject it for `Tool::Generator`. Retain duplicate-key, missing-value, unknown-option, positional, help-alone, count and admission behavior. Keep `makePattern` semantics unchanged for all existing patterns and reference fixtures.
3. `EvidenceWorkload.hpp/.cpp`: exact proposed constructor signature is `Session(const Image&, core::Orientation, SourceFormat sourceFormat = SourceFormat::Mono16);`. Pass `monoFormat(sourceFormat == SourceFormat::Mono12)` to `AcquisitionSettingsSnapshot::create` at the current line 72, so the actual RawFrame descriptor changes along with JSON. `Session::assess(width,height,orientation)` stays unchanged: its U16 layout/pool accounting is format-independent. Do not alter `Session::cycle`, `releaseMeasured`, constructor sentinel creation, helper controls, or resource accounting.
4. Shared evidence helper: `makeMeasurementInput(width,height,sourceFormat)` calls the existing seeded xorshift generator and shifts each pixel right four only for Mono12. A shared input-metadata serializer should conditionally add the v3 derivation and hash the actual derived image. These are evidence-private interfaces, not application APIs.
5. `EvidenceJson.hpp/.cpp`: leave `rowIds()` unchanged for legacy/current Mono16 and references. Add a format-aware helper/overload used by benchmark emission and v3 validation; Mono12 returns just `full_standard_identity`, `full_standard_nonidentity`. Do not replace the global list and accidentally invalidate legacy tests.
6. `EvidenceMeasurement.cpp`: use the shared generated input, hash after derivation, pass the selected format to `Session`, and serialize the same selected descriptor. Keep the measured loop unchanged. Reject an unsupported Mono12 standalone ID if this callable helper is invoked directly, rather than relying only on the main loop to prevent misleading output.
7. `ProcessingBenchmark.cpp`: update help, choose v2 versus v3 from the selected format, add the v3 root discriminator described below, and iterate the format-specific row list. Preserve preallocation admission checks and progress/failure behavior.
8. `ProcessingAllocationProbe.cpp`: update help, root version/discriminator, source construction, Session format, and descriptor/input metadata only. Preserve 64×48 geometry, both orientations, tracker/helper/heap controls, and trace boundaries.
9. `EvidenceValidation.cpp`, `schemas/build-schemas.py`, generated new v3 schemas, `tests/unit/processing/EvidenceArtifactTests.cpp`, `tests/cmake/ProcessingEvidenceSmoke.cmake`, and the tool README cover the new contract, meaningful mutations, and discoverability. No new executable/dependency/public library surface is needed.

## Pinned v3 shape and compatibility

Use only two new metadata fields relative to existing v2 structures:

- Root `sourceFormat: "mono12"`, required for v3 benchmark/allocation, so even an empty incomplete progress artifact is identifiable.
- Each row's existing `input` object gains required `derivation: "uint16(state >> 16) >> 4"`. Existing `patternId: "xorshift32_u16_v1"`, `version: 1`, fixed seed, and `sha256` retain their meanings; the SHA hashes the actual derived input PGM.

No `rowSet` or second selector is necessary: v3 plus `sourceFormat:"mono12"` defines the full-Standard-only product. Keep `workload.kind` and the standard/smoke/custom profile fields unchanged. Allocation still uses its existing `smoke` root flag. Keep pipeline, orientation, resources, timing, checksum, execution, control, failure, and provenance shapes the same as v2.

Schema2 is not sufficient as currently contracted. The generic JSON Schema `source` definition structurally permits Mono12, but typed validation at `EvidenceValidation.cpp:56–75,104–108` fixes input pattern shape, exact Mono16 descriptors, eleven rows per requested size, and the canonical allocation descriptor. `input` disallows unknown members and has no derivation field. Broadening v2 silently changes its semantics; relaxing its row product also misrepresents historical artifact completeness.

Create `processing-benchmark-v3.schema.json` and `processing-allocation-v3.schema.json` from v2 structural definitions plus the above fields/full-row restriction. Append v3 definitions rather than mutating v1/v2 definitions in the schema generator. If new common definitions are needed, add separate names; do not change the old `input`, resource, or artifact shapes. Frozen previous schema bundles/manifests remain untouched.

Update dispatch to accept versions 1/2/3 only for benchmark/allocation; reference/workstation remain v1. Current boolean `version==2` resource dispatch must become an explicit version distinction or `version>=2` after the allowed-version check, so v3 still enforces executor accounting/helper controls. Strict v3 validation must require the exact Mono12 descriptor, derivation, full Standard definition, expected orientation order, two rows per size, matching profile counts, and required/unknown-key rejection. Legacy v1/v2 must reject the new fields and Mono12 descriptors.

**Workstation guard:** `validateWorkstation` currently validates attachments generically, requires a complete `kind:"standard"` benchmark, then inspects 2048 identity timing and Windows provenance; it does not independently restrict source format (`EvidenceValidation.cpp:248–279`). Once generic validation admits v3, that accidentally admits a different workload into the existing review contract. After parsing/validating the attached benchmark and allocation, explicitly require each to have schema version 1 or 2 (canonical Mono16 evidence), before the existing gate/normal checks. Keep an error that explains this is the canonical Mono16 workstation review contract. Do not broaden acceptance authority during this experiment.

## Reproducible input and output contract

`makePattern` uses uint32 state initialized to `0x6D2B79F5` (1831565813), with row-major state updates:

```text
state ^= state << 13   // uint32 wrap
state ^= state >> 17
state ^= state << 5    // uint32 wrap
u16 = uint16(state >> 16)
mono12 = u16 >> 4
```

Its `Pattern.maximum` parameter is **ignored by the xorshift branch** (`EvidencePrimitives.cpp:55–67`), so `{...,4095}` does not generate valid Mono12. Do not clamp, modulo, take the low 12 bits, or change the original generator. Apply the explicit post-derivation only for the new measurement input. New independent tests should pin literal initial values/known hashes instead of comparing two calls to the same helper.

PGM encoding remains exact `P5\nW H\n65535\n` plus big-endian U16 samples, even though source values are 0–4095. The `input.sha256` covers the entire PGM including header. It is not a hash of native-endian RawFrame storage or a max4095 PGM. Preserve this existing encoding (`EvidencePrimitives.cpp:69–85`).

The previous supplementary diagnostic independently reconstructs the same 2048 input and records SHA256 `810ab1ab8416914a9ea65b5e179d7ddcbde7ed620fb095d0456e313d3f573bde`. Its expected composite outputs are identity `51c5ebb22c9d06ddea891f5217932f6f30e2c272bb7ec5f8aea5593e9d2dfa78`, nonidentity `723604514c4851b5490dd45fff692177c856f5e37ad3d8bf4ef6e9e04688403d`. These are read from `out/qa/m08-cpu-next/verify-mono12-diagnostic.py` and the committed performance document, not recomputed during this preflight. They provide useful independent output anchors, subject to confirming the same pipeline/execution/input in the new run.

Keep the full output SHA over concatenated complete PGM EnhancedU16, OriginalGray8, EnhancedGray8 in that order. Gray8 remains zero-extended to U16 for PGM encoding. Keep the extra verification cycle outside timing and the 16 sampled bytes per each of the three payloads, FNV1a uint64 across measured cycles, inside timing. Compare both full hashes and equal-count fingerprints before/after. Normal allocation currently computes but does not serialize its fingerprint/checksum; do not claim it authenticates payloads on its own.

Existing typed `validateInput` verifies syntax/constants, not deterministic hash correctness. Require a separate independent verifier to regenerate all recorded 512/1024/2048 and 64×48 inputs and check their hashes; source-format/derivation consistency belongs in the typed validator. If adding production hash regeneration for v3, keep it outside all measured regions and avoid repeated full-image allocation/hashing on every cumulative progress validation.

## Meaningful compatibility and strictness checks

- Parser: default and explicit Mono16 equivalent; Mono12 accepted on benchmark/allocation with normal, smoke and full custom triplet where applicable; generator rejects source format; reject invalid/case-varied format, empty/missing value, duplicate flag, help combinations, partial custom triplet, smoke+custom. Check code 2, not only any exception.
- Input: independent literal xorshift/upper-bit values, range <=4095, unchanged Mono16 input, exact big-endian PGM header/hash semantics, known independently derived hash. Verify the Session's resulting acquisition descriptor is real Mono12 and known input is normalized (catches JSON-only relabeling).
- Emitted smoke: retain current `benchmark.json` (v2,22 rows) and `allocation.json` (v2,2 rows), add separate `benchmark-mono12.json` (v3,4 rows) and `allocation-mono12.json` (v3,2 rows). Extend `ProcessingEvidenceSmoke.cmake` and generated-artifact tests rather than replacing the existing fixtures or expected row count.
- v3 mutations: missing/unknown root/row/input keys; wrong/absent sourceFormat or derivation; wrong seed/pattern/version; Mono16 descriptor, wrong validBits/maximum/encoding/storage/layout; standalone row insertion, missing/duplicate/swapped orientation, wrong size ordering/count, smoke/profile mismatch; changed Standard pipeline; timing/FPS arithmetic; nonzero errors/drops/allocations/releases; executor/helper accounting/trace polarity. Retain partial progress and incomplete failure-row-count tests.
- Legacy v1 tracked fixtures and synthetic v2 fixtures must continue passing, and reject v3 members/descriptors. Existing `GeneratedBenchmarkAndAllocationRejectSemanticMutations` asserts v2,22 rows; keep that test intact for default outputs. Add separate Mono12 assertions.
- Workstation regression: existing valid synthetic canonical review bundle still passes; replacing either attachment with a valid v3 Mono12 artifact must fail even after its attachment SHA is updated and its timings/Windows provenance otherwise satisfy the gate.
- Independently validate the generated schemas against their exact versions. Existing local `validate-schemas.py` recognizes only v1/v2 and authenticates the old frozen bundle. Do not overwrite it/the old bundle to make new data pass; a new task-local verifier and freeze manifest should dispatch v3 against the new authenticated schema files.

## Verification and delivery

Freeze clean evidence-only Release tools, schemas and build/provider/source provenance, then run the normal six-row Mono12 benchmark and normal portable/glibc allocation profiles. After the exact optimization, freeze the same tooling at the new clean revision and repeat with the same configuration and no concurrent build/test/measurement load. Independently regenerate input hashes and compare complete output SHA256 and equal-count fingerprints, all resources/counters, raw heap traces and controls. Record median, nearest-rank P95 and wall-time FPS for each row; repeat matched measurements only if variance prevents interpretation. Keep all existing QA immutable in its original directory and put new artifacts in `out/qa/m08-mono12-normalization/`.

Run full local Debug/Release suites, native X11 smoke, targeted ASan/UBSan/leak checks and the existing tests-OFF/tools-ON graph. Preserve default v2 smoke and exact reference candidates. The small normal glibc probe is scoped to 64×48; larger normal benchmark rows cover C++ counters, and large-image glibc/helper coverage may be claimed only with a separately labeled matching probe. Allocation artifacts do not independently prove payload hashes.

Record source/build/binary revisions, measured results and limits in README, PROGRESS and `docs/architecture/milestones/m08-mono12-normalization-performance.md`. Complete independent task and whole-branch reviews. Commit locally; do not push, open a PR, run hosted CI or merge without publication authorization for this branch.
