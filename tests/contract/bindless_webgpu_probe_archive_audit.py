"""Replay copied probe build evidence without using the original build directory.

Ninja is invoked only with -t to inspect archived recipes/dependencies. LLVM nm
reads archived objects/libraries. No compiler, linker or GPU work is executed.
This validates selected build evidence, not transitive SDK or runtime authority.
"""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json, verify_file
from bindless_finite_lifecycle_evidence import require_fields
from bindless_webgpu_probe_build_evidence import (
    ROOT, MACRO, TARGET, ORDINARY, MAIN, PROBE,
    inspect_compile_command, inspect_query, inspect_probe_symbols, inspect_build_log)

OBJECTS = {"ordinary": "CMakeFiles/arp_rhi_webgpu.dir/" + ORDINARY + ".o",
           "main": "CMakeFiles/" + TARGET + ".dir/" + MAIN + ".o",
           "probe": "CMakeFiles/" + TARGET + ".dir/" + PROBE + ".o"}


def artifact_inventory(root, manifest):
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise ValueError("PROBE-ARCHIVE-ARTIFACTS")
    sources, archived = {}, set()
    for entry in artifacts:
        if not isinstance(entry.get("source"), str):
            raise ValueError("PROBE-ARCHIVE-SOURCE")
        source = pathlib.Path(entry["source"]).resolve()
        path = verify_file(root, entry)
        if source in sources or path in archived:
            raise ValueError("PROBE-ARCHIVE-DUPLICATE-ARTIFACT")
        sources[source] = path; archived.add(path)
    return sources


def cache_directory(text, key, diagnostic):
    prefix = key + ":INTERNAL="
    owners = [line.split("=", 1)[1] for line in text.splitlines() if line.startswith(prefix)]
    if len(owners) != 1 or not pathlib.Path(owners[0]).is_absolute():
        raise ValueError(diagnostic)
    return pathlib.Path(owners[0]).resolve()


def configuration(text):
    lines = text.splitlines()
    for required in (MACRO + ":BOOL=ON", "BUILD_SHARED_LIBS:BOOL=OFF", "CMAKE_BUILD_TYPE:STRING=Release"):
        if lines.count(required) != 1:
            raise ValueError("PROBE-ARCHIVE-CONFIGURATION")
    return cache_directory(text, "CMAKE_CACHEFILE_DIR", "PROBE-ARCHIVE-BUILD-OWNER")


def inspect_ninja_includes(build, rules):
    directives = lambda text: [line.strip() for line in text.splitlines()
        if re.match(r"\s*(include|subninja)\s", line)]
    if directives(build) != ["include CMakeFiles/rules.ninja"] or directives(rules):
        raise ValueError("PROBE-ARCHIVE-NINJA-INCLUDE")


def dependency_profile(text, target):
    lines = text.splitlines()
    pattern = re.escape(target) + r": #deps (\d+), deps mtime (\d+) \((VALID|STALE)\)"
    match = re.fullmatch(pattern, lines[0] if lines else "")
    paths = [line.strip() for line in lines[1:] if line.startswith("    ")]
    if not match or not paths or len(paths) != int(match[1]):
        raise ValueError("PROBE-ARCHIVE-DEPS")
    # Recreated objects do not inherit their historical mtime. Recheck the exact
    # binary dependency inventory, never promote STALE to current freshness.
    return dict(count=len(paths), mtime=int(match[2]), paths=paths), match[3]


def inspect_ninja_log(text, outputs):
    lines = text.splitlines()
    # Current Windows Ninja 1.13.2 writes version7 with these five fields.
    # Reject other schemas rather than assuming identical timestamp semantics.
    if not lines or lines[0] != "# ninja log v7":
        raise ValueError("PROBE-ARCHIVE-NINJA-LOG")
    if len(set(outputs)) != len(outputs):
        raise ValueError("PROBE-ARCHIVE-NINJA-EXECUTION")
    records = {}
    for line in lines[1:]:
        fields = line.split("\t")
        if (len(fields) != 5 or any(not word.isdigit() for word in fields[:3])
                or int(fields[1]) < int(fields[0]) or int(fields[2]) == 0
                or not re.fullmatch(r"[0-9a-f]{1,16}", fields[4])):
            raise ValueError("PROBE-ARCHIVE-NINJA-LOG")
        output = fields[3].replace("\\", "/")
        if output not in outputs:
            continue
        record = dict(startMs=int(fields[0]), endMs=int(fields[1]),
                      mtime=int(fields[2]), commandHash=fields[4])
        # Incremental Ninja logs append real rebuilds. Preserve all input bytes
        # and select the final row, refusing replayed or regressed timestamps.
        if output in records and record["mtime"] <= records[output]["mtime"]:
            raise ValueError("PROBE-ARCHIVE-NINJA-EXECUTION")
        records[output] = record
    if set(records) != set(outputs):
        raise ValueError("PROBE-ARCHIVE-NINJA-EXECUTION")
    return records


def replay(root, build_log_source, ninja, nm):
    root = root.resolve(); manifest_path = root / "validation.json"
    stamp = fingerprint(manifest_path)
    manifest = read_json(manifest_path)
    require_fields(manifest, dict(contract="WebGpuProbeBuildEvidence/1", complete=True, mode="probe",
        probeEnabled=True, actualLinkExecutionValidated=True, configuredObjectGraphValidated=True,
        originalBackendProbeDefinitionsAbsent=True, assertionsEnabledOnFixture=True,
        objectDependenciesFresh=True, transitiveSdkProvenanceComplete=False, **CLOSED_GATES))
    sources = artifact_inventory(root, manifest)
    caches = [source for source in sources if source.name == "CMakeCache.txt"]
    if len(caches) != 1:
        raise ValueError("PROBE-ARCHIVE-CACHE")
    cache_text = sources[caches[0]].read_text(encoding="utf-8")
    build = configuration(cache_text)
    source_root = cache_directory(cache_text, "CMAKE_HOME_DIRECTORY", "PROBE-ARCHIVE-SOURCE-OWNER")
    if caches[0] != build / "CMakeCache.txt":
        raise ValueError("PROBE-ARCHIVE-CACHE-OWNER")
    def archived(source):
        source = source.resolve()
        if source not in sources:
            raise ValueError("PROBE-ARCHIVE-MISSING-ARTIFACT: " + str(source))
        return sources[source]
    for name in ("CMakeLists.txt", ORDINARY, MAIN, PROBE,
                 "tests/fixtures/webgpu_post_suppression_failure_probe_fixture.h"):
        archived(source_root / name)
    systems = [source for source in sources if source.is_relative_to(build / "CMakeFiles")
               and source.name == "CMakeSystem.cmake"]
    if len(systems) != 1 or 'set(CMAKE_SYSTEM_NAME "Emscripten")' not in archived(systems[0]).read_text(encoding="utf-8"):
        raise ValueError("PROBE-ARCHIVE-PLATFORM")
    build_text = archived(build / "build.ninja").read_text(encoding="utf-8")
    rules_text = archived(build / "CMakeFiles/rules.ninja").read_text(encoding="utf-8")
    inspect_ninja_includes(build_text, rules_text)
    archived(build / ".ninja_deps")
    execution_records = inspect_ninja_log(
        archived(build / ".ninja_log").read_text(encoding="utf-8"),
        list(OBJECTS.values()) + [TARGET + ".js"])
    commands, queries, dependencies, dependency_mtimes = {}, {}, {}, {}
    with tempfile.TemporaryDirectory(prefix="arp-probe-build-replay-") as directory:
        recovered = pathlib.Path(directory)
        for name in ("build.ninja", "CMakeFiles/rules.ninja", ".ninja_deps"):
            destination = recovered / name; destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(archived(build / name), destination)
        def query(tool, target):
            return subprocess.check_output([str(ninja), "-C", str(recovered), "-t", tool, target], encoding="utf-8")
        for label, output in OBJECTS.items():
            matches = [command for command in query("commands", output).splitlines()
                       if " -c " in command and " -o " in command and output in command.replace("\\", "/")]
            if len(matches) != 1:
                raise ValueError("PROBE-ARCHIVE-COMPILE-COMMAND")
            commands[label] = matches[0]
            inspect_compile_command(matches[0], dict(ordinary=ORDINARY, main=MAIN, probe=PROBE)[label],
                                    output, label != "ordinary", label != "ordinary", source_root=source_root)
            captured = manifest["dependencies"][label]
            expected, status = dependency_profile(captured["ninjaOutput"], output)
            require_fields(captured, dict(count=expected["count"]))
            actual, unused = dependency_profile(query("deps", output), output)
            if status != "VALID" or actual != expected:
                raise ValueError("PROBE-ARCHIVE-DEPS-SUBSTITUTION")
            # Real Ninja v7 edge log and deps timestamps can differ. A deps
            # record must cover the latest logged output and precede the link;
            # do not pretend the independent observations have equal times.
            if execution_records[output]["mtime"] > expected["mtime"]:
                raise ValueError("PROBE-ARCHIVE-OBJECT-MTIME")
            dependencies[label] = expected["count"]
            dependency_mtimes[label] = expected["mtime"]
        if any(execution_records[TARGET + ".js"]["mtime"] < mtime
               for mtime in dependency_mtimes.values()):
            raise ValueError("PROBE-ARCHIVE-LINK-MTIME")
        queries["library"] = query("query", "libarp_rhi_webgpu.a")
        inspect_query(queries["library"], [OBJECTS["ordinary"]], [OBJECTS["probe"]])
        queries["browser"] = query("query", TARGET + ".js")
        inputs = inspect_query(queries["browser"], [OBJECTS["main"], OBJECTS["probe"]], [])
        if {name for name in inputs if name.endswith(".o")} != {OBJECTS["main"], OBJECTS["probe"]}:
            raise ValueError("PROBE-ARCHIVE-EXTRA-LINKED-OBJECT")
        matches = [command for command in query("commands", TARGET + ".js").splitlines()
                   if re.search(r'(?:^|\s)-o\s+"?' + re.escape(TARGET + ".js") + r'"?(?:\s|$)', command)]
        if len(matches) != 1:
            raise ValueError("PROBE-ARCHIVE-LINK-COMMAND")
        commands["link"] = matches[0]
    require_fields(manifest, dict(commands=commands, queries=queries))
    inspect_build_log(archived(build_log_source).read_text(encoding="utf-8"), list(commands.values()))
    symbol_files = {**{label: build / name for label, name in OBJECTS.items()},
                    "library": build / "libarp_rhi_webgpu.a"}
    for label, source in symbol_files.items():
        symbols = subprocess.check_output([str(nm), "--defined-only", "--demangle", str(archived(source))], encoding="utf-8")
        inspect_probe_symbols(symbols, label == "probe")
        require_fields(manifest["symbolOutputs"], {label: symbols})
    for name in inputs:
        if name.endswith((".o", ".a")):
            archived(build / name)
    payload = {TARGET + suffix: fingerprint(archived(build / (TARGET + suffix)))
               for suffix in (".js", ".wasm", ".data")}
    require_fields(manifest, dict(payload=payload))
    if fingerprint(manifest_path) != stamp or any(
            fingerprint(verify_file(root, entry)) != dict(bytes=entry["bytes"], sha256=entry["sha256"])
            for entry in manifest["artifacts"]):
        raise ValueError("PROBE-ARCHIVE-CHANGED-DURING-REPLAY")
    return dict(contract="WebGpuProbeArchiveReplay/1", complete=True,
        sourceArchive=dict(path=str(manifest_path), **stamp), mode="probe", artifactsVerified=len(sources),
        logicalSourceRoot=str(source_root),
        archivedRecipesReplayed=True, archivedObjectSymbolsReplayed=True,
        actualVerboseBuildEvidenceReplayed=True, originalBackendProbeDefinitionsAbsent=True,
        latestExecutionRecords=execution_records,
        dependencyMtimes=dependency_mtimes,
        dependencyInventoriesReplayed=dependencies, originalDependencyFreshnessObserved=True,
        liveDependencyFreshnessRechecked=False, transitiveSdkProvenanceComplete=False,
        payload=payload, postSuppressionRuntimeValidated=False, **CLOSED_GATES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=pathlib.Path, required=True)
    parser.add_argument("--build-log-source", type=pathlib.Path, required=True)
    parser.add_argument("--ninja", type=pathlib.Path, default=pathlib.Path("C:/mingw64/bin/ninja.exe"))
    parser.add_argument("--nm", type=pathlib.Path, default=pathlib.Path("C:/emsdk/upstream/bin/llvm-nm.exe"))
    parser.add_argument("--report", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        result = replay(args.archive, args.build_log_source, args.ninja, args.nm)
        with args.report.open("x", encoding="utf-8") as output:
            json.dump(result, output, indent=2)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
