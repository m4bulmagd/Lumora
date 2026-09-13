# QML renderer experiment — Checkpoint 3

This record covers the shared presentation protocol, its Widgets adapter and the
standalone Qt Quick renderer experiment on `codex/qml-foundation`, continuing from
Checkpoint 2 at `42c2340`. The implementation is locally verified at `60b7542`. The
implementation plan is [here](../../superpowers/plans/2026-09-13-qml-renderer-experiment.md).
The default live workstation remains Widgets; `lumora_qml_app` remains an interface
preview. No camera or processing session has been attached to QML in this checkpoint.

## Architecture and ownership

`lumora_presentation::FramePresenter` owns the frontend-independent state machine.
It retains at most one admitted bundle and one completed/frozen recovery bundle;
new publications stay in the existing latest-value slot until the sink can accept
them. `IPresentationSink` accepts one immutable bundle, a display mode and an exact
session/source/presentation-revision ticket. It returns completion, failure and
retirement events through a bounded mailbox. These values remain C++ objects.

Only a matching completion for a new source advances the displayed count and
completion deadline. Mode changes and same-source recovery preserve the original
deadline. Both host receipt age and elapsed time since completion enforce
`max(500 ms, 3 actual frame periods)`. The renderer captures monotonic completion
time before GUI delivery; delayed delivery cannot make an old image current.

Pause closes new-source admission immediately. An unconsumed submission can be
canceled; consumed work must settle before the state changes from Pausing to
Paused. Mode intent coalesces while that work is pending. Surface loss clears the
public visible owner but retains the completed/frozen bundle for recovery without
another source publication. Reset and shutdown require an explicit retirement
receipt before the coordinator acknowledges or releases the old session context.
The presenter's destructor does not substitute for that protocol.

The Widgets presenter is now a wrapper around this shared state machine. Its
private sink completes the exact admitted ticket at the existing paint boundary.
The controller supplies the actual session generation and observes the retirement
proof. Camera command authority, priority barriers, processing, persistence and
frame storage remain in their established C++ modules.

`QuickImageItem` is a C++ `QQuickItem` and sink. It validates the complete bundle
before admission, copies declared Gray8 rows into owned opaque RGB32 images and
creates public scene-graph textures. Compare uses two planes from one bundle,
one shared viewport transform and one completion. There is no second orientation
or tonal transform and no JavaScript pixel route.

The Quick sink correlates consumption with a rendered window frame and captures
the receipt in a direct `frameSwapped` callback. This is submission for presentation,
not physical display scan-out. Scene-graph cleanup, binding epochs and resource
fences keep old-window jobs from releasing a replacement's resources. Retirement
does not depend on a hidden window producing another swap. Owned RGB32 conversion
releases source pool leases before long-lived texture ownership; it is a copy,
not zero-copy rendering.

## Verification and review

Evidence is retained locally in `out/qa/qml-renderer/`; task history and review
follow-ups are in `.superpowers/sdd/2026-09-13-qml-renderer-experiment/`.
`verification.json` indexes accepted commands, results, captures and source hashes;
`verified-source.json` covers 385 source/build/test/benchmark/tool files. The
renderer-specific `review1-verified-manifest.json` binds its nine source files,
four binaries and exact native/measurement commands. Earlier red/intermediate
outputs remain separate from this accepted evidence.

| Final check | Debug | Release |
|---|---|---|
| Full build with QML enabled | Pass | Pass |
| CTest excluding `hardware\|desktop` | 75/75 groups, 70.32 s | 75/75 groups, 41.63 s |
| QML lint | Pass | Pass |
| Native XCB Widgets layout and Pausing projection | 10/10 cases | 10/10 cases |
| Native XCB simulator workflows | 8/8 cases | 8/8 cases |
| Native Quick software, OpenGL basic, OpenGL threaded, threaded DPR 2 | 22/22 cases in each of four runs | 22/22 cases in each of four runs |
| Standalone presentation linkage | Qt Core only among Qt modules | Qt Core only among Qt modules |

The original Release Widgets application also builds with QML OFF against the
original Widgets dependency prefix. The final review corrections changed only
Quick/benchmark code: all nine recorded Widgets/presentation/integration/OFF
binaries remained byte-identical, so their native, linkage and OFF-build evidence
was retained. QML sources and their lint inputs also remained unchanged.

Seven native Widgets captures cover confirmed, expanded review/warning,
paused/stale and capability-dialog states at the supported sizes. Every Debug /
Release pair is byte-identical and matches Checkpoint 2's accepted capture. Fresh
Quick Compare captures at both sizes also match the inspected earlier renderer
captures for their respective backends. These are virtual-display checks.

| Checkpoint 3 requirement | Result and evidence |
|---|---|
| Serialized admission, exact tickets, bounded bundle owners | Pass: standalone `SharedFramePresenter` and `PresentationProtocol`; wrong/old/duplicate/canceled identity, newest-slot coalescing and weak-owner bounds |
| Delayed rendering and separately delayed GUI delivery | Pass: controlled sink plus actual Quick callback clock test; source count/deadline changes only on new-source completion |
| Pause before consume, during render, after swap before delivery | Pass: shared state tests and actual Quick render gate; pending Pause ignores Resume until the exact terminal event |
| Mode repaint, fallback and frozen recovery | Pass: shared and actual Quick tests; completed labels, source identity and freshness remain tied to the completed bundle |
| Compare and invalid/missing planes | Pass: whole-bundle validation, atomic pair and one receipt; Widgets and Quick pixel checks preserve prior valid image on rejection |
| Gray8 stride, opaque equal RGB, already-oriented pixels | Pass: actual captured pixels from padded asymmetric fixtures; no additional orientation/tonal transform |
| Shared Fit, logical 100%, zoom, pan and clipping | Pass: shared transform and Quick pixel/geometry assertions, with actual DPR 1 and 2 reported |
| Hidden/zero-size/ancestor-opacity/clipped surface | Pass: no false receipt, invalidation and retirement tests |
| Reset, close, never-shown surface, graph recreation and old-window callbacks | Pass: actual Quick lifecycle tests, repeated owner-zero checks and exact shared retirement IDs |
| Retire before session acknowledgement; backend changes during acknowledgement | Pass: controlled retirement and real coordinator/pipeline race through a private test-only pre-ack hook |
| Existing Widgets workflows, pixels and 100 lifecycle cycles | Pass: affected 14/14 registrations and explicit 100-cycle fixture, with original freshness/pixel/pool assertions retained |
| Current/replacement image and texture inventory; preparation reserve | Pass for known application owners and CPU-image reserve: checked arithmetic and all four benchmark assessments; Qt/driver allocations remain unknown |
| First initialization error before a ticket | Pass for signal-handler coverage: bounded read-only C++ diagnostic and harness reporting, fresh-window recovery and queued old-window fencing; no fabricated frame receipt |
| Known allocation/node/texture errors | Handling implemented; real allocation/driver fault injection is not verified |
| Minimize-specific native transition | Implemented through exposure/window-state handling; a distinct minimize test is not verified |
| Per-texture asynchronous upload failure; arbitrary occlusion | Not verified: public Qt APIs do not provide the required per-texture success/visibility proof; dedicated-surface limits apply |
| Native Windows and physical GPU/display | Not verified on this Linux Xvfb/llvmpipe host |

Task 1's independent review found two mode-intent errors around the Pause barrier;
behavioral regression tests failed before the fixes and pass afterward. Its
scoped rereview and Task 2's independent specification/quality review pass with no
remaining findings. The first Widgets integration run required a conditional
second actual paint to settle the coalesced latest source or mode after the exact
admitted ticket; no freshness threshold, pixel or pool assertion was weakened.

Quick behavioral failures exposed missing clip geometry, ancestor-opacity loss,
canceled-mode geometry and a fully clipped Compare pane. Their corrections were
verified on actual window renders. Release compilation also exposed a temporary
variant-move diagnostic; constructing concrete events directly in mailbox slots
removed it without suppressing warnings. The first implementation failure log was
reused for its green run; `task3-initial-failure-notes.md` records that evidence
limitation. Initial behavioral-red and expanded-red logs remain retained.

The whole-checkpoint review required two narrow corrections: benchmark telemetry
must outlive the window's render callbacks, and first-initialization errors must
remain observable before any frame ticket exists. Both are corrected at `60b7542`;
their scoped specification and code-quality/concurrency rereview passes with no
remaining findings. The diagnostic is Quick-specific, read-only C++
state exposed by `QuickImageItem::initializationError()`; it leaves the sink
unavailable and does not fabricate a ticket or receipt. Checkpoint 4 must project
it into workstation status. The normal benchmark and an intentional capture-error
exit exercise exposed-window teardown; the latter returns the expected code 14.
The new initialization test failed before the correction, then passed through
the real Qt signal handler. It is not real driver fault injection.

## Release renderer measurements

Accepted measurements are `review1-measure-*.json`, after the review corrections.
Native XCB under Xvfb; Qt 6.11.1. OpenGL is Mesa 26.0.8, llvmpipe
(LLVM 21.1.8), OpenGL 4.5. Times below are milliseconds, p50 / p95 from
60 samples after 12 warmups. These are independent stage distributions, not
percentiles of an end-to-end sum. The harness polls GUI delivery with a 1 ms sleep.

| Backend/loop | Source | Mode | Preparation | Admission → consume | Consume → swap | GUI delivery |
|---|---|---|---:|---:|---:|---:|
| Software/basic | 640×480 | Original | 1.16 / 1.57 | 5.71 / 5.83 | 1.84 / 2.05 | 1.12 / 1.16 |
| Software/basic | 640×480 | Compare | 2.63 / 4.35 | 5.69 / 5.83 | 1.14 / 1.92 | 1.13 / 1.15 |
| Software/basic | 2048×2048 | Original | 6.82 / 7.76 | 5.67 / 5.97 | 1.30 / 2.99 | 1.10 / 1.19 |
| Software/basic | 2048×2048 | Compare | 14.88 / 24.84 | 5.75 / 5.98 | 2.94 / 4.89 | 1.18 / 1.20 |
| OpenGL/basic | 640×480 | Original | 1.07 / 2.17 | 5.69 / 5.98 | 4.00 / 9.15 | 1.14 / 1.18 |
| OpenGL/basic | 640×480 | Compare | 2.40 / 4.77 | 5.75 / 5.96 | 5.09 / 8.06 | 1.12 / 1.16 |
| OpenGL/basic | 2048×2048 | Original | 11.37 / 12.95 | 5.73 / 5.98 | 10.22 / 15.61 | 1.11 / 1.19 |
| OpenGL/basic | 2048×2048 | Compare | 19.26 / 32.73 | 5.67 / 6.00 | 17.38 / 35.08 | 1.10 / 1.21 |
| OpenGL/threaded | 640×480 | Original | 0.55 / 0.69 | 5.70 / 5.93 | 3.11 / 4.47 | 0.39 / 1.09 |
| OpenGL/threaded | 640×480 | Compare | 1.34 / 1.60 | 5.73 / 6.07 | 3.59 / 5.39 | 0.45 / 1.03 |
| OpenGL/threaded | 2048×2048 | Original | 7.20 / 10.16 | 6.11 / 7.12 | 9.09 / 13.04 | 0.29 / 1.00 |
| OpenGL/threaded | 2048×2048 | Compare | 15.70 / 26.94 | 5.95 / 6.86 | 13.64 / 18.60 | 0.35 / 1.11 |

Every case observed exactly 72 consumes and swaps. Peak application-owned texture
and RGB32-image counts were two each for Original and four each for Compare.
After retirement the texture/image counts were zero; after releasing the fixture
source, pool leases were zero. The sink retained no source bundle after submit.

The external CPU image reserve is 2,457,600 / 4,915,200 bytes for 640×480
Original / Compare and 33,554,432 / 67,108,864 bytes for 2048². Nominal old/new
texture bytes are reported separately and have the same numerical sizes; they
are not additional proved GPU allocations. All four preparation assessments
accepted the known CPU reserve without expanding the existing pools.

2048² Compare preparation alone had a p50 of 14.88–19.26 ms across these runs.
That GUI-thread conversion cost must be considered before live integration;
these results establish no 30 FPS processing or end-to-end performance acceptance.
Separate Compare captures at 900×600 and 1280×800 were inspected for both software
and threaded OpenGL. They show synthetic grayscale fixtures, not a live workstation.

## Measurement scope and remaining gates

The bounded benchmark uses synthetic immutable frames at 640×480 and 2048×2048,
Original and Compare, with 12 warmup and 60 measured submissions per case.
Preparation, admission-to-consume, consume-to-`frameSwapped` and GUI delivery are
separate distributions. They do not measure acquisition or processing throughput.
Screenshots run outside timed samples.

The storage assessment uses the real `FrameProcessingEngine::plan` with its
existing nine processing and sixteen display buffers. It reserves known old and
replacement RGB32 images through `externalSessionStorageBytes`. This is a
processing preparation assessment, not complete live-session memory accounting.
Camera/raw pools, fixture storage, Qt/RHI internal staging and driver allocations
are outside that reserve. Application texture-object counts and nominal requested
texture bytes do not establish actual GPU memory use.

The rendering surface is dedicated and unobscured, without layers/effects or
arbitrary ancestor transforms. Supported visibility and rectangular clipping are
checked. A window swap cannot prove arbitrary scene occlusion or per-texture
asynchronous upload success. Known preparation, node/texture creation,
initialization and scene-graph errors are handled; otherwise-undetected driver
failures remain outside the available public API proof.

Local tests use matching Qt 6.11.1 SDK modules; the Qt SDK libraries are Release
in both application Debug and Release configurations. A full pinned-vcpkg Qt
rebuild, native Windows/DPI, physical GPU/display, hardware camera workflows,
production packaging and formal milestone acceptance remain unverified here.
Xvfb and Mesa llvmpipe evidence must not be described as physical GPU acceptance.
The previously recorded lifecycle intermittency and 2048 processing performance
shortfall remain separate open issues.
