# Task 2: Widgets presentation adapter and regression coverage

Status: implementation complete, self-reviewed, and verified. Ready for
independent spec and quality review.

## Interface and behavior delivered

- The Widgets `FramePresenter` is now a thin wrapper around the shared
  `presentation::FramePresenter`. Its private `WidgetsPresentationSink` keeps
  one exact admitted submission and a fixed two-event mailbox. It passes the
  shared presentation revision to `ImageViewport` as the completion token and
  validates the token, mode, original plane, and enhanced plane on completion.
- Paint completion captures the supplied monotonic clock before queuing the
  exact receipt. The sink then notifies the wrapper, which immediately calls
  shared `refresh()` and publishes status, availability, and the completed
  display mode. Notification occurs only after `submit()` has returned and the
  shared presenter has installed its in-flight ticket.
- Cancellation discards the exact pending viewport presentation and releases
  its submission owner synchronously. Retirement clears the viewport, pending
  submission, and old mailbox entries before reporting the exact retirement
  ID. The wrapper explicitly retires and drains before destroying the shared
  presenter. Its existing three-argument constructor/reset remain compatible
  through a process-local increasing nonzero generation; the controller uses
  the real context generation.
- The existing precise 17 ms timer remains, with the monotonic
  `1'000'000'000 / 60` ns minimum delivery interval preserving the 60 Hz
  maximum cadence.
- `WorkstationController` starts each handoff retirement once, waits for
  `retirementComplete()`, and only then calls `completeContextHandoff()`.
  Repeated polls do not restart retirement. Candidate handoffs construct or
  reset the presenter with the real generation. Null handoffs and shutdown
  explicitly retire the renderer and release viewport owners before the
  coordinator acknowledges renderer shutdown.
- `Pausing` projects consistently in Widgets: the pause button reads exactly
  `Pausing…`, has accessible name `Pause pending`, and both it and the shortcut
  are disabled. The overlay reads exactly
  `PAUSING\nWaiting for current image`. The startup panel reads exactly
  `Viewer: Pausing — waiting for current image`.
- A private test-only, thread-local RAII coordinator hook runs immediately
  before `LivePipeline::acknowledgeContext()`. The regression uses the real
  pipeline to acknowledge the validated candidate, perform
  Connect/Disconnect/Connect replacement, and then let the stale coordinator
  acknowledgement fail. It proves Start stays blocked, the newly bound
  candidate remains owned, stale handoff completion is rejected, and a later
  exact renderer retirement releases the old candidate.

## Test-first evidence

The initial buildable Widgets behavioral run used:

```sh
env QT_QPA_PLATFORM=minimal \
  QT_QPA_PLATFORM_PLUGIN_PATH=.tools/qt-official/plugins/platforms \
  out/build/linux-gcc-debug-sim-qml/tests/lumora_ui_tests \
  --gtest_filter='FramePresenter.ModeChangesSerializeBehindTheExactAdmittedTicket:WorkstationView.PausingStatusDisablesToggleUntilTheExactPaintSettles:CameraStartupPanel.CameraAndViewerStatesRemainIndependent'
```

All 3 tests failed behaviorally before implementation:

- The first admitted Enhanced paint was replaced by Original (red value 128
  instead of the exact admitted 224), and the mode completed as Original.
- `Pausing` still exposed an enabled Pause control, hid the overlay, and
  emitted two pause requests.
- The camera panel continued to report the viewer as Live.

The trace is preserved in
`out/qa/qml-renderer/task-2-red-tests.log`. After implementation the same
focused run passed 3/3; see `task-2-ui-focused-green.log`.

The coordinator race test initially ran without the checkpoint and failed
behaviorally: `replacementCompleted` remained false and the old completion
unexpectedly succeeded. After adding the immediate pre-acknowledgement hook,
the first green attempt crashed in the fixture because the scoped hook remained
installed into a later handoff after its thread-local race pointer had been
cleared. The production path had completed the intended race; the failure was
the test fixture's hook lifetime. Restricting both the RAII hook and race pointer
to the one raced completion fixed the fixture. The focused final command was:

```sh
out/build/linux-gcc-debug-sim-qml/tests/lumora_presentation_tests \
  --gtest_filter='WorkstationCoordinator.AcknowledgementRaceRetainsBoundCandidateUntilLaterRendererRetirement'
```

It passed 1/1 in 16 ms; see `task-2-race-green.log`.

## Existing regression alignment

The first affected CTest run preserved all original assertions and found two
legacy integration sequences that assumed an admitted Widgets paint could be
replaced. In both cases, one paint correctly completed the exact previously
admitted ticket and immediately admitted the coalesced latest frame or Compare
mode. The tests now perform the second actual paint only when that captured
target has not completed. The freshness test still checks Current at 499 ms,
Stale at 500 ms, then Current after the new frame's actual completion; its raw
and display pools still assert zero owners. The stopped ROI/format test still
requires the exact Compare mode and retains all native-depth pixel, device,
preset, and pool assertions. The original failure is preserved in
`task-2-final-ctest.log`; focused corrected coverage passed 2/2 in
`task-2-integration-sequencing-green.log`.

## Final verification

```sh
cmake --build --preset linux-gcc-debug-sim-qml \
  --target lumora_ui_tests lumora_presentation_tests lumora_integration_tests \
  --parallel 3

ctest --preset linux-gcc-debug-sim-qml \
  -R '^(SharedFramePresenter|PresentationProtocol|WorkstationCoordinator|FramePresenter|ImageViewport|ComparisonViewport|CompareWorkstation|WorkstationView|CameraStartupPanel|CameraPanelLayout|WorkstationController|LivePipeline|InstallationController|CameraControls)$' \
  --output-on-failure

env QT_QPA_PLATFORM=minimal \
  QT_QPA_PLATFORM_PLUGIN_PATH=.tools/qt-official/plugins/platforms \
  out/build/linux-gcc-debug-sim-qml/tests/lumora_integration_tests \
  --gtest_filter='LivePipeline.OneHundredBoundedLifecycleCyclesReleasePoolsAndResetSessions'

git diff --cached --check
```

- The final focused targets built without warnings or errors.
- All 14/14 affected CTest registrations passed, including shared protocol,
  Widgets presenter/view/controller, viewport pixel coverage, coordinator, and
  full `LivePipeline` integration coverage. Saved Resume, Stop, Disconnect,
  shutdown-stage, camera-control, and installation flows remain included.
- The explicit 100-cycle lifecycle fixture passed 1/1 in 3542 ms with every
  cycle and pool assertion unchanged.
- Logs: `out/qa/qml-renderer/task-2-final-build-green.log`,
  `task-2-final-ctest-green.log`, `task-2-100cycles.log`,
  `task-2-ui-focused-green.log`, `task-2-race-green.log`, and
  `task-2-integration-sequencing-green.log`.

## Deviations and concerns

No production-interface deviation from the Task 2 brief remains. Two existing
integration tests gained a conditional second physical paint because the shared
protocol serializes exact admitted tickets; no freshness threshold, completed
label, pixel value, lifecycle cycle, or ownership expectation was weakened.

The Widgets sink has no asynchronous render thread: `ImageViewport::present()`
validates and prepares synchronously, and its later paint callback supplies the
terminal receipt. Its fixed mailbox overflow path is unreachable under the
serialized admission rule because readiness requires both no pending ticket and
no undrained event; retirement atomically clears older events first. Quick/QML
renderer behavior, GPU upload evidence, and benchmark conclusions remain Task 3
scope.
