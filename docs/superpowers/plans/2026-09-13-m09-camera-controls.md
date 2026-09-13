# M9 camera controls implementation plan

Spec: `docs/superpowers/specs/2026-09-13-m09-camera-controls-design.md`.

## Global constraints

Work only in the existing isolated worktree on `codex/m09-camera-controls`. Root owns Git, CMake and all build/test execution; implementers own disjoint source scopes and report tests ready for RED/GREEN. Preserve Stop → Apply → review → Confirm → Start, source identity/revision guards, priority intents, installation authority and immutable loaded provenance. No push, PR, merge, hardware acceptance or Task 5 work. Use meaningful behavior tests and retain all QA failures. Local source commits follow verified tests and independent review.

### Task 1: Camera access and authoritative readback

Own `src/camera/**`, `src/application/{include/lumora/application/ApplicationState.hpp,src/AcquisitionWorker.cpp}`, `src/app/SimulatorComposition.cpp`, camera tests, AcquisitionWorkerTests and CameraReconfigurationTests. Introduce exact API in the spec, centralized validation/change mask, simulator readback/write planning, and acquisition-thread connect/pre-Apply readback guards. Update owned fixtures and wrappers. Root handles unowned aggregate compatibility/fake wrappers. Tests first: absent gain; independent mode/value access; fixed field changes rejected with zero write mask for retained facts; connect readback does not authorize Start; fresh read-only first Apply rejected without device mutation; fixed controls remain during writable changes/failure rollback. Report all changed files and remaining integration concerns.

### Task 2: Versioned profile access provenance

Own application CameraProfile/StartupPreferences/InstallationProfiles sources and related headers, configuration codec/version sources and corresponding configuration/application tests. Consume Task 1 API; application capability validation delegates to camera common validation while retaining useful existing error identities. Implement fingerprint 2/user 5/machine 2 and mixed legacy/current encoding; optional mode JSON; explicit legacy Resume/binding rejection without file repair. Tests first: access drift rejects Resume, absence roundtrip, v1 retained unchanged, upgrading one installation camera preserves other legacy record and increments target revision only, malformed/new versions reject. Leave filesystem authority/store behavior unchanged except necessary version handling.

### Task 3: Compact panel and capability-driven dialogs

Own `src/ui/**`, `tests/unit/ui/**`, and a new focused controller integration test file if needed (root registers it). Consume frozen camera API and snapshot field; no edits in Tasks 1/2 scopes. Evolve existing panel/presentation, contextual actions/disclosure, accurate viewer state, selection/source gaps and installation guidance. Use current facts for fixed controls and complete requests in controller/dialog while preserving restoration/edit guards. Tests first: absent gain/read-only FPS remains editable elsewhere, fixed mode/writable value, streaming inspection, missing selection, contextual states, review/confirmation and paused/stale truth. Native captures/layout tests remain root QA. Do not alter fullscreen, geometry persistence or sidebar collapse.

### Task 4: Integration, review and evidence

Root updates unowned aggregate/fake compatibility, CMake if new test files, and production integration seams as needed. Review each task against spec/quality; fix concrete findings, then broad final branch review. Verify full Debug/Release and targeted sanitizers/native layout against final source. Record exact source, commands, results and limits in `docs/architecture/milestones/m09-camera-controls.md`; update the M9 master plan and next Task 5 status. Keep old QA artifacts and branch refs intact. Finish with clean local branch and concise completion/next-step summary.
