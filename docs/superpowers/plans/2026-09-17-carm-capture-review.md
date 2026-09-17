# C-arm Capture, Reference, and Saved-image Review Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement CR-1 through CR-3: exact-frame snapshot capture, a persisted evaluation-session library, an independent saved reference, and non-destructive processing of saved source samples.

**Architecture:** Keep the existing live acquisition, processing, and acknowledged presentation path. Add transactional capture storage and a bounded catalog reader, then give the reference pane its own decoded images, presenter, viewport, and processing worker. Persisted capture identity is the connection between these systems; gallery and review never retain live frame-pool leases.

**Tech Stack:** C++20, Qt Quick/QML and Qt Core, existing OpenCV PNG support, GoogleTest/Qt Test/CTest, standard filesystem and worker threads; no database, patient, or DICOM dependency.

**Spec:** `docs/superpowers/specs/2026-09-17-carm-workstation-design.md`; the existing capture artifact contract remains `docs/superpowers/plans/2026-04-25-m10-snapshot-capture.md`.

## Global Constraints

- This is an evaluation workstation: display `EVALUATION — NOT FOR CLINICAL USE` and do not acquire or store real patient data.
- These documents authorize planning only. Unchecked work is not implemented, milestone acceptance is not granted, and clinical approval is not granted.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this work touches them.
- Capture Live always targets the acknowledged displayed or paused **Live** bundle, even while Reference or its inspector has focus. Saving a review edit uses the separately named **Save copy** action.
- Capture queue capacity is exactly four outstanding retained jobs, including the active encoder; a fifth pending request returns `CaptureBusy` without blocking.
- Preserve Original/Processed/Both artifact sets and the original M10 failure, Windows, hardware, performance, and release gates.
- Pin Reference selects a persisted capture and locks it after successful display. It does not save Live. Lock blocks automatic reference changes; explicit browsing and pinning remain available while locked.
- A successful Capture Live follows into Reference only after verified publication and only when automatic following is permitted. A known pre-publication failure creates no gallery capture. A `CommitIndeterminate` result preserves its key/path with an uncertain catalog/status entry; it cannot replace Reference or look successfully saved until reconciliation verifies it.
- Live and Reference have independent frame owners, viewport transforms, display modes, labels, and processing state. Compare means same-frame Original/Enhanced inside the selected pane; it does not replace the other pane.
- Evaluation sessions use opaque identities and neutral test labels only. They are not patients, studies, orders, examinations, or clinical records.
- Keep `captureSessionId` separate from `browsingSessionId`. Opening a library session never redirects Live saves; jobs bind their destination at admission. Cross-session browsing suspends auto-follow, and Save copy uses its parent's session. Read-only legacy groups remain view-only.
- Linux/GCC verification and matching Windows/MSVC simulator CI are required before accepting an implemented slice; native Windows, hardware and earlier formal milestone gates remain separate.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

---

## Current code and boundaries

Verified against the source on 2026-09-17:

| Existing code | Relevant behavior | Planned extension |
|---|---|---|
| `src/presentation/include/lumora/presentation/FramePresenter.hpp` and `src/presentation/src/FramePresenter.cpp` | `presentedBundle()` returns the acknowledged displayed owner; sink receipt/retirement controls replacement | Capture from this owner and expose an atomic presentation snapshot with ticket and displayed mode |
| `src/qml/QmlWorkstation.cpp`, `ViewerAdapter.*`, `QuickImageItem.*` | One live presenter/item; asynchronous context retirement; single live ViewerAdapter | Compose capture plus an independent saved-image review presenter/item; retain shutdown receipts |
| `src/core/include/lumora/core/Frame.hpp` | Immutable raw, enhanced native U16, and oriented display planes; pipeline version but no complete historical recipe | Attach immutable complete recipe provenance to publication, prepared outside the processing hot path |
| `src/processing/` | Validated fixed-order engine with checked preparation and its own scratch | Reuse algorithms with separate review pools, engine, worker and configuration revisions |
| `src/qml/ProcessingAdapter.*` | Commands the live `WorkstationCoordinator` | Remains Live-only; add a distinct review adapter |
| `src/capture/`, `src/review/` | Absent | New storage and saved-image review modules |
| M10 plan | PNG, manifest and capture transaction already specified; old UI file references were Widgets-oriented | Execute its backend contract with this plan's Qt Quick integration and provenance prerequisite |

Do not implement the same capture backend twice: Task 2 below executes M10 Tasks 1–4 and its worker behavior once. Session catalog and review are additions, not retroactive M10 acceptance requirements.

## File map

New paths below are proposed files, not claims that these modules exist. Add target wiring with the first task that uses each target.

| Responsibility | Files |
|---|---|
| Immutable frame recipe provenance | New `src/core/include/lumora/core/PipelineSnapshot.hpp`; modify `src/core/include/lumora/core/Frame.hpp`, `src/core/src/Frame.cpp`, `src/core/src/FrameObjectPool.cpp`, `src/core/src/FrameObjectPoolState.hpp`, `src/processing/src/FrameProcessingEngine.cpp`, `src/processing/src/FrameExecution.cpp`, `src/processing/src/ProcessingPreparation.cpp`; new `src/processing/src/PipelineSnapshot.cpp` |
| Shared recipe wire codec | Extract existing `src/configuration/src/PipelineDefinitionCodec.hpp/.cpp` into proposed `src/serialization/include/lumora/serialization/PipelineDefinitionCodec.hpp` and `src/serialization/src/PipelineDefinitionCodec.cpp`; update existing configuration callers and `src/CMakeLists.txt`; focused codec tests retain existing wire fixtures |
| Existing M10 capture contracts, worker, PNG and transactions | New `src/capture/CMakeLists.txt`, `src/capture/include/lumora/capture/{CaptureTypes,ICaptureStore,ICaptureEncoder,OpenCvPngEncoder,CaptureManifest,IStorageTransaction,FilesystemCaptureTransaction,CaptureService}.hpp` and matching implementation files named in M10 |
| Evaluation sessions and catalog | New `src/capture/include/lumora/capture/{EvaluationSession,CaptureCatalog}.hpp`, `src/capture/src/{EvaluationSession,CaptureCatalog}.cpp` |
| Exact capture application boundary | New `src/presentation/include/lumora/presentation/CaptureController.hpp`, `src/presentation/src/CaptureController.cpp`; modify `FramePresenter.hpp/.cpp` |
| Capture-held live context admission | New `src/application/include/lumora/application/CaptureRetention.hpp` and `src/application/src/CaptureRetention.cpp`; modify `LivePipeline.hpp/.cpp`, `LiveResourcePreparation.cpp`, `WorkstationCoordinator.hpp/.cpp` and lifecycle tests |
| Renderer envelope and saved-image presenter | New `src/presentation/include/lumora/presentation/{PresentationImages,ReferencePresenter}.hpp`, `src/presentation/src/ReferencePresenter.cpp`; modify `PresentationProtocol.hpp/.cpp`, shared `FramePresenter.cpp`, `src/qml/QuickImageItem.cpp`, legacy `src/ui/src/FramePresenter.cpp` and shared sink/test fixtures |
| Reference reader/controller and bounded storage | New `src/review/CMakeLists.txt`, `src/review/include/lumora/review/{ReviewTypes,CaptureReader,ReviewController}.hpp`, matching `src/review/src/*.cpp` |
| Review editing and derived save | New `src/review/include/lumora/review/{ReviewProcessingService,ReviewEditSession,DerivedCaptureWriter}.hpp`, matching `src/review/src/*.cpp` |
| Qt Quick bridge | New `src/qml/{CaptureAdapter,GalleryModel,ReviewAdapter,ReviewProcessingAdapter}.hpp/.cpp`; modify `QmlWorkstation.hpp/.cpp`, `ViewerAdapter.hpp/.cpp`, `main.cpp`, `src/qml/CMakeLists.txt` |
| Functional UI co-delivered with CR-1–CR-3; CR-4 adds dual display | New `src/qml/qml/{WorkstationHeader,ImagePane,CaptureFilmstrip,ContextInspector,ReferenceProcessingControls}.qml`; modify `src/qml/qml/Main.qml` |
| Build/tests and evidence | Modify `src/CMakeLists.txt`, `tests/CMakeLists.txt`; create tests named per task; new `docs/architecture/milestones/carm-capture-review.md` during implementation |

The review target links core, processing, capture and required Qt Core codecs. Presentation composes review state. QML consumes adapters. Capture/review workers do not depend on a window, QML item or camera-command policy. Keep capture/session persistence out of the UI preferences file. UI component names and bindings are shared with `2026-09-17-carm-workstation-ui.md`; co-deliver those CR-1–CR-3 UI tasks rather than building duplicate temporary controls.

## Shared contracts and resource limits

Contract snippets and tests below are implementation pseudocode. Use the existing `core::Result<T>`/`core::Error` conventions and actual fixture builders when implementing; names introduced here define the intended module seam.

```cpp
// capture/CaptureTypes.hpp: preserve M10 CaptureId and decimal JSON encoding.
using CaptureId = std::uint64_t;                 // zero invalid
using SessionId = std::string;                   // persisted opaque UUID text
struct CaptureKey { SessionId sessionId; CaptureId captureId; };
struct SourceIdentity {
    std::string applicationRunId;                // distinguishes app restarts
    std::uint64_t liveContextGeneration;
    std::uint64_t sourceFrameId;                 // zero remains valid
};
struct CaptureDescriptor {
    CaptureKey key;
    SourceIdentity source;
    std::filesystem::path committedDirectory;
    bool originalAvailable;
    bool enhancedAvailable;
};

// review/ReviewTypes.hpp
using ReviewRequestId = std::uint64_t;
struct ReviewSelection {
    CaptureKey key;
    std::uint64_t selectionRevision;             // rejects stale async results
};
struct ReviewBudget {
    std::size_t totalBytes = 256 * 1024 * 1024;   // checked before decode/prepare
    std::size_t thumbnailBytes = 8 * 1024 * 1024;
    std::size_t maximumCatalogRows = 128;
};
```

Persist `SessionId`, `CaptureId`, acquisition UTC, source identity, evaluation release class, camera/source identity, image descriptor, capture mode and recipe in every capture manifest. Store numeric frame/capture/context/revision identifiers as decimal JSON strings. Session identity survives reopening and never aliases a live camera generation. Capture IDs are scoped by `SessionId`; final directory naming includes both to remain unique across sessions and application restarts. The storage worker pre-reserves a bounded range of 64 IDs by atomically advancing the session counter before admission uses any of them; interrupted reservations leave harmless gaps. Admission takes an ID in constant time and returns unavailable if the next reservation has not completed. The capture ticket and capture ID are distinct; neither is inferred from a filename. Task 2 first supplies this reservation against a capture-root counter; Task 3 moves new reservations into the selected persisted session without changing M10 pixel/artifact semantics. A root writer lock rejects a second writer rather than racing reservations; read-only reopen stays possible.

Default resource policies:

- Four capture bundle leases maximum including active storage; separately reserve one of four terminal-result slots during admission. Release pixel owners before feedback reaches QML, but free a result slot only on consumption. Reject new admission if either capacity is exhausted; never overwrite a terminal result. Slow disk cannot consume a fifth live-pool lease.
- Retained live contexts count against the one-old-plus-one-candidate bound through a new generation-scoped capture-retention registry. The current renderer acknowledgement alone does not track storage leases. A second resource replacement is unavailable until the capture-held old context retires; compatible settings that reuse resources keep their existing policy. Do not delay new-context renderer acknowledgement/Start merely to account for the old capture, or allocate extra contexts to bypass a blocked writer.
- Catalog pages contain at most 128 metadata rows; no full-resolution pixels. One active and one latest queued page/open request; superseded requests return cancellation. Metadata parsing is capped at 1 MiB per manifest; checked image dimensions and decoded bytes precede PNG allocation.
- Reference has at most one current decoded image set and one replacement during handoff. Review processing has one active request plus one replaceable latest draft. Both sets, scratch, thumbnail cache, renderer images and textures are charged to the 256 MiB review budget before admission. Do not decode into live pools.
- Save copy may additionally pin an older review result in the shared four-job storage service. Charge every distinct pinned owner and its source/processing storage to the same 256 MiB ledger until completion/cancellation releases it; deduplicate shared-owner accounting. If the next edit or save cannot fit, reject that request without losing the current draft. The current/replacement pair is not permission for unaccounted storage-held results.
- Thumbnail cache is at most 8 MiB, evicted by least recently used entry; derive previews on a background worker. No full-resolution prefetch in CR-2. Cache eviction never deletes source captures.
- Reuse processing preparation to reject layouts/recipes whose checked peak reservation exceeds budget. Return `review_resource_limit` and retain current Reference. Budget/configuration changes are stopped review-context replacements.
- Storage, directory scans, PNG decode, pipeline preparation and saved-image processing run off the GUI/acquisition/live-processing threads. UI callbacks only submit bounded requests and consume immutable results.
- Independent threads do not reserve CPU or memory bandwidth for Live. Initially prepare the review engine with `ProcessingPreparationOptions::cpuExecutionSlots = 1`. Coalesce edits and defer/throttle new review requests against a measured Live regression budget; keep the last complete Reference visibly pending. Never automatically disable Live stages, change its recipe or reduce source FPS. An in-flight stage need not be preemptible, so the admitted hardware/profile must pass concurrent load checks.

## CR-1 — Exact-frame capture and evaluation-session catalog

### Task 1: Preserve complete provenance of the acknowledged Live frame

**Files:** Core/processing provenance paths in the file map; `src/presentation/include/lumora/presentation/FramePresenter.hpp`, `src/presentation/src/FramePresenter.cpp`; `tests/unit/processing/FrameProcessingEngineTests.cpp`, `tests/unit/presentation/FramePresenterTests.cpp`; new `tests/unit/core/PipelineSnapshotTests.cpp`; appropriate existing target lists.

**Interfaces:** Consume existing `PipelineDefinition`, `FrameBundle`, `PresentationTicket` and sink receipts. Produce `core::PipelineSnapshot`, a nullable immutable owner on `FrameBundle` for compatibility with noncapturable test/legacy publishers, and `FramePresenter::captureSnapshot() -> Result<PresentedCaptureSnapshot>`.

- [ ] Define a neutral immutable snapshot in core, with no core dependency on processing or Qt. Each stage carries its stable identifier, enabled flag, and typed named numeric/boolean/string parameters; include schema/order/configuration revision and preset identity/label where known. Build the snapshot once when preparing/activating the validated recipe, share it into produced bundles, and bound its storage with the existing prepared-configuration owners. Missing provenance returns `capture_provenance_unavailable`, never current UI defaults.

```cpp
struct PipelineParameter { std::string name; std::variant<bool, std::int64_t, double, std::string> value; };
struct PipelineStageSnapshot { std::string id; bool enabled; std::vector<PipelineParameter> parameters; };
struct PipelineSnapshot { PipelineVersion version; std::vector<PipelineStageSnapshot> stages;
                          std::optional<std::string> presetId, presetLabel; };
struct PresentedCaptureSnapshot {
    PresentationTicket ticket;
    std::shared_ptr<const core::FrameBundle> bundle;
    DisplayMode displayedMode;
    ViewerState playbackState;
    FrameFreshness freshness;
    std::chrono::milliseconds frameAge;
};
// captureSnapshot drains completed sink events before selecting the last
// acknowledged visible bundle; never reads the latest processing slot.
```

- [ ] Add failing assertions for a paused recipe A frame after live configuration B becomes active; capture retains A's exact parameters and rendered orientation. Add pending-ticket, failed-presentation-with-previous-image, no-frame and context-retirement cases.

```text
present(frame=41, recipe=A); acknowledge(41); pause()
activate_live_recipe(B); publish(frame=42, recipe=B)
snapshot = captureSnapshot()
expect(snapshot.frameId == 41 && snapshot.recipe == A)
expect(snapshot.ticket == receipt_for_41)
retire_source(); expect(captureSnapshot().error == capture_unavailable)
```

- [ ] Run the new focused tests and confirm failure for the missing snapshot behavior, then implement conversion/validation and atomic receipt-bound snapshot selection. Validate all named stage parameters round-trip through the current `PipelineDefinition` semantics; reject unknown order/schema versions when reopening for edits. Preset identity may be null, but numerical recipe parameters may not be omitted. This snapshot is immutable provenance, not a second operator settings format: encode/decode the recipe through the existing validated `PipelineDefinition` schema in the manifest and test the conversion against every current stage.
- [ ] Extract the existing private `configuration::detail::PipelineDefinitionCodec` into a small `lumora::serialization` target exposing `serialization::PipelineDefinitionCodec`. It links only processing/core and Qt Core, not application, capture, review or UI. Configuration and capture/review use that shared target; keep existing schema fixtures byte/semantically compatible. Do not link review to the whole configuration target (which already depends on application), and do not create a second recipe serializer. Numerical completeness/range validation remains in processing/application policy; the wire codec alone does not authorize a recipe.
- [ ] Run `cmake --build --preset linux-gcc-debug-sim` and `ctest --preset linux-gcc-debug-sim -R 'PipelineSnapshot|FrameProcessingEngine|SharedFramePresenter|PresentationProtocol' --output-on-failure`; inspect no per-frame serialization/recipe allocation was added. Review and commit only this provenance/presentation slice during execution.

### Task 2: Deliver M10 backend once, with Qt Quick Capture Live integration

**Files:** M10 capture paths and `src/capture/CMakeLists.txt`; new `CaptureController.hpp/.cpp`, `CaptureAdapter.hpp/.cpp`, `WorkstationHeader.qml`, `tests/integration/QmlCaptureAdapterTests.cpp`; modify `QmlWorkstation.hpp/.cpp`, `main.cpp`, `Main.qml`, `src/CMakeLists.txt`, `src/qml/CMakeLists.txt`, `tests/CMakeLists.txt`.

Also create `CaptureRetention.hpp/.cpp` in application and modify `LivePipeline.hpp/.cpp`, `LiveResourcePreparation.cpp`, `WorkstationCoordinator.hpp/.cpp` and the existing LivePipeline lifecycle tests for capture-held context admission.

**Interfaces:** Consume `FramePresenter::captureSnapshot()`, exact recipe provenance, `ICaptureStore::submit(CaptureJob) -> Result<CaptureTicket>`. Produce `CaptureController::captureLive(CaptureMode)`, result polling by ticket, and `CaptureAdapter::captureLive(QString mode)` with modes `original`, `processed`, `both`; QML properties `busy`, `pendingCount`, `lastSavedDirectory`, `error`.

- [ ] Execute M10 Tasks 1–4 in order: write failing contract/PNG/manifest/transaction tests, prove failure, implement the exact sample-depth/orientation/metadata contract, run the focused `Capture.*` tests. Original-only produces only Original plus manifest; thumbnails stay outside immutable artifact sets. Processed-only remains a valid M10 capture but cannot later be edited from sensor samples.
- [ ] Add worker/adapter regression tests before wiring commands:

```text
hold_storage_worker()
for 4 requests: expect(captureLive(Both) accepted)
expect(5th == CaptureBusy)
advance_live_until(new_acknowledged_frame)
expect(live_display_count increased)
release_storage_worker()
expect(exactly one terminal result per accepted ticket)
focus_reference(); captureLive(Both)
expect(saved_source == live_presenter.captureSnapshot().source)
```

- [ ] Implement the dedicated capture worker: use its pre-reserved identity; begin same-volume partial directory; encode the required planes; write manifest last; close/flush; rename; publish success only after commit. Unsupported mode, unavailable enhanced plane, invalid provenance, full queue, disk-full, encoder, path and rename failure each return a typed result. Follow M10 startup-cleanup containment and 24-hour rules.
- [ ] Implement M10's explicit process-crash commit guarantee and document platform file-sync/metadata barriers; stronger power-loss durability requires separate supported-filesystem evidence. Apply identical persistence/error rules to ID reservations and session manifests. Use no-replace final publication and reconcile reserved counters with maximum committed IDs before issuing new ranges. Test stale counters and collision after interrupted reservation without overwriting any committed artifact.
- [ ] Implement M10's four terminal outcomes, including `CommitIndeterminate` after rename may have occurred. Retain the original key/path, release pixel leases and reconcile that same transaction off-thread; uncertain reservation/session metadata cannot authorize new IDs or a new capture destination. Reserve bounded result capacity at submission. Test successful rename followed by failed metadata barrier, exception/cancellation at that boundary, result-consumer stalls, restart and retry without duplication. Reconciliation emits a separate keyed state update, never a second terminal result for the original ticket.
- [ ] Add generation-scoped capture-retention admission in the application lifecycle boundary. Capture submission atomically validates its generation and binds an application-owned lifetime token to the accepted job, without making the capture library depend on application. Charge retained context resources until the job releases that token, including cancellation/error paths. Check this registry before any preparation that would exceed the context bound, independently of renderer `contextBound`. Verify old capture held → replacement binds and new Live starts → further replacement rejected/deferred → capture completion releases the old charge and allows replacement. Withholding renderer acknowledgement is not a substitute for this registry.
- [ ] Wire `QmlWorkstation.capture` and the explicit **Capture Live** label through the live presenter. This UI action never picks the currently focused pane. Preserve click-time playback/mode/timestamp/orientation in the job; a newer frame/configuration arriving before encoding cannot change them. Capture of a visible stale/paused image is allowed with its true paused/stale state and age recorded, never mislabeled as current acquisition.
- [ ] Coordinate shutdown: reject new commands; stop and join capture independently of live processing; keep any capture-held old context alive until its four leases are released; then finish renderer/context retirement. Cancel queued tickets explicitly. Do not block the GUI event loop waiting for renderer receipts.
- [ ] Register `lumora_capture_tests` and `Qml.CaptureAdapter`; run `cmake --build --preset linux-gcc-debug-sim` then `ctest --preset linux-gcc-debug-sim -R 'Capture\.|Qml.CaptureAdapter|SharedFramePresenter' --output-on-failure`. Run M10's full matrix, including all artifact-boundary shutdown faults, then review and commit the slice during execution.

### Task 3: Persist evaluation sessions and reopen a bounded capture catalog

**Files:** New `EvaluationSession.hpp/.cpp`, `CaptureCatalog.hpp/.cpp`, `GalleryModel.hpp/.cpp`; new `tests/unit/capture/EvaluationSessionTests.cpp`, `tests/unit/capture/CaptureCatalogTests.cpp`, `tests/integration/QmlGalleryModelTests.cpp`; modify capture/QML/test target lists and `CaptureManifest.*`.

**Interfaces:** `EvaluationSessionStore::create(testLabel) -> Result<SessionId>`, `open(SessionId) -> Result<EvaluationSession>`, `reserveCaptureIds(SessionId, count=64) -> Result<CaptureIdRange>` run on the storage worker. `CaptureIdRange` contains inclusive first ID and count; admission consumes only an already persisted range. `CaptureCatalog::requestPage(SessionId, optional<CaptureKey> cursor) -> Result<CatalogRequestId>` produces bounded `CatalogPage` results, each tagged with request ID and session ID. `GalleryModel` exposes `captureId`, `sessionId`, `label`, `capturedUtc`, `mode`, `originalAvailable`, `thumbnailUrl`, and `error` roles.

- [ ] Add failing restart/crash tests with a temporary root. Use neutral labels such as `Test session 2026-09-17 01`; labels are optional, bounded to 80 Unicode characters, and never used as path components. Reject control characters; provide no patient-name/identifier fields.

```text
session = create("Simulator orientation test")
saved = capture_success(session)
restart_services()
expect(open(session).id == session)
expect(page(session).keys == [saved.key])
expect(next_reserved_id > saved.captureId)
leave_partial_transaction(); restart_services()
expect(page(session) contains no partial entry)
```

- [ ] Store atomic session manifests under `<capture-root>/.sessions/<session-id>/session.json`; include schema, created UTC, neutral label, evaluation status and next ID. A root-level capture created before sessions existed is shown in a read-only “Earlier captures” group without rewriting its immutable manifest; new captures require an active durable session. Existing M10 final capture directories stay under the configured root and add session identity in their manifest/name. Catalog pages are reconstructed from valid committed manifests; any disposable index is an optimization, not authority. Orphan committed captures remain discoverable from manifest identity even when an index update was interrupted.
- [ ] Implement bounded background enumeration and page delivery. Validate filenames, directory containment, schema, artifact presence, dimensions/depth, identities and duplicate IDs before returning rows. Ignore `.partial-*` directories; surface an invalid/corrupt-capture issue without decoding it. Do not follow symlinks/reparse points outside the configured root. A failure after storage commit but before catalog notification still reports the durable save and is recovered by catalog refresh; a transaction failure publishes no capture row.
- [ ] Distinguish verified, uncertain and invalid/incomplete catalog states. The preceding no-row rule applies to known pre-publication failures; a complete possible final directory from `CommitIndeterminate` remains visible as uncertain and cannot auto-follow or claim Saved. Reconcile its exact key/artifacts and declared barriers off-thread. On restart, valid recovered entries can become verified-on-reopen without assuming whether a success notification was delivered before the crash. A generic refresh must not mislabel a published but uncertain transaction as a nonexistent failed capture.
- [ ] Generate disposable thumbnails in a separate cache under the capture root, keyed by capture identity plus manifest/artifact identity; never modify Original/Enhanced/preview artifacts. If a thumbnail fails, keep a textual row and retry only on explicit refresh. Page/session generations prevent slow results from replacing a newer session's gallery.
- [ ] Add separate active capture destination and browsing-session state. `openSession` changes the library only; explicit New/Use capture session changes future write admission. Pending jobs keep their captured destination. Reject capture-session changes during recording when CR-5 is present. Verify capture into session A while browsing B: the header identifies A, B's catalog/reference stays selected, and the saved notification identifies A. Derived Save copy writes into the parent's session B. Return to current session restores A's catalog without replaying skipped auto-follow requests.
- [ ] Register `Capture.EvaluationSession`, `Capture.Catalog`, and `Qml.GalleryModel`; run `ctest --preset linux-gcc-debug-sim -R 'Capture\.(EvaluationSession|Catalog)|Qml.GalleryModel' --output-on-failure` after a full Debug build. Include duplicate IDs, unknown schema, lost root, readonly root, Unicode path, ID reservation interruption, stale pages and catalog refresh after interrupted notification. Review and commit during execution.

**CR-1 exit evidence:** Original M10 gates pass unchanged; additionally, a saved capture reappears under the same session/key after restart and failed/partial transactions never appear as saved images. Session/catalog success does not by itself accept M10 or reopen earlier deferred gates.

## CR-2 — Independent Reference, gallery and reopen

### Task 4: Load committed images into bounded review-owned memory

**Files:** `src/review/CMakeLists.txt`, `ReviewTypes.hpp`, `CaptureReader.hpp/.cpp`; new `tests/unit/review/CaptureReaderTests.cpp`; modify `src/CMakeLists.txt`, `tests/CMakeLists.txt`.

**Interfaces:** `CaptureReader::request(CaptureKey, ReviewRequestId) -> Result<void>`; `takeResult() -> optional<ReviewLoadResult>`. A successful result owns a validated `CaptureDescriptor`, parsed recipe and `shared_ptr<const ReviewImageSet>` backed by review-owned pools, plus the request ID. `ReviewImageSet` has optional raw/native enhanced images and available Original/Enhanced display planes; no raw image is fabricated when it is absent from storage. Original-only is rendered from saved raw using its captured recipe's original-display mapping/orientation. Processed-only is viewable from saved enhanced/preview, with `canEdit=false` and Original unavailable; it must not fabricate sensor Original data.

- [ ] Add failing tests for Mono8 and Mono10/12/16 native source round-trip, 90-degree/non-square orientation, Processed-only reopen, malicious dimensions, missing artifact and a slow obsolete selection.

```text
load(capture_A, request=1); hold_decode(A)
load(capture_B, request=2); finish(B); finish(A)
expect(accepted_selection == B)
expect(review_bytes <= budget && live_pool_lease_count unchanged)
load(oversized_capture, request=3)
expect(error == review_resource_limit && current_reference == B)
```

- [ ] Implement header/layout validation before full decode, checked peak-byte reservation, exact sample/depth readback and original metadata reconstruction. Preserve native image buffers; apply recorded orientation only to generated display planes. Read both Original and enhanced artifacts directly when available so reopen reproduces saved output without depending on future algorithm changes.
- [ ] Return explicit `capture_not_found`, `capture_incomplete`, `capture_schema_unsupported`, `capture_decode_failed` and `review_resource_limit` outcomes. On failure retain the preceding displayed Reference and show failed requested identity separately; never pair the new label with old pixels.
- [ ] Register `lumora_review_tests`/`Review.CaptureReader`; build Debug and run `ctest --preset linux-gcc-debug-sim -R 'Review.CaptureReader' --output-on-failure`. Check cancellation releases every replacement owner and no filesystem/decode work occurs on the GUI thread; review and commit during execution.

### Task 5: Independent reference controller and truthful per-pane presentation

**Files:** `ReviewController.hpp/.cpp`, `ReviewAdapter.hpp/.cpp`, `PresentationImages.hpp`, `ReferencePresenter.hpp/.cpp`, `ImagePane.qml`, `CaptureFilmstrip.qml`, `ContextInspector.qml`; modify `PresentationProtocol.hpp/.cpp`, `FramePresenter.cpp`, `QuickImageItem.cpp`, `ViewerAdapter.hpp/.cpp`, `QmlWorkstation.hpp/.cpp`, `Main.qml`; new `tests/unit/review/ReviewControllerTests.cpp`, `tests/integration/QmlReviewAdapterTests.cpp`; modify QML/test target lists.

The shared protocol migration also updates `src/ui/src/FramePresenter.cpp`, `tests/support/ControlledPresentationSink.hpp`, and all affected shared presenter/protocol and Quick renderer fixtures. Legacy Widgets remains regression-only, but its existing sink must compile and retain its behavior with the new envelope.

**Interfaces:** Keep `workstation.viewer` and `workstation.processing` Live-only. Add `workstation.review` with `viewer: ViewerAdapter*`, `gallery: GalleryModel*`, `selectedCaptureId`, `selectedSessionId`, `selectedLabel`, `loading`, `error`, `referenceLocked`, `hasSelection`, `canEdit`, `dirty`; actions `openCapture(QString)`, `openSession(QString)`, `pinCapture(QString)`, `setReferenceLocked(bool)`, `clearReference()`. Capture IDs passed by QML resolve only inside the explicit current session. `pinCapture` explicitly loads an already persisted item and locks it after successful presentation; a failed pin leaves the previous selection and lock unchanged. `openCapture` preserves the existing lock. The lock also has its own visible toggle.

- [ ] Add state-transition tests before implementation:

```text
capture_succeeds(A); expect(reference == A)
setReferenceLocked(true); capture_succeeds(B); expect(reference == A)
openCapture(B); expect(reference == B && referenceLocked)
capture_fails(C); expect(reference == B && gallery excludes C)
setReferenceLocked(false); capture_succeeds(D); expect(reference == D)
advance_live(); expect(reference == D)
reference_zoom(2); expect(live_transform unchanged)
live_compare(); expect(reference remains visible)
```

- [ ] Introduce `PresentationImages { sourceFrameId, optional originalDisplay, optional enhancedDisplay }` as the renderer envelope in `PresentationSubmission`. Validate that each available plane matches identity/orientation, and reject a requested unavailable mode. Live `FramePresenter` still retains its complete `FrameBundle` for capture and converts its display planes only at sink admission. This renderer envelope is necessary because `FrameBundle` requires sensor Original whereas a Processed-only saved capture does not contain one; never fabricate raw data to satisfy that invariant. Add equivalent receipt/failure/retirement regression cases for the migrated renderer.
- [ ] Migrate every `PresentationSubmission` consumer, including the legacy Widgets sink and controlled test sink. Replace direct bundle-plane reads with the validated presentation envelope, preserve owner/token/mode completion checks, and rerun shared presenter/protocol tests plus the opt-in legacy regression configuration. This is shared-protocol compatibility work, not new product wiring for Widgets.
- [ ] Implement an independent `QuickImageItem` plus `ReferencePresenter` for review, consuming `ReviewImageSet` and publishing only its available `PresentationImages` planes. `ViewerAdapter::bindReference(ReferencePresenter*)` delegates saved presentation commands; its existing `bindPresenter(FramePresenter*)` remains for Live. Extend `ViewerAdapter` with an explicit saved-image source kind so Reference shows **SAVED / REVIEW**, captured UTC, identity and orientation; disable meaningless resume-to-live on that adapter. Saved content does not age into Live `STALE` or receive a synthetic acquisition-freshness renewal. Use existing receipt/retirement rules for hidden, replaced and destroyed reference surfaces.
- [ ] Load/publish review-owned image sets through a separate source generation. Keep old reference label and pixels together until the replacement's presentation receipt. Ignore stale decode results and receipts. Clearing reference retires its renderer owners before releasing pools. Live device disconnect/rebind cannot clear Reference or erase its selection.
- [ ] Add minimal two-pane functional layout and bounded gallery pagination. Clicking a gallery row opens saved Reference explicitly, including while locked. Capture success auto-follow uses committed capture identity; suppress automatic replacement while a review edit is dirty. An explicit later selection must outrank a late automatic-load completion. Switching evaluation session clears the catalog selection through the same dirty guard; live acquisition continues.
- [ ] Run Debug `Review.Controller` and `Qml.ReviewAdapter` tests plus existing `Qml.Workstation`, `Qml.Runtime`, `SharedFramePresenter` and renderer tests. Inspect both panes under live, paused, saved, loading, failed decode, hidden surface and disconnect states. Review and commit during execution. Co-deliver the matching CR-2 UI task; CR-4 supplies screen-role persistence and dual-monitor behavior.

The concrete shared UI contract also requires `CaptureAdapter::captureSessionId`, `ReviewAdapter::browsingSessionId`, `ReviewAdapter::followSuspended` (true while an edit or cross-session browsing blocks following), `returnToCaptureSession()` and `ReviewAdapter::sourceKind` (`saved` here, with future scene playback handled in CR-5). `openSession` never changes the capture destination; an explicit separate capture-session command owns that change.

**CR-2 exit evidence:** Reference retains the selected persisted capture while Live advances/reconnects, browse/lock/follow semantics match the state tests, independent view transforms are proved, and no library path retains live pool owners. M10 Original-only/Processed-only/Both remain reopenable with truthful availability.

## CR-3 — Non-destructive processing of saved Original

### Task 6: Isolated review processing and revision-controlled drafts

**Files:** `ReviewProcessingService.hpp/.cpp`, `ReviewEditSession.hpp/.cpp`, `ReviewProcessingAdapter.hpp/.cpp`, `ReferenceProcessingControls.qml`; modify `ReviewController.*`, `QmlWorkstation.*`, `Main.qml`, QML/test target lists; new `tests/unit/review/ReviewProcessingTests.cpp`, `tests/unit/review/ReviewEditSessionTests.cpp`, `tests/integration/QmlReviewProcessingAdapterTests.cpp`.

**Interfaces:** `ReviewEditSession::begin(ReviewSelection, capturedRecipe)`, `edit(PipelineDefinition) -> Result<ReviewDraftRevision>`, `discard()`, `snapshot()`. `ReviewProcessingService::submit(ReviewSelection, ReviewDraftRevision, PipelineDefinition, reviewOwnedRaw) -> Result<void>` produces `ReviewProcessingResult` tagged with exact selection and draft revision. Add `workstation.reviewProcessing`; properties `dirty`, `pending`, `canSaveDerived`, `error`; actions `discardEdits()` and `saveDerived()` plus stage-specific editing actions following current processing parameter ranges.

- [ ] Add failing isolation/out-of-order tests:

```text
open_saved(A); baseline = live_recipe_and_camera_state()
edit_review(window=12000); hold_revision(1)
edit_review(window=8000); complete_revision(2); complete_revision(1)
expect(shown_review_revision == 2)
expect(live_recipe_and_camera_state() == baseline)
discardEdits(); expect(review_recipe == saved_recipe_A && !dirty)
expect(source_file_hashes unchanged)
```

- [ ] Create a review-only engine, processing/display pools and worker. Reuse `FrameProcessingEngine::plan/create/activate/process` with captured source descriptor and recorded installation orientation. Account for active/replacement results, scratch and renderer storage in the review budget. Never call `LivePipeline::requestProcessing*`, mutate live `ProcessingControlsModel`, or save review drafts as live preferences.
- [ ] Set the initial review preparation to one CPU execution slot. Define Live throughput/latency regression and review-admission thresholds from the target profile before acceptance. Test coalescing, deferred admission and pending feedback with deterministic scheduling hooks; then measure Live alone versus concurrent rapid review edits and slow still encoding on the same hardware. Defer new review work under pressure without dropping the draft or changing Live settings; reject an unsupported concurrent profile. Extend the measured matrix with recording at CR-5.
- [ ] Use a monotonically increasing draft revision scoped to selection identity. Coalesce edits to one active plus one latest request; publish only if both selection and revision still match. Keep the last successful result while pending and label its displayed revision. Processing failure leaves the draft available for correction and the last acknowledged image intact; a stale successful result cannot enable Save copy.
- [ ] Initialize edits from saved Original and its recorded recipe; built-in recipe selection copies values into this draft only. Unsupported saved schema/order, missing Original or unavailable required algorithm yields a view-only state with a specific reason. Never reinterpret enhanced/preview pixels as sensor samples or silently substitute the current live recipe. Compare uses this saved source and its new enhanced result within Reference.
- [ ] Register `Review.Processing`, `Review.EditSession`, `Qml.ReviewProcessingAdapter`; build Debug and run these tests plus processing engine reference/regression groups. Inject slow processing, newer selection, cancellation, over-budget preparation, unsupported order and worker exception. Review and commit during execution.

### Task 7: Transactional Save copy and safe selection changes

**Files:** `DerivedCaptureWriter.hpp/.cpp`; modify `CaptureManifest.*`, `CaptureService.*`, `ReviewEditSession.*`, `ReviewController.*`, `ReviewProcessingAdapter.*`, `CaptureFilmstrip.qml`, `ReferenceProcessingControls.qml`; new `tests/unit/review/DerivedCaptureWriterTests.cpp`, `tests/integration/ReviewCaptureWorkflowTests.cpp`; modify test targets.

**Interfaces:** `DerivedCaptureWriter::submit(DerivedCaptureJob) -> Result<CaptureTicket>` reuses the capture transaction/encoder infrastructure and shared four-job storage admission. `DerivedCaptureJob` contains parent `CaptureKey`, selection revision, acknowledged processed draft revision, exact review bundle/recipe and requested UTC. It produces a **new** capture key with `kind=derived`, `parentCapture`, `sourceCapture` and its own recipe revision; source frame identity stays traceable. `ReviewController::requestSelection(key)` yields `Applied`, `Loading` or `NeedsDraftDecision`; `resolveDraftDecision(SaveDerived|Discard|Cancel)` resumes only the still-current pending navigation.

- [ ] Add failing source-immutability and save-race tests:

```text
hashes = files_of(source_capture_A)
edit_A(revision=2); acknowledge_review_render(2)
ticket = saveDerived(); edit_A(revision=3); complete_save(ticket)
expect(new_capture.parent == A && new_capture.recipe_revision == 2)
expect(current_draft_revision == 3 && dirty)
expect(files_of(A) == hashes)
fail_next_save(disk_full); expect(no_new_gallery_item && dirty)
request_selection(B); choose(Cancel); expect(selection == A && dirty)
request_selection(B); choose(SaveDerived); fail_save()
expect(selection == A && dirty)
```

- [ ] Admit Save copy only for a successfully processed and presented current draft with Original available; freeze its bundle, recipe and parent identity at submission. Write a self-contained new capture: byte-equivalent source Original plus native U16 enhanced and oriented preview, with derived provenance. Copying immutable Original consumes disk deliberately so deleting/moving a different directory cannot silently break the derived image; no in-place file changes or hard-link assumptions.
- [ ] Commit manifest and new directory transactionally. On success add exactly one durable gallery item. If selection and draft revision still equal the submitted values, promote the new derived capture key to Reference with its same displayed pixels/recipe and make it the clean baseline, preserving the lock. Otherwise add the copy only to the gallery and preserve newer selection/dirty work. Save failure leaves current pixels/draft/selection unchanged and surfaces the typed cause. Save copy never modifies Live.
- [ ] For `CommitIndeterminate`, preserve the dirty draft, current Reference, lock and pending navigation; do not mark clean, auto-follow, close or navigate on an uncertain result. Reconcile the same derived key without silently creating a second copy. Positive keyed reconciliation with matching submitted selection/draft permits clean-baseline promotion, including an ordinary Save copy with no pending navigation. Completing browse/close additionally requires its still-current navigation token. New edits/selection prevent promotion; newer navigation commands or Cancel invalidate the older navigation continuation. Cancel never rolls back a possibly published copy or suppresses its catalog verification. Test standalone Save copy → uncertain → verified → clean, late reconciliation after new edits, cancellation and application restart.
- [ ] Implement Save copy / Discard edits / Cancel for explicit browse, clear, session change and application close with dirty review work. Discard restores the last persisted selected capture recipe/render; it does not reset Live. Automatic live-capture follow is suppressed while dirty and is not replayed later over a deliberate user selection. Ending the edit restores the prior follow/lock policy without automatically replaying skipped captures. A requested navigation completes only after its exact save succeeds; subsequent commands invalidate older pending navigations.
- [ ] Run `ctest --preset linux-gcc-debug-sim -R 'Review\.|Capture\.|Qml.(CaptureAdapter|ReviewAdapter|ReviewProcessingAdapter|GalleryModel)' --output-on-failure` after build. Verify hashes, parent linkage, rapid edit-save-edit, five competing storage jobs, disk-full, lost root, duplicate save completion, restart/reopen and dirty close. Review and commit during execution.

### Task 8: Combined resource, lifecycle and cross-platform evidence

**Files:** New `tests/integration/CarmCaptureReviewTests.cpp`, `docs/architecture/milestones/carm-capture-review.md`; modify `tests/CMakeLists.txt` and relevant implementation files only for demonstrated defects.

**Interfaces:** Exercise the complete `QmlWorkstation` composition through named public adapters and injectable capture/decode/processing faults. Reuse existing camera simulator and deterministic receipt/test hooks.

- [ ] Build one deterministic integration matrix covering all capture modes and supported source depths; two independent pane modes/transforms; lock/follow/manual selection; session reopen; processed-only view; review save/discard; context replacement; shutdown with hidden reference surface; and faults at every storage boundary.
- [ ] Saturate storage with four captured frames, catalog with more than 128 entries and review with rapid edits while Live advances. Assert four capture leases maximum, review byte reservation <=256 MiB, thumbnails <=8 MiB, one current plus one replacement review set, bounded pending requests, and no acquisition/processing stalls attributable to library disk work. Record allocation/high-water values and existing presentation/performance counters; do not infer a clinical real-time guarantee.
- [ ] Keep the storage worker blocked while repeatedly capturing and requesting source/ROI/format replacement; verify at most one old plus one candidate live context and explicit deferral until leases release. Repeatedly edit → acknowledge → Save copy while the writer is blocked; verify all storage-pinned review owners remain in the review byte ledger and the next over-budget operation is rejected without dropping the visible image or draft.
- [ ] Run the full Linux Debug and Release build/test presets, plus actual native Qt Quick renderer checks already registered in `tests/qml/RendererTests.cmake`. Match the source revision in Windows/MSVC Debug/Release simulator CI. Inspect 900×600 and 1280×800 screenshots for Live/Reference labels, evaluation notice, error/loading states and independent Compare panes; later CR-4 owns wider UI/DPI/dual-screen acceptance.

```bash
cmake --build --preset linux-gcc-debug-sim
ctest --preset linux-gcc-debug-sim --output-on-failure
cmake --build --preset linux-gcc-release-sim
ctest --preset linux-gcc-release-sim --output-on-failure
```

- [ ] Record exact source revision, commands, platform/runtime, artifacts and remaining limitations in the milestone record. Preserve the inherited lifecycle/context-retirement timeout investigation, below-target reference performance and deferred Windows/hardware gates until separately evidenced. Obtain implementation review, then commit the evidence during execution; this plan itself records no completed tests.

**CR-3 exit evidence:** Reprocessing always starts from immutable saved Original with the captured descriptor; review revisions and recipes are separate from Live; stale worker results cannot replace newer edits; Save copy creates a new traceable transaction and never modifies source artifacts; discard/navigation/save failures preserve the documented state.

## Execution handoff

Execute CR-1 first and review its evidence before moving to CR-2; CR-3 depends on the persisted-source reader and independent Reference. CR-4 consumes these adapters for UI refinement and optional second-window placement. Cine/event hold/pedal/image tools and clinical integrations are outside this plan. All checkboxes are intentionally unchecked until implementation is separately authorized and verified.
