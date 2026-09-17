# Future product ideas

A place to keep useful ideas for Lumora and revisit them when the product is ready. These notes capture inspiration, operator benefits, dependencies and open design questions. Saving an idea here does not schedule it or change the approved scope.

For current commitments, use the [PRD](../../prd.md), [implementation authority](../superpowers/README.md#document-authority), [roadmap](../superpowers/plans/2026-04-25-xray-imaging-workstation-roadmap.md) and [progress summary](../PROGRESS.md).

## Saved proposals

| Added | Proposal | Status | Contents |
|---|---|---|---|
| 2026-09-10 | [M2-inspired features](2026-09-10-m2-inspired-features.md) | Saved for inspiration | Eight ideas covering snapshots, gallery/review, circular mask, temporal noise reduction, dark-frame calibration, automatic hold, sequence recording and test-session reports. |
| 2026-09-12 | [Workstation UI enhancements](2026-09-12-workstation-ui-enhancements.md) | Saved for future planning | Proposed screen layout, simpler controls, compact camera setup, fullscreen, Capture, status strip, thumbnails, viewing masks and recording/review UI; linked to existing milestones and M2 ideas. |

## Revisit the UI enhancements

**Selected for planning, 2026-09-17:** The owner's C-arm workflow request is now developed in the [C-arm design](../superpowers/specs/2026-09-17-carm-workstation-design.md), [delivery plan](../superpowers/plans/2026-09-17-carm-workstation-roadmap.md), [capture/review tasks](../superpowers/plans/2026-09-17-carm-capture-review.md) and [UI/UX tasks](../superpowers/plans/2026-09-17-carm-workstation-ui.md). Use those documents for selected capture/review/live-reference work. The notes below retain their earlier context; no implementation or milestone acceptance follows from this planning update.

Keep the current roadmap. The [UI proposal](2026-09-12-workstation-ui-enhancements.md) separates refinements to M9 controls/layout, M10 capture and M11 status presentation from future gallery, mask and recording interfaces. Reuse existing features and the related M2 definitions when selecting work.

## Revisit the M2 ideas

- Follow the existing M9 controls and M10 snapshot plans; F01 records the existing snapshot contract.
- Consider **F02 gallery/review** and **F03 circular viewing mask** after those foundations.
- Choose between **F04 temporal noise reduction** and **F05 dark-frame calibration** using measured image problems.
- Explore **F06 automatic hold** and **F07 recording/review** through separate designs; consider **F08 reports** once saved-session review is stable.

This order is a suggestion from the proposal, not a delivery commitment.

## Keeping ideas useful

Add future proposals as dated Markdown files and link them in the table. Keep each idea's source, benefit, dependencies and unanswered questions together. When an idea is selected, check it against the current product requirements, resolve its design questions, and link the resulting approved specification or plan back here. Keep a dated decision if an idea is deferred, superseded or declined.
