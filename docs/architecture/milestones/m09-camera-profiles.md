# M9 Task 4D: camera preferences and installation orientation

Date: 2026-09-13. Branch: `codex/m09-camera-profiles`. This local continuation builds on [stopped ROI/format changes](m09-format-roi.md), under the [camera profiles design](../../superpowers/specs/2026-09-13-m09-camera-profiles-design.md) and [implementation plan](../../superpowers/plans/2026-09-13-m09-camera-profiles.md).

## Operator behavior

Confirmed acquisition preferences are remembered separately for each manufacturer/model/serial identity. The last selected logical camera is independent of those confirmations. Compatible saved requests return when that physical identity is discovered, including when discovery finishes after preferences loading. A changed logical ID, capabilities, request or installation binding requires review. Resume remains an explicit action; launch never starts acquisition automatically.

Camera installation opens a separate dialog with horizontal/vertical flips, clockwise rotation, and matching asymmetric Original/Enhanced previews. Operators inspect the saved orientation read-only. Editing requires both deliberate `--installation` launch and actual OS administrator authority, a stopped current camera, and explicit confirmation. Edits reset confirmation; source or session drift invalidates the editor.

Save installation writes the machine profile asynchronously. It does not activate it. The next steps are Apply → review Original and Enhanced → Confirm → Start. Apply uses the existing same-device resource transaction, preserving frame IDs and processing settings; preparation failure retains the prior active context. An unresolved save or a changed durable binding blocks Apply/Confirm/Start as appropriate. Stop and Disconnect remain responsive, and a late save cannot start or activate a stale session.

The main status describes active orientation before an image arrives, then uses the immutable orientation of the displayed or paused frame. A newly saved profile cannot relabel an old image. Native Original and intermediate U16 pixels remain unchanged; orientation applies to the paired display outputs.

## Persistence and authority

User schema 4 migrates sequentially from schemas 1/2/3, retaining opaque legacy camera-profile and preset data. Typed per-camera records are authoritative; the old startup field is only an in-memory compatibility value. Capability fingerprint version 1 compares complete validated structural values, with canonical ordering of unique sets. It is not a byte hash. The collection retains confirmation recency and is bounded to 64 identities without eviction. Coalescing preserves accepted identities across blocked loading and writes; independent selection and preset writes cannot conceal an unresolved camera-save failure. Initial loaded provenance remains immutable, while current-run confirmations have a separate cache.

Machine schema 1 stores confirmed, revisioned profiles independently of user preferences. Read-only loading validates directory/file ownership and write authority before accepting records, and rejects symbolic/reparse substitutions. It does not create, repair, rename, chmod or replace anything. The save reread also refuses to adopt untrusted existing files. Only genuine absence under the explicitly composed simulator policy permits identity orientation; required absence, invalid data and mismatched identity/capabilities block activation. Saves use a cross-process lock, reread disk, check the expected revision, preserve other cameras, and replace atomically. Repair requires separate consent and a preserved backup. Every save outcome refreshes published disk facts; conflicts or newly unreadable data cannot leave a known-stale binding eligible for Start. Repository refresh occurs on load/save, with no external-file watcher.

Windows uses the actual ProgramData known folder at `Lumora/Config/installation-profiles.json`. Both application-owned directories are protected with Administrators/System write and Users read access, without changing unrelated ancestors. Linux uses `/etc/lumora/installation-profiles.json` with root ownership and operator-read permissions. Tests inject temporary locations and do not write either system path. There is no automatic elevation.

## Verification

Final verified source: `10e10181101e5c3ad7e6b56fda5a3e8c56edbafc`. Independent task reviews and the whole-branch review plus scoped F1 correction approve this source with no open actionable findings. All nine final command records have the same clean source commit/tree at start and end; the hash manifest validates their logs and five captured images.

| Check | Debug | Release | Evidence |
|---|---|---|---|
| Full simulator build | Passed | Passed | `final-debug-build`, `final-release-build` |
| Full headless suite, excluding hardware/desktop labels | 66/66, 68.88 s | 66/66, 38.89 s | `final-debug-test`, `final-release-test` |
| Native X11 smoke under Xvfb | 1/1, 35 ms | 1/1, 35 ms | `final-debug-native`, `final-release-native` |
| Installation dialog under XCB/Xvfb | 7/7, 173 ms; five captures verified | Not separately captured | `final-native-dialog`, `final-screenshots.json` |
| ASan/UBSan with leak detection | 14/14 registrations, 33.16 s | Not applicable | `final-asan-build`, `final-asan-test` |

The full headless command is `ctest --preset linux-gcc-<configuration>-sim --output-on-failure --no-tests=error -LE 'hardware|desktop'`. The sanitizer selection covers acquisition, startup domain/configuration, machine profiles, installation controller/pipeline/dialog, camera reconfiguration, LivePipeline, camera panel and frame presentation. Native operator/admin layouts were inspected at 580×640 and the platform minimum 428×640, plus a saved horizontal-flip/90° view with active orientation still at 0°. Final captures are byte- and pixel-identical to those inspected images. No sanitizer or leak error was reported.

Independent task reviews closed persistence capacity/durability/recency and legacy-migration defects, stale machine facts after failed saves, and Linux ownership/Windows path protection gaps. UI reviews closed both preferences/discovery loading orders, delayed installation-draft initialization, and preservation of an already edited local camera-settings draft. Behavioral RED was retained before these fixes. Final whole-branch review additionally required read-side machine ownership/write-authority validation and protected save rereads; that regression was also reproduced before fixing.

The first checkpoint at `5083257` passed its builds and native checks before final review found the read-authority gap. Those `verified-*` records remain as pre-correction evidence. The complete final records use `final-*` and bind to `10e10181101e5c3ad7e6b56fda5a3e8c56edbafc`.

Fresh build trees reuse `/home/mo/code/Lumora/out/vcpkg_installed/x64-linux-dynamic` via `CMAKE_PREFIX_PATH` (GCC 15.2.0, Qt 6.11.1, OpenCV 4.12, GoogleTest 1.18). The pinned Qt installation lacks XCB. Native checks therefore use the retained, isolated matching Qt runtime from the official PySide6_Essentials 6.11.1 wheel; its SHA-256 was independently rechecked against the retained source metadata. Repository dependencies were not changed. Xvfb captures are synthetic rendered-window checks, not physical-display or Windows acceptance. Sanitizers run on the host with leak detection enabled because the sandbox's ptrace boundary prevents LeakSanitizer completion.

## Remaining work

Successor: [Task 4E camera controls](m09-camera-controls.md) completes the compact panel and capability access work described below, and advances the current schemas to user 5/machine 2/fingerprint 2. The versions and verification in this Task 4D record remain historical.

This completes the bounded per-camera persistence and installation-orientation slice. M9 Task 4 still has the compact camera panel and remaining capability availability/writability details; Task 5 adds fullscreen, a collapsible sidebar and persisted UI preferences. M10 capture follows the remaining M9 work.

Native Windows compilation/ACL/UAC/visual/DPI execution, physical-camera testing, designated reference/performance work and formal acceptance remain open. The inherited intermittent lifecycle/context-retirement timeout is not fixed by this slice. No processing algorithm or performance benchmark changed, and the recorded Linux 2048 Standard result remains below the 30 FPS target. This original branch evidence includes no push, PR, hosted CI or main merge. Tasks 4C–4E subsequently entered [local main at `e2de210`](m09-camera-controls.md#local-main-integration); publication and acceptance remain separate.

## Local records

QA artifacts are retained at `/home/mo/code/Lumora/.worktrees/m09-camera-profiles/out/qa/m09-camera-profiles/`; task reports, scoped reviews and decisions are in `.superpowers/sdd/2026-09-13-m09-camera-profiles/`. Initial failures remain alongside successful successors. Each final command records its command/environment, source commit/tree, clean start/end status, elapsed time and log SHA-256. The verification manifest checks that chain and native capture hashes.
