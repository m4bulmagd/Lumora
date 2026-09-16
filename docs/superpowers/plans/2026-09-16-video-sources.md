# Flexible Video Sources Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Select and read one live simulator, operating-system camera or registered RTSP source through Lumora's existing workstation.

**Architecture:** Aggregate vendor-neutral providers, isolate Qt Multimedia acquisition behind ICameraDevice, and negotiate per-source resources through the existing pipeline. Add a persistent network-source catalog and QML source management without granting automatic streaming authority.

**Tech Stack:** C++20, pinned Qt 6.11.1 with Multimedia, CMake/vcpkg, GoogleTest and Qt Quick tests.

**Spec:** `docs/superpowers/specs/2026-09-16-video-sources-design.md`

## Global Constraints

- One active acquisition/processing/viewer session.
- Pinned Qt 6.11.1; Linux and Windows source/build compatibility.
- Preserve SIM-LIVE Mono12, existing Apply/Confirm/Start, manual Retry and installation-orientation policy.
- Convert ordinary video to Mono8; do not claim native sensor precision or retain full color.
- Bounded/cancellable retrieval, newest-frame delivery, application-owned immutable RawFrame memory.
- Persist source definitions but never credentials; redact connection details from backend errors.
- No ONVIF, simultaneous feeds, pylon implementation or proprietary capture-card SDK in this increment.

## Task 1: Provider composition and negotiated source startup

**Files:** `src/camera/api/include/lumora/camera/CompositeCameraProvider.hpp`, `src/camera/api/src/CompositeCameraProvider.cpp`, `src/application/src/AcquisitionWorker.cpp`, `src/application/src/LivePipeline.cpp`, `src/presentation/src/WorkstationCoordinator.cpp`, corresponding camera/application/presentation integration tests.

**Interfaces:** CompositeCameraProvider borrows a vector of ICameraProvider pointers, combines discovery and routes create by the IDs discovered. Existing simulator IDs remain unchanged. Media provider IDs are backend-qualified. Connect reads the actual format; Apply uses the established resource replacement path for changed native layout.

- [x] Add failing tests: discovery combines two distinct providers; creation routes correctly; one failing provider does not hide another; duplicate IDs fail clearly; cancellation propagates.
- [x] Add a failing pipeline test connecting a Mono8 provider from a Mono12 initial composition, applying actual readback, confirming and acquiring a frame; verify switching back retains the simulator behavior.
- [x] Implement composition and the smallest negotiation change needed. Preserve fixed-mode validation at Apply/retrieve and resource retirement; do not simply remove safeguards.
- [x] Run covering tests and review source-switch lifecycle, requested/readback synchronization and saved-profile precedence.

## Task 2: Qt Multimedia input adapter

**Files:** `src/camera/media/include/lumora/camera/media/MediaCameraProvider.hpp`, implementation under `src/camera/media/src/`, and `tests/unit/camera/MediaCameraTests.cpp`.

**Public contract:**

```cpp
namespace lumora::camera::media {
struct NetworkVideoSource { std::string id; std::string name; std::string url; };
class MediaCameraProvider final : public ICameraProvider {
public:
    explicit MediaCameraProvider(core::IClock& clock);
    ~MediaCameraProvider() override;
    core::Result<void> setNetworkSources(std::vector<NetworkVideoSource> sources);
    core::Result<std::vector<CameraDescriptor>> discover(std::stop_token = {}) override;
    core::Result<std::unique_ptr<ICameraDevice>> create(const CameraId&) override;
};
}
```

Use `media:local:` plus encoded stable device ID and `media:rtsp:` plus catalog UUID. Local display identities identify the backend and native device; network identities use source UUID, never a credential-bearing URL. Maximum admitted width/height is 1920x1080 for this increment, and reject larger source modes rather than silently scale them.

- [x] Write failing deterministic tests for Mono8 conversion (stride and values), source IDs/validation, fixed/unavailable control capabilities, cancellation and latest-frame handoff. Add test seams where native hardware is unavailable.
- [x] Implement QMediaDevices/QCamera/QMediaCaptureSession/QVideoSink local capture and QMediaPlayer RTSP input on an isolated event thread. Use metadata to negotiate network dimensions/FPS; report missing facts rather than inventing them.
- [x] Decode/convert to grayscale, copy into the supplied pool, attach actual source metadata and monotonically increasing frame IDs, and reject unexpected layout changes.
- [x] Verify open/start/stop/close idempotence, timeout/error paths and cleanup. Do not call play or activate a camera before Start.

## Task 3: Source catalog and QML workflow

**Files:** new source catalog persistence under `src/configuration`, `src/qml/VideoSourcesAdapter.hpp/.cpp`, `src/qml/qml/VideoSources.qml`, `src/qml/qml/CameraStartup.qml`, `src/qml/main.cpp`, source-management tests and relevant CMake files.

**Interfaces:** VideoSourcesAdapter owns the source catalog, updates MediaCameraProvider through setNetworkSources, and requests CameraAdapter::refreshDevices after successful edits. It is optionally attached to CameraAdapter, preserving existing simulator test compositions. Edits are allowed only under existing source-selection policy. Persist source names/IDs/addresses in a separate versioned atomic JSON file so concurrent preference writes cannot overwrite them.

- [x] Add failing tests for valid rtsp/rtsps addresses, malformed/rejected protocols, duplicate/maximum entries, atomic roundtrip, malformed-file preservation and credentials never reaching persistence or labels.
- [x] Implement add/remove and per-session authentication, source list/readback/error presentation, and disabled edits while a source is connected/pending.
- [x] Compose simulator plus media provider in the app; attach hotplug notifications for safe refresh; reserve renderer storage for the admitted 1920x1080 maximum.
- [x] Exercise source management from QML and verify existing simulator startup still requires Apply, Confirm and Start.

## Task 4: Dependencies, verification and documentation

**Files:** `vcpkg.json`, `cmake/Dependencies.cmake`, camera/QML/test CMake files, deployment checks, README, build guides, progress and requirements/roadmap indexes.

- [x] Pin qtmultimedia to Qt 6.11.1 and explicitly enable its FFmpeg backend; wire Multimedia into dependency version checks and runtime deployment.
- [x] Build with the available pinned SDK and run relevant tests, normal CTest and all_qmllint; record exact commands and distinguish SDK verification from a fresh vcpkg build.
- [x] Exercise synthetic network playback if feasible and document unavailable hardware/Windows checks honestly.
- [x] Review the complete diff for lifecycle, buffer ownership, credentials and platform compatibility; resolve findings and rerun affected checks.
- [x] Update operator instructions, current implementation status, roadmap and remaining BNC/industrial/ONVIF/multi-camera work. Mark checkboxes only from observed evidence.

## Execution record

- 2026-09-16: owner approved implementation and requested documentation/plan updates. Working in the existing checkout with a clean starting tree at `f4c4bca`; no branch publication requested. Backend, pipeline and build work may proceed concurrently only with disjoint file ownership; review occurs before completion.

- Integration review reproduced and corrected credential-query persistence, overbroad simulator installation fallback, RTSP first-frame watchdog expiry, latched invalid-frame busy looping, and RTSP Stop/Start seeking. The actual LivePipeline now negotiates Mono8, produces changing processed RTSP frames and resumes the same confirmed session after Stop. See the [verification record](../../architecture/milestones/video-sources.md) for evidence and remaining platform gates.

- Final verification: Linux Debug and Release builds plus `all_qmllint` pass; complete normal suites each pass 70/70 (74.65 s / 42.80 s). Original simulator camera-settings assertions pass after preserving configured defaults for matching source layouts. The fresh Release stage passes its 129-ELF audit and Xvfb/XCB command-line launch. Hardware, Windows, RTSPS handshakes and fresh-vcpkg acceptance remain explicitly open.

- 2026-09-16: owner requested a local commit on `main` with no push. The feature was already in the `main` checkout, so no separate branch merge was needed. Pre-commit verification rebuilt Release and ran `all_qmllint`, then passed the complete normal suite again (70/70, 21.89 s with two parallel tests). All 35 implementation-file hashes matched the earlier verification manifest; only documentation history was refreshed for integration.
