# M8 Exact Mono12 Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish reproducible full Mono12 measurements and reduce normalization cost with exact constant division.

**Architecture:** Add an evidence-private source-format selector with strict v3 Mono12 artifacts, preserving v1/v2 and canonical workstation acceptance. Freeze that evidence-only baseline, specialize the existing normalization loop, then measure the same pipeline again.

**Tech Stack:** C++20, pinned Qt/OpenCV, GCC/MSVC, CMake/Ninja, GoogleTest, Python schema tooling.

**Spec:** [2026-09-08-m08-mono12-normalization-design.md](../specs/2026-09-08-m08-mono12-normalization-design.md)

## Global Constraints

- Preserve exact U16 and display output, validation precedence, first-invalid-sample diagnostics, source immutability, destination padding, unaligned byte views, unequal valid strides, and partial writes before the first invalid sample.
- Preserve the Mono16 identity copy and the generic path for all other validated maxima/storage.
- Add no dependency, public application API, heap storage, worker, approximate arithmetic, fast-math option, or resource-plan change.
- Windows acceptance and the canonical Mono16 workstation contract remain unchanged.
- Default and explicit Mono16 retain v2 shape and eleven rows per size; Mono12 uses v3, required root `sourceFormat: "mono12"`, required input `derivation: "uint16(state >> 16) >> 4"`, and full Standard rows only.
- Keep Session measurement/retention/release and helper/tracker/trace boundaries unchanged; change its real acquisition descriptor along with metadata.
- Preserve all earlier QA; store new evidence in `out/qa/m08-mono12-normalization/`. No concurrent heavy work during timing runs.

## Execution rules

Use the existing isolated worktree and local branch above. The controller owns Git and timed builds/runs; authors may run requested focused tests. Do not publish. Build caches are `out/build/linux-gcc-debug-sim`, `out/build/linux-gcc-release-sim`, `out/build/linux-gcc-debug-memory-sim`; use at most `--parallel 3`. Do not install dependencies or clear caches. Reviewers are read-only and do not duplicate passing checks. Reports and rulings remain in this plan's ignored SDD workspace. A genuine failing new CLI test precedes its implementation; exact normalization is characterized before code and checked with meaningful mutation sensitivity instead of inventing a functional failure. Performance thresholds are diagnostics, not CI assertions.

### Task 1: Strict Mono12 evidence and frozen slow baseline

**Files:** Modify `benchmarks/processing/EvidenceSupport.hpp`, `EvidencePrimitives.cpp`, `EvidenceWorkload.hpp/.cpp`, `EvidenceJson.hpp/.cpp`, `EvidenceMeasurement.cpp`, `ProcessingBenchmark.cpp`, `ProcessingAllocationProbe.cpp`, `EvidenceValidation.cpp`, `schemas/build-schemas.py`, `README.md` (under benchmarks/processing), `tests/unit/processing/EvidenceArtifactTests.cpp`, and `tests/cmake/ProcessingEvidenceSmoke.cmake`; create `schemas/processing-benchmark-v3.schema.json` and `schemas/processing-allocation-v3.schema.json` under benchmarks/processing. Leave production NormalizeStage unchanged.

**Interfaces:** Produce evidence-private `enum class SourceFormat { Mono16, Mono12 };`, `Options::sourceFormat = SourceFormat::Mono16`, `Image makeMeasurementInput(std::uint32_t, std::uint32_t, SourceFormat)`, and `Session(const Image&, core::Orientation, SourceFormat sourceFormat = SourceFormat::Mono16)`. Keep `Session::assess`, global legacy `rowIds()` and `makePattern` unchanged; add a format-aware row helper and shared input metadata serializer. Consume the exact contract in the spec's Evidence contract section.

- [x] Add a parser test before implementation and observe its real failure, e.g. `parseOptions(Tool::Benchmark, {"--source-format", "mono12", "--output", "sample.json"})` must succeed with Mono12 while the original parser rejects it. Use the existing vector/span test conventions, not an invalid temporary initializer-list span.
- [x] Implement the enum/parser/shared input/Session/metadata paths. Reject invalid or case-varied values, missing/duplicate flags, generator use, help combinations, partial custom triplets and smoke+custom with code2. Explicit/default Mono16 are equivalent. Normal Mono12 benchmark uses 512/1024/2048, identity then Hflip90, 100 warm-ups/500 measured; smoke64/128,2/5; complete custom triplets remain custom. Allocation stays64×48,100/1000 or smoke2/20. Direct Mono12 standalone measurement calls must reject. Xorshift uses uint32 wrap at each update, then `uint16(state >> 16) >> 4`; keep makePattern unchanged and PGM maximum65535.
- [x] Append separate v3 schema definitions, preserving all old generated schema bytes. Strict typed validation dispatch accepts only1/2/3 benchmark/allocation and uses the existing v2 executor/helper checks for v3. Enforce exact Mono12 descriptor/derivation/full-only ordered product/counts/pipeline/resources/timings/control semantics and unknown/missing fields, including empty/incomplete progress. Reference/workstation remainv1. Explicitly restrict attached workstation benchmark and allocation versions to1/2 after generic artifact validation; otherwise validv3 attachments with recomputed hashes/Windows provenance/fast timings must be rejected as noncanonical.
- [x] Pin independent initial input literals/known hash and real Session descriptor/normalized output. Keep default smoke `benchmark.json`v2,22rows and `allocation.json`v2,2rows; add separate `benchmark-mono12.json`v3,4rows and `allocation-mono12.json`v3,2rows. Exercise missing/unknown metadata, wrong source descriptor/input, standalone/duplicate/missing/swapped rows, profile counts, changed pipeline, timing/FPS arithmetic, nonzero errors/drops/allocations, executor/helper/trace polarity, and incomplete prefixes. Legacy fixtures and syntheticv2 must pass unchanged and rejectv3 fields/Mono12 descriptors. Update tool help/README.
- [x] Build focused Debug/Release evidence targets and run `ctest --test-dir out/build/linux-gcc-<configuration>-sim --output-on-failure -R '^Processing\.(EvidenceArtifacts|EvidenceSmoke)$'`. Independently validate generated v3 schemas and confirm generator leaves old files unchanged. Report exact commands/results, test-first failure, files and concerns; controller commits and obtains task spec/quality review.
- [x] Controller rebuilds at clean evidence-only commit, freezes tools/schemas/provenance, runs the actual six-row normal Mono12 baseline plus normal portable/glibc allocation profiles, and prepares a frozen narrow normalization timing harness before Task2. The older twenty-call diagnostic is an output anchor, never the controlled baseline.

### Task 2: Exact constant4095 normalization

**Files:** Modify `src/processing/src/NormalizeStage.cpp`; extend `tests/unit/processing/NormalizeStageTests.cpp`. No evidence/schema/resource changes.

**Interfaces:** Consume Task1's `--source-format mono12` tools and frozen baseline. Keep public stage API and all validation/copy paths. Specialize only validated UInt16 plus sampleMaximum4095; never select by name/validBits alone.

- [ ] Add characterization before source edits: all4096 values in odd/padded/unaligned rows with source/padding canaries; independently compute `16*v + (15*v + 2047)/4095` rather than calling the production helper. Include 136→2176,137→2193,2048→32776,4095→65535, repeated values at stride edges, first-invalid samples at multiple row/lane positions with exact prefix/suffix behavior, max4094 and4096 generic descriptors, and validBits16/max4095. Reuse existing malformed-layout/domain/overlap coverage.
- [ ] Run `Processing.NormalizeStage` against unchanged production code and record characterization passing alongside the frozen slow performance baseline. Demonstrate a naive-left-shift or omitted-range-check mutation is caught, then restore it. Do not create a flaky timing test.
- [ ] Extract one private templated pixel loop, for example `template<bool Mono12> core::Result<void> normalizePixels(const ImageView&, MutableImageView, const core::SourcePixelFormat&)`, using `const std::uint16_t maximum = Mono12 ? 4095U : sourceFormat.sampleMaximum;` and `const bool isU8 = !Mono12 && sourceFormat.applicationStorage == core::StorageType::UInt8;`. Retain ordered read/check/write and exact uint64 arithmetic. Dispatch after existing validations and Mono16copy. A comparably small shared-loop implementation is acceptable; do not duplicate full loops or add scratch/LUT/SIMD.
- [ ] Build/run Debug/Release NormalizeStage and engine/allocation registrations that cover this path; run targeted ASan/UBSan/leak checks. Inspect optimized assembly to confirm the fixed path avoids recurring runtime division while the generic path remains. Controller runs repeated paired frozen kernel measurements and checks no material generic regression; retain only useful exact improvement.
- [ ] Report commands/results/mutation proof and scope; controller commits and obtains task spec/quality review before final timed builds.

### Task 3: Final same-protocol evidence and development handoff

**Files:** Update root `README.md`, `docs/PROGRESS.md`, this plan; create `docs/architecture/milestones/m08-mono12-normalization-performance.md`. Keep evidence scripts/artifacts in the new ignored QA directory.

**Interfaces:** Compare only matched before/after v3 rows and frozen provenance from Tasks1/2. Preserve old v1/v2 artifacts and reference contracts.

- [ ] Run full Debug/Release suites, native X11 checks, targeted ASan/UBSan/leak checks, and existing tests-OFF/tools-ON build graph. Generate full reference candidates and compare all26PGMs exactly with the parent bundle; preserve defaultv2 smoke. Obtain whole-branch source review from the new branch base c7cee62, including task reports/rulings.
- [ ] Freeze clean after tools/source/build/provider/schema bindings. Run normal six-row Mono12 benchmark at100/500, normal portable/glibc allocation profiles at100/1000, with no concurrent heavy work and no preload during timings. Only claim larger-image glibc/helper participation if a separately labeled matching Mono12 probe ran; large normal benchmark rows already cover C++ counters.
- [ ] Independently regenerate actual input PGM SHA256 from uint32 xorshift upper12 for512/1024/2048 and64×48; validate schemas, raw trace events and positive/empty/helper controls, full output SHA256/equal-count fingerprints, zero errors/drops/C++ counts, unchanged resources and all frozen hashes. Report actual median/P95/FPS for all six profiles, gains/regressions/variance and evidence scope. Repeat only if needed to interpret noisy results.
- [ ] Write honest performance/readiness documents, complete plan/ledger, and obtain final whole-branch evidence/doc review with any findings fixed by an author and scoped rereview. Commit local documentation and report the result plus next remaining performance cost. Leave Windows gates and publication pending as already scoped.
