# M9 camera preferences and installation orientation

Status: implementation design, authorized by the continuation of M9 Task 4.

## Scope and authority

Implement Task 4's per-camera preferences and separate machine installation profiles on top of the verified stopped ROI/native-format transaction. Authority: `prd.md` installation and persistence requirements, the workstation design sections on orientation and configuration, and M9 Task 4. Fullscreen/preferences, capture UI, physical Basler implementation, hardware performance acceptance, and Windows native acceptance remain subsequent work.

## User workflow

Remember acquisition preferences by manufacturer/model/serial, independently from the last selected logical CameraId. Preserve requested and confirmed actual values and the canonical capability fingerprint. Camera selection alone cannot create confirmation. Restore compatible saved requests when selecting/discovering that identity; unchanged records may offer explicit Resume Live. Discovery drift, changed installation binding, or changed logical CameraId require review. Never start automatically.

Installation orientation is separate, versioned machine data. The ordinary application shows it read-only. Editing requires deliberate `--installation` launch and actual administrator authority, a stopped current session, explicit confirmation, and matching Original/Enhanced asymmetric previews. Flips precede clockwise rotation, matching the processing engine. Native Original and intermediate U16 remain unchanged. Presets never own or reset orientation.

Saving an installation profile and activating it are separate operations. Save is asynchronous and atomic on disk. After success, the UI explicitly instructs Apply → review → Confirm → Start. Apply resolves the durable installation profile, prepares replacement resources before camera mutation when orientation or source layout changes, and publishes the new context and installation binding together. A saved profile that is not yet active blocks Start. Save failure retains the previous durable profile and active session. Stop/Disconnect remain responsive during writes. An admitted write can finish after cancellation, but never activates or starts automatically.

## Domain and persistence boundaries

Plain C++ domain records; Qt Core only in codecs, path/storage adapters. Canonical fingerprint version 1 is the existing complete structural capability representation, with order-independent unique sets and exact finite numeric values. It is not a JSON-byte or implementation-defined hash. Stable identity excludes transport/firmware.

User schema 4 contains typed camera preferences and a separate last-selected CameraId. Sequential migration 1→2→3→4 seeds the old startup record without inventing confirmation and preserves legacy opaque cameraProfiles data under legacyCameraProfiles. Initial loaded provenance stays immutable. A bounded writer preserves accepted updates for distinct camera identities while coalescing repeated updates for the same identity; preset writes remain independent. Bound: 64 camera profiles, explicit capacity failure, no silent eviction.

Machine schema 1 stores up to 64 confirmed profiles: identity, canonical capabilities/version, monotonic revision, orientation. A user preference stores an optional reference containing machine record version, revision, and orientation; identity/capabilities remain in the enclosing record. Migration does not fabricate an installation reference.

Machine load is read-only: never rename, repair, or replace invalid data while loading. Only genuine absence permits the explicitly composed simulator identity fallback. Invalid data and identity/capability mismatch block Apply/Confirm/Start with actionable guidance. A required-profile policy covers future physical backends and is tested independently of camera-name heuristics. Production composition always supplies the installation service; unmanaged constructor composition is retained for existing internal harnesses.

Machine writes run on a separate bounded worker, require administrator mode, use a cross-process lock, re-read disk, validate the expected per-profile revision, preserve other profiles, then atomically replace. Invalid data requires explicit repair consent and a successful preserved backup before replacement. Read/access failures are not repairable by overwriting defaults. Windows uses the actual ProgramData known folder and administrator-write/operator-read ACLs. Linux uses a system path; tests inject temporary roots and authority. No test writes real system configuration. No silent elevation.

## Runtime invariants

Application control-thread checks enforce authority, current generation/identity, stopped state, and explicit confirmation; UI enablement alone is insufficient. One installation save can be outstanding. Apply/Confirm/Start are fenced until its outcome is known. A late durable result cannot authorize a stale session. Stop/Disconnect/Shutdown supersede activation intent.

Normal Apply resolves current durable identity/capabilities and orientation. Orientation changes reuse the existing same-device stopped reconfiguration transaction, retain source-ID continuity and desired processing, account for rotated dimensions/scratch, and require presenter reset/ack plus fresh confirmation. Same source/orientation edits retain the existing cheap path. A custom processor factory must explicitly support orientation or reject nonidentity activation; never silently ignore it. Start compares active binding to the current durable binding.

Active orientation is visible in main status. When displaying a retained or paused frame, its immutable orientation metadata is authoritative. Pending saved orientation must not mislabel an older image.

## Verification

Test migration and round trips; distinct-camera save coalescing during blocked load/save and shutdown; initial provenance; selection without confirmation; drift and installation-reference Resume gating; machine missing/invalid/mismatch policy; authority and stale/streaming/unconfirmed rejection; atomic save/repair/conflict failure; late write cancellation; real engine orientation, raw invariance, same device/IDs, resource failure, processing preservation, and presenter reset. Reuse exhaustive existing orientation arithmetic tests. Verify UI read-only/admin states and both previews, plus inspect native layout. Run Debug/Release headless suites, relevant sanitizer/leak checks, and supplemental native Linux smoke. Report Windows/hardware limitations accurately.
