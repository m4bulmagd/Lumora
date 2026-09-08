# M8 remaining implementation tasks

**Date:** 2026-09-08

**Authority:** The owner approved continuing the following tasks after Tasks1–2 merged: denoise/sharpen, shared orientation, and full pipeline composition. Work uses `feat/m08-completion` in the retained isolated M8 worktree. Native Windows11 M4/M5 acceptance remains deferred; designated Windows reference/workstation evidence remains separately required.

## Completed baseline

CLAHE Task2 merged via [PR #11](https://github.com/m4bulmagd/Lumora/pull/11) as `c0c4102`. Linux/GCC13 and Windows/MSVC Debug/Release PR CI passed at `b85a626`; both post-merge [Linux](https://github.com/m4bulmagd/Lumora/actions/runs/34174289769) and [Windows](https://github.com/m4bulmagd/Lumora/actions/runs/34174289743) jobs also passed. Existing local evidence and review artifacts remain retained. Neither Task1 nor Task2 is being reimplemented as a fresh task.

## Task4 orientation contract

The standalone display transform is independent of Task3 and is implemented while detail-stage and executor preflight run. `OrientationTransform` accepts explicit image layouts, display storage and bounded byte spans, reports a tight output layout, and maps both Gray8 and Gray16 by exact integer coordinates. It applies horizontal flip, vertical flip, then clockwise rotation. Quarter-turns swap dimensions. Identity directly copies to a distinct destination; the executor can bypass it for an identity installation profile.

All validation precedes writes: supported rotation/storage pair, expected output dimensions, complete spans and no complete-payload aliasing. Odd strides and unaligned Gray16 samples are valid; only active sample bytes are copied. Input, padding and canaries remain unchanged. Stable `orientation_*` errors identify failures. No OpenCV dependency or heap allocation is needed on the successful transform path.

Task5 integrates the same immutable stopped-state orientation into both display routes and prepares the required display layouts/pool leases. Native RawFrame and enhanced U16 storage stay untouched. No new live orientation control or administrator profile UI is added in Task4.
