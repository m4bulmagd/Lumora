# Processing evidence tools

These tools characterize the shared explicit Standard pipeline. They do not change
application startup defaults, select a workstation, approve references, or satisfy
M8 acceptance merely by producing valid JSON. Linux and hosted CI results are
characterization; native Windows candidate output remains pending human review.
The gates remain Standard2048 P95 <=33.3ms, sustained >=30FPS, and the separate
60FPS freshness evidence on the designated workstation.

## Build and run

Use the project's already configured dependency toolchain. No new dependency is
required. Both test-enabled builds and the tests-OFF/benchmarks-ON graph are
supported; GTest is never needed by the evidence support or executables.

```sh
cmake --preset linux-gcc-release-sim -DLUMORA_BUILD_BENCHMARKS=ON
cmake --build --preset linux-gcc-release-sim --target lumora_processing_benchmark lumora_processing_reference_generator lumora_processing_allocation_probe --parallel 3
mkdir -p out/qa/processing
out/build/linux-gcc-release-sim/benchmarks/processing/lumora_processing_benchmark --output out/qa/processing/benchmark.json
out/build/linux-gcc-release-sim/benchmarks/processing/lumora_processing_allocation_probe --output out/qa/processing/allocation.json
out/build/linux-gcc-release-sim/benchmarks/processing/lumora_processing_reference_generator --output out/qa/processing/candidate
```

For a separate production graph without tests:

```sh
cmake -S . -B out/build/processing-evidence-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLUMORA_BUILD_TESTS=OFF -DLUMORA_BUILD_BENCHMARKS=ON -DLUMORA_ENABLE_BASLER=OFF -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic -DVCPKG_HOST_TRIPLET=x64-linux-dynamic
cmake --build out/build/processing-evidence-release --target lumora_processing_benchmark lumora_processing_reference_generator lumora_processing_allocation_probe --parallel 3
out/build/processing-evidence-release/benchmarks/processing/lumora_processing_allocation_probe --output out/qa/processing/allocation-production.json
```

The same graph works with the Windows presets/toolchain. For example:

```powershell
cmake --preset windows-msvc-release-sim -DLUMORA_BUILD_TESTS=OFF -DLUMORA_BUILD_BENCHMARKS=ON
cmake --build --preset windows-msvc-release-sim --target lumora_processing_benchmark lumora_processing_reference_generator lumora_processing_allocation_probe --parallel 3
out/build/windows-msvc-release-sim/benchmarks/processing/Release/lumora_processing_benchmark.exe --output out/qa/processing/benchmark.json
out/build/windows-msvc-release-sim/benchmarks/processing/Release/lumora_processing_allocation_probe.exe --output out/qa/processing/allocation.json
out/build/windows-msvc-release-sim/benchmarks/processing/Release/lumora_processing_reference_generator.exe --output out/qa/processing/candidate-windows
```

Create the parent artifact directory first. Vcpkg app-local deployment supplies
Windows runtime DLLs. Keep native candidate artifacts with their provenance for
review; these commands cannot emit an accepted workstation or reference manifest.

Benchmark defaults are sizes512,1024,2048,100 warm-ups and500 measured frames per
row. A custom workload requires the complete triplet
`--sizes 512,1024 --warm-up 100 --measured 500`; it is marked custom.
`--smoke` instead fixes sizes64,128 and2/5 warm/measured frames. The allocation
probe uses a nonsquare64x48 source and separate identity/Hflip+CW90 rows: normal
is100 warm-ups/1000 successful cycles **per row**, smoke is2/20 per row and is
explicitly nonproof. Candidate generation has13 ordinary or13 smoke cases.
All tools reject duplicate/unknown options, missing values, permissive numeric
prefixes, and positional arguments. `--help` must stand alone.

Exit codes:0 success/help,2 CLI/admission argument error,3 output/serialization,
4 preparation/processing,5 allocation/resource/requested heap evidence failure.
A failed run preserves completed rows with `complete:false`, `runStatus:incomplete`
and failure details. Atomic replacements occur after completed rows outside all
measured regions. Candidate output must be new or empty; the generator rejects
`tests/reference/processing` and every descendant, including symlink aliases.

## Measurement and provenance

The benchmark has nine standalone stage rows and two complete Standard engine
rows. Standalone rows serialize a canonical diagnostic definition but time only
the named stage. Invert is enabled only for its isolated row; Standard disables it.
The fixed nonidentity orientation is horizontal flip followed by clockwise90.

Full rows start with a preexisting immutable raw frame and include paired Original
and Enhanced mapping, orientation, pooled frame objects, latest-slot replacement/
consume, five-owner retention, eviction, and release. Preparation, acquisition
metadata, JSON, hashing and working-set queries are excluded. Per-cycle samples
include normal eviction; wall elapsed/FPS additionally includes clearing the ring
and replacing the last measured output with a preexisting sentinel before counters
are disarmed. Median is the midpoint of the central integer-nanosecond samples
(.5 is permitted); P95 uses nearest rank. FPS uses measured cycles/wall elapsed.

Each full measured cycle computes a bounded nonallocating fingerprint over16
sampled bytes from each of EnhancedU16, OriginalGray8 and EnhancedGray8. That small
cost is included in timing. A separately labelled extra verification cycle after
measurement supplies the full SHA256 over the concatenation of three complete P5
files in that order. It is not counted as a measured frame. Standalone buffers stay
alive for post-region P5 hashing. No measured frame is retained past disarming to
obtain a hash.

Both measurement executables link the eight ordinary/array/aligned/nothrow C++
replacement-new routes and require successful non-elidable positive controls
(8 calls/150 bytes/8 releases). Every measured successful region requires zero
C++ allocations, bytes and releases. These counters do not cover C malloc/free or
private DLL heaps. The generator, GTest, application, Core and Processing do not
link replacement operators. Existing latched Original-only allocation regressions
remain separate from these full Standard proofs.

Session resource numbers come from actual admission/getter accounting, including
the raw pool exactly once as external session storage. They are neither RSS nor
observed heap bytes. Standalone budget scope is scratch-only; scratch/fixed stage
owner totals and separate harness image bytes are named explicitly, while engine
fixed/envelope/cache fields are null. See each resource object's exclusions.
Working-set values are Linux resident pages or Windows working-set bytes, never
virtual size; unavailable values are null.

Source revision, dirty tri-state and status hash are refreshed at evidence-target
build time. The generated header is rewritten only when source-status/configuration
content changes, avoiding timestamp rebuild loops. Compiler options are declared
CMake configuration/target/source options, including Processing options, **not**
a claimed exact compiler invocation. Actual Qt/OpenCV runtime versions and the
queried installed OpenCV vcpkg port version are reported; unknown facts are null.
Normal benchmarks reject allocator preload instrumentation.

## Optional Linux glibc allocator-event evidence

Only the allocation probe supports `--heap-trace DIRECTORY`. This requires glibc
markers and a process-local preload of the host's `libc_malloc_debug`; it does not
link that debug library into any target, install software, interpose homemade
allocators, or support Windows heap evidence. Find the appropriate installed
library path on the machine. This host's verified example is:

```sh
env LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libc_malloc_debug.so.0 LD_BIND_NOW=1 out/build/linux-gcc-release-sim/benchmarks/processing/lumora_processing_allocation_probe --output out/qa/processing/allocation-glibc.json --heap-trace out/qa/processing/glibc-traces
```

Use a new/empty trace directory. Adding `--smoke` is a reduced nonproof capability
check. Each process verifies actual mtrace/muntrace/malloc providers, non-elidable
C/C++ and linked shared-library controls, and an empty trace before measurement.
Each orientation gets its own completed start/end trace and hash. Missing preload,
provider mismatch, failed controls, unknown/truncated records or any measured event
produces a nonzero requested-evidence failure. Unavailability is never a zero pass.
Trace durations never enter the performance benchmark.

Trace fields distinguish successful nonnull allocation results, null failures,
releases, old/new realloc transitions and realloc failures. Reported byte totals
are gross trace-reported successful result sizes; they are not original API request
sizes, net live bytes, leaks or RSS. Counters are never added to replacement-new
counts. No claim covers every failed API attempt, private/static/custom allocators,
direct mmap/driver/GPU allocation, other loader namespaces or unexercised private
paths. One linked shared-library control does not establish every dependency's
binding. Zero prepared scope means **no events at all**, including releases.

## References and validation

`tests/reference/processing/fixtures/{ordinary,smoke}` contain seven independently
reviewed exact source/expected pairs each. Complete-file hashes and the independent
rational/coordinate derivation provenance are committed. Passing tests and the
candidate CLI never regenerate these expectations. Gray8 samples are zero-extended
without scaling in the U16 PGM container; Gray16 values are nativeU16. PGM is exactly
P5/max65535 with big-endian sample bytes and no padding/trailer. Swapped sample bytes
form another valid PGM; expected hashes/pixels detect that mismatch.

CLAHE/detail/full-chain generated outputs remain provisional with null thresholds.
The old `clahe-reference.json` remains historical Linux characterization. The
pending workstation template is structurally valid and intentionally contains null
facts. Accepted records require designation, provenance, human review and attached
review-bundle files named `benchmark.json`, `allocation.json`, `manifest.json` and
`freshness.json` matching their hashes. Structural validation cannot authorize a
workstation; the existing independent freshness review and threshold approval are
still required. Unavailable peripheral facts remain explicit nulls.

Checked-in Draft2020-12 schemas enforce owned shapes and types. The focused typed
validator additionally rejects duplicate decoded member names, noncanonical
operation metadata, inconsistent ordered row products/statistics, and reference
payload hashes/dimensions. `schemas/build-schemas.py` maintains schema declarations;
it is not a runtime dependency or a general JSON-Schema interpreter.

CI enables these targets for Debug/Release and runs bounded `Processing.EvidenceSmoke`
with a600-second evidence-only timeout, uploads JSON/PGM artifacts even after a
failure, and preserves the normal1000-cycle Release proof as a separate local run.
No integration/freshness timeout or workload is changed by evidence smoke.
