# C-arm planning second review

**Date:** 2026-09-17

**Scope:** Documentation and source-contract audit at local implementation `ebcf5be`, including the uncommitted C-arm planning documents. This is not feature implementation, a fresh application test run or milestone acceptance.

The owner requested a second review of the plans and progress. Three independent read-only reviews covered capture/processing/storage, UI workflows, and progress/requirements. Findings were checked against source, corrected in the owning documents, propagated to related plans and reviewed again.

## Findings and corrections

| Finding | Corrected contract / evidence |
|---|---|
| Progress and architecture still described QML/layout as future work and omitted the latest compact controls | [Progress](../../PROGRESS.md), [traceability](../requirements-traceability.md), README, index and baseline design now distinguish implemented QML/layout, video inputs at `4e05b7a`, and compact controls at `ebcf5be` from planned C-arm features. Earlier evidence remains tied to its source. |
| PRD networking exclusions contradicted approved RTSP ingestion and source credentials | [PRD security](../../../prd.md#26-security) now distinguishes camera/source communication and session credentials from excluded remote workstation services, cloud storage and application accounts. |
| A directory rename can succeed before the final storage barrier fails | [M10](../../superpowers/plans/2026-04-25-m10-snapshot-capture.md) and the [capture/review plan](../../superpowers/plans/2026-09-17-carm-capture-review.md) now specify `CommitIndeterminate`, same-key reconciliation, no destructive abort after possible publication, and guarded derived-save/navigation state. Standalone Save copy can become clean after matching reconciliation without a navigation token. Terminal results and reconciliation requests have explicit capacity bounds. |
| Captured bundles retain pool storage after the current renderer-bound marker retires | The capture plan adds an application-owned generation retention registry checked before further resource preparation. The replacement Live context can bind/start while one old capture remains charged; another replacement waits for release. This cannot be implemented by withholding renderer acknowledgement. |
| Shared presentation-envelope migration omitted a compiled legacy consumer | The capture plan now includes the Widgets sink and controlled/shared test fixtures, with opt-in legacy regressions. Legacy product wiring remains unchanged. |
| Scene-frame extraction had a backend requirement but no definite UI action/test | The [UI plan](../../superpowers/plans/2026-09-17-carm-workstation-ui.md) now specifies Save selected frame, exact acknowledged recorded-frame identity/time/lineage, parent-session destination, unchanged playback/Live, and busy/failure/uncertainty cases. The design and [roadmap](../../superpowers/plans/2026-09-17-carm-workstation-roadmap.md) agree. |

## Verification performed

- Read current acquisition/processing, preparation/pool, renderer protocol, context replacement and legacy sink code where the plans depend on those contracts.
- Verified the retained video-source manifest's own hash and all **35** implementation-file hashes against commit `4e05b7a`.
- Checked retained compact-controls logs for **70/70** normal Linux Debug tests and **30/30** native software-rendered checks. These results were not rerun; no Release, OpenGL, Windows or hardware verification for that later UI change is implied.
- Checked **16 Markdown files**, **345 local links** and **73 anchors** with zero errors, plus conflict markers and diff whitespace; the historical M2 source-log link includes a supported line-number suffix.
- Confirmed all **167** C-arm/M10 delivery checkboxes remain open, CARM-01–CARM-12 remain present/unique, and no production source was changed by this review.
- Rechecked corrected contracts independently, including the standalone-save versus pending-navigation distinction.

## Remaining boundaries

No unresolved material contradiction was found in the reviewed planning scope after corrections. Implementation must still establish the proposed APIs and pass their acceptance cases; a plan is not runtime evidence.

The owner's scene-content choice remains original incoming frames plus processing settings. The first C-arm/connection remains undecided. Actual hardware compatibility, concurrent Live/review/recording performance, native Windows/DPI/display acceptance and existing milestone/release gates remain open. The review does not authorize clinical use or change those gates.
