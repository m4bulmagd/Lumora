# Flexible live video sources

Status: implemented and locally verified on 2026-09-16 after owner approval.
This records the one-selected-source proposal approved in the accompanying task;
platform/hardware acceptance limits are recorded in the implementation plan.

## Scope

Keep one active acquisition/processing/viewer session. Extend the existing camera
provider boundary to combine SIM-LIVE, operating-system cameras (including USB
webcams and compatible capture devices), and explicitly registered RTSP sources.
Support Linux and Windows using the pinned Qt 6.11.1 Multimedia module. Use its
QCamera capture and FFmpeg-backed QMediaPlayer decoding behind ICameraDevice.

The first version converts ordinary color video to Mono8 for the existing
grayscale processing pipeline. It does not represent decoded consumer video as
native high-bit-depth sensor data. Preserve existing simulator Mono12 behavior.
Full-color processing, ONVIF discovery, simultaneous feeds, Basler/pylon and
device-specific capture-card SDK adapters remain separate planned work.

BNC is a connector, not a software protocol. An attached camera needs a compatible
digitizer for its actual signal (for example composite or SDI). This release can
use such equipment only when its driver exposes a supported operating-system
camera. It does not promise support for every BNC device or USB camera.

## Boundaries and behavior

- A composite ICameraProvider aggregates providers and routes opaque, unique IDs.
  A failed optional provider must not hide usable simulator or other sources.
- MediaCameraProvider owns local discovery and a thread-safe network source list.
  Local IDs derive from the backend device ID; RTSP sources use persisted UUIDs.
  Identity metadata must distinguish devices even when hardware serials are absent.
- Backend Qt objects run on a Qt event thread. Acquisition calls remain confined
  to the existing worker; callbacks hand off only the newest frame. Frame retrieval
  is cancellable, bounded by the caller's timeout, and copies into the supplied
  application buffer pool. Backend-owned image memory never escapes as RawFrame.
- Connection negotiates actual source configuration instead of requiring SIM-LIVE's
  Mono12 format. Apply prepares matching resources, then existing review, Confirm
  and Start gates remain authoritative. Connecting must not display video or call
  QMediaPlayer::play/QCamera::start before explicit Start. Metadata probing is
  permitted; Qt may send RTSP PLAY during this probe, so Connect can initiate
  transport video even though no application frame reaches the viewer.
- Report unsupported controls as unavailable/read-only. Do not invent exposure,
  gain, native format, resolution or nominal FPS. Reject unavailable/unsupported
  metadata, oversized frames, mode changes and invalid frames with clear errors.
- Preserve explicit manual Retry and existing installation-orientation policy.
  Device removal and network stalls must clear live state rather than leave a
  frozen frame labeled live. Selecting another source changes the desired source;
  explicit Disconnect/Connect retires the old session before starting the next.
  Each real Start allows five seconds of initial acquisition timeouts until the
  first valid frame; then retain the existing maximum of 750 ms or three nominal
  frame periods. Unsupported mode/conversion failures are terminal to the session.
- Refresh lists local devices; hotplug notifications request discovery only when
  the existing action policy allows it. Never interrupt a live source to refresh.
- Add a network-source dialog with name/address and add/remove behavior. Accept
  rtsp/rtsps only. Persist names, stable IDs and non-secret connection addresses
  atomically in a versioned source catalog. Credentials, if supplied, stay in
  memory for the current run; they must not appear in saved files, labels or errors.
  Persisted source definitions do not imply streaming permission.

## Resource and platform limits

Use bounded latest-frame handoff and reject dimensions above 1920x1080, supported
by the viewer reservation. Local cameras must advertise a fixed nominal FPS;
choose the largest admitted mode, then its highest rate. Network sources must
report dimensions and nominal FPS. Dimensions, format and FPS are read-only;
exposure and gain are unavailable through this adapter. Keep camera/decoder memory
separate from the existing processing-budget claim. No hard real-time or universal
30 FPS claim follows from adding these adapters. Actual latency, driver/codec
compatibility and hardware acceptance must be measured on intended equipment.

## Verification

Automate provider aggregation/routing, partial discovery failure, source validation
and persistence, credential redaction, frame conversion, timeouts/cancellation and
configuration negotiation. Exercise source switching through the real pipeline and
the QML source workflow with deterministic sources. Run normal CTest and QML lint.
Use a local synthetic stream if the environment permits; distinguish that evidence
from physical USB, RTSP camera, capture-card and Windows acceptance. Document exact
commands, results and any unresolved acceptance work without claiming unrun checks.
