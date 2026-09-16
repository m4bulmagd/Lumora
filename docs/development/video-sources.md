# Live video sources

The current evaluation application offers one selected source at a time: the
existing SIM-LIVE simulator, cameras exposed by the operating system through Qt
Multimedia, or an explicitly added RTSP/RTSPS address. The
[approved design](../superpowers/specs/2026-09-16-video-sources-design.md) and
[implementation plan](../superpowers/plans/2026-09-16-video-sources.md) define this
increment; the [verification record](../architecture/milestones/video-sources.md)
separates implementation from platform and hardware acceptance.

## Supported input scope

| Source | Current behavior |
|---|---|
| SIM-LIVE | Existing 640×480 Mono12 simulator, with its existing capability-driven settings and processing. |
| USB webcam or other OS camera | Listed through Qt Multimedia. A driver must expose a supported camera and a fixed nominal FPS mode. |
| LAN camera | Add its `rtsp://` or `rtsps://` stream address manually. No network-wide or ONVIF discovery. |
| BNC camera | Requires a compatible capture device for the camera's actual signal, such as composite or SDI. It works in this increment only if the capture device appears as a supported OS camera. |

USB, LAN and BNC connectors alone do not establish compatibility. Proprietary
capture-card SDKs, Basler/pylon, ONVIF, full-color processing and simultaneous
feeds remain planned work.

Media sources must report positive native dimensions and nominal FPS, with
width at most **1920** and height at most **1080**. Local cameras must advertise a
fixed rate; the adapter chooses the largest supported fixed-rate mode within
those dimensions, then the highest rate at that resolution. Sources with only
variable-rate modes or missing metadata are rejected. Oversized video is rejected
rather than silently resized. A changed stream mode requires reconnecting and
reviewing the new configuration.

Media input is decoded and converted to **Mono8 grayscale** before entering the
existing pipeline. Its color information is not retained, and decoded consumer
video is not labeled as native high-bit-depth sensor data. Resolution, format and
nominal FPS are read-only in this adapter; exposure and gain are unavailable.
Configure the camera or encoder outside Lumora when these controls are needed.
SIM-LIVE's controls and Mono12 precision remain unchanged.

## Connect and start

1. Keep the source disconnected while selecting a source or editing network
   entries. **Refresh** lists currently available OS cameras; hotplug refreshes
   use the same disconnected-state policy.
2. For a local camera, select it in **Video source**. For a network camera, open
   **Network sources…**, enter a name and non-secret stream address, optionally
   enter a username/password in their separate fields, then select **Add source**.
   Close the dialog and select that saved source in **Video source**.
3. Select **Connect** to inspect the reported configuration. Review or establish
   its machine installation profile as described below. Then **Apply**, review
   actual readback, **Confirm**, and **Start Live**. No application frame is delivered
   to the viewer before Start. An eligible **Resume saved Live** remains an
   explicit user action.
4. **Pause** freezes the viewer while acquisition continues. Use **Stop** to stop
   acquisition; use **Disconnect** before changing sources or network entries.
   After a device removal or stream stall, use **Retry camera** explicitly and
   review the resulting state. There is no timed automatic reconnect loop.

Each Start allows up to five seconds of initial acquisition timeouts for the
first valid frame. After that frame, the existing stall policy uses the greater
of 750 ms or three nominal frame periods. Stop/Start rearms the initial grace;
repeated Start commands while already streaming do not extend it. Unsupported
stream changes or conversion failures require reconnecting immediately.

**Connect can initiate network traffic.** Qt's `QMediaPlayer::setSource()` probes
stream metadata. In the local RTSP fixture, that probe sent RTSP PLAY before the
application's Start action. Lumora does not deliver those probe frames to its
viewer, but Connect is not a promise that the camera sends no network video.

## First-use installation orientation

A real local or network source requires a matching, confirmed machine installation
profile before Apply/Confirm/Start. SIM-LIVE and Lumora's replay simulator retain
the simulator identity fallback. A changed capability fingerprint requires a new
administrator review.

An administrator launches Lumora with `--installation` and actual operating-system
authority: an elevated administrator token on Windows, or effective UID 0 on
Linux. The flag alone grants no authority. While the selected source is connected
and stopped, open **Installation orientation**, choose flips/clockwise rotation,
review the reference previews, check **I confirm this installation orientation**,
and select **Save installation**. Wait for the save result, then Apply, review,
Confirm and Start. Ordinary operator launches can inspect the saved installation
but cannot edit it.

Profiles are machine-scoped: `/etc/lumora/installation-profiles.json` on Linux,
and the Windows ProgramData `Lumora/Config/installation-profiles.json` path.
Network source definitions are per-user and installation matching uses their
saved UUIDs. If administrator setup runs under a separate account, provide that
session a copy of the operator's **credential-free** `video-sources.json` catalog
so it uses the same source IDs. Re-adding the same address creates a new ID and
will not reuse the original source's installation profile. Authentication must be
entered again in the administrator session when needed.

## Saved addresses and session credentials

Lumora atomically saves up to 32 names, stable IDs and non-secret addresses in
`video-sources.json` under Qt's per-user application configuration location.
Credentials entered into the dedicated fields stay in memory for the current
run. To authenticate an existing source after reopening, select it in **Network
sources…**, fill the fields, then select **Use credentials for selected**. Empty
credentials clear the stored authentication for that session. Removing a source
also removes its session credentials.

Do not paste credentials or signed secret tokens into an address. URL user-info
and common credential query keys are rejected, including case-insensitive,
percent-decoded `user`, `username`, `pass`, `password`, `pwd`, `token`,
`access_token`, `auth`, `authorization`, `api_key`, `apikey` and `key`. Ordinary
routing parameters such as `channel` and `subtype` can be retained. Arbitrary
vendor-specific secret meanings cannot be inferred from a URL; use only an
address that is safe to save and display. Backend operator errors are sanitized,
and the application suppresses Qt FFmpeg URL-dump/debug diagnostics at startup.

## Build and evidence limits

Normal builds require matching dynamic **Qt 6.11.1 Multimedia**, including its
FFmpeg media plugin. The manifest explicitly enables `qtmultimedia[ffmpeg]`; the
Linux graph also enables FFmpeg OpenSSL support for TLS. Configure rejects a
missing FFmpeg plugin. Linux staging explicitly includes the plugin, and the
stage audit checks Multimedia plus FFmpeg runtime libraries. See the
[Linux](build-linux.md) and [Windows](build-windows.md) guides and the
[dependency notices](../../THIRD-PARTY-LICENSES/FFmpeg.txt).

The local official Qt SDK bundles FFmpeg **7.1.3**; the pinned vcpkg baseline
selects FFmpeg **9.0.1**. These are distinct dependency builds. The SDK dependency
probe and its isolated staged-runtime audit passed; the audit checked 84 ELF
objects with no loader-isolation error. The fresh full Release application stage
also passed its audit (129 ELF objects, zero errors), and its command-line launch
passed under Xvfb/XCB. This is not release packaging acceptance or a fresh vcpkg
build.

A Linux loopback H.264 RTSP fixture reported 320×240 at 15 FPS, returned five
changing Mono8 frames after Start, honored an already-cancelled retrieval, and
stopped/closed. A missing endpoint produced a sanitized error. Success and error
logs contained neither of the synthetic credential markers. This is one
software-decoding fixture, not physical USB, capture-card, real network camera,
RTSPS-handshake or Windows acceptance. Final normal Linux suites pass 70/70 in
both Debug and Release, with builds and QML lint passing; see the verification
record for commands and scope.

The same stream also passed through the actual LivePipeline, including Mono12
to Mono8 resource negotiation, Apply/Confirm/Start and processing. The first
processed frame arrived about 1.6 seconds after Start; five changing frames were
received with no terminal failure, followed by successful Stop and Disconnect.
Stop/Start in the same confirmed session also resumed fresh changing frames;
the stopped interval delivered no new application frames.

Latest-frame handoff and application pools are bounded; camera-driver and decoder
memory are outside the existing processing-budget claim. No hard real-time,
latency or universal 30 FPS guarantee follows from adding these sources. Use
phantoms, test objects and synthetic material only within the existing evaluation
boundary.
