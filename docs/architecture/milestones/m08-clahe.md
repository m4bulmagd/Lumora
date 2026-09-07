# M8 Task 2: U16 CLAHE development

**Date:** 2026-09-08

**Status:** Bounded implementation authorized; preparation contract recorded before source changes. Functional implementation, review and verification are pending. M8 acceptance remains open.

## Authority and scope

After M7 PR #9 integration, the owner approved “Ok go ahead with M8” in response to the proposed Task 1 PR/CI/merge followed by Task 2 U16 CLAHE. This record extends the earlier Task-1-only continuation. Tasks 3–5 and M9 UI remain subsequent work; native Windows 11 M4/M5 checks remain deferred to the owner.

## Prepared stage contract

Follow [Task 2 of the M8 plan](../../superpowers/plans/2026-04-25-m08-modular-enhancements.md#task-2-u16-clahe-stage). A typed factory creates an immutable configured, extent-bound stage with private OpenCV state and two aligned U16 bridge buffers. Creation and replacement happen while stopped. The existing const stage operation remains unchanged, but the instance is explicitly single-worker because the backend mutates its caches. Calls preserve CanonicalU16 precision, source bytes and padding, and reject invalid/overlapping views before writing. Width/height are bound at creation; row stride may vary between calls.

Direct wrapping is allowed only for U16-aligned starts and even byte strides. Lumora's valid odd-stride/unaligned views use prepared bridge storage, with active-row memcpy. Factory validation preserves clip [0.1,40] and grid [2,32], defines too-small as width or height below the grid, and rejects unsafe OpenCV integer arithmetic before allocation. Backend exceptions become stable stage errors.

The private retained storage includes two width×height U16 bridge images, a grid×grid×65,536-entry U16 LUT (8 MiB at grid 8; 128 MiB at grid 32), and a reflected U16 source extent when either axis is indivisible. OpenCV adds `grid - dimension % grid` to both axes in that case. These are preparation-budget inputs for Task 5; they are not a total heap measurement or a complete executor memory budget.

## Known allocation gap

Pinned OpenCV 4.12.0 CLAHE allocates a 65,536-int histogram inside the tile loop, heap-owned loop bodies per apply, and interpolation scratch for sufficiently wide images. Reusing the CLAHE object retains LUT/border matrices but does not eliminate those allocations. Task 2 records this known gap without changing the dependency or implementing a new CLAHE algorithm. The full M8 zero-allocation gate remains unmet and requires a reviewed backend/workspace solution in Task 5. No pixel-pool counter or repeatability test will be presented as proof of zero heap allocations.

## Reference provenance and open gates

Portable, manually calculable exact cases establish low-bit precision and histogram/interpolation behavior. A fixed Linux gradient/impulse regression fixture may provide additional characterization with explicit pinned-version provenance; it is not the required designated Windows reference. Tests must not regenerate expected values automatically or silently invent cross-platform tolerances. Designated Windows release output and independently reviewed maximum-error/image-difference thresholds remain open, along with matching platform verification and the designated-workstation performance gate.

Production activation continues to reject enabled CLAHE until Task 5 supplies execution. No operator controls, hardware integration, patient input, dependency updates or acceptance claims are included.
