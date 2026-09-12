# M2-inspired features for Lumora

Feature proposal · 10 September 2026

**Status:** Saved for future inspiration; not scheduled or approved for implementation.

**Source:** User-supplied “M2-inspired features for Lumora” proposal, dated 10 September 2026. Feature IDs F01–F08 are local to this proposal.

**Collection:** [Future product ideas](README.md).

This is a proposed backlog based on the inspected M2 installation and Lumora's current source and plans. It does not change Lumora's accepted requirements or milestone sequence. The descriptions below refine M2 concepts for Lumora; they are not claims that M2 implements each proposed behavior.

## Foundation at the time of the proposal

As recorded on 10 September 2026, Lumora already has a viewer, manual presentation pause, camera abstraction and simulator, high-depth processing, spatial denoising/sharpening, and preset storage. Its processing controls and comparison interface are planned or in progress. Physical-camera integration and snapshot export remain unfinished. See [current progress](../PROGRESS.md).

Complete the existing controls, camera integration and snapshot work through their current plans. Avoid adding duplicate brightness, contrast, gamma, inversion, zoom or preset systems.

## Prioritized feature list

| ID | Feature | Operator benefit | Relationship to Lumora |
|---|---|---|---|
| F01 | Exact-frame snapshot export | Save precisely the image being viewed, with its original data and processing settings. | Already planned in M10; finish existing work. |
| F02 | Capture gallery and saved-image review | Find, reopen and inspect captures without leaving the workstation. | New extension after F01. |
| F03 | Circular viewing mask | Focus the display on a circular imaging field. | New display feature, distinct from camera acquisition ROI. |
| F04 | Temporal noise reduction | Reduce frame-to-frame noise in stationary scenes. | Anticipated future processing extension. |
| F05 | Dark-frame calibration | Correct sensor offset and fixed-pattern artifacts using a matching reference. | Anticipated future calibration extension. |
| F06 | Automatic last-image hold and optional capture | Retain a useful image when a defined image event ends. | New automation; manual Pause already exists. |
| F07 | Short sequence recording and review | Review a short event and select individual frames afterward. | Deferred recording extension. |
| F08 | Test-session report export | Share selected test images with acquisition and processing details. | New evaluation workflow; separate from patient management. |

## Refined feature definitions

### F01 — Exact-frame snapshot export

**User story:** As an operator, I want Capture to save the exact frame visible on screen, including when presentation is paused.

**Behavior:** Provide Original, Processed and Both capture modes. Preserve original sensor values and depth; save the enhanced image and its display preview separately. Include the capture time, camera identity, frame identity and processing settings. Clearly report completion or failure.

**Done when:** Exported Original pixels match the selected frame; paused capture saves that paused frame; an unsuccessful save does not appear as a completed capture; saving does not interrupt live acquisition.

**Status:** This is the existing [M10 snapshot contract](../superpowers/plans/2026-04-25-m10-snapshot-capture.md), not a new competing implementation.

### F02 — Capture gallery and saved-image review

**User story:** As an operator, I want a thumbnail strip of my test-session captures so I can review previous images quickly.

**Behavior:** Add a thumbnail only after its capture succeeds. Selecting a thumbnail opens saved-image review with zoom, pan, fit and the available Original/Enhanced views. Show capture time and processing information. Provide an explicit Return to Live action.

**Done when:** A selected thumbnail opens the matching persisted capture; saved images display a clear SAVED IMAGE indication; reviewing them does not accidentally stop or restart acquisition; returning to Live presents a fresh frame or a visible stale/disconnected state. Bound thumbnail loading so large sessions do not exhaust memory.

**Dependency:** F01. Patient names and medical records are not required for a test-session gallery.

### F03 — Circular viewing mask

**User story:** As an operator, I want to hide the area outside the circular image field so I can concentrate on the useful view.

**Behavior:** Allow an adjustable circle with center and diameter, plus enable/reset controls. Apply the same viewing mask to Original and Enhanced presentation. Record the mask when exporting a display preview.

**Done when:** Original sensor pixels remain unchanged; the mask stays aligned during zoom and pan; camera acquisition ROI remains a separate setting; preview metadata identifies the mask. A masked preview must not be presented as an uncropped raw image.

**Dependency:** Viewer and capture metadata. Automatic circle detection is an optional later refinement, not required for the first version.

### F04 — Temporal noise reduction

**User story:** As an operator, I want to smooth random noise across recent frames when the scene is mostly stationary.

**Behavior:** Offer Off and a small set of bounded averaging strengths, initially proposed as 2, 4 and 8 frames. Apply averaging to the enhanced path, leaving the Original available. Explain the increased motion blur and temporal smoothing in the control's help text.

**Done when:** Averaging uses distinct, valid acquired frames; reconnects and incompatible camera/settings changes clear history; memory stays bounded; known stationary and moving test sequences produce the expected results. A held or repeated display frame must not be counted as a new acquisition.

**Dependency:** A separately specified history and processing-order contract. Existing Gaussian/median denoising is spatial and does not implement this feature.

### F05 — Dark-frame calibration

**User story:** As an operator, I want guided collection of a dark reference and a clear indication that it matches the current camera settings.

**Behavior:** Collect and validate a dark-frame sequence, create a versioned reference, and allow correction to be enabled or disabled. Associate it with camera identity, dimensions, pixel format, exposure/gain and other relevant acquisition conditions. Show Missing, Ready or Invalid calibration status with the reason.

**Done when:** An incompatible reference is never silently applied; correction preserves the Original; calibrated exports identify the reference version; canceling or failing calibration retains the previous valid reference. Validate correction with known test images before judging its visual benefit.

**Dependency:** Stable camera configuration and a defined calibration workflow. Hot-pixel correction and flat-field calibration are separate possible extensions.

### F06 — Automatic last-image hold and optional capture

**User story:** As an operator, I want to retain a qualifying image when a configured image event ends, without manually timing Pause.

**Behavior:** Start disabled. Allow explicit arming, hold criteria, a manual override and optional saving. Evaluate valid source frames using defined intensity units, timestamps and hysteresis. Retain the selected complete frame bundle while acquisition continues. Show HELD IMAGE alongside the existing mandatory PAUSED indication, timestamp and increasing age, with an explicit Resume Live action. Preserve camera-error and stale-state visibility while the image is held.

**Done when:** Defined test sequences select the expected frame; camera loss or a stalled stream cannot masquerade as a valid event end; one event cannot produce duplicate captures; both held presentation and export refer to the same selected frame. Motion, changing camera gain/exposure and threshold behavior have documented tests.

**Dependency:** F01, bounded frame history and an explicit event specification. This proposal concerns image selection only; serial feedback and generator-linked triggers are separate integrations. It is not a rename of the existing manual Pause.

### F07 — Short sequence recording and review

**User story:** As an operator, I want to record a short test sequence, play or scrub through it, and export a selected frame.

**Behavior:** Provide explicit start/stop, a visible duration/frame limit, playback, frame stepping and selected-frame export. Record frame timestamps and skipped-frame information. Distinguish a convenient display video from any retained original high-depth sequence.

**Done when:** Recording and playback have bounded memory/storage behavior; slow or full storage cannot stall live acquisition; frame export preserves the selected frame's identity; playback is clearly labeled RECORDED. The representation of processing changes during a recording is explicitly defined.

**Dependency:** Separate recording design. Lumora currently withholds Record UI until that work is specified and verified. Retrospective pre-event history is an additional option, not implied by basic recording.

### F08 — Test-session report export

**User story:** As an evaluator, I want a concise PDF/contact sheet containing selected test captures and their technical context.

**Behavior:** Select saved captures, arrange them with captions, add a test-session identifier and notes, and include camera settings, processing preset/revision and calibration reference where available.

**Done when:** Reports identify which images are Original previews or Enhanced previews; timestamps and settings match the captures; layout remains readable; the PDF carries the evaluation release class and `EVALUATION — NOT FOR CLINICAL USE` statement, while original data remains available separately.

**Dependency:** F01–F02. This is a refined alternative to M2's patient report for the current evaluation product; it does not introduce patient records, diagnostic findings or PACS.

## Scope to keep separate

- **Patient records, clinical reports and DICOM/PACS:** broader future product scope, not prerequisites for an image gallery.
- **COM-port brightness feedback or equipment control:** requires the receiving device's protocol and measured end-to-end behavior. M2's greylevel scaling must not become Lumora's universal intensity model.
- **AI enhancement and tissue-specific labels:** the initial product excludes AI-generated enhancement. Prefer the existing neutral processing presets; a “Bone” label alone does not establish a distinct or validated algorithm.
- **Foot-pedal controls:** potentially useful as an input mapped to Capture or Hold, after those actions exist. Define the pedal interface independently from any equipment actuation. Several keyboard viewer shortcuts already exist.
- **Additional camera brands or RTSP:** part of the user's desired source flexibility, not a feature discovered in M2. Implement separate adapters against Lumora's camera interface.

## Suggested delivery order

Complete the currently planned operator controls and exact-frame snapshots. Then add **F02 gallery/review and F03 circular viewing mask**. Choose **F04 temporal noise reduction or F05 calibration** based on measured image problems. Specify **F06 automatic hold and F07 sequence review** afterward. Add **F08 reports** when saved-session review is stable.

This is a product-priority suggestion; existing physical-camera, performance, Windows and acceptance gates remain applicable. It does not establish a new approved roadmap.

## Evidence and interpretation

The M2 observations below are retained from the supplied proposal and prior inspection; they were not independently revalidated when saving this document. Links into `/home/mo/code/ALI` and the Codex visualization directory are local provenance references outside this repository and may be unavailable on another machine. The feature definitions above remain usable without those files.

M2's packaged code contains camera processing, thumbnail review, dark-frame calibration, cine recording and report modules. Logs confirm circular ROI processing, averaging and automatic image freezing/capture. Source labels below are recovered bytecode locations inside the executable, not editable files in the supplied installation:

- `matrox_camera.processing_function`, source line 284: frame processing and averaging; `extract_custom_roi`, line 854: circular region processing.
- `controllers.main_controller._lih_freeze`, source line 1779: automatic held-frame selection. The [recorded capture sequence](</home/mo/code/ALI/M2 Medical Imaging/MX/logs/medical_imaging_20260813_064653.log:9011>) is the primary evidence for observed behavior.
- `processing.dark_frame_manager.DarkFrameManager`: reference capture, correction, persistence and invalidation methods.
- `views.widgets.thumbnail_viewer.ThumbnailViewer`: thumbnail creation and selection callbacks.
- `views.main_window.video.cine_recorder.CineRecorder`: recording start/stop and frame collection.
- `views.patient_report.ProfessionalPatientReport`: report generation and image layout.

The [M2 inspection report](/home/mo/.codex/visualizations/2026/09/10/01a08a60-3637-77d1-bf6b-e39242d3e81f/m2-gige-assessment/assessment.md) contains the prior analysis and retained evidence. Lumora's [processing extensions](../../prd.md#8-image-processing-requirements), [M9 UI plan](../superpowers/plans/2026-04-25-m09-presets-workstation-ui.md), [M10 capture plan](../superpowers/plans/2026-04-25-m10-snapshot-capture.md), and [v1 exclusions](../../prd.md#27-out-of-scope--v1) define the existing baseline.

No application code was changed or hardware tested while preparing or archiving this proposal. Recheck the current progress and approved plans before selecting an idea for implementation.
