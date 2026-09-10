# M9 Task 4A: stopped exposure and gain settings

Date: 2026-09-10. Branch: `codex/m09-camera-settings`. Status: implementation in local verification; unpublished. This is a bounded part of Task 4, not completion or acceptance of M9.

The owner authorized Compare publication, Linux/Windows Debug/Release CI and merge when both pass, followed by camera settings development. The [refined plan](../../superpowers/plans/2026-09-10-m09-camera-settings.md) extends the existing M5 startup contract and single preferences writer. [Compare integration](m09-compare.md) is recorded separately.

## Operator behavior

After connecting the selected camera, **Camera settings** opens one modeless dialog. While the camera is stopped, the operator may change supported exposure and gain modes and manual values. FPS, format, full ROI and acquisition mode are shown read-only. The dialog shows actual camera readback separately from the requested draft, including quantized values.

Editing or closing the dialog sends no camera command. **Apply settings** submits one complete request asynchronously. The operator reviews the actual readback, then explicitly uses the existing **Confirm** and **Start** controls. Applying new settings invalidates the previous confirmation. A failed or pending Apply cannot use older readback to Confirm or Start. Selecting another camera and returning resets the displayed request to prepared defaults; if these differ from the camera request, Apply and fresh confirmation are required before Start. There is no automatic stop, restart or Start. Viewer Pause continues acquisition and therefore does not enable camera editing; use **Stop** first.

Polling preserves unsubmitted edits. A replaced camera identity, session generation, capability set, selected source or external requested revision invalidates the open draft and requires reopening. Pending operations disable editing and Apply. Numeric editors preserve untouched fractional values; invalid capability/configuration combinations explain why Apply is unavailable.

Confirmed edited requests use the existing background persistence service. A later launch may offer explicit **Resume Live** only when the saved request remains compatible with the prepared source, matches the current desired request, and identity, capabilities and applied readback pass the existing comparisons. New unconfirmed edits cannot be bypassed using an older saved Resume. A late preferences load cannot overwrite a manually submitted request or explicit camera selection.

## Ownership and scope

`CameraSettingsDialog` owns only the unsubmitted draft. `CameraStartupPanel` owns the dialog and forwards its generation/identity-tagged intent. `WorkstationController` owns desired settings, command admission, revision guards and persistence. The application-level `isCameraSettingsCompatible` predicate permits exposure/gain changes while requiring the full source descriptor, complete ROI, optional FPS and acquisition mode to match the prepared request. Capability validation remains authoritative in the camera layer; the controller also validates before admission.

The existing device, session resources and source-ID sequence remain in use. Geometry/format edits need a separate resource-rebinding contract. FPS editing is deferred because the simulator advertises 1 FPS while the current acquisition policy retires a stream after three 250 ms retrieval timeouts; supporting low FPS requires independent watchdog tests and a correction. Current numeric capabilities do not express absent/general read-only features, so this slice does not invent that interpretation or change their schema.

Per-camera records, administrator-managed installation orientation, compact main-panel replacement and stop/apply/verify/restart remain later Task 4 work. Fullscreen and persisted UI layout remain Task 5. SIM-LIVE remains Mono12 in U16; the standalone M4 harness remains Mono8. No processing algorithm or performance measurement changes here.

## Development verification

Behavioral RED was recorded for the application compatibility policy, real dialog widgets, panel entry point and the initial seven controller integration cases. All 44 prior LivePipeline cases passed during that RED run. After implementation, eight focused registrations passed in 11.06 s, including all new camera settings, panel and pipeline tests. The native dialog case passed at 560×560 and 720×640; both captured layouts were inspected with readable labels, full-precision values, actual readback and visible buttons.

Independent Task A/C review passed. Task B review found that an intermediate controller publication could invalidate its own edited Apply, plus a missing-selection presentation mismatch. New real-controller post-Apply assertions and missing-selection widget cases reproduced both failures. The controller now publishes admitted desired settings and pending state together; panel and dialog require selection to match the actual camera. The corrected focused suite passes 8/8 in 11.33 s. Whole-branch review additionally found a selection-away-and-back path that displayed defaults but retained older camera confirmation. A real regression reproduced the mismatch; controller and panel now require exact desired/worker-request equality before Confirm/Start. Independent re-review approves all three corrections with no remaining source findings. The final focused eight-registration suite passes in 11.07 s, including 52 LivePipeline cases. Source-bound full checks follow.

The new integration cases cover stopped Apply/readback/fresh Confirm/Start, retained resources and source continuity, stale generation/identity and invalid-value rejection, pending/streaming rejection, persistence and next-launch Resume, newer unconfirmed edits, slow preferences loading, mismatched selected identity, failed Apply and priority Disconnect. Existing M5 startup and processing-control tests remain part of full verification.

Independent task and whole-branch source reviews are approved. Final source-bound verification will be recorded below after the clean source commit. Development logs and screenshots are retained under `out/qa/m09-camera-settings/` in the preserved settings worktree; task/review records are under `.superpowers/sdd/2026-09-10-m09-camera-settings/`.

Hosted CI for Task 4A, native Windows 11 visual/DPI checks, hardware validation, the 30 FPS target and separate milestone acceptance remain open. The inherited intermittent lifecycle timeout remains unresolved; a passing rerun is not a synchronization fix.
