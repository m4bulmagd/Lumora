# M7 simulator development preflight

**Date:** 2026-09-07

**Status:** Development continuation; not milestone acceptance.

## Development sequence and authority

After reviewing the remaining gates, the project owner stated: “I will Complete Windows 11 validation for M4/M5 later, you can take over and continue the development.” The continuation uses the already designed, hardware-independent M7 processing work with simulator inputs. It does not invent a Basler hardware profile or start M6 camera implementation.

This records a scoped development exception: M7 simulator implementation may proceed while M4/M5 native Windows validation and acceptance, and M6 hardware integration and acceptance, remain pending. It reconciles the simulator-continuation provision in the M6 plan with the roadmap's normal numeric order. It does not authorize skipping subsequent milestone gates, external publication, or release acceptance. M7 acceptance remains pending until its own matching Linux/Windows evidence and the preceding deferred gates are closed.

The [M4 native Windows checklist](m04-deferred-windows-validation.md#deferred-manual-checks-and-closure) still applies to the then-current application, including affected M5 and subsequent controls. The M6 entry gate still requires an approved exact camera/sensor/firmware/NIC/driver/link, source-format and feasible continuous acquisition profile. The build remains `EVALUATION — NOT FOR CLINICAL USE`.

## Implementation scope

Execute the [existing M7 plan](../../superpowers/plans/2026-04-25-m07-high-bit-depth-window-level.md): validated fixed-order processing contracts, deterministic U16 normalization, window/level and terminal Gray8 mapping, then a pooled frame engine integrated with the simulator pipeline. Original numeric samples remain immutable. M8 enhancement algorithms, orientation editing, M9 processing UI and M6 vendor SDK work are outside this continuation.

Extend the existing `IFrameProcessor` boundary rather than introducing a second worker. Preserve the fixed accepted camera mode per session, explicit selection/confirmation/Start, latest-value exchanges, completed-paint freshness, Pause behavior, acknowledged source handoff and joined shutdown. Mode editing remains a later stopped-state resource-rebinding workflow.

## Resolved implementation contracts

- `SensorNative` accepts numeric U8 or U16 application samples; `CanonicalU16` is normalized. RawFrame packing/alignment fields preserve source provenance. M6's adapter has already copied/unpacked sensor values before publication, so normalization must not unpack or shift them again. Validate the complete descriptor/storage and reject numeric samples above its declared maximum.
- Reuse core orientation and pipeline-version types. Schema/order versions must be supported; configuration revision zero remains valid because the existing core contract does not reserve it.
- Default definitions contain all eight stages in the fixed order, with M8 enhancements disabled. Optional stages may be omitted, but a present stage cannot be reordered, duplicated, or carry invalid parameters even while disabled. Normalize is first and enabled. Return all validation violations in a stable documented order, using the existing Result template's custom error parameter; no core Result change is needed.
- A compiled definition owns its configuration and traits rather than borrowing a registry. The compiler validates future-stage definitions; the M7 executor must reject activation of enabled, unavailable M8 stages, leaving the active definition unchanged. It must not implement them as silent no-ops.
- Original always uses the configured window/level parameters, including when that stage is disabled for the Enhanced route. An omitted window/level definition uses the identity default `window=65535.0, level=32767.5`. Enhanced respects the enabled-stage sequence.
- Window bounds are `level-window/2` and `level+window/2`, clipped to `[0,65535]` before interpolation. Values at/below the clipped lower endpoint map to zero, at/above the upper endpoint to 65535, and interior values use rounded interpolation between those endpoints. Terminal Gray8 mapping is `(value+128)/257`. Validate finite parameters before processing.
- Borrowed image views validate layout, span, storage and row access; immutable input never exposes writable memory. Stages reject incompatible extents/storage and overlapping input/output. Padded rows and unaligned byte storage must not cause undefined typed access.
- Extend the existing processor factory to receive the prepared source layout and processing/display pools. The session still has a fixed accepted mode. Checked allocation accounts for 10 native raw, 9 U16 processing and 16 Gray8 buffers: 44 bytes per U8 source pixel or 54 per U16 source pixel. No fallback heap pixel storage is permitted.
- Published Enhanced U16 and display buffers stay immutable while any bundle retains them. Workspace leases must be sealed and replaced through the bounded pools, never reused in place while published. Configuration activation occurs between whole-frame operations, so every product in a bundle carries the same revision and source ID.

## Verification and evidence

Each task adds registered focused tests before implementation and records its failing and passing result. Run complete Linux Debug/Release simulator suites and a tests-OFF/Basler-OFF production build. Reuse only the pinned dependency installation, and record the actual compiler and source used. Windows/MSVC CI and native Windows validation are separate evidence; Linux results do not mark them passed.

Implementation and review results will be appended as they are obtained. No result is implied by this preflight.
