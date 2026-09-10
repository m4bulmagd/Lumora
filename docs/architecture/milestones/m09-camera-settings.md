# M9 Task 4A: stopped exposure and gain settings

Date: 2026-09-10. Branch: `codex/m09-camera-settings`. Status: implemented, independently reviewed and committed locally; clean Linux Debug/Release and native verification passed. Unpublished. This is a bounded part of Task 4, not completion or acceptance of M9.

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

Independent task and whole-branch source reviews are approved with no remaining actionable findings. Development logs and screenshots are retained under `out/qa/m09-camera-settings/` in the preserved settings worktree; task/review records are under `.superpowers/sdd/2026-09-10-m09-camera-settings/`.

Hosted CI for Task 4A, native Windows 11 visual/DPI checks, hardware validation, the 30 FPS target and separate milestone acceptance remain open. The inherited intermittent lifecycle timeout remains unresolved; a passing rerun is not a synchronization fix.


## Final source verification

Implementation commit `28cb1c3` is followed by the reviewed compiler correction `0b6f8f4590bc700f79f3b22f55041a6e81c07f14`. The initial clean Debug run passed 62/62 in 64.86 s, but the subsequent Release build reported GCC 15 `-Wmaybe-uninitialized` during an inlined `std::variant` copy in the new command helper. The helper now borrows the caller's command synchronously instead of making an intermediate moved value; `LivePipeline::post` still receives and queues its own copy. No warning was disabled or timeout extended. Release rebuilt successfully and passed the eight focused registrations in 5.17 s. The failed build and earlier behavioral RED runs are preserved.

All final checks below ran from clean `0b6f8f4590bc700f79f3b22f55041a6e81c07f14`, with unchanged clean start/end revisions. Documentation-only completion follows this source.

| Check | Debug | Release | Evidence labels |
|---|---|---|---|
| Full simulator build, `cmake --build --preset linux-gcc-<configuration>-sim -j 3` | Passed | Passed | `verified-debug-build`, `verified-release-build` |
| Full headless suite, `ctest --preset linux-gcc-<configuration>-sim --output-on-failure -LE 'hardware\|desktop'` | 62/62, 65.95 s | 62/62, 26.67 s | `verified-debug-test`, `verified-release-test` |
| Native X11 desktop smoke, `xvfb-run -a ctest --preset linux-gcc-<configuration>-sim --output-on-failure -L desktop --no-tests=error` | 1/1, 0.12 s | 1/1, 0.06 s | `verified-debug-x11`, `verified-release-x11` |
| Actual dialog under XCB/Xvfb, `CameraSettingsDialog.NativeDialogLayoutFitsSupportedSizesAndCanBeCaptured` | 1/1, 0.087 s | Not separately run | `verified-native-dialog` |

The final native case checks 560×560 and 720×640 and saves two `camera-settings-final-<size>.bmp` captures. Both lossless PNG conversions were inspected: source/read-only fields, mode selectors, fractional values, actual readback, guidance and buttons remain readable and contained. This synthetic dialog fixture uses Mono8 metadata; it does not change SIM-LIVE's Mono12 acquisition. Xvfb screenshots are rendered-window evidence, not physical-display or native Windows DPI acceptance.

`out/qa/m09-camera-settings/verification-manifest.json` validates seven successful immutable command records, matching clean source revisions and SHA-256 log hashes. `final-screenshots.json` binds both original BMP hashes to the native command and source. `review-record.md` preserves preflight, task and final review records. The independent final evidence/documentation audit approved all seven command chains, both screenshot hashes/pixel-identical PNG conversions, review closures and Compare integration evidence. Its stale traceability-status finding was corrected; no actionable findings remain. The audit is preserved in the ignored execution ledger.

The branch remains local for the next publication decision and Linux/Windows Debug/Release CI. The next bounded development step is low-FPS watchdog coverage/correction before enabling frame-rate editing. Remaining Task 4 resource rebinding, capability/schema, per-camera and administrator-orientation work and Task 5 fullscreen/preferences remain separate. Neither the inherited lifecycle timeout nor the outstanding performance and acceptance gates is closed by these checks.
