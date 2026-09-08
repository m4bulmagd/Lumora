# M8 Task 2: U16 CLAHE development

**Date:** 2026-09-08

**Status:** Standalone functional implementation `f5d6642` and test correction `88a9ad8` passed independent task review. Twelve CLAHE cases pass in Linux Debug/Release; full Linux application verification passed. Final branch review and Linux/Windows Debug/Release CI passed; [PR #11](https://github.com/m4bulmagd/Lumora/pull/11) merged as `c0c4102`. M8 acceptance remains open.

## Authority and scope

After M7 PR #9 integration, the owner approved “Ok go ahead with M8” in response to the proposed Task 1 PR/CI/merge followed by Task 2 U16 CLAHE. This record extends the earlier Task-1-only continuation. Tasks 3–5 and M9 UI remain subsequent work; native Windows 11 M4/M5 checks remain deferred to the owner.

## Prepared stage contract

Follow [Task 2 of the M8 plan](../../superpowers/plans/2026-04-25-m08-modular-enhancements.md#task-2-u16-clahe-stage). A typed factory creates an immutable configured, extent-bound stage with private OpenCV state and two aligned U16 bridge buffers. Creation and replacement happen while stopped. The existing const stage operation remains unchanged, but the instance is explicitly single-worker because the backend mutates its caches. Calls preserve CanonicalU16 precision, source bytes and padding, and reject invalid/overlapping views before writing. Width/height are bound at creation; row stride may vary between calls.

Direct wrapping is allowed only for U16-aligned starts and even byte strides. Lumora's valid odd-stride/unaligned views use prepared bridge storage, with active-row memcpy. Factory validation preserves clip [0.1,40] and grid [2,32], defines too-small as width or height below the grid, and rejects unsafe OpenCV integer arithmetic before allocation. Backend exceptions become stable stage errors.

The private retained storage includes two width×height U16 bridge images, a grid×grid×65,536-entry U16 LUT (8 MiB at grid 8; 128 MiB at grid 32), and a reflected U16 source extent when either axis is indivisible. OpenCV adds `grid - dimension % grid` to both axes in that case. These are preparation-budget inputs for Task 5; they are not a total heap measurement or a complete executor memory budget.

## Known allocation gap

Pinned OpenCV 4.12.0 CLAHE allocates a 65,536-int histogram inside the tile loop, heap-owned loop bodies per apply, and interpolation scratch for sufficiently wide images. Reusing the CLAHE object retains LUT/border matrices but does not eliminate those allocations. Task 2 records this known gap without changing the dependency or implementing a new CLAHE algorithm. The full M8 zero-allocation gate remains unmet and requires a reviewed backend/workspace solution in Task 5. No pixel-pool counter or repeatability test will be presented as proof of zero heap allocations.

## Reference provenance and open gates

Portable, manually calculable exact cases establish low-bit precision and histogram/interpolation behavior. A fixed Linux gradient/impulse regression fixture may provide additional characterization with explicit pinned-version provenance; it is not the required designated Windows reference. Tests must not regenerate expected values automatically or silently invent cross-platform tolerances. The provisional Linux comparison is a separately named test with an explicit skip on Windows and a provenance explanation; portable analytical, gradient, stride and repeatability tests continue to run on Windows. Designated Windows release output and independently reviewed maximum-error/image-difference thresholds remain open, along with matching platform verification and the designated-workstation performance gate.

Production activation continues to reject enabled CLAHE until Task 5 supplies execution. No operator controls, hardware integration, patient input, dependency updates or acceptance claims are included.


## Functional implementation and test evidence

Commit `f5d6642` adds the factory-created `ClaheStage`, target-scoped private OpenCV core/imgproc linkage, registry scratch metadata and eleven tests. The two manually calculated 4x4 references cover low U16 bits and differing tile-LUT interpolation. The additional 9x8, grid-2, clip-2 fixture was generated with pinned OpenCV 4.12.0 port-version 9 on Linux/GCC 15.2.0 and records its exact input/output and provenance in `tests/reference/processing/clahe-reference.json`. It is provisional characterization, not the designated Windows baseline. The planned designated Windows gradient/impulse reference at clip 2 and grid 8 remains to be recorded and reviewed.

TDD evidence is retained under `out/qa/m08-task2/`: the first assertion RED compared zero placeholder output against the exact low-bit reference; the expanded suite then failed ten cases against the placeholder (the already-declared metadata case passed). After implementation, all eleven cases passed. Clean Debug and Release processing-test builds passed; each configuration passed the focused CLAHE CTest and all nine Processing CTest entries. No passing test regenerates its reference.

Preflight failures preserve the complete destination bytes. Directly wrapped output may be partly written if OpenCV itself throws after processing begins; this is a frame failure, and the later executor must discard that frame. Bridged output is copied back only after backend success. Backend failures are translated to stable `clahe_*` errors; no unsafe failure injection or public test-only backend seam was added.

The public header documents stopped-state preparation/replacement, fixed width/height with variable valid row strides, and single-worker non-concurrent use. The backend still allocates internally per apply; neither a zero-allocation measurement nor designated Windows reference acceptance is claimed.


## Full local verification

At implementation `f5d6642`, root built all application/test targets and ran all headless CTests separately in Debug and Release: Debug 42/42 in 23.84 s; Release 42/42 in 18.43 s. Native X11 smoke also passed in both (Debug 1/1 in 0.18 s; Release 1/1 in 0.09 s), giving 43 checks per configuration. The native tests ran outside the filesystem sandbox to permit the X11 display connection.

A fresh `out/build/m08-clahe-production` Release configuration with tests, benchmarks and Basler disabled built `lumora_app` successfully through all 112 build steps using the existing pinned dependency prefix. Final processing and complete application build logs contain no compiler warning/error lines. Commands and logs remain in `out/qa/m08-task2/` (`full-build-*`, `full-test-*`, `desktop-*`, `production-configure.log`, `production-build.log`). These checks validate the compiled standalone stage and its application build integration; production activation remains Task 5.

## Independent task review

The initial review identified a Windows warnings-as-errors risk in a Linux-only fixture helper and a missing test that isolated overlap in padding. Test-only correction `88a9ad8` guards the helper consistently and adds a 2x2 case with disjoint active rows but overlapping complete payloads, checking that rejection preserves every backing byte. It also narrows the A–B–A test comment to the behavior its assertions establish.

Incremental Debug and Release processing-test builds passed after that correction, and all twelve `ClaheStage.*` cases passed (0.833 s and 0.178 s). The scoped review confirmed every finding addressed with no new issues. Production source was unchanged, so the full-application and fresh-production evidence above still applies. Reports and fix logs remain under `.superpowers/sdd/2026-04-25-m08-modular-enhancements/` and `out/qa/m08-task2/round1-fix-*`.

## Final branch review and CI

The independent final review of `d6f94e1..dd08b3f` approved the bounded standalone implementation with no Critical, Important or Minor findings, conditional on matching branch CI. It reviewed all fourteen changed files and the retained local evidence. Allocation, designated Windows references/tolerances, performance and live composition remain open.

[PR #11](https://github.com/m4bulmagd/Lumora/pull/11) runs the Linux/GCC and Windows/MSVC Debug/Release simulator workflows. The initial [Linux run](https://github.com/m4bulmagd/Lumora/actions/runs/34173425084) failed compilation because GCC 13 flags two copied structured bindings in test loops under warnings-as-errors; local GCC 15 builds had passed. The correction binds those test pairs by const reference without changing test assertions, production behavior or warning policy. Final CI results and merge evidence are recorded in the pull request; a passing hosted job does not replace designated-workstation reference or native Windows 11 acceptance.

## Merged checkpoint

The reviewed final PR head `b85a626` merged as `c0c4102` with an identical tree. Linux PR run [34173797580](https://github.com/m4bulmagd/Lumora/actions/runs/34173797580) passed Debug 42/42 plus X11 1/1 and Release 42/42 plus X11 1/1; Windows PR run [34173797573](https://github.com/m4bulmagd/Lumora/actions/runs/34173797573) passed 42/42 in each configuration. Both branch push jobs and post-merge main jobs also passed. The earlier GCC13 compiler issue was corrected in `b85a626` and passed scoped review. No code review findings remain open.

The owner subsequently authorized [Tasks 3–5 continuation](m08-continuation.md). Task5 must address the recorded backend allocation debt before claiming full steady-state allocation compliance. The designated reference/workstation and earlier native Windows acceptance gates remain open.
