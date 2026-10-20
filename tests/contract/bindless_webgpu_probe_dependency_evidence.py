"""Preserve the selected probe's actual Ninja-listed dependency bytes.

This is current input preservation, not retrospective compiler-input proof or
complete transitive SDK provenance. It never changes a payload or GPU gate.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json, verify_file
from bindless_webgpu_probe_archive_audit import (
    OBJECTS, configuration, dependency_profile, inspect_ninja_log)
from bindless_webgpu_probe_build_evidence import TARGET


def selected_dependencies(build, inventories, object_mtimes):
    if set(inventories) != set(OBJECTS) or set(object_mtimes) != set(OBJECTS):
        raise ValueError("PROBE-DEPENDENCY-OBJECT-SET")
    owners = {}
    for label, target in OBJECTS.items():
        profile, status = dependency_profile(inventories[label], target)
        if status != "VALID":
            raise ValueError("PROBE-DEPENDENCY-STALE-INVENTORY")
        resolved = []
        for name in profile["paths"]:
            path = pathlib.Path(name)
            path = (path if path.is_absolute() else build / path).resolve()
            if not path.is_file():
                raise ValueError("PROBE-DEPENDENCY-MISSING-SOURCE")
            if path.stat().st_mtime_ns > object_mtimes[label]:
                raise ValueError("PROBE-DEPENDENCY-NEWER-THAN-OBJECT")
            resolved.append(path)
            owners.setdefault(path, []).append(label)
        if len(set(resolved)) != len(resolved):
            raise ValueError("PROBE-DEPENDENCY-DUPLICATE-SOURCE")
    return owners


def freeze(args):
    build = args.build.resolve()
    archive = args.output.resolve()
    reference = read_json(args.build_replay)
    if (not isinstance(reference, dict) or reference.get("complete") is not True
            or reference.get("mode") != "probe"):
        raise ValueError("PROBE-DEPENDENCY-BUILD-REPLAY-REQUIRED")
    for gate in CLOSED_GATES:
        if reference.get(gate) is not False:
            raise ValueError("PROBE-DEPENDENCY-REFERENCE-GATE")
    if set(reference.get("payload", {})) != {TARGET + suffix for suffix in (".js", ".wasm", ".data")}:
        raise ValueError("PROBE-DEPENDENCY-PAYLOAD-SET")
    source_archive = pathlib.Path(reference["sourceArchive"]["path"])
    # The separately preserved build manifest is large; use the standard
    # streaming digest rather than loading its symbol inventories into memory.
    with source_archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if {"bytes": source_archive.stat().st_size, "sha256": digest} != {
            key: reference["sourceArchive"][key] for key in ("bytes", "sha256")}:
        raise ValueError("PROBE-DEPENDENCY-BUILD-ARCHIVE-MISMATCH")
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if configuration(cache) != build:
        raise ValueError("PROBE-DEPENDENCY-BUILD-OWNER")
    records = inspect_ninja_log((build / ".ninja_log").read_text(encoding="utf-8"),
                               list(OBJECTS.values()) + [TARGET + ".js"])
    inventories = {label: subprocess.check_output(
        [str(args.ninja), "-C", str(build), "-t", "deps", target], encoding="utf-8")
        for label, target in OBJECTS.items()}
    mtimes = {label: (build / target).stat().st_mtime_ns for label, target in OBJECTS.items()}
    owners = selected_dependencies(build, inventories, mtimes)
    for label, target in OBJECTS.items():
        profile, unused = dependency_profile(inventories[label], target)
        if records[target]["mtime"] > profile["mtime"] or profile["mtime"] > records[TARGET + ".js"]["mtime"]:
            raise ValueError("PROBE-DEPENDENCY-EXECUTION-ORDER")
    payload = {name: fingerprint(build / name) for name in reference["payload"]}
    if payload != reference["payload"]:
        raise ValueError("PROBE-DEPENDENCY-PAYLOAD-MISMATCH")
    sources = set(owners) | {build / target for target in OBJECTS.values()}
    sources.update(build / name for name in ("CMakeCache.txt", ".ninja_log", ".ninja_deps"))
    sources.update(build / name for name in payload)
    sources.add(args.build_replay.resolve())
    stamps = {path: fingerprint(path) for path in sources}
    input_mtimes = {path: path.stat().st_mtime_ns for path in sources}
    archive.mkdir(parents=True, exist_ok=False)
    rows = []
    for index, (source, stamp) in enumerate(sorted(stamps.items())):
        relative = f"artifacts/{index}/{source.name}"
        destination = archive / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        row = dict(source=str(source), path=relative,
                   dependencyOwners=owners.get(source, []), observedMtimeNs=input_mtimes[source], **stamp)
        verify_file(archive, row)
        if fingerprint(source) != stamp:
            raise ValueError("PROBE-DEPENDENCY-CHANGED-DURING-COPY")
        rows.append(row)
    for label, target in OBJECTS.items():
        current = subprocess.check_output(
            [str(args.ninja), "-C", str(build), "-t", "deps", target], encoding="utf-8")
        if current != inventories[label] or (build / target).stat().st_mtime_ns != mtimes[label]:
            raise ValueError("PROBE-DEPENDENCY-OBJECT-CHANGED")
    if any(fingerprint(path) != stamp or path.stat().st_mtime_ns != input_mtimes[path]
           for path, stamp in stamps.items()):
        raise ValueError("PROBE-DEPENDENCY-INPUT-CHANGED")
    manifest = dict(contract="SelectedProbeDependencyBytes/1", complete=True,
        scope="current dependency bytes for three selected objects only",
        logicalBuildRoot=str(build), files=rows, inventories=inventories, objectMtimes=mtimes,
        uniqueDependencies=len(owners), payload=payload,
        sourceBuildArchive=reference["sourceArchive"], sourceBuildArchivePreservedSeparately=True,
        originalCompilerInputBytesVerified=False, sdkCompilerAndLinkInputsVerified=False,
        transitiveSdkProvenanceComplete=False, gpuExecuted=False, **CLOSED_GATES)
    (archive / "validation.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print("Selected dependency bytes preserved", len(owners), "; full SDK and GPU gates remain closed")


def replay(archive):
    manifest_path = archive / "validation.json"
    manifest_stamp = fingerprint(manifest_path)
    manifest = read_json(manifest_path)
    if (not isinstance(manifest, dict) or manifest.get("contract") != "SelectedProbeDependencyBytes/1"
            or manifest.get("complete") is not True):
        raise ValueError("PROBE-DEPENDENCY-ARCHIVE-CONTRACT")
    if (not isinstance(manifest.get("files"), list) or not manifest["files"]
            or not isinstance(manifest.get("inventories"), dict)
            or not isinstance(manifest.get("objectMtimes"), dict)
            or not isinstance(manifest.get("payload"), dict)
            or not isinstance(manifest.get("logicalBuildRoot"), str)):
        raise ValueError("PROBE-DEPENDENCY-ARCHIVE-SHAPE")
    for field in (*CLOSED_GATES, "originalCompilerInputBytesVerified", "sdkCompilerAndLinkInputsVerified",
                  "transitiveSdkProvenanceComplete", "gpuExecuted"):
        if manifest.get(field) is not False:
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-GATE")
    sources, archive_paths = {}, set()
    for row in manifest["files"]:
        if (not isinstance(row, dict) or not isinstance(row.get("source"), str)
                or not isinstance(row.get("path"), str) or type(row.get("bytes")) is not int
                or row["bytes"] < 0 or not isinstance(row.get("sha256"), str)
                or type(row.get("observedMtimeNs")) is not int or row["observedMtimeNs"] < 0
                or not isinstance(row.get("dependencyOwners"), list)
                or any(not isinstance(owner, str) or owner not in OBJECTS for owner in row["dependencyOwners"])):
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-ROW")
        source = pathlib.Path(row["source"])
        copied = verify_file(archive, row)
        if not source.is_absolute() or source in sources or copied in archive_paths:
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-DUPLICATE")
        sources[source] = (row, copied)
        archive_paths.add(copied)
    build = pathlib.Path(manifest["logicalBuildRoot"])
    if (not build.is_absolute() or set(manifest["inventories"]) != set(OBJECTS)
            or set(manifest["objectMtimes"]) != set(OBJECTS)
            or set(manifest["payload"]) != {TARGET + suffix for suffix in (".js", ".wasm", ".data")}):
        raise ValueError("PROBE-DEPENDENCY-ARCHIVE-SHAPE")
    if build / ".ninja_log" not in sources:
        raise ValueError("PROBE-DEPENDENCY-ARCHIVE-MISSING")
    records = inspect_ninja_log(sources[build / ".ninja_log"][1].read_text(encoding="utf-8"),
                               list(OBJECTS.values()) + [TARGET + ".js"])
    expected = {}
    for label, target in OBJECTS.items():
        profile, status = dependency_profile(manifest["inventories"][label], target)
        if status != "VALID" or build / target not in sources:
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-OBJECT")
        mtime = manifest["objectMtimes"][label]
        if (type(mtime) is not int or mtime <= 0
                or sources[build / target][0]["observedMtimeNs"] != mtime
                or records[target]["mtime"] > profile["mtime"]
                or profile["mtime"] > records[TARGET + ".js"]["mtime"]):
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-OBJECT-TIME")
        paths = [pathlib.Path(name) for name in profile["paths"]]
        paths = [path if path.is_absolute() else build / path for path in paths]
        if len(set(paths)) != len(paths):
            raise ValueError("PROBE-DEPENDENCY-DUPLICATE-SOURCE")
        for path in paths:
            if path not in sources:
                raise ValueError("PROBE-DEPENDENCY-ARCHIVE-MISSING")
            if sources[path][0]["observedMtimeNs"] > mtime:
                raise ValueError("PROBE-DEPENDENCY-NEWER-THAN-OBJECT")
            expected.setdefault(path, []).append(label)
    if len(expected) != manifest["uniqueDependencies"]:
        raise ValueError("PROBE-DEPENDENCY-ARCHIVE-COUNT")
    for source, (row, copied) in sources.items():
        if row["dependencyOwners"] != expected.get(source, []):
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-OWNER")
    for name, stamp in manifest["payload"].items():
        if build / name not in sources:
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-MISSING")
        if fingerprint(sources[build / name][1]) != stamp:
            raise ValueError("PROBE-DEPENDENCY-ARCHIVE-PAYLOAD")
    for row in manifest["files"]:
        verify_file(archive, row)
    if fingerprint(manifest_path) != manifest_stamp:
        raise ValueError("PROBE-DEPENDENCY-ARCHIVE-CHANGED")
    return dict(contract="SelectedProbeDependencyBytesReplay/1", complete=True,
                sourceArchive=dict(path=str(manifest_path), **manifest_stamp),
                filesVerified=len(sources), uniqueDependencies=len(expected), liveSourcesRead=False,
                sourceBuildArchiveDigestRechecked=False,
                originalCompilerInputBytesVerified=False, sdkCompilerAndLinkInputsVerified=False,
                transitiveSdkProvenanceComplete=False, gpuExecuted=False, **CLOSED_GATES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    create = sub.add_parser("freeze")
    create.add_argument("--build", type=pathlib.Path, required=True)
    create.add_argument("--build-replay", type=pathlib.Path, required=True)
    create.add_argument("--ninja", type=pathlib.Path, required=True)
    create.add_argument("--output", type=pathlib.Path, required=True)
    audit = sub.add_parser("replay")
    audit.add_argument("--archive", type=pathlib.Path, required=True)
    audit.add_argument("--report", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        if args.mode == "freeze":
            freeze(args)
        else:
            result = replay(args.archive.resolve())
            args.report.write_text(json.dumps(result, indent=2), encoding="utf-8")
            print("Selected dependency replay passed", result["uniqueDependencies"])
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
