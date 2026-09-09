# Requirements traceability

This matrix records the evidence chain for the approved evaluation architecture. The original M1–M3 results refer to `2c88ec90ab58e3e6719be5e236dc49388dbc72dd`, verified in Linux/GCC and Windows/MSVC Debug and Release. M4 extensions below record merged Tasks 1–4 separately from remaining native visual acceptance; they do not overwrite that accepted baseline.

Current integrated implementation checkpoint: M1–M5 are merged on `main` through `f01b408`; the [progress summary](../PROGRESS.md) and [merged M5 verification](milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) record passing local and post-merge Linux/Windows Debug/Release checks. **M3 is accepted for engineering/evaluation**; the [M3 verification record](milestones/m03-camera-api-simulator.md#cross-platform-ci-verification) retains that accepted baseline. M4/M5 are not fully accepted. Earlier exact-source references below remain historical evidence; they are not claims that later integrated verification is missing. An implemented subset does not imply that its complete design section or a later milestone is accepted.

M4 Tasks 1–4 are merged at `aaf57a678f344864ca6e1f8333f5b774fed5da18` with passing cross-platform Debug/Release CI. Task 4 adds presentation and a non-shipping simulator harness. See the [M4 execution record](milestones/m04-preflight.md) for exact source/review evidence and remaining gates. **M4 is not fully accepted**; native Windows 11 visual/DPI checks remain pending. The follow-up at `6c054a7` passed matching Linux/Windows Debug/Release CI and Windows Release stress. The [2026-09-07 scoped exception](milestones/m04-deferred-windows-validation.md) permits M5 development only; it does not pass the manual gate.

| ID | Design source | Implementation/evidence | Linux/GCC | Windows/MSVC | Status |
|---|---|---|---|---|---|
| DES-1 | §1 Purpose and evaluation boundary | Compile-time evaluation release class, `MainWindowSmoke`, `Logging` | Verified | Verified | M1 baseline implemented |
| DES-2 | §2 Scope and success criteria | Simulator-only presets, pylon-off configure path, evaluation banner; broader product scope maps to M2–M14 | Verified for M1 | Verified for M1 | M1 subset implemented; later scope planned |
| DES-3 | §3 Architectural approach | Qt-free core, camera API and simulator targets; focused configuration/diagnostics/UI targets; typed errors and bounded exchanges | Verified for implemented subset | Verified for implemented subset | M1–M3 foundation implemented |
| DES-4 | §4 Repository and build structure | CMake target contract, pinned vcpkg manifest, Linux and Windows Debug/Release simulator presets | Verified | Verified | Cross-platform build foundation verified; packaging remains M13 |
| DES-5 | §5 Core frame and error model | Capability-derived pixel descriptors, checked layouts, immutable paired frames, reusable aligned buffers, typed errors, separate steady/UTC timestamps; transactional failure-result construction | Verified | Verified | M2 foundation and M3 extensions implemented |
| DES-6 | §6 Camera abstraction | `CameraConfiguration`, `CameraContract`, `SimulatedCamera`, `SequenceSource`, `FaultScript`, `SimulatedCamera.PatternAllocation` | Verified for API/simulator | Verified for API/simulator | M3 accepted; Basler adapter remains M6 |
| DES-7 | §7 Camera and viewer state machines | Simulator lifecycle, presenter and production startup/controller integration | M5 local and CI verification at `5e1c1ec` | M5 Debug/Release CI verified at `5e1c1ec` | Scheduled automatic recovery remains M12 |
| DES-8 | §8 Threading model | Joined camera/processing/control workers, UI presenter and background preferences; no per-frame queued Qt signal | M5 composed ownership/stalls locally and CI tested | M5 composed suites verified at `5e1c1ec` | Review complete; full acceptance remains separate |
| DES-9 | §9 Frame memory ownership and bounded buffering | Fixed production pools, capacity-one exchanges, acknowledged context replacement, final zero leases | M5 reviewed/local and CI verification | M5 Debug/Release CI verified at `5e1c1ec` | No full M5 or performance acceptance |
| DES-10 | §10 Processing pipeline | M5 processor port; merged M7 adds validated configuration, U16 mapping and paired pooled engine | M7 41/41 Debug/Release at `aaf93f0` | M5/M7 simulator checks verified | M7 reviewed; M8 Task1 algorithms reviewed and merged through PR #10 with Linux/Windows CI; Task2 CLAHE reviewed and merged through PR #11, live composition and full acceptance pending |
| DES-11 | §11 Rendering and workstation UI | M4 viewer plus production startup sidebar, explicit camera selection, translated warnings and synthetic live video | M5 controller/panel local and CI tests; native XCB QA passed | M5 CI verified at `5e1c1ec`; native Windows visual/DPI pending | M4 unaccepted; complete parameter editor M9 |
| DES-12 | §12 Capture and future recording | M10 capture plan; recording requires its own approved specification | Not run | Not run | Planned/deferred |
| DES-13 | §13 Configuration and presets | Schema2 startup persistence is integrated; [M9 preset state and schema3 migration](milestones/m09-presets.md) are implemented locally | M9 Debug/Release 56/56 plus X11 at `e3e427a`; evidence below | M5 Tasks4–5 CI verified at `5e1c1ec`; M9 CI pending | Preset selector/full editor remain later M9 work; admission is not persistence |
| DES-14 | §14 Diagnostics, metrics, and logging | `Logging`, pool/slot counters, simulator pacing-slip and bounded acquisition/processing snapshots; full diagnostics remain M11 | Task 3 snapshot local and CI verification | Acquisition and processing verified through `ccabaae` | Partial; overlapping raw-drop counters must not be summed |
| DES-15 | §15 Reliability and failure policy | M3 faults; M4 freshness; M5 blocked startup/Resume cancellation, composed stall/shutdown/factory-failure cases | M5 composed subset reviewed, locally and CI tested | M5 composed suites verified at `5e1c1ec` | M5 interim Retry is one idle attempt; automatic recovery M12 |
| DES-16 | §16 Test strategy | M1–M5 maps below; 100 bounded M5 lifecycle cycles and separate opt-in 600-second M4 stress | Exact-source records separate baseline/review/compiler fixes | M5 CI passed at `5e1c1ec`; historical Release stress passed at `6c054a7` | Native Windows and later reference/performance/hardware gates remain |
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

The [M3 record](milestones/m03-camera-api-simulator.md) retains every M3 acceptance criterion. The [M4 record](milestones/m04-preflight.md) separates completed automated verification from deferred native Windows 11 validation. M5 Tasks 1–2 are merged with matching Linux/Windows Debug/Release CI at `7295fb9`; [Task 3](milestones/m05-processing-worker.md) is merged as `2031848` after matching CI at `ccabaae`. [Task 4](milestones/m05-startup-preferences.md) and [Task 5](milestones/m05-live-integration.md) are implemented, independently reviewed and merged through PR #7 as `f01b408`. Their [integration checkpoint](milestones/m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) retains earlier verification at `5e1c1ec`, passing final PR-head checks, and post-merge Linux/Windows Debug/Release CI. The production simulator composition is distinct from the non-shipping M4 harness. Its exact-source review/fix and verification record follows; full M5 acceptance remains pending. M6 awaits its approved profile, M7 is merged through PR #8 under its [recorded continuation](milestones/m07-preflight.md), and [M8 Task 1](milestones/m08-tone-stages.md) is merged through PR #10 and [Task 2 CLAHE](milestones/m08-clahe.md) has a separate bounded development record; [M9 preset development](milestones/m09-presets.md) now has a scoped continuation while later M9 tasks and M10–M14 remain planned; Windows installer validation (M13) and hardware acceptance (M14) are not supplied by either simulator path.

## Milestone 5 Task 1 verification map

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `Application.CameraSessionStateMachine` | Pure camera state ordering, 192 state/event pairs, unchanged typed rejection, request/success separation and terminal cancellation | Four tests at `51cdc04`; Linux Debug/Release pass; worker identity/revision/device effects remain Task 2 |
| `Application.CameraCommandMailbox` | Fixed 32-command storage, priority under full load, in-flight fences and exact completion IDs, generation/lifecycle coalescing, stop/close/wakeup, bounded counters and concurrent admission | 22 tests at `51cdc04`; Linux Debug/Release pass; no production acquisition connected yet |
| Build/registration | Qt-free application linkage, explicit suites/watchdogs, tests-OFF/Basler-OFF application and state library build | Original checkpoint: full Linux 27/27 Debug and Release; merged Tasks 1–2 later passed matching cross-platform CI at `7295fb9` |

See the [Task 1 checkpoint](milestones/m05-camera-state-mailbox.md) for original source, commands, independent review, counts and limitations, and the [later merged CI record](milestones/m05-acquisition-worker.md#merged-cross-platform-checkpoint-2026-09-07). Task 2's separate verification follows; Tasks 3–5 and native checks are not passed by Task 1.

## Milestone 5 Task 2 verification map

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `Application.AcquisitionWorker` | Exclusive provider/device ownership; explicit configuration/confirmation/start; revision/session guards; latest-only raw publication; categorized failure/drop policy; single-attempt same-ID Retry; priority cleanup and direct cancellation | 28 tests at `1dc19e6`; Linux Debug/Release verified; the checkpoint records the narrow final-Start priority-path coverage limitation; production composition and source-context handoff remain Task 5 |
| Extended state/mailbox suites | Validated device-free initial state, priority-only selection preserving ordinary FIFO, closed-mailbox observation | Five state and 23 mailbox cases; 56 total application tests passing locally |
| Build/registration | Qt-free worker linkage, three explicit Application suites with 60-second watchdogs, tests-OFF/Basler-OFF library/application build | Full local Linux 28/28 Debug and Release; merged Tasks 1–2 passed matching Linux/Windows Debug/Release CI at `7295fb9` |

See the [Task 2 checkpoint](milestones/m05-acquisition-worker.md) for exact source, commands, process limitations, contract decisions and review status. Tasks 3–5 have separate verification below; their results are not supplied by Task 2. Automatic recovery in M12, native Windows checks and full milestone acceptance remain outstanding.

## Milestone 5 Task 3 verification map

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `Processing.Mono8PassThrough` | Exact full-range descriptor/FPS validation, padded rows/all 256 samples, Original-only Gray8 mapping/orientation, unchanged raw storage, shared-owner release and display-pool exhaustion | Eight tests at `914f568`; Linux Debug/Release; no high-depth normalization or enhancement |
| `Application.ProcessingWorker` | Newest input before/after an in-flight call, output replacement, typed failure accounting, waiting/in-flight cancellation, closed slots, invalid/same-ID substituted-owner results, one-shot lifecycle, overflow-safe counter arithmetic and standard/unknown exception containment | 17 tests at `3c59b11`; bounded latch-driven waits and no historical frame queue; private saturation arithmetic coverage, no allocator/mutex fault injection, whole-pipeline teardown or performance acceptance |
| Build/registration | Qt-free core-only processing linkage, application -> processing, two required CTest entries, tests-OFF/Basler-OFF app/library build | Full local native-inclusive Linux 30/30 Debug and Release; matching Linux/Windows Debug/Release CI passed at `ccabaae`, merged as `2031848` |

See the [Task 3 checkpoint](milestones/m05-processing-worker.md) for exact commands, TDD/characterization distinctions, corrected test-timing evidence, review status and the snapshot aggregation contract. Focused repeated runs are not the opt-in ten-minute milestone stress test. The fresh per-session, single-publisher output slot is an owner precondition; production source handoff, complete pool provisioning and presentation remain Task 5.

## Milestone 5 Task 4 verification map

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `Application.StartupPreferences` | Plain structural validation, complete capability comparison and stable-identity Resume guard | Eight cases; configuration-mode isolation corrected in reviewed `d113da9` |
| `Configuration.StartupPreferences` | Schema1 migration, schema2 round-trip/canonical fields, one-thread load/save, coalescing/drain, typed failures and exception containment | Eleven cases; injected I/O and real store/codec boundaries, not physical disk fault certification |
| `ConfigurationStore` | Existing Unicode/default/atomic-save and corruption/schema preservation | All ten cases restored by reviewed filter fix `5b4bff8`; initial `8e7d65e` filter ran only five |
| `CameraStartupPanel` | Confirmed-revision Start gate, requested/actual review, pending priority controls, typed intents and plain text | Four cases; independently testable panel, production hosting remains Task5 |
| Full builds/registration | Qt-free application direction, four focused entries, tests-OFF/Basler-OFF app | Initial native-inclusive Linux33/33 Debug/Release at `5b4bff8`; integrated Linux/Windows CI passed at `5e1c1ec`; native acceptance pending |

See the [Task 4 checkpoint](milestones/m05-startup-preferences.md) for exact commands, process limitations, reviewed fix, native panel QA and recovered generated-cache warning. Saved preferences alone do not authorize silent streaming; Task 5 supplies end-to-end startup and lifetime verification.

## Milestone 5 Task 5 verification map (acceptance pending)

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `LivePipeline.PauseKeepsAcquiringAndResumeJumpsToNewest`, `OneHundredBoundedLifecycleCyclesReleasePoolsAndResetSessions` | Independent acquisition during Pause, newest completed Resume, same-device ID continuity, replacement reset, final owner release | Deterministic simulator cycles; not ten-minute stress or hardware throughput acceptance |
| `IndependentCameraProcessingAndPresentationStallsUseCompletedPaintDeadline` | Independent stalls, exact 30-FPS freshness deadline, retained image and fresh-paint recovery, UI progress through a processing gate | Controlled 100 ms processing stall; no drawing claim while UI loop itself is blocked |
| `MatchingSavedRecordProbesIdleAndResumesOnlyOnOperatorIntent`, `ChangedReadbackCancelsResumeAndExplainsReview`, `UnusableSavedRecordsNeverAuthorizeResumeOrSubstituteCamera`, `FailedSaveWarnsWithoutRevokingExplicitSessionStart` | Explicit first/saved-run startup, exact identity, capability/readback review and warning behavior | Initial delayed-load gap corrected in reviewed `2acc2be`; dedicated cases below |
| `ConfirmationDuringSlowLoadSurvivesImagingAndDisconnect`, `CloseDuringSlowLoadDrainsTheAlreadyConfirmedPreferences`, `PermanentSaveAdmissionRejectionIsNotRetriedByOrdinaryPolling` | Delayed-load confirmation survives imaging/Disconnect/close; rejected admission is not labelled submitted or retried each poll | Task5 review-fix regression and characterization; final verification recorded in checkpoint |
| `ShutdownCapturesConfirmationCompletedSinceTheLastUiPoll`, `ShutdownCapturesResumeConfirmationWithoutAdvancingToStart` | Final capture before cleanup even without another UI poll, background drain and no late Resume Start | Reviewed `d113da9`; observed RED/GREEN plus Resume-close characterization |
| `CameraStartupPanel.WarningBodiesUseExtractableTranslationsAndTypedFallback` | Extractable literal translation lookup for warning bodies, meaningful typed fallback, PlainText and unchanged diagnostics | Exact-context/source translator regression at reviewed `d113da9`; no Qt dependency added to plain errors |
| Configuration slow-load/accepted-save cases | Single newest pending value, monotonic revisions, drain after safe whole-document load, failed/unpreserved load and exceptions never write defaults | Five added service cases; admission/attempt/durable success remain distinct |
| `DuplicatePendingDisconnectRetainsItsCompletionCorrelation`, `LifecycleCancellationDuringPreparationPreventsLateCameraOpen`, `DisconnectSupersedesStopAndBlockedResumeWithoutLateStart` | Stable priority outcomes, preparation cancellation, no late Resume continuation | Releasable deterministic gates, no automatic recovery |
| `ReplacementWaitsForTheOutstandingContextAcknowledgement`, `PipelineRetainsOldContextUntilReplacementBindingAcknowledgement`, `DirectStartRequiresAcknowledgedGenerationAfterConfirmation` | Fresh source and bounded retirement ownership, binding acknowledgement before Start | Public pipeline/controller boundaries, not a second frame history |
| `RefreshAfterDisconnectBindsWaitingSourceWithoutReconnecting`, `RemovalRetainsContextAndRetryOpensOnlyTheSameIdentityIdle`, `ResolutionChangeIsRejectedBeforeMutatingTheSession` | Explicit discovery/idle Retry, manual-Disconnect suppression, immutable M5 mode | Refresh deliberately clears the old image to Waiting when binding a fresh context |
| Composed shutdown/factory-failure cases | Camera-before-processing join, presentation-owner release while widgets live, terminal admission and typed failure containment | Normal stages plus blocked discovery/connection, Error, Paused and Reconnecting; secondary error reporting is best effort |
| `WorkstationController.*`, `ShippingCompositionProducesTheApprovedFullRangeOriginal` and native XCB QA | Hosted sidebar, state-correct actions, stable camera popup/explicit selection, exact shipping simulator mode, visible evaluation/paused indications | Initial five controller cases and production test passed; native Linux 900 x 600, not physical monitor or Windows DPI |
| Full builds/registration | Two new required CTest entries and fresh tests-OFF/Basler-OFF app without harness linkage | Reviewed behavior at `d113da9`: configuration26/26, startup8/8. Compiler-only correction `5e1c1ec`: local Debug35/35(22.39 s), Release35/35(17.84 s), Linux CI34+1/34+1, Windows CI34/34 both configurations, production builds passed; no open review finding |

See the [Task 5 checkpoint](milestones/m05-live-integration.md) for source, commands, TDD/characterization distinctions, review findings and remaining gates. None of these rows accepts the full M5 milestone.

## Milestone 7 verification map (merged implementation)

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `Processing.PipelineCompiler`, `Processing.ImageView` | Fixed order, owned immutable configuration, complete parameter/domain validation, bounded borrowed views and alias checks | Reviewed Tasks 1–3 source through `0eeb0b3`; cross-platform verification at PR head `34cf24a` |
| `Processing.NormalizeStage` | Exact declared-maximum scaling for native U8/U16, source immutability, temporal stability, padded/unaligned rows and malformed-sample diagnostics | Nine cases; deterministic integer arithmetic; mutation evidence distinguished from initial setup-only failure |
| `Processing.WindowLevelStage`, `Processing.DisplayMapper` | Canonical U16 window/level, clipped endpoints, fractional settings, exhaustive independent reference comparisons and terminal Gray8 mapping | Fourteen cases; behavioral RED then Debug/Release GREEN; exact references also passed Windows PR CI at `34cf24a` |
| `Processing.FrameProcessingEngine` | Paired source/revision, raw and retained output immutability, atomic activation, bounded workspace, failure recovery and Original semantics | Nine cases; reviewed `aaf93f0`; Linux/Windows Debug/Release at PR head |
| Native-depth `LivePipeline` and prepared `AcquisitionWorker` cases | Real 8/10/12/16-bit simulator flow, checked 44/54-byte resource sizing, prepared descriptor/ROI enforcement, source replacement and explicit startup after saved-mode changes | Existing lifecycle/stall/100-cycle cases retained; 148 focused cases in ten suites pass |
| Full builds and native smoke | All simulator tests, real XCB exposure, tests-OFF/Basler-OFF app and absence of test/harness/pass-through/pylon production linkage | Debug 40+1 and Release 40+1 passed at `aaf93f0`; fresh production build passed; Windows PR CI 40/40 each configuration at `34cf24a` |

The [M7 execution record](milestones/m07-preflight.md) retains commits, task reviews, commands and evidence limitations. Whole-branch review is approved with no required changes; milestone acceptance remains pending. M8 algorithms are not supplied by the configuration vocabulary; the separately authorized Task 1 implementations and evidence follow below.

## Milestone 8 Task 1 verification map (merged implementation)

| Test/check | Requirement covered | Evidence boundary |
|---|---|---|
| `Processing.ToneStages` (14 cases) | Sequential saturating U16 brightness/contrast, immutable gamma LUT, exact inversion, finite inclusive parameters, stage errors, source immutability, padded/unaligned rows and alias rejection | Reviewed `246a73a`; full-domain rational/integer references plus selected gamma scalar/literal anchors; Debug/Release passed |
| Full application and production build | Source/test registration, existing simulator regression suites, native X11 smoke, tests-OFF/Basler-OFF app | PR #10 passed Linux Debug/Release 41 headless +1 native each and Windows/MSVC 41/41 each; merged as `d6f94e1`, both main CI jobs passed; full M8 acceptance remains open |
| Allocation/cache ownership review | Constructor-owned const gamma table, success-path traversal without heap allocation | Code inspection and repeated outputs, not instrumented counts; cross-activation reuse and whole-frame allocation remain Task 5 |

The [M8 tone-stage record](milestones/m08-tone-stages.md) records scope, arithmetic decisions, TDD/mutation distinctions and commands. Task 1 leaves production activation rejection intact until actual composition exists. Remaining M8 tasks, designated Windows references and workstation performance gates remain open.

No row in this file represents clinical validation, regulatory evidence, or authorization for diagnostic use.


## Milestone 8 Task 2 contract checkpoint

The [CLAHE record](milestones/m08-clahe.md) specifies prepared OpenCV state, typed configuration/shape validation, byte-safe bridge buffers and independent analytical/reference tests. Functional implementation `f5d6642` passed all nine Processing suites and complete Linux application verification (43/43 in Debug and Release). Test correction `88a9ad8` passed twelve CLAHE cases in each configuration and cleared all independent task-review findings; final branch review and Linux/Windows Debug/Release CI passed, and PR #11 merged as `c0c4102`. The designated Windows reference/tolerance gate and actual zero-allocation gate remain open; no allocation claim is inferred from cached objects or repeated outputs.

## M9 Task 1 verification map

| Check | Requirement covered | Local evidence |
|---|---|---|
| `Application.PresetRepository` | Qt-free complete definitions; shared Standard/defaults; injected recipes; immutable/reserved IDs; owned atomic edits, classification, revisioned custom saves and restore | Eight cases pass Debug/Release; domain review approved |
| `Configuration.PresetCodec` | Embedded complete recipes; strict schema/fields/order; exact revision strings and Unicode; first-valid entry recovery; explicit selection; migrations and load-only warnings | Fifteen cases pass Debug/Release |
| `ConfigurationStore`, `Configuration.StartupPreferences` | Atomic invalid-save protection; installed-resource errors preserve files; typed presets and unrelated settings survive startup persistence | Both registrations pass Debug/Release |
| Complete simulator and native checks | Integrated application compatibility and actual XCB window exposure | Clean `e3e427a`: 56/56 plus X11 1/1 in each configuration |
| Tests-disabled app/resource linkage | Preset factory pulls embedded RCC resource without unit-test linkage | Release app builds; factory and RCC initializer present, GoogleTest symbols absent |

The [M9 record](milestones/m09-presets.md#verification-and-completion) retains source, timings, review and evidence locations. Whole-branch spec review is approved; independent quality review is approved with no actionable findings. This is local unpublished Task 1 implementation, with M9 hosted Windows CI and milestone acceptance pending. It does not implement the preset selector, processing controls, Compare, camera dialogs or fullscreen, and does not close M8 performance or deferred Windows/hardware gates.
