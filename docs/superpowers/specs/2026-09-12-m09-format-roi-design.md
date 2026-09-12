# M9 stopped ROI and pixel-format continuation

The owner approved continuing the next development slice on 2026-09-12 after the recommendation to implement safe stopped ROI/pixel-format changes. This extends Task 4 of the M9 plan. The starting source is `9157969`, which merges integrated M9 source `f897a70` with the preserved feature-ideas commit `94985b5`.

## Scope and operator contract

The camera settings dialog edits the full acquisition ROI (x, y, width, height) and selects an exact advertised native source pixel descriptor while the connected camera is stopped. Exposure, gain and explicit FPS remain supported. One Apply carries the complete request. A viewer pause is not a camera stop. Successful geometry/format changes clear the old presentation, retain the device and its frame-ID sequence, and require fresh review, Confirm and Start. No automatic stop/restart is added.

Only capabilities actually advertised by the camera are offered. ROI minima, maxima, increments and sensor containment are validated without rounding the request. Each descriptor retains its name, encoding, packing, bit alignment, valid bits, sample maximum and application storage. Continuous acquisition remains the supported mode. The production SIM-LIVE default remains 640x480 Mono12 at 30 FPS; capability fixtures exercise alternate formats and offsets. Do not invent physical-camera capabilities or enable the gated Basler adapter.

Per-camera preference collections, administrator orientation, richer capability availability/writability fields, compact camera-panel replacement, fullscreen, capture, performance optimization and the inherited lifecycle-timeout investigation remain separate work.

## Chosen resource transaction

Retain the acquisition worker and its device; all device methods remain confined to that worker thread. Use the existing command mailbox and `ApplyConfiguration` request IDs. Add a bounded worker reconfiguration attachment, rather than Disconnect/Connect or a second independent command queue. Move the shared live-session context into its own application header if needed for typed ownership.

The control thread prepares a complete candidate context, checked raw/U16/display pools, processor and processing worker before sending the camera Apply. Existing compatible FPS/exposure/gain changes take their existing path and preserve context identity. The compatibility predicate continues to mean identical full ROI/descriptor; a separate supported-mode predicate admits valid positive-FPS continuous requests for reconfiguration.

The candidate is prepared for the exact requested full ROI and pixel descriptor. Actual FPS/exposure/gain may differ under the existing requested/actual contract; actual ROI and descriptor must match the candidate exactly. A two-phase allocate-after-device-Apply design was considered but would add another mutable device state and failure phase. Reopening the device was rejected because it resets source identity. Exact candidate validation keeps allocation failure before camera mutation and preserves current deterministic ROI validation.

For a rejected camera Apply or mismatched/invalid actual readback, restore the previous successfully applied actual request on the same device and verify the restored full configuration before considering the old resources usable. A successful restoration keeps the old context, clears confirmation and leaves an explicit failed Apply outcome; the operator reapplies/reviews before Start. If there is no previous applied state or restoration fails/is inconsistent, close/retire the unsafe device with the existing error path. Never report successful activation or stream through old buffers after uncertain camera mutation.

On successful Apply, the worker swaps its raw pool/slot binding, updates its prepared mode, preserves frame-ID history and advances generation. The control thread joins old processing and atomically publishes matching camera generation, new context, new resource assessment and final Apply outcome. No snapshot may combine the new camera generation with the old context. The UI resets its presenter before acknowledging that generation. Start is blocked until acknowledgement and fresh confirmation. Old immutable images/context handles remain valid until released; stale acknowledgements and stale commands are rejected.

Retain at most one staged candidate and one retiring context. A second reconfiguration requires the previous context handoff to be acknowledged and retired. Preparation accounts for the active resource assessment as additional external storage when checking the configured budget; report steady-session resources separately from this conservative transition check. Custom processor private storage remains explicitly unknown. Candidate cleanup, failures, Disconnect and Shutdown must release staged owners and must not deadlock mailbox barriers. No allocation, device I/O or thread join occurs in public posting methods or on the UI thread.

Accepted processing settings survive resource rebuilding. Preserve the last successfully activated definition for candidate activation, or fence Start until the UI reactivates its desired processing settings in the new session. Do not silently stream default processing after a successful configured rebind. Concurrent processing submissions either complete against the old generation before preparation or receive the existing retriable unavailable/stale classification.

## UI, confirmation and persistence

Use the existing modeless dialog and complete-request controller route. Controls expose exact capability values with clear labels, keyboard access and translated text. Oversized integer capabilities must not truncate into signed Qt ranges; use an exact unsigned-compatible editor or explicitly disable unrepresentable ranges. Apply is disabled for invalid/unsupported drafts, stale identity/generation, streaming, pending lifecycle work or invalid capabilities.

A successful rebind may close/invalidate the old generation's dialog; show useful actual readback and guidance, and permit reopening for further edits. Source generation changes reset image presentation but retain the selected Original/Enhanced/Compare mode through the existing presenter behavior. Processing controls retain their desired preset/draft across the new session.

Existing typed startup persistence stores the confirmed full request and actual readback. Saved geometry/format requests can be selected and validated against a connected camera. Explicit Resume may carry its own successful Apply across the correlated generation transition only when identity, capabilities, requested settings and actual readback still match the saved record. Any unrelated generation change, new edit, Stop or Disconnect cancels that continuation. Nothing streams automatically at launch.

## Verification

Use Linux/GCC Debug and Release with the existing pinned installed dependencies, native X11 rendering checks and independent spec/quality review. Exercise real simulator/frame processing where feasible; use controlled device doubles only for readback/rollback/cancellation failures. Keep original failure output for any inherited timeout, and do not claim passing reruns fix it.

Behavioral coverage includes changed dimensions and offsets, UInt8/U16 descriptors, unchanged-mode resource identity, exact request/actual metadata, increasing same-device IDs, one open/no reconnect, failed preparation before device calls, rollback success/failure, stale generation/acknowledgement, stopped-only admission, priority cancellation, retained old frames, confirmation/start fencing, processing preservation, UI capability/increment validation and persisted explicit Resume.

This remains the English, localization-ready Apache-2.0 evaluation application with `EVALUATION — NOT FOR CLINICAL USE`, preserved native Original data and paused/stale/orientation status. The 30 FPS target, physical-camera profile, designated Windows performance and native Windows visual/DPI acceptance remain open. Hosted Windows/MSVC verification is required before claiming cross-platform acceptance.
