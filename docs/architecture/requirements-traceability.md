# Requirements traceability

This matrix starts the evidence chain for the approved evaluation architecture. “Verified on Linux” records the local Milestone 1 result; the Windows column remains pending until the committed required CI job completes. Later milestones extend these rows instead of replacing them.

| ID | Design source | Milestone 1 implementation/evidence | Linux/GCC | Windows/MSVC | Status |
|---|---|---|---|---|---|
| DES-1 | §1 Purpose and evaluation boundary | Compile-time evaluation release class, `MainWindowSmoke`, `Logging` | Verified | CI pending | M1 baseline implemented |
| DES-2 | §2 Scope and success criteria | Simulator-only presets, pylon-off configure path, evaluation banner; broader product scope maps to M2–M14 | Verified for M1 | CI pending | M1 subset implemented; later scope planned |
| DES-3 | §3 Architectural approach | Qt-free `lumora_core`, focused configuration/diagnostics/UI targets, typed errors, atomic configuration replacement | Verified | CI pending | M1 foundation implemented |
| DES-4 | §4 Repository and build structure | CMake target contract, pinned vcpkg manifest, Linux and Windows Debug/Release simulator presets | Verified | CI pending | Linux implemented; Windows verification pending |

## Milestone 1 verification map

| Test/check | Requirement covered | Automated evidence |
|---|---|---|
| `MainWindowSmoke` | Resizable shell opens/closes; visible evaluation identity; translation-ready UI strings | `lumora_ui_tests` |
| `ResultSmoke` | Qt-free typed success/failure contract including move-only values | `lumora_core_tests` |
| `Logging` | Writable rotating log; startup/shutdown; version and release class; typed path failure | `lumora_diagnostics_tests` |
| `ConfigurationStore` | Defaults, schema validation, round-trip, corrupt-file preservation, atomic-save failures, Unicode paths | `lumora_configuration_tests` |
| Configure-time target contract | Required M1 targets exist; Basler target cannot appear when disabled | `lumora_verify_milestone_one_targets()` |
| Linux release simulator CI | Pinned dependencies, pylon-off Release configure/build/test | `Linux Simulator / Linux GCC Release Simulator` |
| Windows release simulator CI | Native MSVC Release configure/build/test without pylon | `Windows Simulator / Windows MSVC Release Simulator` |

No row in this file represents clinical validation, regulatory evidence, or authorization for diagnostic use.
