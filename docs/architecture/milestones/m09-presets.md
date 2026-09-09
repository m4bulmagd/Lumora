# M9 Task 1: presets and persistence

Date: 2026-09-09. Status: implemented locally on `feat/m09-presets`; Linux verification and independent reviews complete.

The owner approved M9 Task 1 after PR16 integration at main `145d727`. This scoped continuation adds preset domain operations, a shipped JSON resource, typed saved state, and configuration migration. It does not accept M8 performance, designated Windows evidence, deferred native M4/M5 checks, or M6 hardware work. M9 Tasks 2–5 (processing controls, Compare, camera dialogs, fullscreen) remain later work. Preset activation, startup selection policy and UI controls remain later work; Task 1 only extends the existing settings load/save path.

## Domain and ownership

Application owns Qt-free values and a validated `PresetRepository`. Configuration owns JSON, resource loading and the existing atomic file store. The dependency remains configuration -> application -> processing/core; application must not include Qt or configuration headers. Repository operations execute outside per-frame processing and never touch cameras, image buffers, viewports or worker lifecycle.

`Preset` contains stable ID, name, description, builtIn, schemaVersion 1, positive uint64 revision, and a complete PipelineDefinition. `PresetState` contains saved custom recipes, selected ID and the complete active pipeline. Defaults select Original and use `processing::defaultPipeline()`. The repository owns this state, returns owned copies and validates candidates before committing changes. Invalid apply/edit/restore/save leaves the previous state intact.

Four immutable recipes ship: original, standard, high-contrast, soft-detail. The reserved custom item is an editing slot, not an immutable recipe: its resource definition is an initial Original template, find/list expose current values, and applying Custom preserves them. All five IDs are reserved against custom save/delete. Custom is marked builtIn=false; only the four immutable recipes are builtIn=true. Names/descriptions are English, neutral configuration descriptions; Qt UI localization remains a later adapter concern.

All preset definitions contain the eight canonical stages in order, including disabled stages, with every parameter. Use PipelineCompiler for existing finite/range/schema/order validation, plus a complete-stage check because the compiler intentionally permits partial pipelines. Standard must be semantically equal to `processing::standardPipeline()`; do not add the plan's obsolete makeStandardPipelineDefinition name. Runtime pipeline revision is ignored in classification and is not persisted; decoded definitions start at revision 0.

Original uses defaultPipeline: Normalize and full-range WindowLevel enabled, optional stages disabled. Standard is the canonical existing definition. High Contrast changes Standard contrast to 1.25 and CLAHE clipLimit to 3.0. Soft Detail changes Standard CLAHE clipLimit to 1.5 and sharpen amount to 0.5. All other values stay explicit and equal to Standard. These are conservative visual examples, not validated diagnostic modes or new performance claims.

Applying a named recipe updates selected ID and the complete active definition together in repository state and returns the owned definition for a future application command. Editing validates and always selects custom, even when values equal a named recipe. classify is a separate value lookup: original/standard/high-contrast/soft-detail first, then saved custom recipes in insertion order, then custom. Equal-value user recipes remain distinct IDs, and restoring an explicit valid selection never reclassifies it. Saving captures current values, selects the supplied user ID, creates revision 1 or increments the existing revision with overflow rejection. Deleting the selected user recipe keeps current values and selects custom. Restoring a state is strict and atomic.

## JSON and migration

The version-controlled resource is `config/default-presets.json`, embedded in lumora_configuration with Qt resources and loaded explicitly through a configuration factory. Missing/invalid shipped data fails factory creation; no silent replacement with duplicate C++ preset tables.

ApplicationConfiguration advances to schema 3. Its `presets` field becomes PresetState; the existing `processing` JSON object remains preserved opaque legacy metadata. The sole new authoritative active definition is presets.activePipeline. All other sections and the typed startup record are preserved. A configuration-layer `legacyPresets` object retains old opaque preset metadata without interpreting it as an approved recipe. Per-entry warnings are load metadata, never serialized as settings. Successful recovery also sets a summary loadWarning through the existing startup status path, with usedDefaults=false so unrelated valid settings can still save.

Migration is explicit 1 → 2 (startup becomes null) then 2 → 3 (empty validated preset state plus old presets retained under legacy). Schema 1/2 have no defined typed preset encoding, so migration does not guess named values or auto-apply settings. Nonempty legacy data produces a migration warning. Schema 3 presets is an object with schemaVersion 1, selectedId, activePipeline, customPresets array, and legacy object. Each saved recipe includes schemaVersion 1, revision as a canonical positive decimal uint64 string, id/name/description/builtIn=false and pipeline. Pipeline JSON has schemaVersion/orderVersion and all ordered stages, enabled flags and all parameters; runtime revision is omitted.

An invalid configuration/preset envelope is a whole-file failure, preserving existing ConfigurationStore quarantine/default behavior. A missing or invalid shipped resource is an installation error and must propagate without quarantining a valid user file. The JSON boundary rejects lossy UTF-8 conversion of application strings so IDs and names round-trip exactly. A malformed custom entry is reported and skipped; first valid duplicate ID wins, so an invalid occurrence does not reserve an ID. Reserved IDs/builtIn=true, future versions/stages, incomplete pipelines and bad parameters are rejected per entry. Valid entries survive. Unknown/mismatched selected IDs recover to custom while preserving a valid active pipeline; an invalid active pipeline recovers to Original. Explicit valid selection with duplicate values is preserved. Encode is strict: invalid state cannot replace an existing valid file. Existing startup persistence continues to preserve the complete configuration; no new I/O or worker is introduced into application/UI/processing.

The supported schema, customPresets array and legacy object form the required envelope. An absent or wrongly typed activePipeline recovers to Original; an absent or wrongly typed selectedId recovers to Custom when active values are valid. Unknown fields in pipeline, stage, parameter and recipe records are rejected, including a persisted runtime revision. Both UTF-8 and unpaired UTF-16 conversion failures are rejected.

## Verification and completion

The domain is committed at `4b26d8b`, configuration/resource/migration at `609dfa1`, and the final synthetic injected-recipe tests and contract clarifications at `e3e427a`. Domain fixtures prove the application accepts injected High Contrast/Soft Detail recipes; independent literal resource tests verify the official values. Production keeps those recipes in JSON only.

All final full-suite checks ran at clean `e3e427a`, with unchanged Git revision and clean status at both ends of each recorded command:

| Local Linux/GCC check | Debug | Release |
|---|---|---|
| Complete simulator build | Passed | Passed |
| Headless CTest suite | 56/56, 59.63 s | 56/56, 25.66 s |
| Native X11 smoke | 1/1, 0.11 s | 1/1, 0.05 s |
| Focused preset domain | 8/8 cases | 8/8 cases |
| Focused configuration | All 3 registrations passed | All 3 registrations passed |

The separate tests-OFF/Basler-OFF Release application build passed at production source `609dfa1`; the later checkpoint changes tests/docs only. Symbol inspection confirms both `qInitResources_default_presets()` and `PresetCodec::loadDefaultRepository()` in the app, with no GoogleTest symbols. This is independent of the resource-lifetime test, whose direct RCC reference could otherwise mask a static-library linkage problem.

The checks cover owned/atomic state transitions, immutable/reserved IDs, complete disabled-stage validation, classification versus explicit identity, revisions/overflow, strict restore, complete recipe parameters/modes, exact uint64 and Unicode persistence, indexed partial recovery, migration equivalence, unrelated settings/startup preservation and failed-save byte preservation. A real Qt resource unregister/register test confirms installation failures neither quarantine a valid user file nor invent defaults. The existing startup service code is unchanged.

The preset domain spec/quality review is approved after a test-only injection refinement. The independent whole-branch spec review, including the configuration adapter, is approved with no actionable findings. The independent configuration and whole-branch quality reviews are also approved with no actionable findings. Reports and immutable command/log/result records remain locally under `.superpowers/sdd/2026-09-09-m09-presets/` and `out/qa/m09-presets/validation/`. Labels `final-debug-*` and `final-release-*` record the full/native checks; `app-no-tests-build` and `app-no-tests-linkage.json` record the standalone app proof. Earlier RED records intentionally capture missing behavior before implementation.

The local overrides reuse the pinned dynamic dependency installation through ignored `CMakeUserPresets.json`. Builds use the canonical `linux-gcc-debug-sim` and `linux-gcc-release-sim` build/test presets; headless checks exclude `hardware|desktop`, while native checks run `xvfb-run -a ctest --preset <preset> --output-on-failure -L desktop --no-tests=error`. The separate app override sets tests and benchmarks OFF. This configuration-only work adds no performance claim or benchmark rerun.

The branch is committed locally and remains unpublished. `main` retains the PR16 integration checkpoint. Linux/Windows hosted CI for M9 and milestone acceptance remain pending; M8 performance, designated/native Windows validation and M6 hardware gates remain open. The next development slice is M9 Task 2: the preset selector and coalesced processing controls, using these typed domain and persistence APIs.
