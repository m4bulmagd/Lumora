# ADR 0001: Adopt Qt Quick/QML for the workstation frontend

Date: 2026-09-13

Status: Accepted technology direction; migration not implemented.

## Context

The owner chose to continue Lumora with QML and Qt Quick in a new session after committing the current camera work. The [completed local M9 Task 4E](../architecture/milestones/m09-camera-controls.md) provides the Widgets baseline, including compact camera controls, capability access, per-camera preferences and installation orientation.

## Decision

Use Qt Quick/QML for the future workstation frontend, retaining the existing C++ camera, processing, frame-ownership and configuration contracts. Keep the tested Widgets implementation available during migration. The [migration design](../superpowers/specs/2026-09-13-qt-quick-qml-migration-design.md) proposes the presentation seams, renderer experiment and stages; their detailed implementation still needs a bounded plan in the next session.

## Consequences

The architecture's Widgets-specific composition now describes the existing implementation. QML bindings, rendering, dependencies, deployment and parity need their own verification; current Widgets evidence does not verify Quick rendering. Remaining M9 fullscreen, sidebar and UI preferences should be planned for the selected frontend. This decision does not change milestone acceptance, hardware, Windows, performance or release gates.
