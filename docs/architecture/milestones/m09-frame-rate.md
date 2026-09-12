# M9 Task 4B: low-FPS acquisition and stopped frame-rate editing

Date: 2026-09-10. Branch: `codex/m09-frame-rate`. This continuation follows the merged [Task 4A exposure/gain dialog](m09-camera-settings.md#pr-20-integration) under the [bounded implementation plan](../../superpowers/plans/2026-09-10-m09-frame-rate.md). Implemented, independently reviewed and merged through [PR #21](https://github.com/m4bulmagd/Lumora/pull/21) after Linux/Windows Debug/Release CI passed; see the [integration record](#pr-21-integration). This is a partial Task 4 implementation, not M9 acceptance.

## Operator behavior

While stopped, **Camera settings** now offers an explicit numeric frame rate alongside exposure and gain. SIM-LIVE advertises 1–60 FPS with increment 1. The editor uses the connected camera's bounds and increment; it does not impose those simulator limits on other capabilities. Viewer Pause continues acquisition, so use **Stop** to edit camera settings.

Editing remains a local draft. **Apply settings** submits the complete request; the operator reviews separate actual readback and explicitly uses **Confirm**, then **Start**. Off-increment requests remain exact: requested 1.25 FPS is submitted as 1.25, while the simulator reads back actual 1 FPS. Ordinary Apply retains the edited request. There is no automatic stop or restart and no Auto FPS option.

Compatible stopped FPS changes retain the device, live context, raw/U16/display pools, source generation and frame-ID sequence. The presenter, paused image and viewport transform remain in place. Pixel format, full ROI and acquisition mode remain read-only because their resource-binding contract is separate.

The existing source/selection/capability/revision invalidation and pending-operation guards remain. Confirmed requested and actual FPS persist through the existing writer. Explicit Resume requires the saved request to match current desired settings, and fresh actual readback to match the saved actual settings exactly. A changed actual FPS stays idle with `startup_readback_changed`; new unconfirmed edits cannot use an older Resume record, and late preference loading cannot overwrite submitted edits.

## Acquisition timeout contract

The old worker retired a stream after its third `Acquisition / acquisition_timeout` result. A healthy real-time 1 FPS simulator produced ordinary 250 ms retrieval timeouts and was retired after about 751 ms, before its second frame.

The worker now evaluates elapsed time with the injected steady clock. Exact acquisition timeouts become terminal only after no accepted frame for at least **max(750 ms, three applied-actual frame periods)**. At 1 FPS this is 3 s; at 2 FPS, 1.5 s; from 4 FPS upward, the 750 ms floor applies. The latest successfully applied actual FPS is authoritative, including before the first frame; neither the prepared/requested FPS nor timeout count supplies the deadline.

Successful Start arms a private progress timestamp. A valid accepted frame renews it; successful Stop and cleanup disarm it. Restart after idle gets a fresh timestamp, while an idempotent Start during Streaming does not extend the deadline. Historical `lastAcquiredAt` still records only accepted frames. Invalid/null/wrong-ROI frames and pool exhaustion remain drops and do not renew progress; only a subsequent exact acquisition timeout invokes this terminal rule. Error ownership, counters, source replacement and manual Retry remain unchanged.

Each device retrieve retains its 250 ms budget. A quick exact timeout is padded only to the end of that same real retrieval slice, using cancellation-aware waiting. This prevents rapid timeout spinning without adding another full delay after slow retrieval. Stop and Disconnect retain their existing controlled 500 ms completion allowance; Shutdown interrupts cooperative retrieval or padding. Real polling time is separate from injected policy time. Unsigned elapsed tick arithmetic avoids signed epoch-subtraction overflow and reciprocal FPS-to-integer-duration conversion.

Viewer freshness remains an independent existing policy: max(500 ms, three actual frame periods), using the newest completed frame's actual FPS. No presenter API or production timing change was needed. A same-presenter 30 → 1 → 60 regression verifies changing rates without source replacement.

## Numeric precision boundary

Review identified that 17 fixed decimal places could round a requested sub-1 FPS value, and could round a tiny positive capability bound to zero. The FPS editor now configures Qt's maximum decimal capacity before initializing its range. For IEEE doubles this is 323 decimal places; visible values still use the shortest round-tripping representation. Exposure and gain behavior is unchanged.

The UI verifies that predecessor-double spacing at the advertised positive minimum exceeds the editor's decimal quantum. If the advertised range is finer than the widget can faithfully represent, FPS editing and Apply are disabled with numeric-precision guidance. This is an explicit UI representation limit, not a 1 FPS minimum, a capability-schema change or a restriction on the application's generic positive finite FPS admission. The regression set covers exact requested `0.0012345678901234567`, an editable `1e-20` endpoint, and rejection of unrepresentable normal/denormal ranges.

## Development evidence

Worker RED recorded 12 selected cases, eight expected failures in 2.407 s, including the real simulator premature retirement. After correction, the complete AcquisitionWorker/state-machine/mailbox registrations pass 3/3 in 13.18 s; related simulator/sequence/fault registrations pass 3/3 in 2.86 s. Independent Task 1 spec and production-quality review passes. A nonblocking scripted-test acknowledgement observation is retained for final review.

FPS policy RED recorded two expected failures among six cases. All seven selected controller/dialog/persistence integration cases initially failed the old FPS admission or missing editor. Dialog RED recorded ten expected failures while the changing-FPS presenter regression passed unchanged. After implementation, eight complete focused registrations pass in 11.29 s, including the complete LivePipeline registration in 10.62 s. The precision review finding was then reproduced by three failing exact-value/range regressions before its correction.

Exact commands, failure logs, result/source records and review reports remain under `out/qa/m09-frame-rate/` and `.superpowers/sdd/2026-09-10-m09-frame-rate/` in the preserved worktree. Final precision review, whole-branch review, clean Debug/Release checks and native dialog captures are recorded below.

## Final source verification

The elapsed watchdog is committed as `790db71`; stopped FPS admission/editor and the reviewed precision correction are committed as `9a39335`. Clean source `8794c1ce5538775fe280a0f7f37c713564f5d123` incorporates the published Task 4A integration documentation `2be1e48` without a source conflict. The source checks below have matching clean start/end revisions. Documentation-only completion follows this source.

| Check | Debug | Release | Evidence labels |
|---|---|---|---|
| Full simulator build, `cmake --build --preset linux-gcc-<configuration>-sim --parallel 2` | Passed | Passed | `verified-debug-build`, `verified-release-build` |
| Full headless suite, `ctest --preset linux-gcc-<configuration>-sim --output-on-failure -LE 'hardware\|desktop'` | 62/62, 78.41 s | 62/62, 38.94 s | `verified-debug-test`, `verified-release-test` |
| Native X11 desktop smoke via Xvfb, `ctest --preset linux-gcc-<configuration>-sim --output-on-failure -L desktop --no-tests=error` | 1/1, 0.11 s | 1/1, 0.05 s | `verified-debug-x11`, `verified-release-x11` |
| Actual dialog under XCB/Xvfb at 560×560 and 720×640 | 1/1, 0.086 s | Not separately run | `verified-native-dialog` |

The precision correction passes all eight focused registrations in 10.97 s. Its independent scoped re-review closes the P2 with no new finding. Final whole-branch spec review passes; quality review passes with one retained nonblocking P3: the scripted worker test's aggregate-counter wait can acknowledge an earlier autonomous timeout before its queued result under descheduling. This is a test-robustness concern inferred from source, not an observed failure or a production defect. It remains recorded, with a separate read-only follow-up proposal; it is not claimed fixed.

Both native dialog captures were inspected. The FPS row, fixed source fields, exposure/gain controls, actual readback, guidance and buttons are readable and contained at both sizes. The synthetic dialog fixture uses Mono8 metadata; production SIM-LIVE remains Mono12. Xvfb captures are rendered-window evidence and do not establish native Windows 11 DPI or physical-display acceptance.

`verification-manifest.json` validates seven successful immutable command records, common clean source and SHA-256 log hashes. `final-screenshots.json` binds both original BMP hashes to that source and native command; PNG conversions were verified pixel-identical and inspected. `review-record.md` retains task, precision and whole-branch review records. At this local checkpoint the continuation was unpublished; subsequent publication, hosted CI and merge are recorded below.

The independent final evidence/documentation audit approves all seven command chains, both capture hashes and pixel identities, review dispositions, documented source/timings, local links and separation from PR #20 hosted evidence. No actionable evidence contradiction remains. The audit is retained as `final-evidence-audit.md` in the execution ledger and QA directory; it does not close the recorded P3 or external acceptance gates.

## Remaining scope

SIM-LIVE remains Mono12 in U16; the standalone M4 harness remains Mono8. No processing algorithm or performance benchmark changes here. The previous 2048 Standard Linux performance result remains about 16.81 FPS identity and 15.31 FPS with flip/rotation, below the 30 FPS target.

The inherited intermittent context-retirement/lifecycle integration timeout remains unresolved. This acquisition timeout correction fixes a different reproduced defect. Deferred native Windows 11 visual/DPI checks, hardware validation and separate milestone acceptance remain open. Task 4 format/ROI rebinding, richer capabilities, per-camera records and installation orientation, plus Task 5 fullscreen/UI preferences, remain separate work.

## PR #21 integration

On 2026-09-10, [PR #21](https://github.com/m4bulmagd/Lumora/pull/21) merged reviewed publication head `a5972f221cd5314ac08269373f2aacb78122daf6` as `322a1cfa0bc24dafb1e54845fb4103a80cf00435`. Both platform logs check out PR merge `ddaf1a6881700f1129e1113ef4460b665334d8eb`, combining that head with base `2be1e4808e9831298997a9ffe46d626f94eae41a`. The publication head, tested PR merge and actual merge all have tree `f37988ed3c4e143c245d57b31dce6de797943fd2`; both comparisons have empty diffs. The clean FPS worktree was fetched and fast-forwarded to the merge. The root checkout's unrelated documentation edits remain untouched.

| Check | Debug | Release | Evidence |
|---|---|---|---|
| Fresh local publication-head full build and headless suite | Build passed; 62/62, 77.85 s | Build passed; 62/62, 38.37 s | Six immutable publication records, including native X11 1/1 each, in `publication-verification.json` |
| [Linux PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34506963052) | 62/62, 79.06 s; X11 1/1, 0.13 s | 62/62, 35.51 s; X11 1/1, 0.03 s | `pr21-linux-job.log`, job `102971412060` |
| [Windows PR CI](https://github.com/m4bulmagd/Lumora/actions/runs/34506963034) | 62/62, 88.10 s | 62/62, 49.83 s | `pr21-windows-job.log`, job `102971412229` |

All required PR job steps passed before merging the expected head. Optional Windows stress was skipped. No CI correction or rerun was needed. Logs, source association, publication verification and integration metadata are retained under `out/qa/m09-frame-rate/` in the preserved worktree. Post-merge main CI has not been checked. This integration documentation changes no production or test source.

The nonblocking scripted-test acknowledgement P3 remains a separate test-robustness follow-up. The inherited lifecycle/context-retirement timeout remains unresolved. ROI/format rebinding and remaining Task 4 work, then Task 5 fullscreen/UI preferences, remain separate. Hosted CI does not close native Windows 11 visual/DPI, hardware, the 30 FPS target or formal milestone acceptance.
