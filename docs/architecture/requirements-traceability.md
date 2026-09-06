# Requirements traceability

This matrix records the evidence chain for the approved evaluation architecture. The original M1–M3 results refer to `2c88ec90ab58e3e6719be5e236dc49388dbc72dd`, verified in Linux/GCC and Windows/MSVC Debug and Release. M4 extensions below record merged Tasks 1–4 separately from remaining native visual acceptance; they do not overwrite that accepted baseline.

Current checkpoint: M1–M3 implementation is merged and pushed to `main`. **M3 is accepted for engineering/evaluation**; the [M3 verification record](milestones/m03-camera-api-simulator.md#cross-platform-ci-verification) records the exact-SHA successful CI runs, runner/compiler versions, local checks, and limitations. An implemented subset below does not imply that its complete design section or a later milestone is accepted.

M4 Tasks 1–4 are merged at `aaf57a678f344864ca6e1f8333f5b774fed5da18` with passing cross-platform Debug/Release CI. Task 4 adds presentation and a non-shipping simulator harness. See the [M4 execution record](milestones/m04-preflight.md) for exact source/review evidence and remaining gates. **M4 is not fully accepted**; native Windows 11 visual/DPI checks remain pending. The follow-up at `6c054a7` passed matching Linux/Windows Debug/Release CI and Windows Release stress. The [2026-09-07 scoped exception](milestones/m04-deferred-windows-validation.md) permits M5 development only; it does not pass the manual gate.

| ID | Design source | Implementation/evidence | Linux/GCC | Windows/MSVC | Status |
|---|---|---|---|---|---|
| DES-1 | §1 Purpose and evaluation boundary | Compile-time evaluation release class, `MainWindowSmoke`, `Logging` | Verified | Verified | M1 baseline implemented |
| DES-2 | §2 Scope and success criteria | Simulator-only presets, pylon-off configure path, evaluation banner; broader product scope maps to M2–M14 | Verified for M1 | Verified for M1 | M1 subset implemented; later scope planned |
| DES-3 | §3 Architectural approach | Qt-free core, camera API and simulator targets; focused configuration/diagnostics/UI targets; typed errors and bounded exchanges | Verified for implemented subset | Verified for implemented subset | M1–M3 foundation implemented |
| DES-4 | §4 Repository and build structure | CMake target contract, pinned vcpkg manifest, Linux and Windows Debug/Release simulator presets | Verified | Verified | Cross-platform build foundation verified; packaging remains M13 |
| DES-5 | §5 Core frame and error model | Capability-derived pixel descriptors, checked layouts, immutable paired frames, reusable aligned buffers, typed errors, separate steady/UTC timestamps; transactional failure-result construction | Verified | Verified | M2 foundation and M3 extensions implemented |
| DES-6 | §6 Camera abstraction | `CameraConfiguration`, `CameraContract`, `SimulatedCamera`, `SequenceSource`, `FaultScript`, `SimulatedCamera.PatternAllocation` | Verified for API/simulator | Verified for API/simulator | M3 accepted; Basler adapter remains M6 |
| DES-7 | §7 Camera and viewer state machines | Simulator lifecycle; `WorkstationView` and `FramePresenter` pause/resume, waiting/stale, session-reset cases | M4 viewer subset verified | M4 viewer subset verified in CI | Production orchestration remains M5/M12 |
| DES-8 | §8 Threading model | Thread-confined camera port; UI-timer presenter and joined test-only simulator worker; no per-frame queued Qt signal | M4 harness subset verified | M4 harness subset verified in CI | Production worker orchestration remains M5/M12 |
| DES-9 | §9 Frame memory ownership and bounded buffering | Core pool/frame/slot/queue tests; `ImageViewport`, `FramePresenter`, `SimulatedViewer` retained-ownership checks | M4 viewer subset verified | M4 viewer subset verified in CI | Production pipeline pools remain later work |
| DES-10 | §10 Processing pipeline | Frame/provenance contracts exist; processing, high-bit-depth mapping and enhancements remain M5/M7/M8 | Not yet verified as pipeline | Not run | Planned |
| DES-11 | §11 Rendering and workstation UI | `ViewportTransform`, `ImageViewport`, `WorkstationView`, `FramePresenter`, `SimulatedViewer`; native Linux desktop smoke | M4 subset verified | Tasks 1–4 verified in CI; native Windows visual/DPI pending | M4 unaccepted; minimal startup controls remain M5, complete editor M9 |
| DES-12 | §12 Capture and future recording | M10 capture plan; recording requires its own approved specification | Not run | Not run | Planned/deferred |
| DES-13 | §13 Configuration and presets | `ConfigurationStore`; startup preferences remain M5, presets/installation profiles/full controls M9 | Verified for store only | Verified for store only | Partial |
| DES-14 | §14 Diagnostics, metrics, and logging | `Logging`, pool/slot counters and simulator pacing-slip tests; full diagnostics remain M11 | Verified for foundation only | Verified for foundation only | Partial |
| DES-15 | §15 Reliability and failure policy | M3 device faults; M4 paused/stale, blocked-image-path, event-loop recovery and observable harness-timeout tests | M4 subset verified | M4 subset verified in CI | System recovery remains M12 |
| DES-16 | §16 Test strategy | M1–M4 verification maps below; default short integration and opt-in 600-second M4 stress | See exact-source M4 execution record | M4 and follow-up CI passed; Release stress passed at `6c054a7` | Reference-image/performance/hardware gates remain later work |
| DES-17 | §17 Milestones and acceptance criteria | Per-milestone plans, [M3 acceptance](milestones/m03-camera-api-simulator.md#acceptance-evidence), [M4 execution](milestones/m04-preflight.md) | M4 local evidence, not acceptance | M4 external gates incomplete | M3 accepted; M4–M14 unaccepted |
| DES-18 | §18 Technical risks and mitigations | Bounded storage/retrieval and SDK isolation tested; rendering, throughput, hardware and distribution risks remain in their milestone gates | Partial evidence | Partial evidence | Ongoing |
| DES-19 | §19 Deferred decisions | [Open hard gates](../superpowers/README.md#open-hard-gates) record required camera, workstation and distribution inputs | Not an executable test | Not an executable test | Deferred until the specified gates |
| DES-20 | §20 References informing the design | References remain in the approved specification; they are background, not verification evidence | Not applicable | Not applicable | Reference material |

## Milestone 1 verification map

| Test/check | Requirement covered | Automated evidence |
|---|---|---|
| `MainWindowSmoke` | Resizable shell opens/closes; visible evaluation identity; translation-ready UI strings | `lumora_ui_tests` |
| `ResultSmoke` | Qt-free typed success/failure contract including move-only values | `lumora_core_tests` |
| `Logging` | Writable rotating log; startup/shutdown; version and release class; typed path failure | `lumora_diagnostics_tests` |
| `ConfigurationStore` | Defaults, schema validation, round-trip, corrupt-file preservation, atomic-save failures, Unicode paths | `lumora_configuration_tests` |
| Configure-time target contract | Required M1 targets exist; Basler target cannot appear when disabled | `lumora_verify_milestone_one_targets()` |
| Linux simulator CI | Pinned dependencies, pylon-off Debug and Release configure/build/test | `Linux Simulator / Linux GCC Debug and Release Simulator` |
| Windows simulator CI | Native MSVC Debug and Release configure/build/test without pylon | `Windows Simulator / Windows MSVC Debug and Release Simulator` |

## Milestone 2 verification map

| Test/check | Requirement covered | Automated evidence |
|---|---|---|
| `Core.Result`, `Core.CheckedMath` | Typed errors, move-only results, checked addition/multiplication boundaries | `lumora_core_tests` |
| `Core.ImageLayout`, `Core.FrameMetadata` | Fail-closed format descriptors; zero/stride/payload/overflow layout validation; complete applied acquisition snapshot | `lumora_core_tests` |
| `Core.BufferPool` | Fixed capacity, deterministic exhaustion/high-water metrics, aligned preallocation, immutable sealing, final-reference recycling, safe state lifetime, 100,000 concurrent acquisitions | `lumora_core_tests` |
| `Core.Frame` | Sealed-buffer size checks, native raw/processed storage, pipeline/timing provenance, display mapping/orientation, source-ID and dimension pairing | `lumora_core_tests` |
| `Core.LatestValueSlot` | Capacity-one newest-value delivery, monotonic revision, explicit replacement accounting, close/stop wake within 250 ms, 100,000 publications | `lumora_core_tests` |
| `Core.BoundedQueue` | Constructor-fixed FIFO capacity, full rejection, ordered draining, close/stop wake within 250 ms | `lumora_core_tests` |
| `Core.Clock` | Independent monotonic and UTC time domains plus deterministic manual advancement | `lumora_core_tests` |
| Target dependency review | `lumora_core` has no Qt, OpenCV, spdlog, or pylon link dependency | `src/CMakeLists.txt`; Linux/Windows Debug/Release full builds |

## Milestone 3 verification map

| Test/check | Requirement covered | Automated evidence |
|---|---|---|
| `CameraConfiguration` | Complete capability-derived format descriptors, strict ROI increments, in-range numeric requests, reserved-trigger rejection, deterministic ordered violations | `lumora_camera_api_tests` |
| `CameraContract` | Compile-time vendor-neutral provider/device port shape, cancellation-aware immutable-frame retrieval signature, non-throwing cleanup signatures | `lumora_camera_api_tests`; closed-state and thread-confinement requirements are documented in `ICameraDevice.hpp` |
| `SimulatedCamera` | Stable discovery identity, initially closed/idempotent lifecycle, generated pattern determinism, applied-value quantization, streaming write permissions, ROI/format metadata, pool exhaustion, pacing, whole-retrieval budgets and cancellation | `lumora_camera_simulator_tests`, including `ProviderDiscoversOneStableDescriptorAndRejectsWrongId` and `LifecycleIsIdempotentAndCapabilitiesRequireOpen` |
| `SequenceSource` | Lexical P5 replay, exact payloads, big-endian U16 decoding, declared maxima 255/1023/4095/65535/1000, CRLF handling, invalid-file rejection, Loop/Stop/Error | `lumora_camera_simulator_tests`; committed synthetic fixtures |
| `FaultScript` plus `SimulatedCamera` fault/replay cases | Frame/time-indexed deterministic faults, persistent disconnect and explicit restoration, shared configuration validation, replay cursor preservation and transactional fault consumption | `lumora_camera_simulator_tests` |
| `SimulatedCamera.PatternAllocation` | Public pattern generation propagates allocation failure without termination from an incorrect `noexcept` boundary | Isolated `lumora_pattern_allocation_tests` executable |
| `Core.Clock` | Stop-aware steady/manual waits, independent real elapsed bound, extreme/zero/negative timeout and cancellation precedence | `lumora_core_tests`; filter includes `Clock*`, `ManualClock*`, and `SystemClock*` |
| `Core.Result` | Final failure result is constructed before fault state commits; failed copies cannot consume an occurrence | `lumora_core_tests` |
| Target dependency review | Camera API links only core; simulator links only camera API; no Qt/OpenCV/spdlog/pylon dependency in either camera target | `src/CMakeLists.txt`; simulator-only Linux/Windows Debug/Release configure/build |

## Milestone 4 verification map (acceptance pending)

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `ViewportTransform.*` | Exact aspect-preserving Fit, logical-pixel 100%, manual zoom limits, pan/resize and extreme geometry | 28 cases; merged cross-platform Debug/Release CI |
| `ImageViewport.*` | Immutable image ownership, staged/completed paint distinction, stride/orientation, letterboxing, interaction, DPR 1/1.25/1.5/2 logical-pixel rendering | 16 cases; merged cross-platform CI; image renders are not native Windows DPI checks |
| `WorkstationView.*`, `MainWindowSmoke.*` | Evaluation identity, scoped controls/shortcuts, paused UTC/age, stale/waiting indications and unavailable metadata | 14 workstation and 2 smoke cases; merged cross-platform CI |
| `FramePresenter.ResumeShowsNewestBundleNotBacklog`, `PauseBetweenStageAndPaintDiscardsReplacement`, `ResumeRecoversConsumedUnpaintedFrameWithoutPublication` | Latest-only delivery and freezing the completed image, including consumed-but-unpainted recovery | Merged cross-platform Task 4 UI tests |
| `FramePresenter.DoesNotCountOrOwnFrameUntilPaintCompletes`, `RejectionDoesNotAdvanceAcceptedIdOrFreshness`, `RepeatedAndOutOfOrderIdsDoNotReplaceCompletedFrame`, `ZeroIsAValidFirstFrameId` | Distinct slot/accepted/completed state and valid ID zero | Merged cross-platform Task 4 UI tests |
| `FramePresenter.ThirtyFpsBecomesStaleAtFiveHundredMilliseconds`, `OneFpsBecomesStaleAtThreeSeconds`, `InvalidActualFpsCannotReplaceCompletedFrame`, `TinyPositiveFpsDoesNotOverflowStaleDeadline`, `DelayedFrameIsStaleImmediatelyAfterResumeAndPaint`, `WallClockJumpDoesNotChangeFreshnessAge` | Exact monotonic freshness deadlines, invalid-FPS rejection, safe tiny-FPS arithmetic, delayed Resume and separate UTC | Merged cross-platform deterministic manual-clock tests |
| `FramePresenter.UnsupportedReplacementCannotPaintWhileHeartbeatBecomesStale`, `EventLoopStallBecomesStaleOnFirstRecoveryRefresh` | Status heartbeat independent of accepted images; reevaluation after UI-loop recovery | Merged cross-platform deterministic tests; no claim of drawing while the UI loop is blocked |
| `FramePresenter.DestructionClearsObserverAndDisconnectsViewIntents`, `ResetSourceAcceptsLowerIdAndResetsSessionState`, `ResetSourceReleasesOldPresenterAndViewportOwnership` | Teardown, source-session identity reset and ownership release | Merged cross-platform deterministic Task 4 UI tests |
| `SimulatedViewer.PresenterTimerStartStopAndCadence` | Actual Qt timer lifecycle/cadence with manual-clock decisions and bounded eventual event-loop progress | Integration coverage also invoked by the default short case; presenter unit tests contain no real-time sleeps |
| `SimulatedViewer.HarnessOperatorTextUsesQtTranslationBoundary` | Harness titles, error headings and operator summaries use extractable Qt translation source text; technical diagnostics remain data | Translation-lookup regression also invoked by the default short integration case |
| `SimulatedViewer.ResponsiveTenSeconds` | Real simulator worker publications/completed paints, interactive transforms/pause/resize, continued/late progress, bounded retained bundles, clean stop; also invokes timeout recovery and suppressed-publication checks | Default 60-second-bounded CTest entry; merged cross-platform CI |
| `SimulatedViewer.TimeoutIsObservableAndRecovers`, `SuppressedPreparedPublicationKeepsAcquiringAndBecomesStale`, `StoppedProducerDoesNotPublish`, `ClosedSlotSuppressesPreparedPublicationAfterAcquisition` | Typed timeout reporting/recovery, later terminal replacement, acquiring while prepared delivery is suppressed, publication close/stop boundaries | Named focused cases; required boundaries also covered by the default integration driver/helper calls |
| `SimulatedViewer.TimeoutPredicateRequiresExactTypedRecoverableError` | Only Acquisition + acquisition_timeout + recoverable is transient; mismatched category, code or recoverability is rejected | Focused regression also invoked by the default short integration case |
| `SimulatedViewer.StressTenMinutes` (`SimulatedViewer.Stress` CTest entry) | Same interaction/ownership/liveness driver over 600 seconds | Linux Release passed at `c4b7e7f` in 600.16 s; max 2 retained bundles, observable timeouts counted; Windows Release passed at `6c054a7` in 600.198 s, with 16,831 observed publications, 12,122 paints, max 2 retained bundles and 0 timeouts. See M4 execution evidence for counts/limits |
| `CMake.StressEvidence` | CTest stress-only selection, successful-output retention, failure/missing-test propagation, and preservation of previous evidence | Fast infrastructure fixture check; not a 600-second stress or hosted Windows result |
| Native Linux checks and tests-disabled build | XCB exposure, synthetic render inspection, harness excluded from normal app links and install scope | Local-only engineering evidence; not physical-display or Windows validation |

The [M3 record](milestones/m03-camera-api-simulator.md) retains every M3 acceptance criterion. The [M4 record](milestones/m04-preflight.md) separates completed automated verification from deferred native Windows 11 validation. M5 Task 1 development is authorized under the scoped exception; later M5 tasks and M6–M14 remain planned; production live composition, Windows installer validation (M13), and hardware acceptance (M14) are not supplied by the test harness.

## Milestone 5 Task 1 verification map (local only)

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `Application.CameraSessionStateMachine` | Pure camera state ordering, 192 state/event pairs, unchanged typed rejection, request/success separation and terminal cancellation | Four tests at `51cdc04`; Linux Debug/Release pass; worker identity/revision/device effects remain Task 2 |
| `Application.CameraCommandMailbox` | Fixed 32-command storage, priority under full load, in-flight fences and exact completion IDs, generation/lifecycle coalescing, stop/close/wakeup, bounded counters and concurrent admission | 22 tests at `51cdc04`; Linux Debug/Release pass; no production acquisition connected yet |
| Build/registration | Qt-free application linkage, explicit suites/watchdogs, tests-OFF/Basler-OFF application and state library build | Full Linux 27/27 Debug and Release; Windows CI for new source pending |

See the [Task 1 checkpoint](milestones/m05-camera-state-mailbox.md) for source, commands, independent review, counts and limitations. M5 Tasks 2–5 and native checks are not passed by this partial implementation.

No row in this file represents clinical validation, regulatory evidence, or authorization for diagnostic use.
