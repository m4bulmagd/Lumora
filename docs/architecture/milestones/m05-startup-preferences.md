# M5 Task 4: startup controls and saved preferences

**Date:** 2026-09-07

**Task baseline:** `ccabaae3bdff84c28e78be3de745cb3d30768cff`

**Historical branch:** `feat/m05-startup-and-integration` (deleted locally and remotely after merge; worktree retained detached).

**Implementation source:** `8e7d65e5a28b2abea45e9cea8dd624584877d39f`

**Reviewed registration fix:** `5b4bff87fe0ca3ece179dbaf9afbebe9bc212d20`

**Status:** Implemented, reviewed and merged together with Task 5 through [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) as `f01b408`. The independent task review's blocking finding is resolved; the deferred test-isolation Minor was corrected in `d113da9`, with scoped re-review clear. The [merged integration checkpoint](m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) records passing local and post-merge Linux/Windows Debug/Release verification. Native Windows 11 checks and full milestone acceptance remain pending.

## Scope and authorization

After authorizing the Task 3 push and [PR #6](https://github.com/m4bulmagd/Lumora/pull/6), the owner asked to continue M5 without waiting for CI. Task 4 implements the [approved startup contract](m05-preflight.md#3-minimal-startup-ui-and-persistence): plain startup values and canonical comparisons, schema-1 migration to schema 2, a background preferences adapter and an independently testable native camera startup panel. Task 5 wires those modules into the production simulator composition.

First-run camera selection, Apply/readback, Confirm and Start remain explicit. Later-run Resume Live requires matching stable identity/capabilities and matching confirmed actual settings; loading a record never authorizes silent streaming. M9's complete editor/per-camera profiles and M12's automatic recovery remain outside this task. The evaluation release forbids real patient data and clinical use.

## Implementation decisions

The preferences adapter owns a small load/save interface declared in its planned header. Its production constructor wraps the existing final `ConfigurationStore`; deterministic tests can control I/O through that same boundary. This avoids making the store virtual, adding test observers or doing file/JSON work on the UI thread. If the injection shape proves unsuitable, changing the constructor adapter does not require changing the persisted schema or service lifecycle.

The plain status distinguishes load completion and the initial loaded record from attempted/successfully saved submission revisions and typed warnings. `loadedPreferences` retains initial-load provenance; later saves do not rewrite it. Admission is not persistence. The panel consumes an authoritative immutable camera snapshot and separate controller-owned selection, pre-Apply request and pending/startup inputs; it does not maintain an independent camera state or confirmation flag. If these inputs prove inadequate, adjust the small status/presentation contracts and consumer tests before integration, preserving truthful confirmation and save status.

The service retains the complete loaded document and one immutable coalesced pending save. Shutdown drains the active and newest accepted pending saves, then joins; it does not promise bounded filesystem latency. Unreadable or unpreserved corrupt source files cannot be overwritten using guessed defaults. Unexpected standard/unknown exceptions are contained at the worker boundary; secondary warning publication is best effort. The codec stores canonical structural capability fields, not hashes, and preserves all six existing JSON sections.

## Verification and review

At the unchanged Task 3 baseline, fresh native-inclusive Linux CTest passed 30/30 Debug (16.41 s) and 30/30 Release (13.63 s). Those results establish the starting point only; they do not verify Task 4.

At exact reviewed source `5b4bff8`, the controller independently ran:

```bash
cmake --build --preset linux-gcc-debug-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-debug-sim --no-tests=error --output-on-failure
cmake --build --preset linux-gcc-release-sim --parallel 4
xvfb-run -a ctest --preset linux-gcc-release-sim --no-tests=error --output-on-failure
cmake --build out/build/m05-tests-disabled --target lumora_application lumora_configuration lumora_ui lumora_app --parallel 4
ctest --preset linux-gcc-debug-sim -N -R '^(Application.StartupPreferences|Configuration.StartupPreferences|CameraStartupPanel|ConfigurationStore)$'
out/build/linux-gcc-debug-sim/tests/lumora_configuration_tests --gtest_brief=1
out/build/linux-gcc-debug-sim/tests/lumora_application_tests --gtest_filter=StartupPreferences.* --gtest_brief=1
```

Full native-inclusive Linux CTest passed **33/33 Debug (16.34 s)** and **33/33 Release (13.58 s)**. The direct configuration run passed **21/21 cases**: ten existing store/preservation cases and eleven new codec/service cases. Plain startup values passed **8/8 cases**; the panel's four cases passed through its registered entry. Listing found exactly the four expected entries. The tests-OFF/Basler-OFF Debug library/app rebuild and complete-range whitespace check passed. No Windows result is inferred.

Observed RED/GREEN covers migration, unknown-mode and missing-actual-FPS rejection, background loading, save coalescing/drain, and panel Start/review/priority behavior. The initial value RED batched several behaviors rather than following strict vertical TDD. The round-trip test was written first but its RED command was missed; only GREEN was observed. Later codec/failure/exception/intent coverage includes characterization tests, not additional claimed RED cycles. The service entry passed 100 focused repetitions before review; that is not milestone stress or a filesystem latency guarantee.

Independent task review found an Important registration defect: the new store filter omitted the `FailureMatrix/` prefix and silently excluded five parameterized corruption/schema cases. Fix `5b4bff8` restores wildcard-prefix matching while keeping startup tests separate. Verbose Debug/Release CTest then executed all ten store cases; scoped re-review confirmed the finding addressed with no new breakage. Earlier 33/33 entry counts at `8e7d65e` covered only five of those ten store cases and are not complete preservation evidence.

One Minor concern was deferred to final whole-branch review: `ValidationRejectsUnknownConfigurationModes` also invalidated capabilities, so it did not isolate configuration-enumerator rejection. Final fix `d113da9` validates the baseline, keeps capabilities valid, mutates only the requested acquisition-mode enumerator and expects `startup_configuration_invalid`. Scoped re-review confirmed the correction. The [Task5 checkpoint](m05-live-integration.md) records the final source-matched Linux matrix, translation and shutdown fixes, and end-to-end lifetime/startup evidence; earlier counts here remain the exact Task4 checkpoint, not current total case counts.

A temporary native XCB/Xvfb check used the existing 240-pixel sidebar width: the panel fit at 208 pixels with a minimum hint of 166 x 351. This was panel-only QA, not final hosting, Windows DPI or physical-monitor validation. The minimal plugin's dummy fonts were unsuitable for visual judgment. The reduced Qt build exports BMP but not PNG; the ignored screenshot was mechanically converted without dependency changes.

The local Release Ninja dependency log repeatedly reported recovery and rebuilt one test object. Read-only inspection found a duplicate path record, matching the [upstream recovery-offset defect](https://github.com/ninja-build/ninja/pull/2764). An earlier yielded-then-reissued agent build was a possible origin, not proven. The controller preserved the generated log and re-compacted its metadata; a clean no-work build preceded the final passing matrix above. No source, toolchain version or dependency was changed to hide the warning.

## Remaining gates

Task5 now supplies controller/pipeline wiring and end-to-end first/later-run behavior. Matching Windows/MSVC Debug/Release CI and native UI checks affected by the controls remain required for acceptance. The [M4 native Windows deferral](m04-deferred-windows-validation.md) remains open. Neither this checkpoint nor continued local development authorizes another push, merge, hardware validation, installer acceptance or clinical use.
