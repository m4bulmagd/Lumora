# ADR 0001: Adopt Qt Quick/QML for the workstation frontend

Date: 2026-09-13

Status: Accepted; sole normal QML application and current camera/installation parity implemented locally, local verification and staged review complete (2026-09-15).

## Context

The owner chose to continue Lumora with QML and Qt Quick after completing the Widgets camera baseline. That historical [M9 Task 4E checkpoint](../architecture/milestones/m09-camera-controls.md) supplied compact camera controls, capability access, per-camera preferences and installation orientation for parity. The local `codex/qml-foundation` branch now implements that parity in QML, including FPS, ROI/pixel format and operator/admin installation orientation, and makes QML the sole normal application.

## Decision

Use Qt Quick/QML for every workstation screen and control, retaining the existing C++ camera, processing, frame-ownership and configuration contracts. On 2026-09-15 the owner confirmed the order: finish the remaining camera controls, then switch to QML only. The local implementation has completed the [QML-only workstation plan](../superpowers/plans/2026-09-15-qml-only-workstation.md): `lumora_app` is the sole normal QML application and the normal build does not require Widgets. Unique legacy regression tests remain available behind an explicit test-only option.

## Consequences

Widgets-specific composition descriptions remain historical migration-baseline records; [PROGRESS](../PROGRESS.md) records the current implementation at local commit `920b4f6`. Current source is bound by manifest SHA-256 `5626179b29588aaab2547ba631f1e35eae9abb7231a948e6086fa6e53da87d1e`: full Linux Debug/Release suites and six native configurations pass, the explicit legacy suite passes before the option is restored to OFF, and the staged inventory/loader audit reports 113 ELF objects with no errors. The staged real-application workflow passes in 27.496 seconds; independent trace and visual reviews pass. Remaining M9 fullscreen, sidebar and UI preferences belong in QML. This decision does not change milestone acceptance, hardware, Windows, performance or release gates; the branch remains unmerged and unpushed from main `e4520dd`.
