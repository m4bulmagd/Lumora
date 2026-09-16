# Flexible live video sources — local verification

The owner approved the [design](../../superpowers/specs/2026-09-16-video-sources-design.md)
and [implementation plan](../../superpowers/plans/2026-09-16-video-sources.md) on
2026-09-16. This increment extends the working tree from `f4c4bca`; it has not
been published. [Operator instructions](../../development/video-sources.md)
describe connection, installation orientation, authentication and source limits.

## Implemented behavior

One acquisition/processing/viewer session can select SIM-LIVE, an OS camera
reported by Qt Multimedia, or a saved RTSP/RTSPS address. USB webcams and capture
devices depend on their drivers. BNC requires hardware compatible with the
actual signal and an OS-camera interface; it is not a directly supported
software protocol. ONVIF discovery, proprietary capture SDKs, pylon, full color
and simultaneous streams remain outside this increment.

`CompositeCameraProvider` aggregates discovery and routes unique opaque IDs.
Failure of an optional provider does not hide another usable provider. Connect
reads the selected source's actual configuration. Apply prepares the matching
pipeline resources; review, Confirm and Start retain their existing authority.
Source selection changes the desired source, while explicit Disconnect/Connect
retires the old session. The application title identifies the actual connected
source. SIM-LIVE retains its Mono12 format and configured settings. A matching
native layout keeps configured control defaults; a different layout starts
from actual readback. Compatible confirmed profiles take precedence.

`MediaCameraProvider` owns Qt Multimedia objects on a dedicated event thread.
The callback retains only the latest decoded Mono8 image, then retrieval copies
into an application-owned immutable frame. Retrieval observes cancellation and
deadlines. Reconfiguration, invalid frames, loss of video and backend errors
follow the existing explicit Retry path. Driver/decoder memory is outside the
processing resource budget.

Each real Start allows five seconds of initial acquisition timeouts until the
first accepted valid frame. Steady-state timeout handling retains the greater of
750 ms or three reported nominal frame periods. Stop/Start rearms initial grace;
an idempotent Start while streaming cannot renew it. Latched media mode/conversion
failures use a recoverable connection error so the worker retires the session.

Maximum admitted dimensions are 1920x1080. Local cameras must advertise a fixed
nominal FPS; selection chooses the largest admitted mode and then the highest
rate at that size. Network sources must provide dimensions and nominal FPS.
Dimensions, format and FPS are read-only; exposure and gain are unavailable.
Color video becomes Mono8 grayscale; this does not establish native sensor
precision or performance acceptance.

`VideoSourceCatalog` saves up to 32 validated names, UUIDs and non-secret
addresses atomically in a separate versioned file. The source dialog adds,
removes and authenticates sources while disconnected. Credentials remain in
memory; user-info and common decoded credential query keys cannot be saved.
Qt FFmpeg URL-dump/debug diagnostics are suppressed by application startup,
and backend errors do not forward raw URL-bearing error text. Arbitrary
vendor-specific secrets in URL paths or custom query parameters cannot be
recognized; operators must use an address safe to save and display.

The application does not call `QMediaPlayer::play()` or `QCamera::start()` until
Start. Qt metadata probing can nevertheless send RTSP PLAY during Connect.
No application frame is delivered to the viewer during that probe.

## Automated and review evidence

Final Linux Debug and Release builds and `all_qmllint` pass. The complete normal
CTest suite, excluding `hardware|desktop`, passes **70/70 in Debug (74.65 s)** and
**70/70 in Release (42.80 s)**. QML lint emits one existing informational unused
import in `ViewingToolbar.qml`; there are no lint errors. The new dialog passes
all five adapter/scene tests and was visually inspected using the application's
Basic control style; its placeholder contrast was corrected.

Commands used after the final source changes:

```sh
cmake --build out/build/linux-gcc-debug-sim --target all all_qmllint --parallel 3
cmake --build out/build/linux-gcc-release-sim --target all all_qmllint --parallel 2
ctest --test-dir out/build/linux-gcc-debug-sim --output-on-failure -LE 'hardware|desktop'
ctest --test-dir out/build/linux-gcc-release-sim --output-on-failure -LE 'hardware|desktop'
```

Both caches use the official `.tools/qt-official` SDK and existing non-Qt
`out/vcpkg_installed/x64-linux-dynamic` dependencies, without a vcpkg toolchain.
Local configuration set `CMAKE_CXX_SCAN_FOR_MODULES=OFF`; this project uses no
C++ modules, and the setting avoided a local Ninja dyndep failure. The corrupted
Debug dependency cache was compacted before the final full rebuild. These are
local verification settings, not changes to the committed presets.

Deterministic tests cover composite routing, partial failure, duplicate IDs and
cancellation; Mono8/Mono12 source switching and retained immutable frames;
readback-based defaults and saved-profile precedence; latest-frame handoff,
stride conversion, invalid layouts, cancellation and deadlines; catalog schema,
duplicates, secret rejection, atomic persistence and malformed-file preservation;
and disconnected-only UI edits, session authentication and persistence rollback.
The real QML dialog is exercised through its controls.

Independent reviews covered provider/pipeline negotiation, media lifetime and
buffer ownership, catalog credentials, Qt dependency deployment and application
composition. Six integration findings were fixed with failing regression tests
or the actual RTSP pipeline probe:

- Credential query parameters could otherwise be persisted. Decoded,
  case-insensitive common keys are now rejected; all six catalog tests pass.
- Simulator installation fallback previously applied to any camera identity.
  It now recognizes only manufacturer `Lumora`, transport `Simulator`, and
  model `Generated Camera` or `PGM Replay Camera`. Real local/network cameras
  require a matching confirmed machine installation profile. The isolated
  installation test executable passed all 38 cases across five suites, including
  missing-profile rejection and explicit Save → Apply → Confirm → Start.
- The actual RTSP decoder took about 1.6 seconds to produce its first frame,
  longer than the existing 750 ms initial watchdog. A real LivePipeline probe
  reproduced zero processed frames and recovery after three timeouts. Bounded
  initial grace fixes startup while preserving steady-state stall handling.
- Latched invalid media frames were classified as discardable samples, causing
  repeated immediate errors and stale Streaming state. They now require recovery.
  The pipeline regression verifies one failing retrieval, cleared actual/applied/
  confirmed state, unavailable processing, rejected stale Start and manual Retry.
- RTSP Stop/Start attempted an unsupported seek when using QMediaPlayer's Stop.
  The adapter now pauses the live network decoder while stopping application
  handoff. Explicit Start resumes it; Close stops, detaches and clears the source.
  The real pipeline probe reproduced the failure and verified the correction.
- Unconditional readback seeding replaced configured simulator exposure/manual
  controls. The shared source-default policy now preserves them when the native
  layout matches. Three new coordinator regressions cover this behavior, and
  the original QML assertions pass unchanged. Initial complete runs were 68/70;
  the final complete runs above include the correction.

The final targeted media executable passed 13 tests. A separate fresh-worker
executable passed all 68 AcquisitionWorker/CameraReconfiguration tests in 14.859 s.
Independent final review inspected the corrections and their RED/GREEN logs;
no actionable finding remains. Final focused coordinator/camera-settings/real
QML checks also pass in both configurations before the full-suite runs.

Reports, logs, probe sources, the dialog capture and a manifest of changed-source
SHA-256 hashes are retained locally under `out/qa/video-sources/`. The manifest
binds the verified implementation files to their baseline; it is not a published
commit or hosted-CI result. Before the requested local commit on `main`, all 35
implementation-file hashes still matched, Release build and QML lint passed,
and the normal Release suite passed again (70/70, 21.89 s with two parallel tests).
JSON parsing, stage-check Python syntax,
`git diff --check` and relative Markdown file-link checks pass.

## Synthetic RTSP acceptance

A loopback GStreamer RTSP fixture used a moving ball encoded as baseline H.264,
320x240 at 15 FPS, at `rtsp://127.0.0.1:18554/lumora`. The actual production
media provider, Qt 6.11.1 and SDK FFmpeg 7.1.3 negotiated the mode, applied
readback, started explicitly, returned five distinct Mono8 frames, honored an
already-cancelled retrieval, stopped and closed. The probe exited zero.

The same fixture's missing endpoint produced a sanitized `media_backend_error`.
Success and error logs contained neither synthetic username nor password with
the application's logging rules. Server tracing established the metadata PLAY
behavior described above. These probes establish one software-decoded codec and
error path, not universal credential redaction by every decoder diagnostic.

The fixture was then exercised through the actual **LivePipeline and processing**.
Starting with Mono12 resources, Connect negotiated Mono8 320x240 at 15 FPS; Apply
replaced resources before Confirm and Start. The first processed frame arrived
1.593 s after Start. Five frame hashes differed, six initial timeouts were
tolerated, and terminal failures remained zero. Stop and Disconnect completed.
Before the startup correction, the same probe recovered after about 0.78 s with
zero frames and three timeouts. Probe sources and RED/GREEN logs are retained in
`/tmp/lumora-startup-watchdog/`.

The same-session restart probe also passed: after Stop, a 500 ms observation
found no newly acquired frames. Start reused the confirmed session and returned
three fresh changing frames with increasing IDs 7, 8 and 9; the first arrived
after 86.9 ms. Terminal failures remained zero, and final Stop/Disconnect
completed. These timings are observations, not performance guarantees.

Local reproduction sources and detailed commands are retained in
`/tmp/lumora-rtsp-fixture/` and `/tmp/lumora-rtsp-fixture.md`. The server binds only
loopback; the temporary missing GStreamer introspection/runtime packages were
unpacked under `/tmp`, with no global installation. The probe links the actual
adapter sources and existing core/API libraries in a separate build directory.
The temporary RTSP server was stopped cleanly after verification.

## Dependency and deployment evidence

The manifest pins Qt Multimedia 6.11.1 with its FFmpeg backend. Linux also enables
FFmpeg OpenSSL support; the Windows port uses Schannel. Configure requires the
matching FFmpeg plugin, Linux deployment includes it explicitly, and stage audit
requires Multimedia and its FFmpeg runtime libraries.

The official local Qt 6.11.1 SDK includes FFmpeg 7.1.3. The unchanged vcpkg baseline
selects FFmpeg 9.0.1; this graph has not been freshly built here. Their license
variants and provenance are recorded in [FFmpeg notices](../../../THIRD-PARTY-LICENSES/FFmpeg.txt).
The isolated SDK probe and its staged-runtime audit passed: 84 ELF objects,
zero loader-isolation errors; removing the copied FFmpeg plugin caused the
expected audit failure. This isolated probe is separate from full application
staging.

A fresh **full Release application stage** at `/tmp/lumora-video-release-stage`
also passes the updated audit: **129 ELF objects, zero errors**. The audit checks
relocatable loader paths and required media libraries/plugins; it does not prove
all interactive runtime imports or release packaging acceptance. The staged
`lumora_app --help` exits zero with a clean environment under Xvfb/XCB. The stage
ships XCB only; an attempted offscreen launch was inapplicable, and local X11
socket access required a host-permission retry.

```sh
cmake --install out/build/linux-gcc-release-sim --component QmlPilot --prefix /tmp/lumora-video-release-stage
python3 tools/qa/verify-qml-stage.py /tmp/lumora-video-release-stage --output /tmp/lumora-video-release-stage-audit.json
```

## Remaining acceptance

Physical USB webcams, capture cards/BNC signals, real network cameras, RTSPS
handshakes, native Windows/MSVC builds, Windows runtime packaging and a fresh
vcpkg build remain unverified for this increment. Intended camera/driver/codec
combinations need acceptance on the target workstation. Sustained FPS, latency,
physical display/GPU behavior and the existing designated-workstation and
evaluation gates are not established by these tests.
