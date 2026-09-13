# QML Renderer Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Checkpoint 3's shared bounded presentation protocol and a Qt Quick image renderer, preserving the Widgets workstation and recording measured Linux feasibility.

**Architecture:** A shared C++ presenter submits one immutable bundle and one exact ticket to a renderer sink. The sink reports timestamped completion, terminal failure, and explicit retirement through bounded events. Widgets and Quick implement the same sink; Quick uploads owned RGB32 staging images through public scene-graph textures, keeping pool leases out of long-lived GPU nodes.

**Tech Stack:** C++20, Qt 6.11.1, GTest, Qt Quick scene graph, Linux XCB/Xvfb, existing simulator/processing resource admission.

**Spec:** [migration design](../specs/2026-09-13-qt-quick-qml-migration-design.md), [Stage One Checkpoint 3](2026-09-13-qml-stage-one.md#checkpoint-3-presentation-protocol-and-renderer-feasibility). User approved proceeding after Checkpoint 2, `42c2340`. This detailed plan refines that approved checkpoint; Checkpoint 4 remains separate.

## Global Constraints

- “No automatic stream starts.” Camera, processing algorithms, configuration codecs and immutable frame storage remain in their existing C++ modules.
- “application/core/processing targets must not gain QML, Quick or Widgets dependencies.” Shared presentation tests run without Widgets/Quick.
- “Do not apply orientation or a second tonal adjustment in QML.”
- “A texture upload must not be described as zero-copy.” Record staging/conversion, texture storage and unknown driver overhead separately.
- “Compare must draw both planes from one validated bundle with one shared viewport transform and one presentation receipt.”
- “The first implementation should serialize presentation submissions while one ticket is in flight, reading the newest available source when ready.”
- Preserve both host-receipt age and elapsed time since a new source frame completed, using `max(500 ms, 3 actual frame periods)`; GUI delivery delay cannot renew freshness.
- Pause closes new-source admission immediately and is pending until the visible frozen bundle is known. Cancellation must prove the ticket was never consumed; consumed work needs a terminal event.
- Retire rendering owners before acknowledging replacement context. Hidden windows and teardown cannot depend on another swap.
- Keep the QML application labeled as an interface preview. Renderer fixtures/measurement runs are explicitly an experiment, not live camera integration or Windows acceptance.

## Shared interface contract

Task 1 produces `lumora/presentation/PresentationProtocol.hpp` and `FramePresenter.hpp`. These C++ values never travel through JavaScript:

```cpp
struct PresentationTicket {
    std::uint64_t sessionGeneration;
    std::uint64_t sourceFrameId;
    std::uint64_t presentationRevision;
    bool operator==(const PresentationTicket&) const noexcept = default;
};
struct PresentationSubmission {
    PresentationTicket ticket;
    std::shared_ptr<const core::FrameBundle> bundle;
    DisplayMode mode;
};
struct PresentationReceipt {
    PresentationTicket ticket;
    std::chrono::steady_clock::time_point completedAt;
};
struct PresentationFailure {
    PresentationTicket ticket;
    core::Error error;
    bool previousImageRetained;
};
struct PresentationRetired { std::uint64_t retirementId; };
using PresentationEvent = std::variant<PresentationReceipt,
    PresentationFailure, PresentationRetired>;

class IPresentationSink {
public:
    virtual ~IPresentationSink() = default;
    virtual core::Result<void> submit(PresentationSubmission) = 0;
    virtual bool cancelPending(PresentationTicket) = 0;
    virtual void retire(std::uint64_t retirementId) = 0;
    virtual std::optional<PresentationEvent> takeEvent() = 0;
};
```

All sink calls occur on the frontend thread. Render-thread activity is internal to the sink. `cancelPending=true` means the exact submission was never consumed, cannot complete, and its owners are released. False leaves it admitted until completion/failure/retirement. A failure is terminal for its exact ticket; `previousImageRetained=false` also invalidates displayed state. Invalidation after a completion may fail that exact completed ticket. Retired proves all old CPU/render/upload owners have been released and supersedes old terminal events. Events must not retain another bundle or create an unbounded queue.

Preserve receipt-then-invalidation ordering when a frame swaps and the surface disappears before GUI polling. A fixed mailbox of two terminal events is sufficient with serialized admission: a receipt followed by loss of that same image, or one terminal failure. Retirement atomically supersedes older events. Admission must reject until any prior terminal events are drained. If completed image A and admitted ticket B both lose the surface, fail B with `previousImageRetained=false` to settle both. Shared state also recognizes loss of its exact completed ticket when no newer ticket is admitted. Late failures from unrelated tickets/sessions cannot invalidate a replacement.

Image loss marks visible state unavailable/waiting but retains the last completed/frozen bundle internally for recovery. This uses the existing completed owner, not a third candidate. While paused with no new publication, retry that frozen bundle through normal submission on refresh; unavailable sinks reject without retaining new work. Re-presentation of the same source cannot increment its count or reset its original completion timestamp. Discard the retained recovery owner only at explicit retirement/reset or replacement by a new completed source.

The presenter consumes `LatestValueSlot<FrameBundle>`, `IPresentationSink`, `IClock` and a C++ session generation. Its public operations are `refresh()`, `pause()`, `resume()`, `setDisplayMode()`, `resetSource(slot, generation)`, `retire()`, plus read-only `status()`, `displayMode()`, `displayedFrameCount()`, `presentedBundle()`, mode availability, `error()` and `retirementComplete()`. `refresh()` drains events before considering source admission. Adapters can drain after a synchronous completion to publish immediate state. Reset retains old bundle owners until retirement completes, then binds the requested source; callers keep source contexts alive across that interval. No automatic asynchronous destruction/acknowledgement is implied by a destructor.

`ViewerState` gains `Pausing`. Mode intent while a ticket is in flight coalesces to the last requested mode; it does not replace a consumed ticket or create more in-flight work. The pending mode gets a new revision on the same retained bundle after completion. Only a newly completed source frame increments count or refreshes the source-completion deadline; mode repaint and duplicate receipts do neither. Rejected/invalid frames cannot advance accepted source identity. Source reset changes the generation and rejects old receipts, including reuse of low frame IDs.

### Task 1: Shared protocol and deterministic presenter

**Files:** Create `src/presentation/include/lumora/presentation/PresentationProtocol.hpp`, `FramePresenter.hpp`, `src/presentation/src/PresentationProtocol.cpp`, `FramePresenter.cpp`, `tests/support/ControlledPresentationSink.hpp`, `tests/support/PresentationTestFrames.hpp`, `tests/unit/presentation/PresentationProtocolTests.cpp`, `FramePresenterTests.cpp`. Modify presentation and test CMake files and `WorkstationStatus.hpp`.

**Interfaces:** Produces the shared interface above. Use `validatePresentation(const PresentationSubmission&) -> core::Result<void>` for shared ticket/bundle/mode/storage validation, called before either sink mutates its visible state. Controlled sink separates consume, render completion, GUI delivery and retirement; it is test support, not a second presenter.

- [ ] Write and run a behavioral red test with a buildable minimal interface:

```cpp
// fixture owns real pooled immutable frames, manual clock, slot, controlled sink
slot.publish(frame(1));
presenter.refresh();
EXPECT_EQ(presenter.displayedFrameCount(), 0u);
sink.consume();
clock.advance(20ms);
sink.complete(clock.steadyNow()); // hold GUI delivery
clock.advance(700ms);
sink.deliver();
presenter.refresh();
EXPECT_EQ(presenter.displayedFrameCount(), 1u);
EXPECT_EQ(presenter.status().freshness, FrameFreshness::Stale);
```

- [ ] Implement serialized admission and receipt identity. Test delayed rendering separately from delayed delivery; duplicate/wrong-generation/wrong-revision receipts; zero first frame ID; repeated/out-of-order source IDs; invalid FPS; malformed tickets; missing/invalid second plane and unsupported storage; newest-slot coalescing with no held candidate backlog.
- [ ] Test then implement Pause before consume (cancel succeeds), after consume (Pausing until terminal event), after render before GUI receipt, resume while Pausing, mode changes while paused, failed consumed work with/without retained image, and source reset during each state. Freeze visible/reported exact bundle, not a newest slot value.
- [ ] Test mode-only repaint does not advance count/deadline; missing Enhanced selects Original only when actually completed; shared orientation metadata comes from the completed image. Preserve existing tiny-positive-FPS and wall-clock-jump behavior.
- [ ] Test retained old source owners and delayed retirement using weak pointers; stale retirement IDs and late receipts cannot bind/reset another source. Destructor/caller retirement responsibilities are documented. Test invalidation and re-presentation of an already counted source without false count/freshness renewal.
- [ ] Build and run `lumora_presentation_tests` in `linux-gcc-debug-sim-qml`; save red and green output under `out/qa/qml-renderer/`. Self-review and commit only Task 1 files; independent spec/quality review follows.

### Task 2: Widgets adapter and regression coverage

**Files:** Modify `src/ui/src/FramePresenter.cpp`, its header, `src/ui/src/WorkstationController.cpp`, `WorkstationView.cpp`, `CameraStartupPanel.cpp`, and relevant tests. Add `src/ui/include/lumora/ui/WidgetsPresentationSink.hpp` / `src/ui/src/WidgetsPresentationSink.cpp` if the sink cannot stay a focused private adapter. Modify CMake accordingly. `ImageViewport` remains the concrete paint/transform owner; change it only for the sink's exact completion needs.

**Interfaces:** Consumes Task 1's shared presenter/sink. Widgets keeps its existing public presenter API and 60 Hz maximum timer cadence. Pass the real context generation from the controller; compatibility test constructors may use a local increasing generation. Widgets paint completion captures the supplied monotonic clock at the boundary, queues the exact receipt and immediately drains/publishes it from the wrapper. Clear/discard synchronously release old owners before reporting retirement/cancellation.

- [ ] Run the existing affected presenter, viewport, comparison, view/controller and camera workflow tests against the adapter. Retain pixel assertions and lifecycle ownership assertions.
- [ ] Replace only the old pending-mode replacement expectation with the serialized sequence:

```cpp
presenter.setDisplayMode(DisplayMode::Compare);
presenter.setDisplayMode(DisplayMode::Original);
paint(view); // exact previously admitted ticket settles
presenter.refresh();
paint(view); // last coalesced intent settles, same bundle
EXPECT_EQ(presenter.displayMode(), DisplayMode::Original);
EXPECT_EQ(presenter.displayedFrameCount(), 1u);
```

- [ ] Add `Pausing` text and disabled/toggle behavior consistently in camera and workstation status projections. Widgets cancellation is synchronous, but the shared value must never be rendered as completed Pause before it is true. Add status projection tests without altering layout style.
- [ ] Make controller handoff acknowledgement conditional on completed renderer retirement. Initial and replacement contexts remain unavailable to Start until the exact current handoff completes. Repeated polling must not restart retirement. Shutdown retires presenter/viewport before `completeRendererShutdown()`.
- [ ] Cover the Checkpoint 2 acknowledgement race deterministically using a private test-only hook immediately before `acknowledgeContext`, following the existing thread-local/RAII simulator hook pattern. The hook drives real pipeline replacement/disconnect after candidate validation; assert failed acknowledgement retains the newly bound candidate until a later renderer retirement, blocks Start and rejects stale completions. Enable the hook only in test builds; do not add a generic pipeline mock or change backend source.
- [ ] Run affected UI/integration groups, including the 100-cycle fixture and saved Resume/Stop/Disconnect flows. Keep every cycle and pool assertion. Save logs, self-review, commit adapter/test files, then independent spec/quality review.

### Task 3: Quick image sink and renderer experiment

**Files:** Create `src/qml/QuickImageItem.hpp/.cpp` and focused private render-state/image-preparation files as needed, `tests/qml/QuickImageItemTests.cpp`, `tests/qml/QuickRendererTestMain.cpp`, and a renderer measurement executable under `benchmarks/presentation/` or the QML test harness. Modify QML/test CMake. The interface preview scene stays unchanged.

**Interfaces:** `QuickImageItem` derives from `QQuickItem` and `IPresentationSink`; production calls remain C++ only. Consume Task 1's submission/events. The item accepts a supplied clock for deterministic boundary timestamps. A compiled registered QML type is permitted, but no pixel arrays or authoritative ticket IDs are writable in QML. Use shared `ViewportTransform` for Fit, logical 100%, zoom/pan and Compare geometry. C++ geometry operations are testable before QML controls are connected in Checkpoint 4.

- [ ] Write/run a red actual-window test: a known Gray8 ramp with padded rows and asymmetric already-oriented pixels must produce expected RGB output; `submit()`/texture creation alone must yield no receipt. Invalid/missing plane rejects atomically and retains the prior valid image. Test Original, Enhanced and same-bundle Compare.
- [ ] Prepare owned RGB32 staging images row by row using declared stride, with identical R/G/B and opaque alpha. No pool lease escapes the bounded submission/processing scope into a long-lived scene-graph texture. Use public `createTextureFromImage` and texture/image nodes; reserve atlas-independent textures and report conversion/storage costs. No extra orientation or tone operation.
- [ ] `updatePaintNode` consumes one admitted ticket only for a drawable, visible item. Correlate the consumed ticket with that rendered window frame and its `frameSwapped` signal; capture monotonic time there using a direct connection. Another control's repaint cannot re-complete a ticket. Protect render-thread state and bounded event delivery without touching GUI QObjects from the render thread.
- [ ] Implement pre-sync cancellation under the same synchronization as consume. Implement explicit retirement and terminal invalidation for hidden/zero-size item, unexposed/minimized/closed window, window move, scene-graph destruction/recreation, allocation/upload error and initialization failure. Use direct `sceneGraphAboutToStop`/`sceneGraphInvalidated`/node destruction cleanup and render jobs where appropriate. `NoStage` jobs can be discarded for unexposed windows, so they cannot be the sole retirement mechanism. All resource deletion occurs on the correct thread; stale jobs cannot clear a newer session.
- [ ] Prove the production shared presenter against this sink: Pause before consume, after consume/before swap, after swap/before GUI delivery; mode-only repaint; duplicate/unrelated swap; delayed reset and close without another swap; retained pool leases/texture counts return to baseline over repeated transitions. Avoid production-only test bypasses for receipt emission; any controlled render gate must exercise the real consume/swap transitions.
- [ ] Verify image pixel fixtures, Compare shared pan/zoom/clip, Fit and logical 100% under DPR 1 and 2, hidden/zero-size state, invalidation/recreation, and failure state. Capture both supported workstation-sized experiment windows. Offscreen/software coverage is separate from native XCB render-loop runs.
- [ ] Add bounded storage assessment/metrics for current/staged texture pairs and conversion images. Use checked arithmetic. Report source bundle lease counts and known requested bytes separately from Qt/driver overhead. Demonstrate `ProcessingPreparationOptions::externalSessionStorageBytes` accounts for known renderer storage in an experiment pipeline preparation without changing backend algorithms or expanding pools.
- [ ] Measure 640×480 and 2048×2048 Original/Compare over a bounded warmup/sample run. Record preparation/conversion time, admission-to-consume and consume-to-frameSwapped latency, GUI delivery delay, maximum concurrent texture/staging counts, nominal known storage and backend/version. Report percentiles and scope; do not claim processing FPS, physical scan-out latency, zero-copy or zero allocation.
- [ ] Build/run QML tests in Debug; run software/offscreen and native XCB software/OpenGL as supported by this host. Any unavailable backend is explicitly recorded. Self-review and commit; independent concurrency/spec/quality review follows.

### Task 4: Final verification and checkpoint record

**Files:** Create `docs/architecture/milestones/qml-renderer-experiment.md`. Update this plan, Stage One plan, `docs/PROGRESS.md` and relevant build guide commands. Evidence remains in `out/qa/qml-renderer/`.

- [ ] Run full Linux Debug/Release QML-enabled builds and `ctest --preset linux-gcc-{debug,release}-sim-qml -LE 'hardware|desktop' --output-on-failure`; run QML lint. Build the original Widgets application with QML OFF.
- [ ] Run affected native Widgets window/camera/workflow checks and inspect supported sizes. Run native Quick tests/measurements against the recorded backend; retain any first failure and its diagnosis. Baseline evidence is the source-bound 72/72 suites at `42c2340`, not an assumed fresh main run.
- [ ] Tie logs, measurements and captures to a source hash manifest and dependency versions. Inventory all required cases from Stage One Checkpoint 3 as pass/fail/not verified with reasons. Native Windows and physical GPU/display/hardware checks stay open when unavailable.
- [ ] Obtain one final checkpoint review of the entire change from `42c2340`; resolve required findings and rerun affected checks after fixes. Document remaining feasibility limits, memory accounting, timestamp semantics and owner-retirement ordering.
- [ ] Commit the locally verified checkpoint and leave the isolated branch/worktree available. No merge, push or Checkpoint 4 live composition is part of this approval.
