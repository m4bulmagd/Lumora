#!/usr/bin/env python3
"""Inventory a trusted local Linux stage and check its ELF dependency isolation.

This uses ldd on the supplied binaries. It verifies loader resolution, not a
graphical launch, QML import completeness, or redistribution-license readiness.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


CLEAN_ENV = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"}
SYSTEM_LIBRARY_ROOTS = tuple(
    Path(path).resolve() for path in ("/lib", "/lib64", "/usr/lib", "/usr/lib64")
)
DEVELOPMENT_PARTS = {".tools", ".worktrees", "vcpkg_installed", "vcpkg_qml_installed", "vcpkg_legacy_tests_installed"}


def run_tool(command):
    result = subprocess.run(
        command, env=CLEAN_ENV, capture_output=True, text=True, timeout=30,
        check=False,
    )
    return result.returncode, result.stdout + result.stderr


def is_within(path, roots):
    return any(path.is_relative_to(root) for root in roots)


def dependency_location(path, stage):
    resolved = path.resolve(strict=True)
    if resolved.is_relative_to(stage):
        return "stage", str(resolved)
    if DEVELOPMENT_PARTS.intersection(resolved.parts) or "build" in resolved.parts:
        return "development", str(resolved)
    if is_within(resolved, SYSTEM_LIBRARY_ROOTS):
        return "system", str(resolved)
    return "external", str(resolved)


def check_rpaths(path, dynamic_output, stage, errors):
    entries = []
    for tag, value in re.findall(r"\((RPATH|RUNPATH)\).*?\[(.*?)\]", dynamic_output):
        for entry in value.split(":"):
            entries.append({"tag": tag, "value": entry})
            expanded = entry.replace("${ORIGIN}", str(path.parent))
            expanded = expanded.replace("$ORIGIN", str(path.parent))
            if not re.match(r"^\$(?:ORIGIN|\{ORIGIN\})(?:/|$)", entry):
                errors.append(f"{path}: non-relocatable {tag}: {entry!r}")
            elif "$" in expanded or not Path(expanded).resolve().is_relative_to(stage):
                errors.append(f"{path}: {tag} escapes the stage or has unsupported tokens: {entry!r}")
    return entries


def check_elf(path, stage, errors):
    record = {"path": path.relative_to(stage).as_posix(), "dependencies": []}
    status, dynamic_output = run_tool(["/usr/bin/readelf", "-d", str(path)])
    record["readelf_status"] = status
    if status:
        errors.append(f"{path}: readelf failed: {dynamic_output.strip()}")
        return record

    record["rpaths"] = check_rpaths(path, dynamic_output, stage, errors)
    status, output = run_tool(["/usr/bin/ldd", str(path)])
    record["ldd_status"] = status
    record["ldd_output"] = output
    dynamic = "Dynamic section" in dynamic_output
    if status and not (not dynamic and "not a dynamic executable" in output):
        errors.append(f"{path}: ldd returned {status}")

    for line in output.splitlines():
        if "not found" in line:
            errors.append(f"{path}: missing dependency: {line.strip()}")
            continue
        resolved_path = re.search(r"(?:=>\s+|^\s*)(/.*?)\s+\(0x[0-9a-fA-F]+\)", line)
        if resolved_path is None:
            # linux-vdso and static executables do not name filesystem libraries.
            if line.strip() and not any(marker in line for marker in (
                "linux-vdso", "statically linked", "not a dynamic executable",
            )):
                errors.append(f"{path}: unrecognized loader output: {line.strip()}")
            continue
        dependency = Path(resolved_path.group(1))
        location, resolved = dependency_location(dependency, stage)
        record["dependencies"].append({
            "path": str(dependency), "resolved": resolved, "location": location,
        })
        if location not in ("stage", "system"):
            errors.append(f"{path}: {location} dependency outside stage: {resolved}")
    return record


def inventory_stage(stage, errors):
    inventory = []
    elf_paths = []

    def fail_walk(error):
        raise error

    # Do not follow directory symlinks, including links back into the source tree.
    for directory, directories, files in os.walk(stage, followlinks=False, onerror=fail_walk):
        for name in sorted(directories + files):
            path = Path(directory) / name
            relative = path.relative_to(stage).as_posix()
            if path.is_symlink():
                target = os.readlink(path)
                inventory.append({"path": relative, "type": "symlink", "target": target})
                try:
                    resolved = path.resolve(strict=True)
                    if not resolved.is_relative_to(stage):
                        errors.append(f"{path}: symlink escapes stage: {target}")
                    if Path(target).is_absolute():
                        errors.append(f"{path}: absolute symlink is not relocatable: {target}")
                except (OSError, RuntimeError) as error:
                    errors.append(f"{path}: invalid symlink: {error}")
            elif path.is_file():
                with path.open("rb") as stream:
                    magic = stream.read(4)
                    stream.seek(0)
                    checksum = hashlib.file_digest(stream, "sha256").hexdigest()
                inventory.append({
                    "path": relative, "type": "file", "size": path.stat().st_size,
                    "sha256": checksum,
                })
                if magic == b"\x7fELF":
                    elf_paths.append(path)
            elif not path.is_dir():
                errors.append(f"{path}: unsupported file type")
    return sorted(inventory, key=lambda entry: entry["path"]), sorted(elf_paths)


def verify(stage):
    errors = []
    inventory, elf_paths = inventory_stage(stage, errors)
    if not elf_paths:
        errors.append("Stage contains no regular ELF files to verify")
    elf_objects = []
    for path in elf_paths:
        try:
            elf_objects.append(check_elf(path, stage, errors))
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            errors.append(f"{path}: ELF check could not complete: {error}")
    return {
        "schema_version": 1,
        "stage": str(stage),
        "scope": "Linux file inventory and ELF loader isolation; no graphical or license acceptance",
        "passed": not errors,
        "system_library_roots": sorted({str(root) for root in SYSTEM_LIBRARY_ROOTS}),
        "inventory": inventory,
        "elf_objects": elf_objects,
        "errors": errors,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path, help="trusted local staged installation")
    parser.add_argument("--output", required=True, type=Path, help="JSON evidence path outside the stage")
    args = parser.parse_args()
    stage = args.stage.resolve()
    output = args.output.resolve()
    if not stage.is_dir():
        parser.error(f"stage directory does not exist: {stage}")
    if output.is_relative_to(stage):
        parser.error("output must be outside the stage so the inventory stays stable")
    try:
        report = verify(stage)
    except (OSError, RuntimeError) as error:
        report = {"schema_version": 1, "stage": str(stage), "passed": False,
                  "errors": [f"Inventory could not complete: {error}"]}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    status = "PASS" if report["passed"] else "FAIL"
    print(f"{status}: {len(report.get('elf_objects', []))} ELF objects; "
          f"{len(report['errors'])} errors; {output}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
