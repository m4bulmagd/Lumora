# M3 camera API and simulator verification record

Recorded: 2026-09-05 (UTC).

Status: **accepted for the engineering/evaluation milestone; Linux/GCC and Windows/MSVC Debug and Release CI passed**.

The camera implementation was merged into `main` at `916ee1a945487fd5291d44e6cf4ac3b945736d68`. The first locally tested checkpoint was `9676038ddc3bf6efb153ef38c68297fa6cc904c7`, which corrects the `Core.Clock` CTest filter to include the existing `SystemClock` cancellation regression.

The accepted source checkpoint is **`2c88ec90ab58e3e6719be5e236dc49388dbc72dd`**. Its completed cross-platform CI runs are recorded below. The CI fixes were fast-forward merged and pushed to `main` without changing that SHA. They address native dependency prerequisites, binary caching, and Qt test startup; they do not change camera behavior.

The [M3 plan](../../superpowers/plans/2026-04-25-m03-camera-api-simulator.md), [design §6](../../superpowers/specs/2026-04-25-xray-imaging-workstation-design.md#6-camera-abstraction), and [roadmap review gates](../../superpowers/plans/2026-04-25-xray-imaging-workstation-roadmap.md#4-repository-wide-review-gates) define acceptance. A local merge or Linux-only test result does not close the cross-platform gate.

## Acceptance evidence

The full Linux/GCC and Windows/MSVC Debug and Release runs include every suite named below, together with the earlier M1/M2 regression suites.

| M3 criterion | Evidence | Result |
|---|---|---|
| Simulator-only configuration links no pylon target | Debug/Release configure reports Basler disabled; camera API links core only and simulator links camera API only in `src/CMakeLists.txt`; configure-time target contract | Pass |
| Generated and PGM-replayed frames are deterministic | `SimulatedCamera.RampFramesAreRepeatable`, `EveryGeneratedPatternIsDeterministic`, `ReplayClearsPoolTailForDeterministicPublishedBytes`; `SequenceSource.ReadsSixteenBitPgmInLexicalOrderAndLoops` | Pass |
| PGM fixtures cover 255, 1023, 4095, 65535 and a non-power-of-two maximum | `SequenceSource.DerivesStorageBitsAndDeclaredMaximumWithoutScalingSamples`; `tests/fixtures/sequences/max{255,1023,4095,65535,1000}` | Pass |
| Generated and sequence devices share capability validation | `SimulatedCamera.InvalidConfigurationUsesSharedValidatorError`, `ReplaysImmutableNumericFramesThroughPoolAndSharedRoiValidator`; `CameraConfiguration` suite | Pass |
| Lifecycle is idempotent; timeout/cancellation paths are tested | `SimulatedCamera.LifecycleIsIdempotentAndCapabilitiesRequireOpen`; manual/real pacing, production-budget, replay and cancellation regressions; `Core.Clock` | Pass |
| Exact-frame timeout, malformed-frame, disconnect and configuration faults | `SimulatedCamera.FaultFailuresRepeatAtExactNextIdWithoutConsumingReplayPosition`, `ConfigurationFaultConsumesOnlyAnApplicableValidatedAttempt`, `DisconnectPersistsAcrossLifecycleUntilExplicitRestore`; `FaultScript` suite | Pass |
| Complete Debug and Release simulator validation on both platforms | Completed CI runs below: both configurations configure/build and pass 18/18 CTest entries with pylon disabled | Pass |

Additional M3 regressions cover elapsed-time fault triggers, strict CRLF/raster boundaries, whole-operation retrieval budgets, streaming configuration permissions, lease recycling after failure, and fault-result construction before state commitment. Exact CTest names and target mappings are in [requirements traceability](../requirements-traceability.md#milestone-3-verification-map).

## Cross-platform CI verification

Both runs tested `2c88ec90ab58e3e6719be5e236dc49388dbc72dd` on `fix/ci-bootstrap-cache` before the fast-forward merge. Both completed successfully on 2026-09-05 (UTC).

| Platform and run | Runner image | Compiler | Debug CTest | Release CTest |
|---|---|---|---|---|
| [Windows Simulator 33992463441](https://github.com/m4bulmagd/Lumora/actions/runs/33992463441) | `windows-2022`, image `20260830.290.1` | MSVC `19.44.35228.0` | 18/18 pass; 5.72 s | 18/18 pass; 1.78 s |
| [Linux Simulator 33992463439](https://github.com/m4bulmagd/Lumora/actions/runs/33992463439) | `ubuntu-24.04`, image `20260831.293.1` | GCC `13.3.0` | 18/18 pass; 2.85 s | 18/18 pass; 0.86 s |

Each workflow runs fresh Debug and Release configuration through the pinned vcpkg toolchain, builds all targets, and invokes its simulator CTest preset with `--output-on-failure -LE hardware`. All four configure steps report `Basler pylon support is disabled`. Binary archives may be reused; these results do not claim that every dependency was compiled from scratch in these runs.

The previous Windows blocker was Qt failing to discover the `minimal` platform plugin, leaving the unattended UI smoke process stalled. `MainWindowSmoke` now resolves `QT_QPA_PLATFORM_PLUGIN_PATH` from the configuration-matched `Qt6::QMinimalIntegrationPlugin` target and has a 60-second timeout. In the accepted Windows run it passes in 2.48 s (Debug) and 0.08 s (Release). Non-blocking cache-service warnings did not affect configure/build/test success.

The initial [Windows run 33964511101](https://github.com/m4bulmagd/Lumora/actions/runs/33964511101) and [Linux run 33964511078](https://github.com/m4bulmagd/Lumora/actions/runs/33964511078) failed and are superseded by the successful runs above. Linux needed native dependency build prerequisites; Windows needed the Qt plugin-path correction. The composed CI-fix diff was reviewed before integration.

Post-merge repeats at the same SHA are [Windows run 33995266201](https://github.com/m4bulmagd/Lumora/actions/runs/33995266201) and [Linux run 33995266249](https://github.com/m4bulmagd/Lumora/actions/runs/33995266249). They were still running when this record was prepared; acceptance is based on the completed, exact-SHA runs above, not an assumed outcome of these repeats.

## Local Linux verification

Environment: Linux x86-64, GCC 15.2.0, CMake 4.4.3, Ninja; existing dynamic dependencies under `out/vcpkg_installed/x64-linux-dynamic`. The dependency manifest pins Qt 6.11.1, OpenCV 4.12.0, GoogleTest 1.18.0 and spdlog 1.17.0.

Both `linux-gcc-debug-sim` and `linux-gcc-release-sim` configure, build and pass **18/18 CTest entries**. `Core.Clock` now executes all 12 clock cases, including `SystemClock.PreCancellationWinsForNonPositiveMaximumWaits`.

Commands used from the repository root (repeat the three commands for each preset):

```bash
export PYTHONPATH="$PWD/.tools/python"
export PATH="$PWD/.tools/python/bin:/usr/bin:/bin"
cmake --preset linux-gcc-debug-sim --fresh \
  -DCMAKE_PREFIX_PATH="$PWD/out/vcpkg_installed/x64-linux-dynamic"
cmake --build --preset linux-gcc-debug-sim --parallel 2
ctest --preset linux-gcc-debug-sim --output-on-failure -LE hardware

cmake --preset linux-gcc-release-sim --fresh \
  -DCMAKE_PREFIX_PATH="$PWD/out/vcpkg_installed/x64-linux-dynamic"
cmake --build --preset linux-gcc-release-sim --parallel 2
ctest --preset linux-gcc-release-sim --output-on-failure -LE hardware
```

This reuses already installed dependencies; it is not a clean vcpkg bootstrap or a CI run. Fresh configuration reports unused `VCPKG_*` cache variables because this local check uses `CMAKE_PREFIX_PATH` instead of the vcpkg toolchain. The canonical toolchain-based procedure remains in the [Linux build guide](../../development/build-linux.md).

The clock-registration check was first observed failing because verbose CTest output did not contain the `SystemClock` case. After including that suite in the filter, the case ran and passed. After the CI-fix merge, both local presets were configured and built again at the accepted SHA: Debug passed 18/18 in 3.37 s and Release passed 18/18 in 1.08 s. `git diff --check` passed for the merged changes.

## Implementation clarifications carried forward

- ROI increments are strict validation constraints. Finite numeric requests inside declared ranges are accepted; the adapter returns actual quantized values without mutating the request. Ordered diagnostic details retain all validation violations.
- `ICameraDevice::retrieve(timeout, pool, stopToken)` is cancellation-aware and returns application-owned immutable frames. Device calls, including cleanup, remain on the owning thread.
- The simulator shares one real elapsed-time budget across pacing, pixel production, metadata and publication. Tests cover cancellation and timeout preserving frame ID, sequence position and uncommitted faults. This is cooperative bounded work, not hard real-time preemption of the allocator or operating system.
- Changing ROI or source format requires a stopped stream. Other changes obey capability writability flags; unchanged configuration can be reapplied. An absent requested FPS selects the device maximum, while initial simulator configuration uses its default FPS.
- PGM P5 loading preserves numeric samples and the declared maximum, validates exact payloads and uniform sequence geometry/maximum, and treats CRLF after the maximum as one header separator. It never guesses the raster boundary from payload length. Replay is eagerly loaded, so its source-memory use scales with the chosen sequence; live frame pools remain fixed-capacity.
- Sequence position and frame ID advance only after successful publication. Disconnect faults remain active through close/open until explicit restoration. Fault occurrences commit only once the final failure result is constructed; allocation failure before that point leaves the occurrence unconsumed.
- Simulator frame IDs increase for a device instance, including across stop/start. A newly created device starts a new sequence of IDs; consumers must not assume IDs are global across devices or sessions.

## Acceptance decision and next boundary

The missing cross-platform gate is now satisfied. This record and the updated traceability matrix close M3 acceptance, including Windows regression evidence for the implemented M1/M2 foundations. The acceptance documentation is committed separately from feature changes. M4 remains unimplemented; use the [M4 preflight](m04-preflight.md) and existing task plan before starting its viewport-transform tests.

The configured CI runner is `windows-2022` and verifies MSVC compatibility. It does not substitute for the clean Windows 11 installation, upgrade and runtime acceptance required by M13, or the hardware acceptance required by M14. No physical camera is needed for the M3 CI gate.

This record is engineering/evaluation verification, not clinical validation or authorization for diagnosis.
