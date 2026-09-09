# M9 Task 2: preset selector and processing controls

Approved continuation: the owner authorized publication/integration of Task 1 and development of the selector and controls. Base: Task 1 `01735ba`; integrate its successful PR before finalizing this branch. This plan refines the older M9 Task 2 against the actual application interfaces. It does not close hardware, native Windows or M8 performance gates.

## Design and boundaries

A single scrollable processing panel presents preset selection and the fixed pipeline order: Window/Level, Brightness/Contrast, Gamma, Local contrast (CLAHE), Denoise, Sharpen, Invert. Normalize remains mandatory. Each optional stage has an enable control. Numeric entry uses compiler ranges; all disabled values remain valid. Median mode changes atomically choose a supported kernel and zero sigma. Sharpen radius/threshold live in Advanced. Reset applies shipped Original and preserves custom recipes, camera settings/lifecycle, pause and viewport transform. No Capture/Record placeholders, camera dialog or Compare work.

The current viewer always displays Original. To make processing controls useful, Task 2 adds a labeled single Enhanced preview, falling back to Original when enhancement is unavailable. Original/Enhanced switching and synchronized Compare remain Task 3. Presentation completion tracks the selected display frame from the same FrameBundle; pause retains the last actually painted bundle and freshness timestamps. Edits never recompute a paused bundle.

### Application activation

Add `ProcessingConfigurationCommand { sessionGeneration, definition }` and `ProcessingConfigurationOutcome { sessionGeneration, configurationRevision, optional<PipelineValidationError> error }`. A nonzero definition.configurationRevision identifies each increasing submission. `LivePipeline::setProcessingConfiguration` admits one pending/executing command and rejects busy/stale/unavailable/non-increasing requests explicitly. The controller coalesces the latest UI draft, so the application does not need an unobservable queue of superseded completions. The snapshot has a capacity-one outcome and a pending flag independent of camera outcomes. Admission is not activation success.

Expose the existing concurrent-safe engine activate contract through a default unsupported virtual `IFrameProcessor::activate`, preserving custom adapters. Validate and prepare outside the UI/frame thread; run activation on the existing control owner, after priority lifecycle dispatch. Never hold the public snapshot mutex during preparation. Publish an outcome when activation returns, even without frames; executed ProcessorStatus revisions are distinct evidence. Preserve structured validation/preparation errors and prior active configuration on rejection. Session retirement settles queued work, rejects stale generation, and never applies an old definition to a replacement. No camera command is emitted by a processing change. Activation storage is already bounded by engine preparation; this does not change per-frame algorithms.

### One preferences owner

Extend StartupPreferencesStatus with initial loadedPresets, latestAttemptedPresetSaveRevision and latestSavedPresetRevision. Extend StartupPreferencesService with postPresetSave(revision, PresetState), using an independent monotonic preset save revision. Validate against the shipped repository. Keep one worker/document; coalesce latest camera and preset submissions separately, merge both sections into the owned document before saving, and report revisions per section. Preserve unrelated data, load safety, warnings, durable-save distinction and shutdown draining. Never add a competing file writer.

### UI model and integration

ProcessingControlsModel owns a validated repository and keeps draft, in-flight and acknowledged state separate. Labels update immediately. Drag publication spacing is at least 33,333,334 ns using IClock::steadyNow; a tick publishes the latest valid complete draft. Release flushes exact values even within that interval and suppresses identical duplicate release. Preset/Reset cancel pending drag values. Manual edits select Custom. A newer draft survives an older acknowledgment. The latest rejected draft restores the last acknowledged state and displays the error; earlier rejection cannot overwrite a later edit. Session replacement discards old in-flight association and submits the current desired state with a fresh revision.

The controller waits for the one asynchronous preset load before creating/enabling the model. Loading never issues Connect/Apply/Start. Existing camera startup behavior remains unchanged. The model submits to the current prepared session; successful activation establishes accepted state and is the only trigger for preset persistence. Source/load errors remain visible. Shutdown captures an already completed activation outcome without issuing new processing/camera intents. Widgets use translated labels, buddies, accessible names, numeric values and normal keyboard focus. Display synchronization blocks widget signals and preserves arbitrary loaded decimal values until explicitly edited. MainWindow inserts camera/processing content through explicit view APIs, replacing the existing positional layout-index dependency.

## Implementation and verification

1. **Application activation seam** — tests first for idle/no-frame acknowledgment, complete valid revisions, invalid/busy/stale rejection, adapter unsupported, replacement/Stop/Disconnect behavior; then interface/control-owner implementation. Own LivePipeline and IFrameProcessor/engine declaration plus focused application/integration tests.
2. **Preferences extension** — tests first for loaded state, concurrent section submissions, coalescing/drain, unsafe source and independent revisions; then service/status implementation. Own StartupPreferences service/status and its tests.
3. **Model and widgets** — tests first for 0/33/34 ms burst timing, exact release, preset cancellation, Custom, rejection/stale completion and session rebinding; implement model, then accessible controls and controller/presentation binding. Check unchanged numeric precision, Reset scope, Enhanced bundle identity/fallback and paused view.
4. **Review and evidence** — build Debug/Release simulator targets, focused tests, complete headless suites and native X11 smoke. Run a desktop rendering smoke for the panel layout. Independent review of changes against this plan and repository architecture; fix findings, then document exact checks and remaining gates. Commit reviewed source/tests/docs. Task 2 publication is a later integration decision.

Root owns shared builds and Git. Independent application and preferences implementations may proceed in parallel with UI work after these contracts are recorded. Record actual RED/GREEN outcomes; do not claim pre-existing lifecycle flakiness fixed by passing repeats.

## Completion checkpoint

Task 1 merged through PR #17 as `f3e1bcc`, and this branch starts from its integration documentation checkpoint `2c02ddb`. All four implementation/verification steps above are complete locally at source `e55cddf`; the [execution record](../../architecture/milestones/m09-processing-controls.md#verification-record) contains review outcomes, Debug/Release and native X11 results, and the exact evidence location. Task 2 remains unpublished. Hosted platform CI and integration are next, with Task 3 and the deferred acceptance gates still separate.
