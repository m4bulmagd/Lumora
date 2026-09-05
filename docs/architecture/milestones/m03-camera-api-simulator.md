# M3 camera API and simulator verification record

Recorded: 2026-09-05 (UTC).

Status: **implemented; Linux verification passed; Windows/MSVC CI pending; not formally accepted**.

The camera implementation was merged into `main` at `916ee1a945487fd5291d44e6cf4ac3b945736d68`. The tested source checkpoint is `9676038ddc3bf6efb153ef38c68297fa6cc904c7`, which also corrects the `Core.Clock` CTest filter to include the existing `SystemClock` cancellation regression. No camera behavior changes in this checkpoint.

The [M3 plan](../../superpowers/plans/2026-04-25-m03-camera-api-simulator.md), [design §6](../../superpowers/specs/2026-04-25-xray-imaging-workstation-design.md#6-camera-abstraction), and [roadmap review gates](../../superpowers/plans/2026-04-25-xray-imaging-workstation-roadmap.md#4-repository-wide-review-gates) define acceptance. A local merge or Linux-only test result does not close the cross-platform gate.

## Acceptance evidence

Each pass below is Linux/GCC evidence only. The full Windows Debug and Release run remains required for acceptance.

| M3 criterion | Evidence | Local result |
|---|---|---|
| Simulator-only configuration links no pylon target | Debug/Release configure reports Basler disabled; camera API links core only and simulator links camera API only in `src/CMakeLists.txt`; configure-time target contract | Pass |
| Generated and PGM-replayed frames are deterministic | `SimulatedCamera.RampFramesAreRepeatable`, `EveryGeneratedPatternIsDeterministic`, `ReplayClearsPoolTailForDeterministicPublishedBytes`; `SequenceSource.ReadsSixteenBitPgmInLexicalOrderAndLoops` | Pass |
| PGM fixtures cover 255, 1023, 4095, 65535 and a non-power-of-two maximum | `SequenceSource.DerivesStorageBitsAndDeclaredMaximumWithoutScalingSamples`; `tests/fixtures/sequences/max{255,1023,4095,65535,1000}` | Pass |
| Generated and sequence devices share capability validation | `SimulatedCamera.InvalidConfigurationUsesSharedValidatorError`, `ReplaysImmutableNumericFramesThroughPoolAndSharedRoiValidator`; `CameraConfiguration` suite | Pass |
| Lifecycle is idempotent; timeout/cancellation paths are tested | `SimulatedCamera.LifecycleIsIdempotentAndCapabilitiesRequireOpen`; manual/real pacing, production-budget, replay and cancellation regressions; `Core.Clock` | Pass |
| Exact-frame timeout, malformed-frame, disconnect and configuration faults | `SimulatedCamera.FaultFailuresRepeatAtExactNextIdWithoutConsumingReplayPosition`, `ConfigurationFaultConsumesOnlyAnApplicableValidatedAttempt`, `DisconnectPersistsAcrossLifecycleUntilExplicitRestore`; `FaultScript` suite | Pass |
| Complete Debug and Release simulator validation on both platforms | Linux full configure/build/CTest below; [Windows workflow](../../../.github/workflows/windows-simulator.yml) has not been executed for this checkpoint | Pending Windows |

Additional M3 regressions cover elapsed-time fault triggers, strict CRLF/raster boundaries, whole-operation retrieval budgets, streaming configuration permissions, lease recycling after failure, and fault-result construction before state commitment. Exact CTest names and target mappings are in [requirements traceability](../requirements-traceability.md#milestone-3-verification-map).

## Linux verification

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

The clock-registration check was first observed failing because verbose CTest output did not contain the `SystemClock` case. After including that suite in the filter, the case ran and passed. `git diff --check` is part of this checkpoint's final verification.

## Implementation clarifications carried forward

- ROI increments are strict validation constraints. Finite numeric requests inside declared ranges are accepted; the adapter returns actual quantized values without mutating the request. Ordered diagnostic details retain all validation violations.
- `ICameraDevice::retrieve(timeout, pool, stopToken)` is cancellation-aware and returns application-owned immutable frames. Device calls, including cleanup, remain on the owning thread.
- The simulator shares one real elapsed-time budget across pacing, pixel production, metadata and publication. Tests cover cancellation and timeout preserving frame ID, sequence position and uncommitted faults. This is cooperative bounded work, not hard real-time preemption of the allocator or operating system.
- Changing ROI or source format requires a stopped stream. Other changes obey capability writability flags; unchanged configuration can be reapplied. An absent requested FPS selects the device maximum, while initial simulator configuration uses its default FPS.
- PGM P5 loading preserves numeric samples and the declared maximum, validates exact payloads and uniform sequence geometry/maximum, and treats CRLF after the maximum as one header separator. It never guesses the raster boundary from payload length. Replay is eagerly loaded, so its source-memory use scales with the chosen sequence; live frame pools remain fixed-capacity.
- Sequence position and frame ID advance only after successful publication. Disconnect faults remain active through close/open until explicit restoration. Fault occurrences commit only once the final failure result is constructed; allocation failure before that point leaves the occurrence unconsumed.
- Simulator frame IDs increase for a device instance, including across stop/start. A newly created device starts a new sequence of IDs; consumers must not assume IDs are global across devices or sessions.

## Remaining acceptance work

1. Repository selection is complete: [m4bulmagd/Lumora](https://github.com/m4bulmagd/Lumora) is connected as `origin` at `git@github.com:m4bulmagd/Lumora.git`. The reviewed `main` checkpoint `fd0f4dd6cb6daf63bfd941a3e284e6da841ea2ef` was pushed on 2026-09-05.
2. The push started the [Windows Simulator run](https://github.com/m4bulmagd/Lumora/actions/runs/33964511101) and [Linux Simulator run](https://github.com/m4bulmagd/Lumora/actions/runs/33964511078). Their final results are not yet recorded here. The `Windows MSVC Debug and Release Simulator` job must configure, build and pass all simulator CTest entries in both configurations with pylon disabled. The [Windows build guide](../../development/build-windows.md) also gives native reproduction commands.
3. Record the tested commit SHA, workflow/run URL, runner/compiler versions and both test summaries here; resolve any failures and rerun before changing the status to accepted. The run should also cover the earlier M1/M2 regression suites, whose Windows evidence is still pending.
4. Commit the completed acceptance record separately from feature changes, then begin M4 according to the roadmap order.

The configured CI runner is `windows-2022` and verifies MSVC compatibility. It does not substitute for the clean Windows 11 installation, upgrade and runtime acceptance required by M13, or the hardware acceptance required by M14. No physical camera is needed for the M3 CI gate.

This record is engineering/evaluation verification, not clinical validation or authorization for diagnosis.
