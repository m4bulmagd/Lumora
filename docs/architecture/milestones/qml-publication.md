# QML workstation publication

## Publication status

On 2026-09-15, the owner authorized integrating all completed work into main, pushing it and checking hosted CI. The publication check confirmed `main` and `origin/main` matched **`d97b269d82b9d9127236e9865766e58e82c6700e`** at that checkpoint. This includes the [QML migration](qml-main-integration.md), camera/processing parity and [layout implementation](qml-layout.md) at `5f6b82f`; the publication commit records the completed layout evidence.

Both initial hosted runs at the full `d97b269` commit above concluded **cancelled**. Their annotations explicitly report that the jobs exceeded **1 hour 30 minutes**:

| Workflow | Exact-head run | Recorded outcome |
|---|---|---|
| Linux Simulator | [34981154785](https://github.com/m4bulmagd/Lumora/actions/runs/34981154785) | Timed out during the dependency build; qtdeclarative had been building for over an hour and was in its Release build |
| Windows Simulator | [34981154704](https://github.com/m4bulmagd/Lumora/actions/runs/34981154704) | Dependency installation/configuration completed in 5290.5 s; the following Lumora Debug build was cancelled at the job limit |

Neither run reached QML lint, tests or the Lumora Release stages. These are job-limit cancellations, not passing hosted builds or tests. Linux restored 31 older cached packages and Windows restored 16; saving newly built packages was skipped after cancellation. Tool-cache service warnings were nonfatal and do not explain the cancellations.

The timeout recovery at `0740ead` raises both workflow job caps from **90 to 180 minutes**, leaving dependency pins, application build commands and tests unchanged. It gives the initial dependency build more time to complete and enables subsequent hosted verification; changing the cap is not itself verification.

For later run conclusions, use the [Linux main workflow page](https://github.com/m4bulmagd/Lumora/actions/workflows/linux-simulator.yml?query=branch%3Amain) and [Windows main workflow page](https://github.com/m4bulmagd/Lumora/actions/workflows/windows-simulator.yml?query=branch%3Amain), checking the exact commit and job results. This record preserves the initial publication and recovery evidence without asserting a current branch head or an unverified CI outcome.

## PNG dependency failure after timeout recovery

The timeout-recovery Linux run [34991995125](https://github.com/m4bulmagd/Lumora/actions/runs/34991995125), at `0740ead`, completed the Debug build and QML lint but failed `Qml.Workstation` initialization: Qt Quick Controls Basic PNG icons reported **Unsupported image format**. The other **66 of 67** test groups passed; the overall test command failed. Lumora Release and native desktop checks did not run. This is a distinct dependency-capability failure after the earlier job-limit cancellations.

The completed package cache was uploaded successfully (**1,552,049,858 bytes**). Subsequent runs can reuse compatible packages, but enabling PNG changes the Qt package build and requires the affected Qt packages to be rebuilt. The companion Windows run [34991995204](https://github.com/m4bulmagd/Lumora/actions/runs/34991995204) was still configuring when this correction was prepared; no passing Windows outcome is recorded here.

The correction explicitly requests `qtbase[png]` within the `qml-ui` vcpkg feature and requires the exported `Qt6::Gui` public feature `imageformat_png` in `cmake/Dependencies.cmake`. This supplies the image-format support used by Basic controls and rejects a Qt configuration without it before application build or scene initialization. These dependency/build checks do not establish a passing successor CI result. The original `d97b269` local results below remain bound to their recorded source and retained Qt SDK.

Local correction checks bind to the 431-file aggregate **`5ac63c515d10037332a7711b959cad76dcb45dabe59a6035d3982324a9056f2d`**: Debug/Release builds and QML lint pass, and the full suites pass **66/66** each (73.007 s / 41.982 s). A vcpkg dry run with the CI Linux host/target triplet requests Qt PNG support. Configuration accepts the retained PNG-capable SDK and rejects a probe with `imageformat_png` removed from its imported capability metadata, even with tests disabled. This negative probe checks the configuration error; it does not emulate PNG decoding. Fresh hosted runs remain responsible for verifying the corrected vcpkg-built Qt on both platforms.

## Platform failures after PNG correction

Both hosted runs at **`4fea5fec12370b57668e92568a9ece6c02e31595`** concluded **failure**, with different causes:

| Workflow | Exact-head run | Recorded outcome |
|---|---|---|
| Linux Simulator | [35003814375](https://github.com/m4bulmagd/Lumora/actions/runs/35003814375) | Debug build and QML lint passed, followed by **67/67** regular Debug test groups and the software desktop test. The OpenGL desktop test then aborted during initialization at `qxcbintegration.cpp:213`; Lumora Release stages were not reached. |
| Windows Simulator | [35003814438](https://github.com/m4bulmagd/Lumora/actions/runs/35003814438) | Dependency configuration succeeded, but the Debug build failed in two places: the renderer benchmark referenced OpenGL APIs unavailable in this Qt configuration, and generated QML cache code instantiated Qt templates that triggered MSVC C4702 under warnings-as-errors. QML lint, tests and Lumora Release stages were not reached. |

The Linux Qt feature list included `xcb` and `opengl`, but neither `xcb-xlib` nor `egl`. In the pinned Qt 6.11.1 source, the XCB GLX integration requires `xcb_xlib`; the EGL alternative requires `egl`. The resulting Qt build therefore lacked either XCB OpenGL integration. Changing plugin search paths alone cannot supply an integration that was not built.

The correction is limited to four implementation/build files: `vcpkg.json` requests Linux `qtbase[xcb-xlib]`; `cmake/Dependencies.cmake` requires the exported public `xcb_glx_plugin` capability on Linux; `benchmarks/presentation/QuickRendererBenchmark.cpp` guards OpenGL diagnostics with `QT_CONFIG(opengl)`; and `src/qml/CMakeLists.txt` suppresses C4702 only for generated QML cache translation units on MSVC. Handwritten source retains the existing warning policy, and dependency versions and test assertions remain unchanged. This records the correction scope, not a passing successor run or local verification result.

Both failed runs saved their completed dependency caches: **2,652,381,914 bytes** on Linux and **1,797,786,647 bytes** on Windows. Only compatible packages can be reused; the Linux Qt feature change requires affected Qt packages to be rebuilt. Later hosted outcomes remain available through the workflow links above.

Local correction checks use source aggregate **`e2aa77e6c743216070c81deb181a8003418915838ad079de0ee68ef32c2a22fe`**. Debug/Release builds, QML lint and **67/67** regular test groups pass; both Debug desktop groups pass. A syntax probe with OpenGL declarations disabled reproduces the original benchmark compile error and passes with the guard. A configure-only probe confirms the MSVC exception reaches exactly 17 generated cache sources and no other translation units. The GLX capability probe accepts the retained SDK and rejects missing capability metadata; the Linux manifest dry run requests `xcb-xlib`.

Local desktop verification is **incomplete**: Release software passed, but OpenGL timed out after 180 seconds during the stop/disconnect/resume scenario, after earlier scene cases passed. A subsequent retry could not load `libQt6QuickTest.so.6`; the retained worktrees and Qt SDK had disappeared from the workspace during this verification period. The owner subsequently confirmed deleting the merged branches and worktrees on 2026-09-16. The timeout's cause is not established, and the failed attempt is retained in `platform-local-verification.json`. These local checks do not substitute for corrected vcpkg/MSVC builds or passing successor hosted CI.

## Windows offscreen dependency failure

The Windows run [35075944147](https://github.com/m4bulmagd/Lumora/actions/runs/35075944147), at **`c388590f658d6538ccbc0e1543b43f4c559b239a`**, passed the Debug build and QML lint. **60 of 67** Debug test groups passed, including `EvidenceSmoke` in **10.51 s**. The seven QML test groups could not initialize because the `offscreen` platform plugin was missing; each reached its **120-second** timeout. Release stages were skipped. The run saved its completed dependency cache (**1,797,786,661 bytes**).

Qt 6.11.1's platform build includes `offscreen` only when FreeType is enabled; the Windows Qt dependency configuration lacked that feature. The correction requests `qtbase[freetype]` within `qml-ui`, requires the imported `Qt6::QOffscreenIntegrationPlugin` target when tests are enabled, and gives all seven QML test groups a shared environment pointing to that matching plugin. On Windows, the shared environment supplies an existing font directory from explicit `QT_QPA_FONTDIR`, or otherwise `WINDIR/Fonts`, for the offscreen font database.

Both simulator workflows also stop cancelling an active main-branch run when a newer main run arrives, allowing the active run to finish and upload its dependency cache. These changes describe the prepared correction; they do not establish successful offscreen initialization, passing local tests or a passing successor hosted run. Compatible cached packages remain reusable, while enabling FreeType requires affected Windows Qt packages to be rebuilt.

Fourteen local configuration checks pass for source aggregate **`a646a3ddf50c1ad55347e61988645a53d0040d110132f1ff1c49beaf5ee2d498`**: Debug/Release plugin-path resolution, default and explicit Windows font directories, rejection of missing font directories, required/missing offscreen targets, and the workflow concurrency-only semantic change. These probes use imported-target stand-ins and fixture directories; they do not validate Qt runtime behavior or font rendering. The local SDK remains unavailable, so hosted builds and scene tests are the runtime verification gate. Details are retained in `offscreen-configuration-verification.json`.

## Local publication checks

Fresh checks ran from the ordinary main checkout at `d97b269`, using the same **431-file** source aggregate as the reviewed layout implementation: **`0c40c6b37f6ebdc616dfa8d8dd0bf2acfe49dcc398f764ce702f036cbe82cbcc`**. Documentation is outside that source manifest.

| Command record | Duration | Result |
|---|---|---|
| `publication-debug-build` | 4.439 s | Full build and QML lint pass |
| `publication-debug-ctest` | 64.448 s | 66/66 pass |
| `publication-release-build` | 3.753 s | Full build and QML lint pass |
| `publication-release-ctest` | 41.171 s | 66/66 pass |

Records, logs, the source manifest, integration audit and captured hosted-run status are retained under `/home/mo/code/Lumora/out/qa/qml-publication`. The matching-source six native runs (28/28 each), installed-app workflow and independent source/visual/trace reviews remain bound in the [layout record](qml-layout.md); they are not new publication runs. These historical local builds used the then-retained Release Qt 6.11.1 SDK, including for the Debug application; that SDK was later removed with the merged worktree.

## Integration completeness

Before publication, all five local branches had zero commits absent from main: `main`, `codex/qml-foundation`, `codex/m09-camera-controls`, `codex/m09-camera-profiles` and `codex/m09-format-roi`. All four registered worktrees had no tracked changes or nonignored untracked files. The audit is `local-integration-audit.json`; its remote comparison predates the push. The subsequent publication check confirmed `origin/main` equalled `d97b269`. The fetched remote-tracking audit also found zero commits absent from main in every listed tracking ref and no open pull requests; `remote-tracking-completeness.json` records that snapshot. Retained tracking refs for deleted server branches are harmless and were not deleted.

At the original publication checkpoint, worktrees, ignored QA and the local SDK were preserved. The documentation and workflow-timeout recovery follow that clean audit; neither changes application source.

## Remaining acceptance gates

Hosted simulator CI, even when passing, does not establish native Windows 11 visual/DPI behavior, installation/upgrade acceptance, the designated workstation performance target, the M6 camera/NIC profile or physical-camera acceptance. Formal milestone acceptance remains separate. The application remains an engineering/evaluation build, not for clinical use. Capture, recording and custom-preset management remain outside this publication scope.
