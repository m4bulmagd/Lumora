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

## Local publication checks

Fresh checks ran from the ordinary main checkout at `d97b269`, using the same **431-file** source aggregate as the reviewed layout implementation: **`0c40c6b37f6ebdc616dfa8d8dd0bf2acfe49dcc398f764ce702f036cbe82cbcc`**. Documentation is outside that source manifest.

| Command record | Duration | Result |
|---|---|---|
| `publication-debug-build` | 4.439 s | Full build and QML lint pass |
| `publication-debug-ctest` | 64.448 s | 66/66 pass |
| `publication-release-build` | 3.753 s | Full build and QML lint pass |
| `publication-release-ctest` | 41.171 s | 66/66 pass |

Records, logs, the source manifest, integration audit and captured hosted-run status are retained under `/home/mo/code/Lumora/out/qa/qml-publication`. The matching-source six native runs (28/28 each), installed-app workflow and independent source/visual/trace reviews remain bound in the [layout record](qml-layout.md); they are not new publication runs. Local builds use the retained Release Qt 6.11.1 SDK, including for the Debug application.

## Integration completeness

Before publication, all five local branches had zero commits absent from main: `main`, `codex/qml-foundation`, `codex/m09-camera-controls`, `codex/m09-camera-profiles` and `codex/m09-format-roi`. All four registered worktrees had no tracked changes or nonignored untracked files. The audit is `local-integration-audit.json`; its remote comparison predates the push. The subsequent publication check confirmed `origin/main` equalled `d97b269`. The fetched remote-tracking audit also found zero commits absent from main in every listed tracking ref and no open pull requests; `remote-tracking-completeness.json` records that snapshot. Retained tracking refs for deleted server branches are harmless and were not deleted.

Worktrees, ignored QA and the local SDK were preserved. The documentation and workflow-timeout recovery follow that clean audit; neither changes application source.

## Remaining acceptance gates

Hosted simulator CI, even when passing, does not establish native Windows 11 visual/DPI behavior, installation/upgrade acceptance, the designated workstation performance target, the M6 camera/NIC profile or physical-camera acceptance. Formal milestone acceptance remains separate. The application remains an engineering/evaluation build, not for clinical use. Capture, recording and custom-preset management remain outside this publication scope.
