# M8 tone stages: development continuation

**Date:** 2026-09-07

**Status:** Task 1 implemented at `246a73a`, independently reviewed and merged through PR #10 as `d6f94e1`. Linux/GCC and Windows/MSVC Debug/Release PR CI passed. M8 is not accepted.

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

Commit `246a73a` adds the three stages, their private shared byte-safe validation helper, and `Processing.ToneStages` test/build registration. Fourteen tone cases include exhaustive integer/rational brightness/contrast and gamma-0.5 comparisons, gamma identity and selected scalar/fixed references, full-domain inversion, repeated configured gamma calls, numeric validation and all-stage row/alias guards. No production activation or engine code changed.

Independent task review approved both spec compliance and code quality without findings. At `246a73a`, GCC 15.2.0 passed all eight Processing CTest suites and all fourteen tone cases in Debug and Release. Root's full Debug suite passed 41/41 headless in 29.18 s plus native X11 1/1 in 0.24 s; full Release passed 41/41 headless in 18.43 s plus native X11 1/1 in 0.06 s. Both use the existing pinned dependency prefix; no dependency version changed.

The observed pre-implementation assertion RED directly covered brightness/contrast. Gamma, inversion, numeric validation and overlap tests were compile-ready but not separately run before implementation; later deliberate mutations made those tests fail, then source was restored and fresh focused suites passed. This is mutation evidence, not a claim of pre-implementation RED for those cases.

The immutable gamma table and successful process paths have no heap allocation by code inspection. Repeated-output tests confirm stable configured behavior; neither LUT-build count nor allocation count was instrumented. The full-frame allocation gate remains open.

Commands: `cmake --build --preset linux-gcc-{debug,release}-sim --parallel 3`, `ctest --preset linux-gcc-{debug,release}-sim --output-on-failure -LE 'hardware|desktop'`, and `xvfb-run -a ctest --preset linux-gcc-{debug,release}-sim --output-on-failure --no-tests=error -L desktop`, run separately by configuration. Focused suites use `-R '^Processing\.'`; tone cases use `--gtest_filter=ToneStages.*`. Logs, including restored mutation results, remain in `out/qa/m08-task1/` in the retained worktree. Matching Windows evidence, Tasks 2–5, whole-frame allocation and designated-workstation performance acceptance remain pending.


A fresh Release `lumora_app` build with tests, benchmarks and Basler disabled passed with GCC 15.2.0. Configure/build logs are `production-configure.log` and `production-build.log` under the same QA directory.

Final whole-branch review of `d191097..7c6f8bf` approved Task 1 with no Critical, Important or Minor findings and no required changes. The separately reviewed M7 test-only correction `43a7401` is included locally; its publishing/Windows follow-up was pending at that checkpoint and is now recorded under M7 PR #9. Matching M8 Windows evidence and milestone acceptance remain open.

After carrying the reviewed M7 stall-test correction into this branch, the covering Debug LivePipeline suite passed (37 cases, 6.38 s). Release passed the corrected stall test but timed out in the pre-existing `PipelineRetainsOldContextUntilReplacementBindingAcknowledgement` case waiting for a Disconnect outcome. Its failure log is retained as `stall-integration-test-release.log`; diagnosis remains open. The earlier full 42/42 results above identify the earlier source and are not a claim that this later integration check passed.

Failure-only diagnostics `8c63f36` were subsequently included and independently approved without code findings. The final M8 checkout rebuilt the integration target and passed the affected context test once in Debug and Release; these checks confirm the diagnostic integration, not a fix for the intermittent timeout. Latest diagnosis, 100-run characterization and remaining uncertainty are retained in the linked M7 record.


## Authorized integration continuation — 2026-09-08

After M7 PR #9 merged, the owner approved the proposed next steps with “Ok go ahead with M8”: publish and integrate the already completed Task 1 through Linux/Windows CI, then implement bounded Task 2 (U16 CLAHE). This extends the earlier Task-1-only development scope; Tasks 3–5 and M9 remain subsequent work. Manual Windows 11 validation remains deferred.

The existing isolated worktree was retained. Merge `e4fcbb5` incorporates main `61d91fb`, including the reviewed M7 stall-test correction and context-timeout diagnostics. Task 1 source remains `246a73a`; no tone algorithm or public interface changed for publication. Task 1 publishing is authorized; CI results will be recorded against the actual PR head before merge.


## Task 1 PR integration — 2026-09-08

[PR #10](https://github.com/m4bulmagd/Lumora/pull/10) merged exact head `e4dcc7949149d125c0b46419617c63a82ac0068c` as `d6f94e183d9b5f5f74238a4b2036a7be839bc14e` after all four Linux/Windows PR and branch-push jobs passed. No CI source correction was needed. The expected head SHA was checked at merge.

| Verification | Debug | Release |
|---|---|---|
| Local GCC 15.2.0 | 41/41 headless, 22.96 s; X11 1/1, 0.12 s | 41/41 headless, 18.16 s; X11 1/1, 0.06 s |
| [Linux PR CI, GCC 13.3.0](https://github.com/m4bulmagd/Lumora/actions/runs/34170321747) | 41/41, 22.41 s; X11 1/1, 0.13 s | 41/41, 17.59 s; X11 1/1, 0.04 s |
| [Windows PR CI, MSVC 19.44.35228.0](https://github.com/m4bulmagd/Lumora/actions/runs/34170321728) | 41/41, 31.24 s | 41/41, 21.28 s |

Branch-push runs [Linux](https://github.com/m4bulmagd/Lumora/actions/runs/34170277132) and [Windows](https://github.com/m4bulmagd/Lumora/actions/runs/34170277090) also passed. Full logs are retained under `out/qa/m08-task1/integration-2026-09-08/`. These are Task 1 implementation checks, not full M8 acceptance. The intermittent context-retirement timeout remains open; native Windows 11 manual validation, Tasks 2–5, zero-allocation and designated-workstation performance/reference gates remain separate.

Post-merge [Linux main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34170808653) and [Windows main CI](https://github.com/m4bulmagd/Lumora/actions/runs/34170808643) both completed successfully at `d6f94e1`. This is additional automated implementation evidence; the acceptance limits above remain unchanged.
