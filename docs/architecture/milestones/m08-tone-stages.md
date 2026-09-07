# M8 tone stages: development continuation

**Date:** 2026-09-07

**Status:** Task 1 authorized; implementation and verification pending. M8 is not accepted.

## Authority and scope

After the owner deferred M4/M5 native Windows 11 validation, the proposed next steps were to integrate M7 through a reviewed PR with Linux/Windows CI, explicitly extend simulator development to M8, and implement M8 Task 1. The owner replied “Ok do that.” This records that additional authorization before M8 source changes.

The continuation covers independent U16 brightness/contrast, gamma and inversion implementations and their tests under the [M8 plan](../../superpowers/plans/2026-04-25-m08-modular-enhancements.md). It does not mark M4–M8 accepted. The [native Windows checklist](m04-deferred-windows-validation.md), exact M6 hardware-profile entry gate, matching Windows/MSVC verification, designated Windows performance workstation, packaging and release requirements remain open. Use synthetic evaluation inputs only.

Task 1 begins from the reviewed M7 branch while [PR #8](https://github.com/m4bulmagd/Lumora/pull/8) runs cross-platform CI. M7 integration must pass those checks. M8 Tasks 2–5 and M9 UI are subsequent work, outside this bounded implementation.

## Task 1 contracts

The [design §10](../../superpowers/specs/2026-04-25-xray-imaging-workstation-design.md#102-stage-contract) is authoritative. The existing `IProcessingStage` owns constructor configuration and exposes const processing. Each tone stage follows that interface, reports its canonical ID/traits, consumes and produces CanonicalU16 of equal extent, and rejects incompatible or overlapping views before writing. Byte rows may be padded or unaligned; read/write U16 with alignment-safe access and preserve input and padding. Direct calls validate the compiler's inclusive finite parameter ranges: brightness [-1,1], contrast [0,4], gamma [0.1,5]. Error codes carry the stable stage name.

Brightness/contrast arithmetic is clarified as sequential saturating operations:

1. Round `brightness * 65535` to an integer, ties away from zero.
2. Add that signed offset to the sample, saturating to [0,65535].
3. Apply `(brightened - 32767.5) * contrast + 32767.5`, saturate to [0,65535], then round to nearest, positive halves upward.

Thus `(60000, brightness=0.5, contrast=0.5)` produces 49151; contrast zero always produces 32768. Gamma uses `round(pow(v/65535.0, 1/gamma)*65535)` with exact endpoints, and inversion is exactly `65535-v`.

Gamma owns an immutable 65,536-entry array built once for a valid constructor parameter. Repeated const process calls reuse it without rebuilding. The plan's mutable-parameter example is replaced by configured-instance tests; Task 5 must retain/reuse configured gamma state when only unrelated parameters or the whole configuration revision change. A per-frame temporary gamma stage would violate that later requirement.

The existing compiler registry already declares all canonical stage IDs. Task 1 adds concrete implementations and build registration. Production activation continues to reject enabled tone stages until Task 5 supplies actual execution; removing that guard now would silently ignore requested operations in the current engine.

## Verification contract and later integration work

Observe assertion failures against compile-ready placeholders before implementation. Test exact full-domain outputs, independent integer/rational brightness/contrast and selected gamma references, cache reuse, parameter errors, source immutability, extents, strides and aliasing. Run focused processing and complete Linux Debug/Release checks, and retain the actual source/commands/results. Cross-platform exactness is established only by corresponding Windows evidence.

Task 1's successful pixel operations must allocate no heap memory. Any measured allocation claim must isolate repeated calls on prepared views and already configured stages; error paths may build diagnostics. Code inspection alone is not measured allocation evidence.

Whole-frame zero allocation remains an M8 Task 5 acceptance requirement: the current engine allocates timing vectors/strings and frame/shared-ownership metadata. Pixel-pool counters alone cannot prove it. Task 5 must also modify `FrameProcessingEngine.cpp`, preserve Original's normalization/window-level path, compose enabled Enhanced stages in fixed order, reuse gamma by value and preserve one immutable configuration per frame. These are recorded follow-ups, not Task 1 scope expansion.

## Verification evidence

Pending implementation.
