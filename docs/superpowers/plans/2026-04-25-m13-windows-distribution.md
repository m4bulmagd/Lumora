# Milestone 13 Windows Distribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce reproducible Windows release layouts, a separately available MSI, and the primary `Lumora-Setup.exe` bootstrapper that install on a clean Windows 11 x64 machine without developer paths or tools.

**Architecture:** CMake installs Lumora-owned targets into a staging tree, dynamic Qt deployment is explicit, third-party runtime/license/SBOM inventory is verified, and WiX Toolset v4 builds an upgrade-safe per-machine MSI plus Burn bootstrapper. Burn checks/installs the official VC++ x64 redistributable before the MSI. The official Basler pylon Runtime remains a separately managed vendor prerequisite unless redistribution approval explicitly permits bundling it.

**Tech Stack:** CMake install/CPack staging, Qt `windeployqt`, WiX Toolset v4, MSVC runtime, PowerShell verification, Windows code signing tools.

**Spec:** `docs/superpowers/specs/2026-04-25-xray-imaging-workstation-design.md`

**Clarification baseline:** 2026-09-04; see docs/superpowers/README.md for document authority and hard gates.

## Global Constraints

- This milestone contributes only to the open-source evaluation release, which must display `EVALUATION — NOT FOR CLINICAL USE` and must not acquire or store real patient data.
- Unless a step is explicitly Windows packaging or hardware work, execute it on Linux/GCC and require the matching Windows/MSVC simulator CI job before milestone acceptance.
- Preserve the fixed versioned processing order, native-orientation Original storage, shared installation orientation, bounded freshness, and mandatory paused/stale indications wherever this milestone touches them.
- Lumora-owned code uses Apache-2.0; dependencies stay pinned and target-scoped, pylon remains optional/external, and only dynamically linked LGPL-compatible Qt modules may enter distributed builds.

- Target Windows 11 x64 only.
- Release package contains no source, test fixtures, developer DLL paths, or debug runtimes.
- Simulator remains available for installation verification without hardware.
- Never redistribute pylon binaries until the exact SDK/runtime license and deployment approval are recorded.
- Internal/developer evaluation artifacts may be unsigned only with obvious unsigned status and hashes. External evaluation and future clinical artifacts require version identity plus Authenticode signing/timestamping; signing identity availability is a release gate.
- Installer must support clean install, upgrade, repair, and uninstall without deleting user captures/config/logs.
- Installation is administrator-managed per-machine under Program Files; normal operators run non-admin and all mutable data is per-user.

---

### Task 1: Runtime dependency and license manifest

**Files:**
- Create: `packaging/dependencies.json`
- Create: `packaging/THIRD-PARTY-NOTICES.txt`
- Create: `packaging/sbom/`
- Create: `docs/release/dependency-and-license-policy.md`
- Create: `tools/packaging/verify-dependencies.ps1`

**Interfaces:**
- Consumes: resolved CMake package versions, built executable import tables, Qt plugin list, and legal-approved pylon deployment mode.
- Produces: machine-readable dependency manifest, Apache-2.0 `LICENSE`/NOTICE/source-relink materials, CycloneDX or SPDX SBOM, and a verification script that rejects unknown/missing/debug binaries or unmet license policy.

- [ ] **Step 1: Define dependency manifest schema**

Each entry includes name, exact version, source/package identity, license, required runtime files, SHA-256, redistributable status, dynamic/static linkage, source/relink offer location, and owning CMake targets. Entries cover official VC++ x64 redistributable, dynamically linked LGPL-compatible Qt modules/plugins, OpenCV, spdlog, GoogleTest build-only, pylon build/runtime, and WiX build-only. Reject GPL-only Qt modules and unreviewed dependencies.

- [ ] **Step 2: Implement failing unknown-DLL test**

Point the verifier at a fixture staging directory containing `unexpected.dll`; expect nonzero exit and an error naming that file. A known release set must pass.

- [ ] **Step 3: Generate notices and enforce release-only binaries**

Verify x64 architecture, reject names/metadata indicating debug Qt/OpenCV/MSVC runtimes, check hashes, confirm every imported DLL is supplied or a documented Windows system DLL, and generate notices/SBOM/source-relink information from the approved manifest. Require qualified license review before any external distribution.

- [ ] **Step 4: Record pylon deployment decision**

Default policy: deployment staff install the approved official pylon Runtime separately. The Basler-enabled setup/MSI detects the approved runtime version and blocks with a clear vendor-prerequisite message when absent; neither copies SDK/runtime binaries. If written redistribution and version approval exists later, update the manifest and installer in a separately reviewed change.

- [ ] **Step 5: Commit dependency policy**

```powershell
git add packaging/dependencies.json packaging/THIRD-PARTY-NOTICES.txt packaging/sbom LICENSE NOTICE docs/release tools/packaging/verify-dependencies.ps1
git commit -m "docs(release): define verified Windows runtime inventory"
```

### Task 2: Reproducible install staging tree

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `cmake/Packaging.cmake`
- Create: `tools/packaging/stage-release.ps1`
- Create: `tests/packaging/VerifyStaging.ps1`

**Interfaces:**
- Consumes: `windows-msvc-release-sim` or `windows-msvc-release-basler` build, generated version, resources, Qt deployment tool, and dependency manifest.
- Produces: `out/package/stage/Lumora/` containing executable, approved runtime DLLs/plugins, presets, license/notices/source-relink/SBOM material, and no developer-only files.

- [ ] **Step 1: Write failing staging verification**

The test requires `Lumora.exe`, platform/imageformats plugins, default presets, Apache-2.0 license, third-party notices, source/relink information, SBOM, version/release-class manifest, and simulator resources; rejects `.pdb` in public staging, test executables, fixtures, unrelated source files, absolute build paths, debug DLLs, static Qt, and unapproved Qt modules.

- [ ] **Step 2: Add target-scoped CMake install rules**

Install the app and production resources by component. Do not install libraries that are statically linked, unit tests, benchmarks, viewer harness, soak runner, or hardware profiles.

- [ ] **Step 3: Implement deterministic staging script**

Remove only the resolved `out/package/stage/Lumora` directory after containment validation, run CMake install, run `windeployqt` with explicit release/no-translations/no-compiler-runtime policy chosen by manifest, copy approved OpenCV runtime, and run dependency verification.

- [ ] **Step 4: Smoke-run staged application**

Set a temporary `%LOCALAPPDATA%` test profile, launch staged application with `--camera-provider=simulator --smoke-test`, require clean exit and startup/shutdown logs, then inspect that no DLL was loaded from the build tree or developer Qt directory.

- [ ] **Step 5: Commit staging**

```powershell
git add CMakeLists.txt src/CMakeLists.txt cmake/Packaging.cmake tools/packaging/stage-release.ps1 tests/packaging/VerifyStaging.ps1
git commit -m "build(release): create verified Windows staging tree"
```

### Task 3: WiX v4 MSI and Burn bootstrapper with safe upgrades

**Files:**
- Create: `packaging/wix/Package.wxs`
- Create: `packaging/wix/Folders.wxs`
- Create: `packaging/wix/Files.wxs.in`
- Create: `packaging/wix/UI.wxs`
- Create: `packaging/wix/Bundle.wxs`
- Create: `tools/packaging/build-msi.ps1`
- Create: `tests/packaging/TestInstallLifecycle.ps1`

**Interfaces:**
- Consumes: verified staging tree, semantic version, stable UpgradeCode/Bundle UpgradeCode, generated component/file list, official VC++ x64 redistributable identity/source/hash, and pylon prerequisite policy.
- Produces: separately available per-machine x64 `Lumora-<version>-x64.msi`, primary `Lumora-Setup.exe` Burn bundle, ARP registration, start-menu shortcut, upgrade/repair/uninstall behavior, and install logs.

- [ ] **Step 1: Define stable installer identities**

Generate one product code per version, store one stable UpgradeCode in source, set x64/per-machine scope, install under `ProgramFiles64Folder\Lumora`, and never install mutable user data under Program Files. Create `%PROGRAMDATA%\Lumora\Config` with administrator-write/operator-read ACLs for the versioned installation camera profile; preserve it across uninstall unless an administrator explicitly requests removal.

- [ ] **Step 2: Add VC++ bootstrap and pylon prerequisite checks**

Burn detects the Microsoft-supported official VC++ x64 redistributable version, verifies the approved installer hash/signature, and installs it before chaining the MSI when necessary. Do not repackage runtime DLLs ad hoc. Basler setup/MSI also checks the approved pylon Runtime registry/file identity; absence blocks with a clear instruction to obtain/install the vendor runtime separately. Simulator-only internal packages omit only the pylon check.

- [ ] **Step 3: Build MSI from staged files**

Generate deterministic file/component IDs from normalized relative paths, include license/notices/SBOM/default resources, and use major-upgrade rules that prevent downgrade and schedule removal safely. The MSI contains the application; the Burn bundle chains the VC++ prerequisite and MSI. Publish both artifacts.

- [ ] **Step 4: Implement lifecycle test script**

In an isolated clean Windows 11 VM: run `Lumora-Setup.exe` as administrator with VC++ absent/present, test the standalone MSI with VC++ already present, launch simulator as a non-administrator operator, verify the operator cannot modify `%PROGRAMDATA%\Lumora\Config`, create sentinels there and in `%LOCALAPPDATA%\Lumora\Config`, `%LOCALAPPDATA%\Lumora\Logs`, and `%USERPROFILE%\Pictures\Lumora\Captures`, repair, upgrade from the preceding test version, uninstall, assert Program Files are removed and configuration/data sentinels are preserved.

- [ ] **Step 5: Test paths and rollback**

Cover spaces/Unicode user profile, elevation cancellation, VC++ download/install verification failure, missing pylon prerequisite, locked executable upgrade, repair after deleted DLL, and uninstall while app closed. Installer never kills a running imaging session silently; it requires the app to close.

- [ ] **Step 6: Commit installer**

```powershell
git add packaging/wix tools/packaging/build-msi.ps1 tests/packaging/TestInstallLifecycle.ps1
git commit -m "build(release): add Lumora MSI and setup bootstrapper"
```

### Task 4: Versioning, signing, checksums, and release record

**Files:**
- Create: `tools/packaging/sign-release.ps1`
- Create: `tools/packaging/write-release-manifest.ps1`
- Create: `docs/release/release-checklist.md`
- Create: `packaging/release-manifest.schema.json`

**Interfaces:**
- Consumes: organization signing certificate supplied through the protected build environment, staged binaries, MSI, test reports, and dependency hashes.
- Produces: Authenticode-signed executable/Lumora DLL/MSI/bootstrapper for external distribution, SHA-256 checksums for every artifact, release manifest, and approval checklist.

- [ ] **Step 1: Write manifest-schema verification**

Require product/version/commit, `evaluation` release class and safety statement, UTC build time, toolchain/dependency versions, configuration flags, file hashes, signature status/subjects/timestamps, simulator test result, reliability report, hardware report reference, VC++ bootstrap identity, pylon deployment mode, SBOM identity, and license-review status.

- [ ] **Step 2: Implement signing script with explicit inputs**

Accept certificate selector and RFC3161 timestamp URL as protected pipeline inputs, sign executable and Lumora-owned DLLs before MSI creation, sign the MSI, then sign the final Burn bootstrapper; fail closed if signature verification or timestamping fails. Do not store certificate secrets in repository files. An explicit internal-evaluation mode may omit signatures only when filenames/UI/manifest state `UNSIGNED INTERNAL EVALUATION — NOT FOR CLINICAL USE` and hashes are still produced; it cannot create an external release record.

- [ ] **Step 3: Generate checksums and release manifest**

Hash exactly the final artifacts. Validate JSON against the schema. External mode verifies every required signature/timestamp using Windows trust tooling; internal mode verifies the unsigned status and prevents publication to the external channel.

- [ ] **Step 4: Execute clean-machine release checklist**

Record current Linux/GCC and Windows/MSVC simulator tests, Basler-off check, dependency/license/SBOM verification, Burn and standalone-MSI lifecycle, non-admin startup/config/log/capture smoke, antivirus/security scan outcome, signature status, and known hardware report reference.

- [ ] **Step 5: Commit release tooling**

```powershell
git add tools/packaging docs/release packaging/release-manifest.schema.json
git commit -m "build(release): add signed release evidence workflow"
```

## Milestone 13 acceptance gate

- [ ] Verified staging runs the simulator without developer tools or paths.
- [ ] Runtime inventory, licenses, architecture, hashes, and debug-binary checks pass.
- [ ] MSI clean install, repair, upgrade, and uninstall pass on Windows 11 x64.
- [ ] `Lumora-Setup.exe` installs/checks the official VC++ x64 redistributable and chains the MSI; the MSI also remains separately available.
- [ ] Administrator installation and non-administrator operation pass.
- [ ] Uninstall preserves configuration, logs, and captures.
- [ ] External artifacts are signed/timestamped/hashed and represented by a validated manifest; any unsigned internal evaluation artifact is unmistakably labeled, hashed, and cannot enter the external channel.
- [ ] Basler package enforces the approved pylon Runtime prerequisite policy.
- [ ] Apache-2.0, LGPL-compatible dynamic Qt, notices, source/relink, SBOM, and license-review gates pass.
