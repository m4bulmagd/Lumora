# Milestone 10 Snapshot Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Capture the exact displayed or paused bundle as Original, Processed, or Both while storing immutable native-orientation source samples at their effective depth, a native-orientation enhanced U16 artifact, a screen-equivalent oriented preview, complete evaluation metadata, and transactional failure behavior.

**Architecture:** The UI submits an immutable displayed `FrameBundle` to a four-job capture FIFO. A dedicated worker encodes into a same-volume temporary directory and publishes the final directory only when the artifact set is complete.

**Tech Stack:** C++20, OpenCV imgcodecs, Qt Core JSON/path support, Qt Quick/QML frontend, standard filesystem, GoogleTest/Qt Test/CTest.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

**Qt Quick planning update (2026-09-17):** The current frontend is Qt Quick/QML. [CR-1–CR-3 capture/review planning](2026-09-17-carm-capture-review.md) supplies the frame-bound recipe provenance prerequisite and the current adapter/composition path for this milestone. Execute this backend once; the subsequent evaluation-session catalog, independent Reference and saved-image edits are extension work, not added M10 acceptance gates. This documentation update authorizes no implementation and accepts no milestone.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Capture never blocks acquisition, processing, or Qt rendering.
- At most four accepted jobs retain capture bundles, including the active encoder. Reserve one of four bounded terminal-result slots at admission as well; return `CaptureBusy` if either capacity is unavailable. Never overwrite an unconsumed result.
- The captured bundle is the exact currently displayed or paused Live bundle. The explicit **Capture Live** action never follows focus into Reference; saved-image edits use the separate **Save copy** action in CR-3.
- The complete recipe must belong to that bundle, including an older paused revision; never substitute current controls or the latest active recipe.
- Original samples round-trip exactly; metadata identifies source/stored formats.
- Mono8 Original uses 8-bit PNG; Mono10/Mono12/Mono16 numeric samples use 16-bit PNG with valid-bit/packing/alignment metadata.
- No incomplete transaction appears under a final capture directory name.
- Initial Saved/committed acceptance covers application-process crash recovery after closed artifacts and atomic publication, not an untested power-loss guarantee. Document and test the supported platform/filesystem synchronization and metadata publication barriers, propagate sync failures, and require separate evidence for a stronger power-loss claim.
- A failure known to precede final publication is distinct from an uncertain outcome after rename may have succeeded. Preserve the latter under its existing capture identity for reconciliation; never delete a possibly published capture or silently retry it with a new ID.
- Cleanup operations remain inside the configured capture root.

---

### Task 1: Capture contracts and bounded submission

**Files:**
- Create: `src/capture/include/lumora/capture/CaptureTypes.hpp`
- Create: `src/capture/include/lumora/capture/ICaptureStore.hpp`
- Create: `tests/unit/capture/CaptureContractTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: immutable `FrameBundle`, destination path, application version, and typed errors.
- Produces: `CaptureMode`, `CaptureId`, `CaptureJob`, `CaptureArtifact`, `CaptureResult`, `CaptureTicket`, and `ICaptureStore::submit`.

- [ ] **Step 1: Write failing job-validation tests**

```cpp
TEST(CaptureJob, ProcessedModeRequiresEnhancedFrames) {
    auto bundle = makeOriginalOnlyBundle(7);
    auto job = CaptureJob::create(CaptureMode::Processed, bundle, validDestination());
    ASSERT_FALSE(job.hasValue());
    EXPECT_EQ(job.error().code, "enhanced_frame_unavailable");
}
```

- [ ] **Step 2: Verify capture contracts are absent**

Build `lumora_capture_tests`; expect failure.

- [ ] **Step 3: Define exact contracts**

```cpp
enum class CaptureMode { Original, Processed, Both };
struct CaptureJob { CaptureId id; CaptureMode mode; std::shared_ptr<const FrameBundle> bundle;
                    std::filesystem::path root; std::chrono::system_clock::time_point requestedUtc; };
class ICaptureStore {
public:
    virtual ~ICaptureStore() = default;
    virtual Result<CaptureTicket> submit(CaptureJob) = 0;
};
```

Serialize frame IDs and capture IDs as decimal strings in JSON to avoid loss through floating-point JSON implementations.

Every accepted ticket yields exactly one terminal result: `Saved`, `FailedBeforePublication`, `CancelledBeforePublication`, or `CommitIndeterminate`. Results retain the capture ID and intended final path; CR-1 also supplies the session key. Completion releases pixel/retention owners independently of UI delivery, but its result slot stays reserved until consumed. Later reconciliation publishes a separately keyed state update, not a second terminal result for the ticket.

Reconciliation also uses bounded admission: one active request and one replaceable pending Check save request, with no busy-loop retries or unbounded in-memory unresolved-key list. Unprocessed keys remain discoverable through validated final paths/catalog pages; bounded keyed updates may coalesce state, but terminal ticket results may not be overwritten. Reconciliation does not retain pixel bundles.

- [ ] **Step 4: Test mode artifact expectations and safe names**

Original requires `original.png` and metadata; Processed requires `enhanced_u16.png`, `preview_u8.png`, and metadata; Both requires all four. `original.png` and `enhanced_u16.png` use native sensor orientation; only `preview_u8.png` includes the shared installation orientation. Sanitize serial text into a filename component while preserving the original serial in metadata.

- [ ] **Step 5: Commit capture ports**

```powershell
git add src/capture tests/unit/capture src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(capture): define bounded snapshot contracts"
```

### Task 2: Lossless PNG encoder and exact round-trip tests

**Files:**
- Create: `src/capture/include/lumora/capture/ICaptureEncoder.hpp`
- Create: `src/capture/include/lumora/capture/OpenCvPngEncoder.hpp`
- Create: `src/capture/src/OpenCvPngEncoder.cpp`
- Create: `tests/unit/capture/OpenCvPngEncoderTests.cpp`

**Interfaces:**
- Consumes: validated U8/U16 frame layouts and output file path.
- Produces: `ICaptureEncoder::encodeOriginal`, `encodeEnhanced`, `encodePreview` and OpenCV PNG implementation.

- [ ] **Step 1: Write failing exact pixel round-trip tests**

Create Mono8, Mono10, Mono12, and Mono16 fixtures containing 0, 1, midpoint, maximum, odd width, and padded input rows. Encode Mono8 Original as `CV_8UC1`; encode higher-depth numeric samples as `CV_16UC1`. Read with `cv::IMREAD_UNCHANGED` and assert type, dimensions, native orientation, and every sample exactly.

- [ ] **Step 2: Verify encoder is missing**

Build `lumora_capture_tests`; expect failure.

- [ ] **Step 3: Implement validated row-view encoding**

Wrap immutable buffers in `CV_8UC1` or `CV_16UC1` with explicit stride. Original encoding receives RawFrame samples directly and must not normalize, window, enhance, flip, or rotate them. Enhanced encoding receives the pre-orientation U16 result; preview receives the final oriented Gray8 presentation. Copy only when the encoder requires contiguous input. Use deterministic PNG compression configuration recorded in diagnostics, and reject unsupported storage/channel types.

- [ ] **Step 4: Test encoder failures**

Cover nonexistent parent, unwritable destination, injected encoder failure, zero/overflow layout rejected before OpenCV, and Unicode Windows paths.

- [ ] **Step 5: Run and commit**

Run: `ctest --preset linux-gcc-debug-sim --output-on-failure -R OpenCvPngEncoder`

```powershell
git add src/capture tests/unit/capture/OpenCvPngEncoderTests.cpp
git commit -m "feat(capture): encode exact grayscale PNG snapshots"
```

### Task 3: Versioned metadata manifest

**Files:**
- Create: `src/capture/include/lumora/capture/CaptureManifest.hpp`
- Create: `src/capture/src/CaptureManifest.cpp`
- Create: `tests/unit/capture/CaptureManifestTests.cpp`
- Create: `tests/fixtures/capture/manifest-v1.json`

**Interfaces:**
- Consumes: capture job, frame metadata, applied camera settings, pipeline definition/revision, timings, application version, and artifact results.
- Produces: `CaptureManifest::fromJob`, `toJson`, and schema version 1.

- [ ] **Step 1: Write failing required-field test**

Parse emitted JSON and assert schema/application version, `evaluation` release class, UTC timestamp, paused/live capture state, frame ID string, camera manufacturer/model/serial/transport/firmware, dimensions/stride/source format stable encoding/name/packing/alignment/valid bits/sample maximum/stored format, native and presentation dimensions, active installation orientation, ROI/FPS/exposure/gain, preset, fixed order version/stages/parameters, timing, and artifact filenames/outcomes.

- [ ] **Step 2: Verify manifest type is missing**

Build `lumora_capture_tests`; expect failure.

- [ ] **Step 3: Implement stable JSON names and units**

Use ISO-8601 UTC with millisecond precision; exposure unit `microseconds`, gain unit `dB`, duration unit `milliseconds`, and source/presentation dimensions separately after orientation. Optional unavailable camera values serialize as `null`, not zero. The release class and safety statement are mandatory schema fields.

- [ ] **Step 4: Add golden schema test**

Normalize key ordering/whitespace for comparison with `manifest-v1.json`. Assert locale-independent decimal points and round-trip of Unicode camera model text.

- [ ] **Step 5: Commit manifest**

```powershell
git add src/capture tests/unit/capture/CaptureManifestTests.cpp tests/fixtures/capture/manifest-v1.json
git commit -m "feat(capture): add versioned snapshot manifest"
```

### Task 4: Same-volume capture transaction

**Files:**
- Create: `src/capture/include/lumora/capture/IStorageTransaction.hpp`
- Create: `src/capture/include/lumora/capture/FilesystemCaptureTransaction.hpp`
- Create: `src/capture/src/FilesystemCaptureTransaction.cpp`
- Create: `tests/unit/capture/FilesystemCaptureTransactionTests.cpp`

**Interfaces:**
- Consumes: validated capture root and final safe directory name.
- Produces: `begin`, temporary artifact paths, `commit`, `abort`, and startup cleanup restricted to the capture root.

- [ ] **Step 1: Write failing atomic visibility test**

Begin a transaction and assert no final directory exists. Create all temporary artifacts, commit, and assert one final directory appears with all files. Inject failure before manifest/rename and assert no final directory appears.

- [ ] **Step 2: Verify transaction implementation is missing**

Build `lumora_capture_tests`; expect failure.

- [ ] **Step 3: Implement safe path containment**

Resolve/canonicalize the configured root and candidate paths, reject traversal/reserved names, create temporary directory `<final>.partial-<capture-id>` inside the same root, and use a same-volume directory rename for commit. Default the root through `QStandardPaths` to `%USERPROFILE%\Pictures\Lumora\Captures` on Windows and the corresponding XDG Pictures path on Linux; keep it injectable/configurable through the storage adapter.

Define the storage adapter's commit sequence for each supported filesystem: complete/close artifacts and manifest, perform the documented OS file synchronization operations, use a no-replace same-volume publication, then perform the supported containing-directory/metadata publication barrier or explicitly record its platform limitation. Rename is the publication boundary. Before publication, a known failure can abort the owned partial directory. If rename may have succeeded, or a subsequent barrier fails, report `CommitIndeterminate` with the original key/path, retain all final artifacts and never ordinary-abort or reuse that key. After publication, finish the barriers where possible rather than treating cancellation as rollback. Atomic visibility alone is not evidence of power-loss persistence.

Reconcile an indeterminate save off-thread at that same contained final path: verify matching manifest/key and all required artifacts, and complete the declared barriers where supported before reporting verified completion. Missing, inaccessible or corrupt output remains an explicit unresolved/error state; it is not silently retried under a new identity. On reopen, complete recovered entries are identified as verified on reopen without claiming whether a pre-crash success notification was delivered. Apply equivalent admission guards to ID reservations and session manifests: an uncertain reservation/session is not usable until verified. Reconcile counters against committed captures, never overwrite an existing final capture, and test termination before/after each boundary. Hardware/power-loss acceptance is a separate platform-specific campaign.

- [ ] **Step 4: Implement safe abort/startup cleanup**

Abort closes handles and removes only the exact validated temporary directory. Startup cleanup enumerates only immediate children matching Lumora's `.partial-<id>` pattern and removes entries older than 24 hours after containment revalidation.

- [ ] **Step 5: Test disk and path faults**

Use a fake storage API for disk-full, access denied, destination removed, file collision, flush failure, rename failure, and abort failure. Preserve enough diagnostic context without exposing a false success.

- [ ] **Step 6: Commit storage transaction**

```powershell
git add src/capture tests/unit/capture/FilesystemCaptureTransactionTests.cpp
git commit -m "feat(capture): add transactional snapshot storage"
```

### Task 5: Capture worker, UI integration, and failure isolation

**Files:**
- Create: `src/capture/include/lumora/capture/CaptureService.hpp`
- Create: `src/capture/src/CaptureService.cpp`
- Create: `tests/unit/capture/CaptureServiceTests.cpp`
- Create: `tests/integration/SnapshotCaptureTests.cpp`
- Create: `src/presentation/include/lumora/presentation/CaptureController.hpp`
- Create: `src/presentation/src/CaptureController.cpp`
- Create: `src/qml/CaptureAdapter.hpp`
- Create: `src/qml/CaptureAdapter.cpp`
- Create: `src/qml/qml/WorkstationHeader.qml`
- Create: `tests/integration/QmlCaptureAdapterTests.cpp`
- Modify: `src/presentation/include/lumora/presentation/FramePresenter.hpp`
- Modify: `src/presentation/src/FramePresenter.cpp`
- Modify: `src/qml/QmlWorkstation.hpp`
- Modify: `src/qml/QmlWorkstation.cpp`
- Modify: `src/qml/main.cpp`
- Modify: `src/qml/qml/Main.qml`
- Modify: `src/qml/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: four-entry `BoundedQueue<CaptureJob>`, accepted-job and terminal-result admission credits, encoder, transaction, manifest, current displayed bundle, and per-ticket result delivery.
- Produces: capture worker start/stop/join, nonblocking submission, per-ticket result, destination feedback, and shutdown cancellation.

- [ ] **Step 1: Write failing capacity/failure-isolation test**

Block the fake encoder, submit four jobs successfully, assert the fifth returns `capture_busy`, continue publishing/processing live frames, then release the encoder and verify all accepted ticket outcomes.

- [ ] **Step 2: Verify service is missing**

Build `lumora_capture_tests lumora_integration_tests`; expect failure.

- [ ] **Step 3: Implement worker and transaction order**

For each job: begin; encode required original/enhanced/preview; build manifest using successful artifact records; write manifest last; flush/close; commit; publish success. Failures known to occur before publication abort and publish one failure result. Exceptions or errors after publication may have occurred produce `CommitIndeterminate`, preserve the key/path and trigger bounded reconciliation; never claim the capture does not exist. Catch exceptions at the worker boundary and preserve the transaction's last known phase.

- [ ] **Step 4: Integrate exact displayed bundle**

`QmlWorkstation` composes `CaptureController` and exposes `CaptureAdapter` as `workstation.capture`. When **Capture Live** is clicked, the controller snapshots the live `FramePresenter`'s acknowledged `presentedBundle()` together with its receipt identity, visible display mode, pause/stale state and frame-bound recipe. The refined plan adds `captureSnapshot()` to take these values atomically on the frontend thread. It does not query the latest processing slot or current controls again. Paused captures therefore match the visible image and historical processing recipe exactly.

Wire `CaptureAdapter::captureLive(QString mode)` for `original`, `processed`, and `both`; show pending count and typed failure or final destination feedback. Disable submission before the first acknowledged frame, during source-context retirement or when storage admission is unavailable. Gallery publication follows only durable success. Widgets controllers are legacy regression surfaces and receive no new product wiring.

Show `Save outcome uncertain — checking` for `CommitIndeterminate`, then its verified or unresolved keyed status. It is not a success thumbnail or an ordinary failed/nonexistent capture. A retry checks that same transaction first; no automatic duplicate capture is submitted. The extension catalog may list the uncertain entry with its truthful state, and promotes it only after verification.

- [ ] **Step 5: Implement deterministic shutdown behavior**

Stop accepting jobs, allow the active transaction to finish or observe cancellation at artifact boundaries, cancel queued tickets with `CancelledBeforePublication`, join worker, then allow logging/configuration shutdown. Keep capture-held context/pool owners alive until those jobs release them; coordinate `QmlWorkstation`'s existing renderer retirement receipts before final context destruction. Drain shutdown asynchronously rather than blocking the GUI event loop on a renderer receipt. The four accepted-job limit includes the active encoder, so a blocked worker cannot retain five bundles.

Use the capture/review plan's explicit generation-scoped capture-retention registry independently of renderer acknowledgement. Binding and starting a new Live context may complete while one old context is retained for saving; a further resource replacement remains unavailable until the old capture leases release. Cancellation after publication cannot delete that transaction or turn its result into `CancelledBeforePublication`.

- [ ] **Step 6: Run end-to-end capture matrix**

For Live and Paused, test Original/Processed/Both, matching IDs, paused-frame historical recipe after a newer activation, focus remaining on Reference during Capture Live, pending versus completed renderer tickets, Mono8/U16 Original file type and exact round-trip, native-orientation enhanced U16, oriented screen-equivalent preview U8, release/pause/orientation/source-format manifest content, queue/result capacity, disk full, permission, destination removal, and shutdown during each artifact. Inject rename-success followed by barrier failure; verify retained identity, no destructive abort, bounded reconciliation and restart/retry without duplicate capture IDs.

- [ ] **Step 7: Commit complete snapshot feature**

```powershell
git add src/capture src/presentation src/qml src/CMakeLists.txt tests/CMakeLists.txt tests/unit/capture tests/integration/SnapshotCaptureTests.cpp tests/integration/QmlCaptureAdapterTests.cpp
git commit -m "feat(capture): save transactional traceable snapshots"
```

## Milestone 10 acceptance gate

- [ ] All three capture modes produce exactly their required complete artifact set.
- [ ] Original PNG read-back equals every input sample.
- [ ] Original file depth matches the source contract; Original and enhanced U16 are native-orientation while preview matches the displayed orientation.
- [ ] Manifest and every artifact share the displayed source frame ID.
- [ ] Manifest records evaluation status, paused/live state, format/valid-bit/packing/alignment, fixed order version, and active installation orientation.
- [ ] Fifth pending job returns Capture Busy without blocking or allocating an unbounded item.
- [ ] Injected failures create no final-looking partial capture and do not interrupt live viewing.
- [ ] Post-publication uncertainty preserves the complete possible capture, reports its identity truthfully and reconciles without silent duplication; only verified outcomes appear as Saved.
