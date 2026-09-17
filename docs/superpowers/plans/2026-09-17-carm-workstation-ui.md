# C-arm Workstation UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Root alone runs serial builds, tests and apps, with at most three compiler jobs.

**Goal:** Give Lumora a familiar, attractive C-arm workstation interface in which operators can capture Live, browse and adjust a saved Reference, and review cine without losing track of acquisition or which image a command affects.

**Architecture:** Retain one acquisition pipeline and its existing Live presenter. Compose a separate Reference renderer and review processing context into the QML workstation, with a durable-capture filmstrip, explicitly targeted inspector, and role-aware window layout. QML owns presentation and input; C++ adapters own image identity, action eligibility, persistence, and lifecycle.

**Tech Stack:** Existing C++20, Qt Quick/QML, Qt Quick Controls, Qt Test, GoogleTest/CTest, and the existing asynchronous configuration writer. No additional UI framework or new dependency is proposed.

**Spec:** [C-arm workstation design](../specs/2026-09-17-carm-workstation-design.md). Read its companion [capture/review plan](2026-09-17-carm-capture-review.md) before implementing shared adapters.

**Status:** Draft requested by the owner on 2026-09-17; the dark visual direction remains a reviewable proposal pending the owner’s final preference. This document updates the plan only; it does not assert implementation, verification, milestone acceptance, or approval of a clinical workflow. CR-1 through CR-6 below are bounded continuations alongside the existing roadmap, not replacements for its release gates.

## Global constraints

- This work contributes only to the engineering/evaluation release. Display `EVALUATION — NOT FOR CLINICAL USE`; do not acquire or store real patient data.
- Existing guarded source startup, installation-orientation authority, frame ownership, freshness, renderer retirement, and asynchronous writer rules remain authoritative.
- One acquisition owner serves every window. Opening Review, choosing a preset for Reference, moving a window, or changing a layout never starts, stops, or duplicates acquisition.
- Capture Live always captures the exact presented or paused **Live** bundle. Save copy acts only on the saved Reference draft. Neither command infers its target from keyboard focus.
- Live and Reference own separate image identity, processing recipe/preset selection, display mode, zoom and pan. Installing the same reusable preset into both is two explicit actions; changing one context does not change the other.
- Mandatory acquisition, paused/stale, saved-image, error, orientation, source identity, and evaluation indications stay visible with collapsed panels, fullscreen, and detached image windows.
- Video activity, viewer freeze, recording, and radiation status are separate facts. Without an authoritative exposure input, show `Radiation status unknown`; never infer `X-ray on` or `X-ray off` from video motion or camera state.
- Native Windows visual/DPI/hardware checks, existing performance acceptance, storage guarantees, signing, and clinical-release gates remain open until their own evidence exists. UI development does not waive them.

## Current code and intended boundaries

These observations describe the inspected source; everything identified as proposed below is future work.

| Existing file | Current responsibility | Planned boundary |
| --- | --- | --- |
| `src/qml/qml/Main.qml` | One window, source panel, one `ViewerSurface`, global processing panel | Compose named Live and Reference regions; keep startup actions accessible; delegate pane, filmstrip and inspector rendering |
| `src/qml/qml/Theme.qml` | Charcoal/olive palette; 34 px control height | Preserve palette; add measured contrast, semantic status/focus tokens and 44 logical-pixel action targets |
| `src/qml/qml/ViewingToolbar.qml` | Original/Enhanced/Compare, pause/resume and viewport controls for Live | Explicit pane-scoped view controls; Live freeze and Reference playback are separate controls |
| `src/qml/qml/ProcessingControls.qml`, `PresetControls.qml` and effect controls | Bind directly to live `ProcessingAdapter` | Keep Live binding; share only controls/value presentation with a separate Reference adapter |
| `src/qml/qml/StatusStrip.qml` | Camera/view state, frame age/orientation, processing fallback | Keep Live/source status; extract reusable image-status presentation without giving Reference a false Live label |
| `src/qml/QmlWorkstation.hpp/.cpp` | Owns one viewer, processing adapter and layout adapter | Own Live and Reference lifetimes plus capture/review services; expose explicit adapters |
| `src/qml/ViewerAdapter.hpp/.cpp`, `ViewerSurface.hpp/.cpp` | A surface visually attaches one `QuickImageItem` | Continue one visual attachment per item; never attach the Live adapter to both panes |
| `src/qml/QuickImageItem.hpp/.cpp` | Pixel sink, scene-graph ownership and presentation receipts | Reuse proven renderer behavior with a separately budgeted Reference instance |
| `src/presentation/include/lumora/presentation/PresentationProtocol.hpp` | Current renderer envelope follows Live frame ownership | Companion adds an honest saved-image presentation envelope; Processed-only captures must not invent a missing Original `FrameBundle` |
| `src/qml/LayoutAdapter.hpp/.cpp` | One-window geometry/fullscreen/panel preferences | Add role-aware layout/screen coordination; preserve geometry validation and layout-transition input cancellation |
| `src/application/include/lumora/application/UiPreferences.hpp` and configuration codec/service | Typed layout preferences using one document writer | Add versioned role, display, splitter and inspector preferences via that writer |
| `src/presentation/include/lumora/presentation/FramePresenter.hpp` | Exact presented bundle, pause, freshness and retirement | Authoritative Live capture source; Reference uses its own presentation lifecycle |
| `src/presentation/include/lumora/presentation/WorkstationCoordinator.hpp` | Serialized source and live processing policy | Remains the acquisition/live policy owner |
| `tests/integration/QmlWorkstationTests.cpp`, `QmlLayoutAdapterTests.cpp`, `QmlRuntimeTests.cpp` | Real-scene, window-policy and runtime coverage | Retain regressions; place the new capture/review and two-display cases in focused suites |

Proposed files owned by this UI plan:

| File | Responsibility |
| --- | --- |
| `src/qml/qml/WorkstationHeader.qml` | Source summary, evaluation label, capture action and essential source actions |
| `src/qml/qml/ImagePane.qml` | One role-labelled image region, pane toolbar, targeted focus and status |
| `src/qml/qml/ImageStatus.qml` | Role/state/time/source/orientation presentation shared by main and detached panes |
| `src/qml/qml/CaptureFilmstrip.qml` | Virtualized durable-capture thumbnails, selection and session disclosure |
| `src/qml/qml/ContextInspector.qml` | Explicit Live/Reference selector and context-specific controls |
| `src/qml/qml/ReferenceProcessingControls.qml` | Saved-image processing draft controls and Save copy/Discard edits actions |
| `src/qml/qml/ReferenceWindow.qml` | Detached Reference role with its own mandatory status and safe close behavior |
| `src/qml/qml/DisplaySettings.qml` | Identify displays, assign Live/Reference roles, restore one-screen layout |
| `src/qml/qml/CineControls.qml` | Recorded-loop transport, frame/time position and gaps |
| `tests/integration/QmlCaptureReviewTests.cpp` | Capture, filmstrip, independent targeting and edit workflows through real controls |
| `tests/integration/QmlDualDisplayTests.cpp` | Screen-role preferences, return-to-one-screen, focus and teardown behavior |
| `tests/integration/QmlCineReviewTests.cpp` | Recorded playback and scrub semantics without affecting Live |
| `docs/architecture/milestones/carm-workstation-ui.md` | Implementation evidence and remaining acceptance gates, created when execution produces evidence |

`CaptureAdapter`, `ReviewAdapter`, `GalleryModel`, `ReviewProcessingAdapter` and their backend services belong to the companion capture/review plan. Its `ReviewImageSet` and `ReferencePresenter` provide optional-original saved-image ownership independently of Live; adapter reuse must not fabricate unavailable raw samples. UI-1–UI-3 co-deliver the named QML files with their matching backend tasks; there is no separate temporary UI tree to replace in CR-4. Update `src/qml/CMakeLists.txt` and `tests/CMakeLists.txt` when their owning task adds a source; do not create parallel composition roots or register a second application. A UI task depending on an unfinished backend can land a test fixture or design prototype, but must not enable a production control whose command cannot fulfill its promise.

## Visual and interaction contract

### Default workspace

Use restrained dark surfaces, readable neutral text, olive accents for selection/active context, amber for warnings and a distinct danger token for actual errors. Keep the image background nearly black. Avoid gradients, decorative charts, large cards or unused dashboard space. Separate regions with fine borders and spacing, not competing bright frames. Color always accompanies text or an icon with an accessible name. Measure text contrast at least 4.5:1 and control/focus contrast at least 3:1. Prefer installed Noto Sans on Linux or Segoe UI on Windows with an explicit sans-serif fallback. Use tabular timestamp digits. Keep image changes immediate: no image crossfades, automatic zoom, or animation that obscures incoming frames. Capture Live retains a stable location while state changes.

```text
LUMORA    Source · acquisition state        Capture Live    Stop/Disconnect
EVALUATION — NOT FOR CLINICAL USE            Radiation status unknown
┌ LIVE · live/frozen/stale ───────┐ ┌ REFERENCE · saved ──────┐ ┌ Adjust ───┐
│ Original | Enhanced | Compare  │ │ image label/time · lock │ │ Live     │
│                               │ │ Original | Enhanced    │ │ Reference│
│       current Live view       │ │     saved still/cine    │ │ preset   │
│                               │ │                         │ │ view     │
│ source · orientation · time   │ │ source · orientation    │ │ effects  │
└───────────────────────────────┘ └─────────────────────────┘ └───────────┘
Captures · session     [still] [still] [derived] [cine 00:12]    Browse sessions
Capture result / storage problem                    persistent critical state
```

The drawing describes hierarchy, not exact pixel dimensions. Live stays left and Reference right by default. The bottom filmstrip contains **successful durable captures**. Pending saves appear in a small capture-status area, not as successful thumbnails. Selection highlights, reference lock and inspector target use different shapes/text so they are distinguishable.

- Reference starts with `No reference selected` and `Capture Live or choose a saved image`. Do not populate it with a duplicate unsaved Live feed.
- Inspector title reads `Adjust Live` or `Adjust Reference — <capture label>`; the chosen pane receives a subtle labelled focus outline. Clicking a pane selects its inspection target. Explicit thumbnail/open-reference selection starts the inspector on Reference. Automatic following after Capture Live never silently retargets the inspector.
- Preset selection, window/level, enhancement, zoom, pan, Fit and 100% apply only to the named context. Arbitrary image rotation/flip and measurement are outside these phases; installation orientation remains separately administrator-managed. Camera setup remains a separate acquisition section, never a Reference inspector control.
- A within-pane Original/Enhanced toggle is the usual presentation. Existing same-frame Compare remains available under `Compare original/enhanced`; it splits only that pane and preserves the other role. Compare is never renamed Reference. If room is limited, close the inspector before invoking Compare, or offer an explicit focus view; never remove Live automatically.
- `Pause Live` retains the displayed bundle and reads `PAUSED — FROZEN LIVE VIEW` with its frozen timestamp. `Resume Live` resumes presentation. Both leave acquisition policy unchanged; Stop remains a distinct camera action.
- `Capture Live` is the primary action. A secondary mode selector exposes Original, Processed or Both and reflects capability; unavailable processed data cannot produce a misleading enabled option. With frozen Live, helper text states `Capture frozen Live image`.
- A saved capture displays `SAVED REFERENCE`; a derived draft adds `Unsaved adjustments`; cine displays `RECORDED CINE`. A saved image’s historical timestamp does not make it a stale Live feed.
- Reference lock reads `Reference locked` / `Follow latest capture`. Lock suppresses automatic following only. Explicit thumbnail/session browsing continues to work while locked and updates the locked selection. Successful saves remain visible in the filmstrip regardless of lock.
- Review never navigates to a whole-window page that silently replaces Live. The session drawer is nonmodal and closes without changing acquisition. Deletion and actual unsaved-edit loss may require confirmation; ordinary capture, pin, lock, browsing, opening panels and layout changes do not.
- Keep the header's active capture destination separate from the filmstrip's browsed session. `CaptureAdapter.captureSessionId` and `ReviewAdapter.browsingSessionId` are different state. Browsing an earlier session suspends automatic following and exposes Return to current session; it never redirects Capture Live or changes acquisition. New/Use capture session is a separate explicit action, unavailable during recording; pending still saves retain their admitted destination. Save copy remains in its parent capture's session. Read-only legacy groups are view-only.

### Sizing and input

| Available logical size | Layout policy |
| --- | --- |
| 1920×1080 preferred | Side-by-side Live/Reference, visible inspector around 280 px, filmstrip and essential actions |
| 1280×800 baseline | Side-by-side panes, inspector around 240 px, compact labels, expandable advanced effects |
| 900×600 minimum | Side-by-side panes remain visible; inspector defaults closed and opens as a nonmodal overlay within the image workspace; it cannot cover pane status, the global capture/source actions or the Live return control; horizontal filmstrip scrolls |
| Screen work area below 900×600 after DPI/hotplug | Present an explicit compact focus choice. Until selected, fit a stacked Live/Reference fallback with inspector closed. Focus view always includes a persistent `Live hidden · <live state>` indicator and `Show Live` action, plus Stop and Capture Live. Screen recovery offers restoration without auto-starting or discarding Reference. |

Ordinary supported windows keep the existing 900×600 minimum. Use logical coordinates and screen available geometry rather than physical-pixel assumptions. The below-minimum fallback exists for recovery from screen/DPI changes, not as a claim of accepted operation on every tiny display.

Routine buttons, thumbnail actions, toggles and slider handles get at least 44×44 logical-pixel hit areas, including in compact mode. Text fields need visible labels, units and an exact-entry route. Use 14–16 logical px body/action text and essential state at least 14 px; do not solve overflow by shrinking mandatory text. Critical messages wrap in a reserved status region. Low-importance metadata may elide with an accessible full description.

Tab order follows source/capture controls → Live → Reference → inspector → filmstrip. Provide clear focus rings against all dark surfaces. Existing F11/Escape fullscreen and F/1/+/- viewport shortcuts remain. Space is scoped to the focused image: freeze/resume Live or play/pause recorded cine in Reference; text inputs, sliders and popups consume their normal keys. Provide `F9 — Capture Live` in the action tooltip/help, ignore auto-repeat, and disable that shortcut during a blocking source-confirmation or unsaved-edit dialog. Do not bind Ctrl+S to an ambiguous cross-context save. Filmstrip arrows move thumbnail focus, Enter opens, and Home/End move to the first/last loaded item; lazy loading preserves visible selection by capture ID.

### State vocabulary and priority

| Condition | Visible state and next action |
| --- | --- |
| Source not configured, no frame | `Live unavailable — set up source`; retain source setup and saved Reference browsing |
| Source connecting/starting, no frame yet | `Waiting for Live image`; disable Capture Live and show an actual busy indicator, not an invented progress percentage |
| Live current | `LIVE`; named source and acquisition state remain visible |
| Viewer freeze | `PAUSED — FROZEN LIVE VIEW · <time>`; frame age continues, acquisition status remains separate; Resume Live available |
| Freshness timeout | `STALE IMAGE — NOT LIVE` with source/time; never remove the existing timeout overlay to make the layout quieter |
| Source disconnected after an image | Keep invalidated last Live image, stale/disconnected indications and explicit reconnect path; saved Reference remains usable |
| Reference loading | `Loading reference…`; preserve the previous image only with an explicit `Previous reference` label until replacement commits; controls cannot accidentally edit the previous capture under the new label |
| Capture save queued/running | `Saving capture…` with bounded job count; keep capture target identity stable; no success thumbnail before durable completion |
| Capture queue full | `Capture busy — wait for saving to finish`; do not drop the operator’s action silently or freeze Live |
| Storage unavailable/full/write denied | `Capture not saved` with concise cause and a contextual recovery action; preserve review draft, successful gallery entries and acquisition |
| Publication outcome uncertain | `Save outcome uncertain — checking` with the existing capture identity; retain draft/selection, reconcile that transaction and show a verified or unresolved result; never offer a silent duplicate save |
| Saved item missing/corrupt/unsupported | `Reference unavailable` and a reason; Retry/Open another available; do not show unrelated pixels under the requested item identity |
| Reference from another source/orientation | Keep saved provenance visible with `Different source` or `Different saved orientation`; never apply current installation orientation to old pixels automatically |
| Reference cannot be reprocessed | Explain `Original source not stored — viewing only`; disable source-dependent edits, retain viewing/pan/zoom and native saved representation |
| Unsaved Reference edit | `Unsaved adjustments`; Save copy / Discard edits; context changes guard actual draft loss only |
| Radiation input absent/stale | `Radiation status unknown`; camera streaming is not evidence of exposure |

Critical stale/disconnected/failure state takes precedence over success toasts. A transient saved acknowledgement must not cover a warning. Image role, source, timestamp and orientation follow the image into detached windows and remain visible in fullscreen. Recordings display recorded frame position/time and gaps; they must never inherit a green `LIVE` badge.

## Shared UI-facing interfaces

These are proposed contracts for implementation, not properties that exist today. The companion plan owns backend translation. IDs are opaque `QString` values at the QML boundary; QML does not derive filenames, infer source sessions, or interpret protocol counters.

```text
QmlWorkstation.viewer: ViewerAdapter*                // existing Live view
QmlWorkstation.processing: ProcessingAdapter*        // existing Live recipe
QmlWorkstation.capture: CaptureAdapter*              // proposed CR-1
QmlWorkstation.review: ReviewAdapter*                // proposed CR-2
QmlWorkstation.reviewProcessing: ReviewProcessingAdapter* // proposed CR-3

CaptureAdapter.captureLive(QString mode)             // "original", "processed", "both"

ReviewAdapter.viewer: ViewerAdapter*                 // independent Reference renderer/view state
ReviewAdapter.gallery: GalleryModel*
ReviewAdapter.selectedCaptureId, selectedSessionId, selectedLabel: QString
ReviewAdapter.loading, referenceLocked, hasSelection, canEdit, dirty: bool
ReviewAdapter.error: QString
ReviewAdapter.openCapture(QString id)
ReviewAdapter.openSession(QString id)
ReviewAdapter.setReferenceLocked(bool locked)
ReviewAdapter.clearReference()
ReviewAdapter.pinCapture(QString id)                 // loads persisted item, locks on success; no save

ReviewProcessingAdapter.dirty, pending, canSaveDerived: bool
ReviewProcessingAdapter.error: QString
ReviewProcessingAdapter.discardEdits()
ReviewProcessingAdapter.saveDerived()
```

The inspector does not forward Reference edits to `ProcessingAdapter`. `ReferenceProcessingControls` binds to the separate Reference processing API supplied by CR-3; reusable visual controls take values and emit intents rather than storing live command pointers. `ViewerAdapter` may expose common view operations to either pane, but Reference role/status comes from review metadata, not the existing `ViewerState::Live` enum. Any refactoring must preserve public Live semantics and existing test coverage.

An explicit selection while Reference is dirty enters Save copy / Discard edits / Cancel. Save waits for durable completion before switching; failure keeps the draft and selected item. Automatic following is suppressed during a dirty draft or active edit operation even when unlocked, with the new saved capture visible in the filmstrip; show `Following paused while editing` until the prior follow/lock policy resumes. Target switches that retain the same Reference draft need no confirmation. Uncommitted numeric text is cancelled on layout/target changes rather than submitted to another context. `pinCapture` changes lock only after the requested saved image is successfully presented; failure preserves the existing Reference and lock. `openCapture` changes selection without changing lock.

`CommitIndeterminate` does not clear a dirty draft or complete pending navigation/close. Show its uncertain status separately from successful filmstrip items and reconcile the existing key. Positive reconciliation matching the submitted selection/draft may promote a clean baseline, including standalone Save copy. Resuming browse/close additionally requires its current navigation token. New edits/selection prevent promotion; Cancel invalidates navigation only and never rolls back the copy or prevents catalog verification. An unresolved outcome offers Check save; it does not automatically resubmit a second copy.

## Implementation tasks and dependency order

The companion backend plan owns storage, reconstruction, source fidelity and command admission. This UI plan owns the visible workflows and scene verification. Land each phase only after its required backend behavior is available and verified; do not add fake success states to satisfy screenshots.

### Task UI-1 / CR-1: Clear Live identity, capture action and save feedback

**Depends on:** CR-1 immutable capture submission, durable result and storage-failure contract. **Produces:** One unambiguous Capture Live action and truthful success/failure feedback in the existing single-Live layout.

**Files:** Modify `Main.qml`, `Theme.qml`, `ViewingToolbar.qml`, `StatusStrip.qml`, `QmlWorkstation.hpp/.cpp`, `src/qml/CMakeLists.txt`, `tests/CMakeLists.txt`; create `WorkstationHeader.qml`, `ImageStatus.qml`, `tests/integration/QmlCaptureReviewTests.cpp`. Capture adapter files remain companion-owned.

- [ ] Add scene cases `captureLiveUsesLiveWhileFocusElsewhere`, `frozenLiveCaptureKeepsFrozenTimestamp`, `pendingSaveIsNotSuccess`, and `failedSaveLeavesLiveUsable`. Use SIM-LIVE and a temporary capture root; hold/release the worker via the capture plan’s deterministic test seam. Assert successful saved metadata against the presented Live bundle at admission, not against the later newest camera frame.
- [ ] Add `uncertainSaveReconcilesWithoutDuplicate` for rename-success/barrier-failure and `lateDerivedReconciliationKeepsNewerDraft` in UI-3. Verify uncertain status is distinct from failure/success, Check save retains the key, and navigation/close remains guarded until matching verified completion.
- [ ] Run the new scene suite and observe failures for the absent action/status contract. Register a CTest `Qml.CaptureReview` test backed by `lumora_qml_capture_review_tests`, following the existing Qt test environment.
- [ ] Introduce the role/status header and capture action. Keep Capture Live outside the inspector, attach its mode explicitly, and raise routine action hit areas to 44 logical px. The essential command binding is:

```qml
Button {
    objectName: "captureLiveButton"
    text: qsTr("Capture Live")
    implicitHeight: 44
    onClicked: workstation.capture.captureLive("both")
}
```

  The example shows only command routing; production enabled/pending/mode state must consume the CR-1 eligibility/result contract, including no frame, processed unavailable and busy cases. Do not use `root.activeFocusItem`, the inspected pane, or the Reference viewer to select the bundle.
- [ ] Verify freeze copy, capture mode capability, F9 non-repeat behavior, source-disconnected status and save failure using real controls. Assert camera state and displayed-frame count continue appropriately during the held save.
- [ ] Run focused adapter/capture/scene regressions, record the exact implementation revision and inspect live/frozen/stale/saving/error scenes before integrating this task.

### Task UI-2 / CR-2: Dual contexts, durable filmstrip and Reference lock

**Depends on:** CR-1 and CR-2 bounded gallery/query, immutable saved-image loading, independent Reference presenter and durable selection policy. **Produces:** Side-by-side Live/Reference with filmstrip and browse/pin workflows.

**Files:** Create `ImagePane.qml`, `CaptureFilmstrip.qml`, `ContextInspector.qml`; modify `Main.qml`, `ViewingToolbar.qml`, `ImageStatus.qml`, `QmlWorkstation.hpp/.cpp`, module registration and `QmlCaptureReviewTests.cpp`. Companion owns `ReviewAdapter`/`GalleryModel` and new renderer composition; this task reviews the resulting UI integration.

- [ ] Add `browseSavedReferenceDoesNotChangeLive`, `successfulCaptureFollowsUnlessLocked`, `lockedReferenceAllowsExplicitBrowsing`, `loadFailureDoesNotMislabelPreviousPixels`, and `galleryShowsDurableResultsOnly`. Use two captures with distinct synthetic fiducials/timestamps/source identities; inspect actual rendered pixels as well as labels.
- [ ] Observe failing scenes, then introduce the two retained pane components with distinct object names. Bind them to separate viewer instances:

```qml
ImagePane {
    objectName: "livePane"
    roleLabel: qsTr("Live")
    viewer: workstation.viewer
}
ImagePane {
    objectName: "referencePane"
    roleLabel: qsTr("Reference")
    viewer: workstation.review.viewer
}
```

  Define `ImagePane.required property string roleLabel` and `required property ViewerAdapter viewer`; supply its complete status from the role-specific adapter. A shared renderer item is forbidden: `ViewerSurface` currently reparents its item, so sharing would steal one pane’s image.
- [ ] Implement the filmstrip as a virtualized horizontal list backed by stable capture IDs. Use a fixed thumbnail size and the companion’s bounded thumbnail cache; no synchronous file decode or full-resolution image loads in QML delegates. Add saved/derived/cine type labels, current selection, lock indication and session disclosure.
- [ ] Implement explicit inspector target selection and Reference lock actions. Keep Original/Enhanced/Compare inside the selected pane; Compare cannot consume the second role. Browse saved sessions without stopping Live; asynchronous query changes clear stale selection affordances until their results match the requested session.
- [ ] Verify at 900×600, 1280×800 and 1920×1080, with no Reference, loading, saved, locked, different-source and corrupt-item states. Assert pane rectangles stay nonempty, status and Capture Live are visible, and browsing changes only Reference identity/transform.
- [ ] Run focused suites and source-lifecycle regression tests; inspect the saved/reference filmstrip scenes and record CR-2 evidence.

### Task UI-3 / CR-3: Explicit Reference adjustments and derived saves

**Depends on:** CR-2 plus CR-3 independent review recipe worker, saved-original availability policy and non-destructive derived transaction. **Produces:** A Reference-only processing draft with Save copy, discard and truthful failure handling.

**Files:** Create `ReferenceProcessingControls.qml`; modify `ContextInspector.qml`, `ImageStatus.qml`, `CaptureFilmstrip.qml`, `ProcessingControls.qml`/shared effect controls only where presentation reuse is useful, and `QmlCaptureReviewTests.cpp`.

- [ ] Add `referencePresetDoesNotChangeLiveRecipe`, `livePresetDoesNotChangeReferenceDraft`, `targetSwitchCancelsUncommittedText`, `saveDerivedPreservesOriginalAndLive`, `dirtyBrowseSaveDiscardCancel`, and `failedDerivedSaveKeepsDraft`. Assert recipe identities, source samples and selected capture IDs, not only field values.
- [ ] Run the cases to establish the missing context isolation/guard behavior.
- [ ] Compose separate controls under an explicit target rather than rebinding the existing Live processing command owner:

```qml
StackLayout {
    currentIndex: inspector.targetRole === "live" ? 0 : 1
    ProcessingControls { processing: workstation.processing }
    ReferenceProcessingControls { processing: workstation.reviewProcessing }
}
```

  Define `ContextInspector.targetRole` as the validated strings `"live"` or `"reference"`; define `ReferenceProcessingControls.required property ReviewProcessingAdapter processing`. The inspector calls each current editor’s cancellation path before changing target. The Reference controls expose their own recipe values and edit intents; their API is established by CR-3 before this binding ships.
- [ ] Add `Save copy` and `Discard edits` adjacent to the Reference draft state. Disabled editing explains missing Original source or unsupported recorded content. Saving shows pending state. Durable success promotes the new derived capture to the clean Reference baseline only when submitted selection and draft revision still match; otherwise add it to the gallery and retain newer selection/edits. The original remains selectable and unchanged.
- [ ] Guard only actual Reference-draft loss on replacement, clearing or application close. Save failure leaves the guard/draft recoverable; Cancel keeps current selection. Clicking Live or changing inspector target preserves the draft without a modal. Automatic following cannot discard it.
- [ ] Verify numeric entry, preset changes and pan/zoom against both contexts while Live advances; run relevant processing/persistence/scene regressions and inspect dirty/failed-save copy.
- [ ] Exercise deferred/throttled Reference processing while Live remains enhanced. Keep the previous complete Reference visible with a pending indication; inspector target, unsaved draft and Live settings remain unchanged. Do not imply a fixed processing rate before the concurrent profile is measured.

### Task UI-4 / CR-4: Two screens, responsive layout and usability finish

**Depends on:** CR-2 stable dual lifecycles, CR-3 draft handling, approved renderer memory bounds for two contexts. **Produces:** Persisted display roles and usable one/two-screen layouts through DPI, hotplug, fullscreen and shutdown.

**Files:** Create `ReferenceWindow.qml`, `DisplaySettings.qml`, `tests/integration/QmlDualDisplayTests.cpp`; modify `LayoutAdapter.hpp/.cpp`, `QmlWorkstation.hpp/.cpp`, `UiPreferences.hpp`, existing UI preference codec/service/tests, `Main.qml`, `Theme.qml`, `ContextInspector.qml`, `ImageStatus.qml`, and build/test registration.

- [ ] Add `restoreDisplayRolesByIdentity`, `missingReferenceScreenReturnsReferenceToMain`, `liveScreenRemovalKeepsAcquisitionAndStatus`, `screenChangeRetainsFrozenAndDirtyContexts`, `mixedDpiKeepsControlsReachable`, `compactLayoutRequiresExplicitFocusChoice`, and `closeReferenceWindowKeepsLiveRunning`. Mock screen inventory only for deterministic policy tests; native visual checks remain required for actual screen-graph/window-system behavior.
- [ ] Observe failing layout cases. Extend typed preferences with role assignments, normal geometry per window, inspector visibility/target and safe splitter fraction. Use display identity plus validated available geometry; duplicate/missing identities fall back to a usable single-screen split with a visible notice. Add version/migration cases to the existing writer’s tests; never add `QSettings` or another writer.
- [ ] Implement Identify displays / Assign Live / Assign Reference / Use one screen. Persist role choices only after explicit assignment. Main is the acquisition-owning workstation; closing the detached Reference window docks it back. The detached window exposes source/evaluation/radiation state and a route to the Live window and essential Stop action.
- [ ] Move presentation between windows using the existing retire/rebind/acknowledgement discipline. Freeze, selected saved item, recipe and viewport survive the move. Budget full-resolution buffers, textures and replacement overlap for both contexts, including within-pane Compare. If a requested layout exceeds the verified budget, leave the working layout intact and explain the rejected change.
- [ ] Apply the sizing/input contract, 44 px targets, focus indicators and keyboard behavior. At minimum size the inspector overlay remains nonmodal and does not cover mandatory status or Capture Live. In constrained recovery, an explicit Reference focus view always displays `Live hidden`, current Live state and Show Live. Fullscreen preserves mandatory status in each image window.
- [ ] Run focused layout/runtime/renderer/scene suites, then inspect native scenes across software and threaded hardware rendering, DPI 1/1.25/1.5/2, negative monitor coordinates, swapped primary display and disconnect/reconnect of each role. Windows mixed-DPI and real two-screen hotplug remain named external gates until executed there.
- [ ] Record representative scenes at all three supported sizes and both display roles. Perform a keyboard-only capture → lock → browse → adjust → derived-save → return-live walkthrough, then integrate the reviewed task with evidence.

### Task UI-5 / CR-5: Cine review without ambiguous Live controls

**Depends on:** CR-1–CR-4 and the companion CR-5 bounded cine recorder/player, dropped-frame/timebase metadata, finalized loop persistence and capacity limits. Backend preparation may start after CR-3; these user-visible controls wait for CR-4 layout/state acceptance. **Produces:** An explicit Record Live action and clearly recorded Reference transport.

**Files:** Create `CineControls.qml`, `tests/integration/QmlCineReviewTests.cpp`; modify `WorkstationHeader.qml`, `ImagePane.qml`, `ImageStatus.qml`, `CaptureFilmstrip.qml`, `ContextInspector.qml` and build/test registration. Recorder/player adapter signatures come from CR-5 before these controls are enabled.

- [ ] Add `recordingDoesNotReplaceCaptureLive`, `cinePlaybackNeverControlsLive`, `scrubHasRecordedTimestampAndGapState`, `loopSaveFailureDoesNotLookSaved`, `freezeViewDoesNotStopRecording`, and `recordingCapacityFailureKeepsLiveUsable`. Exercise a synthetic loop with visible frame numbers and an intentional timestamp gap.
- [ ] Observe failing scenes, then add a separate `Record Live`/`Stop recording` action with elapsed recording time and capacity/error state. Viewer freeze does not imply recorder pause; show that distinction explicitly. Cine cannot appear as a successful filmstrip item before finalization succeeds.
- [ ] Add play/pause, frame step, scrub, position/duration, speed and loop toggle inside Reference. Each button targets the recorded player, not `ViewerAdapter::pause()` on Live. Show `RECORDED CINE` during playback and pause, recorded frame time and gaps; the playback cursor never implies a live clock.
- [ ] Add **Save selected frame** inside Reference, separate from **Capture Live**. Bind the acknowledged recorded frame's original incoming pixels, time and scene/frame lineage at admission, saving into its scene's session through bounded capture admission. Disable before a complete eligible recorded frame is available. Keep scene selection/playback unchanged; show saving, busy, failure or uncertain status against that frame. Enhanced derivatives use the separately identified review recipe/Save copy contract.
- [ ] Add `saveSelectedCineFrameUsesAcknowledgedFrame`: scrub a numbered loop while Live advances, save a known frame, then advance playback/seek before encoding completes. Assert stored pixels, timestamp, parent scene/frame and destination still match the selected frame. Busy, failure and uncertain outcomes must never substitute Live, a later playback frame or a new capture key.
- [ ] Preserve reference lock/browse behavior for successful loops. Changing selected cine stops its old playback and retires its rendering owners before replacement. Viewing controls remain available while Live continues; per-frame capture/processing actions appear only when CR-5 defines a supported immutable source and derived-save contract.
- [ ] Verify seeking at beginning/end, absent frames, dropped gaps, long bounded loops, corrupted records, offline source and simultaneous Live save/recording pressure. Run focused suites plus renderer/resource regressions; record native playback scenes and measured limits without claiming real-time acceptance from screenshots.

### Task UI-6 / CR-6: Authoritative exposure/event indications

**Depends on:** A separately selected and validated event source with CR-6 identity, timestamp, freshness and reconnect semantics. **Produces:** Exposure/event indications distinguishable from video/recording status, only when their truth is available.

**Files:** Modify `WorkstationHeader.qml`, `ImageStatus.qml`, `StatusStrip.qml`, the owning event adapter integration in `QmlWorkstation.hpp/.cpp`, and `QmlCaptureReviewTests.cpp`/`QmlDualDisplayTests.cpp`. The companion CR-6 service owns event interpretation; QML never detects radiation from image brightness or motion.

- [ ] Add `videoStreamingDoesNotAssertExposure`, `missingOrStaleEventSourceShowsUnknown`, `eventSourceMismatchCannotLabelCurrentLive`, and `allWindowsShowExposureState`. Use synthetic authoritative events with deliberate stale, reordering and identity mismatch cases.
- [ ] Observe failing scenes, then bind the explicit event state and operator-facing explanation. Keep `Radiation status unknown` when no authoritative source is configured or currently valid.
- [ ] Distinguish Live video, recording active, detector/generator exposure event and unknown/stale event status in every image window. Keep raw protocol messages, sequence counters and transport addresses in diagnostics.
- [ ] Verify disconnect/reconnect and source changes cannot leave a stale reassuring status; run event-policy and scene suites and record the bounded source-specific evidence. Do not claim exposure-control or clinical safety certification.

## User-task acceptance matrix

| Operator task / injected condition | Required result | Evidence |
| --- | --- | --- |
| Start with no source and no captures | Clear empty Live and Reference states; setup available; Capture Live disabled | Real-scene keyboard/pointer check at minimum and baseline sizes |
| Capture current Live while Reference/inspector has focus | Saved object matches exact presented Live bundle; successful item appears only after durable save | Capture metadata/pixel assertion plus UI selection check |
| Capture frozen Live | Frozen identity/time retained; save says it used frozen Live; acquisition state remains independent | Deterministic bundle assertion and timestamp scene |
| Browse a past session during Live | Reference changes; Live keeps source/session/recipe and continues presenting | Two distinct pixel fiducials plus live frame-count evidence |
| Lock Reference, then capture again | Existing Reference retained; new durable capture appears in filmstrip | UI lock state and durable-model count |
| Browse another item while locked | Explicit selection succeeds and remains locked; no implicit save | Stable ID assertions and storage count |
| Adjust Reference or apply its preset | Live recipe and viewport unchanged; draft displayed as unsaved | Recipe/transform/source assertions |
| Save copy, then reopen original | New durable child; original bytes/metadata unchanged; both browsable | Storage lineage and round-trip scene |
| Try to browse/close with dirty Reference | Save/Discard/Cancel preserves each documented choice; save failure keeps draft | Deterministic writer failure and input walkthrough |
| Open missing/corrupt item or switch during slow load | Truthful loading/error state; no wrong image labelled as requested capture | Controlled out-of-order completion and image-identity assertions |
| Storage full, permission denied, root offline, queue full | No success thumbnail/toast; clear cause; draft/selection retained; Live usable | Injected backend result plus live progress assertion |
| Source disconnect or stale Live while reviewing | Last Live visibly invalidated; Reference stays saved/usable; capture eligibility follows backend policy | Clock-driven freshness and offline scenes |
| Load Reference from different source/orientation | Saved provenance visible; mismatch called out; current orientation does not rewrite it | Two-source metadata/pixel fixture |
| Enter fullscreen, collapse panels, switch inspector | Mandatory status/actions remain accessible; no camera command or unintended numeric commit | Input/command spies plus rendered scene |
| Hot-unplug either display, then reconnect | Usable remaining window; acquisition unchanged; explicit/persisted role recovery; draft retained | Policy tests and actual two-monitor native run |
| Replay/step/scrub saved cine while Live advances | Recorded role/time/gaps clear; transport changes only Reference | Numbered loop plus Live-frame progress assertions |
| Save selected cine frame, then continue playback/seek | Exact admitted recorded frame and lineage saved in its parent session; Live and scene transport unaffected | Pixel/identity/lineage assertions with delayed storage and injected failures |
| Event input absent/stale but video moving | Radiation status unknown, including detached windows | Synthetic event/clock test |

## Verification and delivery gates

Document-only planning requires link/path review and consistency with the companion plan; it does not require application builds and does not create a passing implementation record. During implementation, every task uses a failing behavioral test before its production change and records passing results afterward. Use existing Qt scene helpers for actual clicks, polish completion and viewport inspection rather than tests that merely search QML text.

Register the proposed suites explicitly; until added, these names are expected to be absent:

```sh
cmake --build --preset linux-gcc-debug-sim --parallel 3
ctest --preset linux-gcc-debug-sim -R '^Qml\.(CaptureReview|DualDisplay|CineReview|Workstation|Runtime|LayoutAdapter|ImageRenderer)$' --output-on-failure
```

For each integrated phase, rerun the affected backend/adapter tests and source-bound Debug/Release suites after the final change. Use the existing native desktop registrations for software/OpenGL rendering, supplement with the threaded renderer and supported DPR checks, and inspect captured scenes. Update only relevant evidence rather than rerunning unrelated performance campaigns for cosmetic text changes. Matching Windows/MSVC simulator CI and deferred native Windows/hardware/performance gates stay separate.

UI verification must include readable text/contrast, clipped bounds, target hit areas, focus order, minimum-size overflow, keyboard/text-input collisions, saved-image identity, unexpected automatic acquisition, processing-context isolation, memory bounds and renderer ownership through shutdown. A screenshot alone cannot prove those behaviors. Native visual evidence should show empty, loading, current Live, frozen, stale, disconnected, saved Reference, dirty Reference, write failure, different-source and recorded cine states at the sizes where they matter.

Record implementation revision, commands, environment, results, inspected scenes and unexecuted platform checks in the milestone record. Keep this plan’s checkbox state truthful. No task is complete because its controls merely render; its end-to-end behavior, adverse states and resource/lifecycle constraints must pass first.
