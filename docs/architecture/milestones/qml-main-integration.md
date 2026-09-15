# QML workstation integration on local main

Date: 2026-09-15

The owner approved integrating the verified QML branch into main before completing M9 layout controls. Local main fast-forwarded from `e4520dd` to `0469db8`, including implementation `920b4f6`. The branch tree is unchanged by integration. Main now builds the sole Qt Quick/QML `lumora_app`.

An existing untracked `docs/superpowers/plans/2026-09-13-qml-stage-one.md` differed from the incoming committed file. Its exact bytes were preserved first as `out/qa/qml-main-integration/preexisting-untracked-qml-stage-one.md`, SHA-256 `4efd6a87535793652c630cb70e0962adc0a95a9c71fdced449d85a0614f39d1c`. The tracked plan now occupies its normal path. No user content was discarded.

Fresh configuration of the normal `linux-gcc-debug-sim` and `linux-gcc-release-sim` presets replaces stale Widgets-only Qt package paths with the matching Qt 6.11.1 SDK retained at `.worktrees/qml-foundation/.tools/qt-official` plus existing non-Qt vcpkg dependencies. The normal checkout's own application, generated QML, libraries and tests are rebuilt. The retained worktree and SDK must remain available while these cached paths are in use.

| Check | Debug | Release |
|---|---|---|
| Normal build and QML lint | Pass, 130.074 s | Pass, 185.018 s |
| Full CTest suite | **65/65**, 61.777 s | **65/65**, 38.313 s |
| Widgets in normal source compilation/direct app linkage | Absent | Absent |

Durations are command wall times. Evidence is retained under `out/qa/qml-main-integration`; aggregate `verification.json` SHA-256 is `7924c25c4ed051fef203a120b6689b3e54d25587662b23704794cf47a319f469`. Every command records the integrated commit and unchanged source manifest `5626179b29588aaab2547ba631f1e35eae9abb7231a948e6086fa6e53da87d1e`. This is fresh normal-checkout verification; the prior [QML-only record](qml-only-workstation.md) separately owns its native/staged evidence.

Launch from `/home/mo/code/Lumora` with:

```bash
cmake --build --preset linux-gcc-debug-sim --target run-lumora
```

New layout implementation continues in the retained QML worktree. No push, hosted CI, fresh vcpkg build, Windows, physical-camera or formal release acceptance is claimed by this local integration.
