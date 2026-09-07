# M5 independent live pipeline preflight

**Date:** 2026-09-06

**Inspected source:** `6c054a7404c32a9be61c0922e6aa115e21eefe73`

**Status:** Approved implementation contracts; Tasks 1–5 are implemented, reviewed and merged under the scoped M4 manual-validation deferral. [Tasks 1–2](m05-acquisition-worker.md#merged-cross-platform-checkpoint-2026-09-07) and [Task 3](m05-processing-worker.md#merged-cross-platform-checkpoint-2026-09-07) retain their earlier matching cross-platform evidence. [Task 4](m05-startup-preferences.md) and [Task 5](m05-live-integration.md) merged through PR #7 as `f01b408`, with all recorded review findings resolved. The [merged integration checkpoint](m05-live-integration.md#merged-main-verification-and-branch-cleanup-2026-09-07) records passing post-merge Linux/Windows Debug/Release CI. Native Windows 11 checks and full M5 acceptance remain pending.

This record clarifies the [M5 plan](../../superpowers/plans/2026-04-25-m05-independent-live-pipeline.md) against the implemented M2–M4 interfaces. Read it with the [design](../../superpowers/specs/2026-04-25-xray-imaging-workstation-design.md) and [roadmap](../../superpowers/plans/2026-04-25-xray-imaging-workstation-roadmap.md). It is not implementation, test, or milestone-acceptance evidence.

## Entry gate and scope

The [2026-09-07 scoped exception](m04-deferred-windows-validation.md) authorizes M5 development before full M4 acceptance. Windows Release stress and matching Linux/Windows Debug/Release CI passed at `6c054a7`; native Windows 11 visual/DPI evidence remains pending, not passed, and mandatory before Windows release acceptance. This exception does not waive other gates. Tasks 1–2 were merged and pushed by separate authorization, with matching Linux/Windows Debug/Release CI at `7295fb9`. The owner first authorized the Task 3 push and local Tasks 4–5 continuation without waiting for CI, then separately authorized publishing and merging the completed M5 work. [PR #6](https://github.com/m4bulmagd/Lumora/pull/6) at `ccabaae` was merged as `2031848` after its own cross-platform checks passed. Tasks 4–5 then merged through [PR #7](https://github.com/m4bulmagd/Lumora/pull/7) as `f01b408` after independent exact-head CI passed; matching post-merge CI also passed. This integration authorization does not accept either milestone or waive native Windows checks.

M5 delivers a simulator-backed evaluation application: independent camera/processing workers, the existing presenter, minimal explicit startup controls, saved startup preferences, and bounded lifecycle/error handling. M9 expands the controls and per-camera settings; M12 adds scheduled automatic recovery. Neither milestone owns a replacement camera state machine or a second startup policy. Linux/GCC development and Windows/MSVC verification remain mandatory; Windows CI is not native Windows 11 visual, installer, or hardware evidence.

## Resolved implementation contracts

### 1. Camera commands and bounded mailbox

Keep the roadmap's `struct CameraCommand`; put its variant payload inside that struct, not in a conflicting alias. `CameraCommand.hpp` owns the command values and payloads. All are plain C++ values; no QObject, callback, borrowed widget pointer, or SDK object crosses this interface.

| Command | Payload and meaning |
|---|---|
| Discover | Refresh descriptors; never start acquisition. |
| Connect | Explicit stable `CameraId`; same connected ID is idempotent. A different connected ID requires Disconnect first. |
| ApplyConfiguration | Current session generation, complete `CameraConfiguration`, monotonically increasing request revision; never individual node writes. |
| ConfirmConfiguration | Current session generation and successful applied request revision; acknowledge the actual values displayed to the operator. |
| StartStream | Current session generation and confirmed applied request revision. Reject unconfirmed or superseded configurations. |
| StopStream | Stop the current stream and cancel pending start intent; does not destroy the device. |
| Disconnect | Cancel desired connection/start/recovery and close/destroy the device. |
| Retry | Retry the retained desired stable identity, subject to the milestone recovery policy below. Disabled after manual Disconnect. |
| Shutdown | Terminal cancellation of the worker, independent of mailbox fullness. |

Each command also carries a caller-assigned request ID for diagnostics. Session generation is an application control value, separate from core frame IDs and slot revisions; never add it to or reinterpret the M2 frame model. The worker rejects a stale-generation Apply/Confirm/Start with `CameraConfiguration / stale_camera_session` without mutating the new session.

`CameraCommandMailbox` owns fixed storage for 32 pending commands, a mutex, and a stop-aware condition variable. Do not adapt `BoundedQueue` by exposing its private storage: its FIFO `tryPush`/`waitPop` interface cannot atomically coalesce/reorder commands or check pending commands without blocking streaming. Keep that core FIFO unchanged for FIFO consumers.

The mailbox interface is `post(CameraCommand) -> core::Result<void>`, `tryPop() -> std::optional<CameraCommand>`, `waitPop(std::stop_token) -> std::optional<CameraCommand>`, worker-only `completeBarrier(std::uint64_t requestId) noexcept`, `close() noexcept`, `size() const noexcept`, and `stats() const noexcept`. A dequeued Stop/Disconnect keeps its admission fence until the worker calls `completeBarrier` after execution/cleanup; dequeue alone must not enable a racing Start/Connect. `post` acknowledges admission only, not device success. The worker publishes actual state, requested/applied revisions, and the most recent typed command outcome in an immutable `CameraStatusSnapshot`; the UI derives enabled actions from that state, not optimistic button clicks. M5 permits only one ordinary UI operation awaiting its outcome; Stop/Disconnect/Shutdown remain available. Coalescing/eviction counters are bounded aggregate diagnostics, not an unbounded result history. The read-only stats snapshot exposes saturating coalesced/cancelled totals so the worker can publish these admission-side facts without per-command history.

Admission, replacement, and selection occur under the same lock:

1. Shutdown supersedes every pending command, seals admission, and wakes an idle worker. Later posts return `Cancelled / cancelled`; repeated Shutdown is idempotent.
2. Disconnect precedes Stop and ordinary commands. It cancels all pending camera-specific commands, including Connect, Retry, Apply, Confirm, and Start. It cannot be refused because the mailbox is full.
3. Stop precedes ordinary commands. It cancels pending Start and Confirm commands and clears desired streaming; pending Apply may subsequently execute only in the stopped state. It cannot be refused because the mailbox is full.
4. Duplicate pending Stop or Disconnect commands coalesce. To admit a priority command when necessary, evict the oldest ordinary pending command and count that cancellation. Never evict a higher-priority command.
5. Coalesce Apply only for the same session generation and only within the ordinary-command suffix after the last Connect/Disconnect/Start/Stop barrier. Never move a configuration across a lifecycle barrier. Replacing Apply invalidates a confirmation tied to the replaced revision.
6. Other commands retain FIFO order. An unrelated full mailbox returns `ResourceExhaustion / camera_mailbox_full`; it does not block the UI or allocate additional capacity.
7. The controller disables new Start while Stop is pending and new Connect while Disconnect is pending. At the application interface, pending Stop rejects newly posted Start/Confirm; pending Disconnect rejects Connect/Retry/Apply/Confirm/Start as `Cancelled / cancelled`. A later explicit action is accepted only after the barrier completes. Already executing device operations cannot be preempted; the next operation must observe priority/cancellation before retrieval or starting a stream.

Closing a mailbox discards pending work and wakes waiters. Whole-worker shutdown also requests the worker stop token directly: it cannot depend on enqueueing one more ordinary command. `AcquisitionWorker::post(Shutdown)` requests cancellation after successful admission as well as waking the mailbox. A stable worker-owned `std::stop_source` supplies the same token to waits, discovery and retrieval, so cancellation does not race access to the owner-serialized `jthread` start/join handle. The thread remains owned and joined; this is not detachment. Idle acquisition uses `waitPop`; streaming acquisition uses `tryPop` before each bounded retrieval. Process priority work first and a bounded batch of at most 32 commands per iteration, then recheck stop/priority before retrieving. Do not spin on an empty or closed mailbox.

Task 2 adds `tryPopPriority()` for an atomic priority-only final check that leaves ordinary FIFO entries untouched, and a read-only `closed()` query for the worker's loop exit. Both use the mailbox's existing lock and preserve barrier/admission semantics; neither adds queue capacity.

### 2. State, startup guards, and milestone recovery policy

`CameraSessionStateMachine` is pure: no provider/device calls, clocks, persistence, or widgets. Define the state/event table before the worker. Events are facts reported by the worker; requested operations are validated before a device call and successful-state transitions happen only after that call succeeds. `CameraStatusSnapshot` includes state, desired and actual identity, session generation, capabilities, requested/applied configuration and revisions, confirmation/restore eligibility, desired streaming, timeout count, recovery mode, and latest typed error/outcome.

Task 2 extends that latest immutable snapshot with the latest discovered descriptors, saturating acquisition counters, an optional worker-observed last-acquired steady time (not a replacement for adapter frame timestamps), and `sourceReplacementRequired`. The pure machine's `fromInitialState` factory accepts only Disconnected, Error, or Reconnecting; the worker validates all remaining device-free initial-snapshot invariants before launch. These additions carry current facts, never unbounded command or frame history.

The transition tests must cover the following rows and every remaining state/event pair:

| Event/operation | Valid source and resulting state |
|---|---|
| Discover requested / completed | Disconnected -> Discovering -> Disconnected; descriptors are returned even when empty. Discovery failure -> Error. |
| Connect requested / open succeeded | Disconnected, Error, or Reconnecting -> Connecting -> ConnectedIdle. Same-ID Connect in ConnectedIdle/Streaming succeeds without restarting or recreating the device. |
| Open/capability/start failure | Error, with the typed failure and no partially active stream. Device cleanup remains on the camera thread. |
| Apply requested / succeeded | ConnectedIdle remains ConnectedIdle. Validate the entire request, apply/read back, publish actual values, invalidate previous confirmation. A rejected request leaves the prior valid applied configuration intact. |
| Confirm requested | ConnectedIdle remains ConnectedIdle only for the current applied revision; mark it eligible for Start. |
| Start requested / succeeded | Confirmed ConnectedIdle -> Streaming only after successful `startStream`; Streaming Start for the same revision is idempotent. |
| Stop requested / succeeded | Streaming -> ConnectedIdle; ConnectedIdle/Disconnected Stop is an idempotent no-op. In Error/Reconnecting, clear desired streaming without starting or recreating anything. |
| Manual Disconnect | Every nonterminal state -> Disconnected after cleanup; clear desired identity, confirmation, pending startup continuation, and recovery. Repeated Disconnect succeeds. |
| Removal / third consecutive timeout | Streaming -> Reconnecting after stop/close/destruction; retain desired identity for recovery, not the removed device. |
| Retry requested | Error/Reconnecting with retained desired identity -> Connecting; no desired identity is a typed invalid command. |
| Shutdown | Every state -> ShuttingDown; repeated Shutdown succeeds. All other events/commands after terminal shutdown are cancelled. |

Other state/event pairs return `CameraConfiguration / invalid_camera_state` unchanged. Device-operation failure events apply only to their outstanding operation; stale completions cannot mutate a replacement session. Apply while streaming is rejected in the minimal M5 UI/application contract; M9 adds the operator-confirmed stop/apply/verify/restart transaction. Tests distinguish this rejection from a camera rejecting an invalid configuration while idle. A failed stop/close still attempts remaining cleanup and records the error; it must not falsely report a healthy idle/streaming session.

| Worker result | M5 behavior | M12 extension |
|---|---|---|
| Successful valid retrieval | Publish, reset consecutive timeouts. | Preserve. |
| `Acquisition / acquisition_timeout` | First and second: metrics only, remain Streaming. Third: controlled teardown -> Reconnecting. | Schedule the approved five same-ID attempts. |
| Device removal / recoverable lost connection | Destroy device -> Reconnecting; visibly report that automatic recovery is not available in this milestone. | Scheduled discovery/open/restore; success requires a valid frame. |
| `InvalidFrame / invalid_frame`, or an unsupported individual frame | Count/discard before processing, retain stream; do not reset timeout streak without a successful valid retrieval. | Preserve. |
| `ResourceExhaustion` | Count the exact raw/display boundary, drop incoming work, no state transition or fallback allocation. | Preserve. |
| Rejected settings | Typed error, retain prior valid settings; if no valid configuration can be established, remain unable to Start. | Preserve. |
| Nonrecoverable open/start/negotiated-format failure | Error, no automatic retry. This is distinct from one malformed frame. | Preserve. |
| Processor failure | Discard that frame, count/error snapshot; no fabricated fresh bundle. M5 has only Original. | Add three-failure Enhanced-to-Original fallback. |
| Stop-token cancellation | Normal shutdown, not a reconnect trigger; discard unpublished work. | Preserve. |

M5 Retry performs **one operator-requested** discover/open attempt for the retained identity and returns to ConnectedIdle on success; failure enters Error. It never silently starts a stream. Revalidate capabilities, review/confirm settings, then require Start. No retry timer, sleeping retry loop, or five-attempt policy ships in M5. M12 replaces this explicitly interim recovery mode with the specified schedule and verified last-known-settings restoration; manual Disconnect remains authoritative. The non-shipping M4 feed's timeout retry behavior is not the production worker policy.

### 3. Minimal startup UI and persistence

M5 keeps the already planned first-run and later-run startup acceptance. Introduce `CameraStartupPanel` for selection/Refresh, Connect, requested/actual configuration review, Confirm, Start, Stop, Disconnect, Retry, and optional Resume Live. It is a small functional panel, not M9's complete parameter editor. Read-only review of the fixed supported M5 mode is sufficient; changing unsupported modes is not offered. All visible strings use Qt translation lookup, in English. No Record, patient, processing-slider, installation-profile editor, or clinical controls are added.

Enabled actions must match the existing worker state table: Refresh/Discover is available only from Disconnected, and Retry requires a retained desired identity in Error/Reconnecting. A discovery failure with no desired identity requires explicit Disconnect before Refresh; it cannot authorize a guessed-camera Retry. Task 5 corrects the panel's earlier broader enabled-state predicates without changing camera transitions or adding automatic recovery.

The camera selector shows a translated unselected placeholder until explicit activation. Unchanged descriptor lists retain their Qt model entries across controller polling, so an open popup remains usable; actual discovery changes still update the list. Visual selection must not imply consent while the authoritative selected identity remains absent.

Add `MainWindow::workstationView()` and explicit panel intents instead of searching children or fabricating signals that `WorkstationView` does not have. `FramePresenter` remains the sole owner of Pause/Resume and image freshness; `WorkstationController` handles camera/startup intents and separate camera status. It must not replace the presenter's `WorkstationStatus` with acquisition-derived freshness or connect a second handler that toggles viewer state twice.

Use a plain C++ `StartupPreferences` value in the application module containing: record version 1, stable `CameraId`, manufacturer/model/serial identity, the capability snapshot used for confirmation, requested and last-applied `CameraConfiguration`, and whether that configuration was confirmed. Transport addresses/display labels are not identity keys. A capability fingerprint is a **versioned canonical structural value**, not `std::hash`, raw struct bytes, pointer identity, vector order, or an unspecified JSON hash. Compare all pixel-descriptor fields, ROI bounds/increments, numeric bounds/increments/writability, and supported exposure/gain mode sets; sort set-like fields and reject duplicates/nonfinite values. Include identity separately. Store the canonical capability fields rather than introducing a crypto dependency. M9 can extend this versioned value without redefining its meaning.

Extend `ApplicationConfiguration` to schema 2 with an optional typed startup record; Qt JSON remains in the configuration adapter. Schema 1 -> 2 preserves existing six sections and initializes startup to absent, so existing installs require review, not inferred consent. Schema 2 round-trips the typed record and leaves unrelated preferences intact. Missing, malformed, future-version, unreadable, or unconfirmed records never authorize Resume. Preserve invalid files and prior valid saves through the existing `ConfigurationStore` policy; do not write over an unreadable source using guessed defaults.

`StartupPreferencesService` in the configuration module owns one background `std::jthread` for load/save, one pending coalesced immutable save value, and one latest immutable result/status slot. Its public operations are `start() -> Result<void>`, `postSave(std::uint64_t revision, StartupPreferences) -> Result<void>`, `latestStatus() -> std::shared_ptr<const StartupPreferencesStatus>`, `requestStop() noexcept`, and `join() noexcept`. The status value records load completion, the optional loaded record, latest attempted/successfully saved submission revisions, and typed warning; no frame or UI pointer is included. Save revisions are monotonically increasing service submission IDs, not source frame IDs or durable confirmation inferred from admission. No Qt object owned by the UI is touched from that worker. `main` owns the service, injects it into the controller, and keeps it alive until after final persistence. Load/save/JSON validation never runs on the UI, camera, or processing worker. Return save warnings without stopping imaging; successful in-memory confirmation is not reported as durably saved until save succeeds. Retain the loaded complete document for read/modify/write of startup so other sections survive. Cancel new submissions on shutdown, finish an active atomic save and the newest accepted pending value, then join. Stop tokens cannot interrupt an arbitrary filesystem call; do not claim the camera's 500 ms stop bound for persistence or the whole application.

Startup sequence:

The Task 4 implementation makes the adapter boundary explicit: `StartupPreferencesService` owns an `IStartupPreferencesIo` load/save port declared in its header; the production convenience constructor wraps the existing final `ConfigurationStore`, while tests supply deterministic I/O implementations. The plain status distinguishes load completion, the loaded record, attempted save revision, successful save revision and typed warning. The panel consumes an immutable authoritative camera snapshot plus controller-owned selection, fixed pre-Apply request, pending-operation and startup-eligibility inputs; it does not maintain a second independently writable camera state or confirmation flag. These constructor/presentation choices leave the service operations and startup sequence unchanged.

Task 5 review identified a slow-load confirmation race and extends the service's admission policy: after successful service start, one latest valid confirmed save may be accepted/coalesced while initial load is pending. Acceptance is not durable persistence. The background worker completes load first, preserves the whole document, and writes only if the source was safely loaded or preserved; otherwise it reports the accepted revision's failure without overwriting guessed defaults. This accepted value survives camera Disconnect and is drained after load during shutdown. New submissions remain rejected after stop. The controller records submission only on successful admission and must not retry a permanent rejection on every UI poll. This changes service admission/caller tests, not schema or operator confirmation; it avoids a duplicate UI-owned pending-save queue.

1. Start background load and worker discovery; show Waiting, no stream, and disable Resume until both relevant results are available.
2. With no valid saved record: require explicit selection -> Connect -> successful Apply/readback -> show actual values -> Confirm -> Start. Start is gated again on the camera worker, not just by button enabled state.
3. With a valid saved record and matching discovered identity: the startup controller may connect that identity **idle only** to read its current capabilities. This later-run probe is visible as Connecting/ConnectedIdle; it never applies/starts silently. No other discovered camera is substituted.
4. If the identity and canonical capability fingerprint still match and the saved request validates, expose Resume Live. One operator click authorizes apply/readback and Start only if the actual values still match the previously confirmed actual configuration. Otherwise remain idle and require review/Confirm/Start. Recheck generation/fingerprint at execution, not only when rendering the button.
5. Identity/capability drift, a changed actual applied value, an unsupported mode, or a load warning leads to review-required state with no automatic continuation. Manual Disconnect cancels the probe/Resume continuation; only a new explicit Connect reenables connection.
6. Save only successfully confirmed actual configuration, not an unacknowledged edit or a failed Apply. Disk-save failure warns and disables later-run eligibility for that unsaved revision; the current explicitly confirmed session can still start.

M9 expands this same panel/controller and schema with capability-driven editing, per-serial profiles, and the separate machine installation profile. Do not create a competing last-camera preference or startup state machine there. Basler machine-profile and hardware gates are unchanged.

### 4. Ownership, source handoff, and shutdown

`LivePipeline` owns camera/processing workers and each session's pools and raw/bundle slots. The provider and `IClock` are constructor-injected and outlive the pipeline. `main` composes these top-level objects; it does not separately create worker/pool owners behind the pipeline. `WorkstationController` owns the UI-thread presenter but no worker thread. The configuration service is a separate composition-owned persistence adapter, not part of camera/frame work.

A session context owns the raw/display/U16 pools and both slots; shared context handles keep the exchanges alive while a presenter is bound. At most one retiring and one prepared context may exist during a handoff; do not accept a second replacement until the first retires. Publish context changes through bounded latest control state, not per-frame Qt signals. A controller acknowledgement binds the new context before streaming is enabled.

Task 5 concretizes that control boundary with one pipeline-owned control thread and a bounded command mailbox. Normal UI posts, status reads and generation acknowledgements use brief synchronization; pool construction and worker retirement happen outside UI-visible locks. The existing acquisition worker has no completion callback, so the control thread uses a stop-aware bounded wait, woken by commands/acknowledgements, to observe its latest status. It does not busy-spin or send per-frame Qt events. A latest pipeline snapshot retains a stable ordinary-command outcome and priority completion separately; these are bounded current values, not histories. Priority admission explicitly cancels affected startup continuations because the acquisition worker's latest outcome may already have been overwritten before it is observed. Stop/Disconnect admission fences remain active through actual cleanup, including when Disconnect supersedes Stop.

The injected accepted request is immutable for M5. Checked arithmetic accounts for `44 * width * height` allocated pixel bytes: ten Mono8 raw, nine U16 processing and sixteen Gray8 display buffers; this is accounting, not an arbitrary global memory cap. Unsupported requests are rejected before device mutation, never silently replaced. A Result-returning processor factory consumes the session display pool and creates the owned processor on the control thread; the default is the existing Mono8 pass-through. This preserves the real processor seam for deterministic finite/releasable stall tests without test-only lifecycle hooks. Shared context handles expose presentation exchanges and pool lifetime, not worker ownership. If these unspecified interface choices prove unsuitable, adjustment is confined to pipeline/control snapshot, factory and controller consumers; the costs are coordinated lifetime/command tests and M9/M12 integration, not a new frame format or persisted schema.

The M5 acquisition worker is bound to one context. For a replacement, `LivePipeline` retains the last nonterminal control snapshot before retiring that worker and seeds the replacement with an explicit initial snapshot: only Disconnected, Error, or Reconnecting is permitted without an open device. Carry forward desired identity/manual-disconnect intent and error diagnostics, assign a new generation, and clear applied/confirmed revisions and transient device facts. Do not reconstruct desired connection from a terminal ShuttingDown snapshot or silently reset a manual disconnect. The old and new workers never access the provider concurrently. This internal worker retirement is not whole-application Shutdown; the UI observes the retained control state until the replacement reports its actual state. The constructor-injected raw pool is sized for the composition's accepted mode, validated again against capabilities before Start. M5 selects 640 x 480 in its simulator composition, not as a hardcoded limit in the reusable worker; M6 supplies the separately approved hardware mode and its checked pool layout.

Task 2 makes this boundary explicit: after destroying its first non-null device, a worker marks `sourceReplacementRequired` and cannot create another device in the old context, including after failed open. Connect/Retry that require replacement return `CameraConnection / camera_context_replacement_required`. Task 5's session owner handles the operator's explicit action by joining the old worker, creating fresh context/generation, clearing this flag in the validated device-free seed, and forwarding that action. A fresh worker seeded Reconnecting can execute Retry's single same-ID discovery/open attempt to ConnectedIdle. The intermediate replacement requirement must not be shown as an unexplained dead-end to the operator, and it never authorizes an automatic Start.

Because the pool exposes byte capacity rather than mode dimensions, the worker validates the first successful Apply/readback against capabilities and checked byte fit, then fixes the complete source descriptor and ROI for its context. Subsequent mode-changing requests are rejected before device mutation. The composition chooses the approved initial request and matching raw/downstream layouts; the worker does not infer image geometry from bytes or reinterpret initial requested configuration as a hidden mode parameter. Extending this to editable modes requires the explicit stopped-state resource-rebinding operation described below.

Distinguish these lifecycles:

| Operation | Device and frame lifetime |
|---|---|
| Viewer Pause | No acquisition/processing change; freeze only the completed painted frame. |
| Stop/Start same device | Stop retrieval, keep device, pools, slots, and frame-ID sequence. Retain the contextual image with existing paused/stale semantics. Start must use the still-confirmed configuration. |
| Disconnect | Stop/close/destroy device on camera thread; stop/join processing for that session. Retain at most the retired context needed for the last contextual image until reset or exit; do not claim zero pool use yet. |
| New device/session | Quiesce/join old publishers; create fresh raw **and** bundle slots, reset revisions/processing consumption, bind presenter with `resetSource`, then acknowledge handoff and permit new publication. Never compare new IDs with old IDs. |
| Final shutdown | Join workers and explicitly release all presentation/slot/snapshot owners before checking pool use. No new session is created to fake this check. |

For replacement, complete the old session stop/join before publishing the new context; the UI calls `FramePresenter::resetSource(newContext.bundleSlot)` while both contexts still live, releases the old handle after reset, and acknowledges the new generation. Stale acknowledgements cannot start a replacement session. The controller keeps polling freshness while waiting for a handoff; it does not let a successful camera open imply a fresh image. M5 exposes only its fixed mode and rejects a resolution-changing request without mutating the running context. When M9 enables such editing, add an explicit stopped-state resource-rebinding operation on the still-owning camera worker; do not recreate a same-device worker/device and accidentally reset its frame-ID sequence.

After Disconnect has joined both workers, an explicit Refresh may prepare a fresh context under that same acknowledgement guard and run Discover on its acquisition worker. Binding this fresh source clears the retired contextual image to Waiting; Refresh never connects, applies or starts a stream, and does not lift manual-Disconnect suppression of saved-session probing. If a fresh discovery worker already exists, Refresh uses it rather than allocating another context. This choice preserves one retirement order instead of introducing a separate retained-idle-camera-worker policy; its visible tradeoff is clearing the last image on post-Disconnect Refresh.

Final shutdown order:

1. Disable camera/startup actions and pending Resume continuations; stop accepting configuration-service submissions after capturing the latest valid preference.
2. Request camera cancellation, wake the mailbox, stop/close/destroy the device on its owning worker, and join it. No UI-thread camera calls, even in destruction.
3. Request processing cancellation, wake its wait, discard unpublished work, and join it. Each worker checks stop before consuming and before publishing; a cancelled `waitForNewer` can still return the slot's retained value.
4. On the UI thread, stop/destroy the presenter while the view is alive, clear the viewport's pending/completed QImages, and release controller/test snapshots. Stopping the timer alone is not release. Do not destroy a slot while the presenter still points to it.
5. Destroy active/retiring raw and bundle slots/contexts. Keep test-owned pool handles temporarily to assert `stats().inUse == 0`, then release them too. `LatestValueSlot::close()` wakes/rejects publishers but deliberately retains its current value; do not change this M2 contract to make the assertion pass.
6. Finish final preference persistence on its background adapter and join it; report failures. Flush logging and destroy remaining dependencies only after their consumers are gone.

The camera retrieval budget is 250 ms, with another 250 ms scheduling allowance in controlled acquisition-stop tests. It is not a universal bound on discovery/open/driver teardown, filesystem calls, arbitrary future algorithms, or application exit. Test camera/provider fakes cooperate with cancellation. The M5 processor performs bounded row-copy work and does not block on external I/O; finish/discard an in-flight call on stop. Blocking test processors must be explicitly releasable on all test exits. Never detach or force-kill a blocked worker.

### 5. Processor and deterministic tests

`IFrameProcessor::process(std::shared_ptr<const core::RawFrame>) -> core::Result<std::shared_ptr<const core::FrameBundle>>` remains the M7/M8 seam. No stop parameter is silently added to this established planned interface. `ProcessingWorker` takes a processor and both slots by constructor injection, owns its `jthread`, and provides `start() -> Result<void>`, `requestStop() noexcept`, and `join() noexcept`. Tests use those same methods; `stopAndJoin()` is not an additional production method.

Task 3 exposes the required bounded diagnostics through `snapshot() const -> ProcessingWorkerSnapshot`, a mutex-protected plain value with saturating `rawFramesSkipped`, `bundlesReplaced`, `processingErrors`, `displayPoolExhaustions` and optional typed `currentError`. The raw count is the revision gap when selecting an input for processing; it overlaps acquisition's `droppedBeforeProcessing` and must not be added to it as a second drop boundary. Bundle replacement uses the output slot's `replacedUnconsumed` result. Only `ResourceExhaustion / display_buffer_pool_exhausted`, emitted when display-pool acquisition fails, counts as display-pool exhaustion; metadata allocation and other failures retain their typed errors under processing errors. Normal stop/cancel is not a processing error. The current error clears after successful non-cancelled publication, not merely processor entry or a failed publication. The session supplies a fresh output slot with this worker as its sole publisher; tracking its returned output revision distinguishes successful publication from rejection after close without changing the core slot. Returning a snapshot may copy diagnostic strings and is not declared `noexcept`; no history, per-frame Qt events or processor-signature change is introduced.

For the production M5 simulator select MovingBar, seed `0x4C554D4F`, 640 x 480, 30 FPS, continuous acquisition, and explicitly `SimulationPacingMode::RealTime`. The simulator's default is Fastest and is not a suitable accidental desktop default. Declare the M5 simulator capability/configuration factory in `src/app/SimulatorComposition.hpp/.cpp`; never link/copy the non-shipping `SimulatorFeed` into the application. Use deterministic Manual pacing in worker policy tests.

Mono8 pass-through accepts the complete descriptor: canonical `Mono8`, encoding `0x01080001`, 8 valid bits, maximum 255, unpacked, least-significant alignment, `StorageType::UInt8`. It checks padded-row layout through existing factories and copies only active pixels to separate pooled Gray8 rows, with `DisplayMapping{0, 255, 255, 1}` and identity `Orientation{false, false, Rotation::Degrees0}`. Use `core::FrameBundle::create(raw, originalDisplay, nullptr, nullptr)`; never label the result Enhanced or mutate raw pixels. Descriptor mismatch returns `Processing / processing_format_not_available`; display-pool exhaustion stays `ResourceExhaustion` for separate accounting. Validate positive finite actual FPS before presenting; no fabricated clock or settings metadata.

Use the specified 10 raw, 9 U16 processing, and 16 Gray8 display buffer capacities, sized with checked arithmetic for the accepted mode before Start. The U16 pool has no consumer until M7. Record that distinction rather than silently changing the global capacity baseline. No fallback heap frame allocation occurs when a pool is exhausted. Mode changes must stop first and provision replacement pools before acquisition resumes.

Deterministic slow-processing test sequence:

1. Start with raw frame 1. Block inside the first `process` call and wait on an explicit entry latch (bounded test watchdog).
2. Publish 2, then 3 while call 1 is blocked. Release call 1. Its output is legitimately frame 1, or can be replaced before the test consumes it.
3. Wait for entry into call 2 and assert its input is 3, never 2. Release it and assert the final bundle source ID is 3.
4. An RAII test fixture releases every blocked call, requests worker stop, and joins even after a fatal assertion. Do not rely on sleeps, scheduling luck, or pretending an in-flight input can change.

Also test replacement **before any consume** by publishing 1/2/3 before starting the processing worker. Verify the first processor input is 3. Keep this separate from the in-flight test.

Qt integration uses an actual `WorkstationView`, `WorkstationController`, `FramePresenter`, injected clock, and simulator. Await completed paint through the existing render helper; acquiring or scheduling is not presentation. Compare acquired counter before/after Pause separately from displayed source IDs; do not compare a lifetime count to an ID. Verify Resume takes the newest retained candidate, including when no additional publication follows Resume. Independent camera, processing, and presentation stalls must use the M4 exact stale deadline, retained-context and pause rules, not arbitrary frame-count thresholds.

### 6. Build wiring and verification evidence

Every M5 task that creates source or tests modifies `src/CMakeLists.txt` and/or `tests/CMakeLists.txt` in the same change. Tests are explicitly registered; a missing target or zero matching tests is a setup failure, not a behavioral red test.

| Target | Sources and linkage |
|---|---|
| `lumora_application`, alias `lumora::application` | State/command/startup values first, then workers and pipeline; PUBLIC `lumora::core`, `lumora::camera_api`, and, when introduced, `lumora::processing`; public `application/include`; no Qt or pylon. |
| `lumora_processing`, alias `lumora::processing` | Processor interface and Mono8 adapter; PUBLIC `lumora::core`; public `processing/include`; no Qt or pylon. |
| `lumora_application_tests` | M5 application test files, PRIVATE application + simulator + `GTest::gtest_main`; no Qt test main. |
| `lumora_processing_tests` | Mono8 tests, PRIVATE processing + `GTest::gtest_main`. |
| `lumora_configuration` / tests | Typed startup codec/service; PUBLIC application for plain startup values, existing Qt Core serializer linkage; extend existing configuration test executable with service/codec tests. No application -> configuration dependency cycle. |
| `lumora_ui` / tests | Panel/controller headers and sources, AUTOMOC remains ON; PUBLIC application + existing UI linkage, PRIVATE configuration for the controller's persistence adapter. Add panel/controller tests to existing Qt test target. |
| Existing `lumora_integration_tests` | Append `LivePipelineTests.cpp`, preserve `QtTestMain.cpp` and M4 tests, add application/simulator linkage. No second executable with the same name and no second test main. |
| `lumora_app` | Composition factory + main, link application, simulator, configuration, UI, diagnostics. No viewer-harness support or test hooks. Builds with tests OFF and Basler OFF. |

Call `lumora_enable_warnings` on new targets and use target-scoped includes/linkage. Add focused CTest entries `Application.CameraSessionStateMachine`, `Application.CameraCommandMailbox`, `Application.AcquisitionWorker`, `Application.ProcessingWorker`, `Application.StartupPreferences`, `Processing.Mono8PassThrough`, `Configuration.StartupPreferences`, `CameraStartupPanel`, `WorkstationController`, and `LivePipeline`. Use 60-second outer watchdogs for unit/Qt suites and 180 seconds for the 100-cycle integration suite; internal deterministic waits must fail promptly with diagnostic state. Existing M4 stress remains opt-in and unchanged.

Qt CTest entries keep `QT_QPA_PLATFORM=minimal` and `QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>` for the matching configuration. Native Linux desktop smoke retains its separate XCB/display prerequisite. Do not replace this with offscreen or a machine-specific plugin path.

At each red/green step reconfigure after editing target lists, build the named target, list the focused test with `ctest -N`, then execute it with `--no-tests=error`. A behavior assertion must fail at red and pass at green. Helpers used in plan tests are test fixtures, not undeclared production methods. Add new sources to their target before using build failure as evidence; do not accept an unknown-target error or the unchanged M4-only integration suite as M5 verification.

Before acceptance: run complete Linux Debug/Release and matching Windows Debug/Release simulator CI, a separate tests-OFF/Basler-OFF application build, the native UI checks affected by the controls, deterministic failure/cancellation tests, and the 100-cycle lifecycle test. Record exact source SHA, commands, counts, and platform; update traceability and write separate acceptance only after the evidence exists.

## Review coverage

| Preflight finding | Clarification and required proof |
|---|---|
| Stop priority / FIFO mismatch | Section 1; concurrent admission, full-mailbox Stop/Disconnect/Shutdown, barriers and same-session coalescing tests. |
| Retained frames at shutdown | Section 4; every state shutdown, old-session handoff, legitimate contextual retention, final zero leases after releasing all owners. |
| M5/M9 startup overlap | Section 3; first-run, matching saved record, drift/readback mismatch, schema migration, failed load/save, and cancelled Resume tests. |
| M5/M12 recovery gap | Section 2; timeout 1/2/3, valid-frame reset, malformed-frame discard, manual Retry and Disconnect cancellation; automatic schedule remains M12. |
| Race-dependent freshness test | Section 5; separate pre-consume and in-flight tests using latches and RAII teardown. |
| Missing CMake/test registration | Section 6; named focused suites, correct test mains/plugins, tests-OFF application composition and cross-platform evidence. |

This review maps planned work only. No row is a claim that the corresponding M5 behavior or test has been implemented.

## Documentation verification (2026-09-06)

Self-review checked the six findings against the current core/camera/presenter interfaces and the design's M5/M9/M12 staging. Local validation checked 23 Markdown file/anchor links across the seven changed documents, balanced code fences, M5 Task 1–5 ordering, no checked implementation steps, existing Modify paths, absent planned Create paths, and removal of obsolete M5 command/viewer method spellings. `git diff --check` passed. Changes are documentation only; no application build, runtime test, Windows check, commit, or push was performed for this revision.
