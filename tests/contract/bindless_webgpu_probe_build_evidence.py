"""Inspect the real Ninja object/link graph for the finite negative-probe build.

Read-only compiler/symbol inspection never supplies GPU completion or capability
authority. Complete build provenance additionally requires a successful verbose
fresh-build log containing the exact object and link commands.
"""
import argparse
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import json

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, ROOT

MACRO = "ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES"
TARGET = "arp_rhi_webgpu_bindless_pipeline_browser_smoke"
ORDINARY = "src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp"
MAIN = "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"
PROBE = "tests/integration/webgpu_post_suppression_command_preparation.cpp"
SYMBOL = "arp::test::WebGpuPostSuppressionFailureProbeFixture::"


def command_tokens(command):
    # Ninja's Windows compiler invocation grammar, not a shell evaluation.
    return [token[1:-1] if len(token) > 1 and token[0] == token[-1] == '"' else token
            for token in shlex.split(command, posix=False)]


def inspect_compile_command(command, source, output, probe, assertions, source_root=None):
    source_root = ROOT if source_root is None else source_root
    tokens = command_tokens(command)
    normalized = [token.replace("\\", "/") for token in tokens]
    try:
        if (tokens.count("-c") != 1 or tokens.count("-o") != 1
                or normalized[tokens.index("-c") + 1] != (source_root / source).as_posix()
                or normalized[tokens.index("-o") + 1] != output):
            raise ValueError("PROBE-BUILD-COMPILE-OWNER")
    except IndexError as error:
        raise ValueError("PROBE-BUILD-COMPILE-OWNER") from error
    definitions = [token for token in tokens if token.startswith("-D" + MACRO)]
    if definitions != (["-D" + MACRO + "=1"] if probe else []):
        raise ValueError("PROBE-BUILD-MACRO-ISOLATION")
    if [token for token in tokens if token.startswith("-std=")] != ["-std=c++17"]:
        raise ValueError("PROBE-BUILD-LANGUAGE")
    if assertions:
        assertion_flags = [token for token in tokens if token in ("-DNDEBUG", "-UNDEBUG")]
        if not assertion_flags or assertion_flags[-1] != "-UNDEBUG":
            raise ValueError("PROBE-BUILD-ASSERTIONS")


def inspect_query(query, required, forbidden):
    if "  input: " not in query or "  outputs:" not in query:
        raise ValueError("PROBE-BUILD-QUERY")
    inputs = []
    for line in query.split("  input: ", 1)[1].split("  outputs:", 1)[0].splitlines()[1:]:
        if line.startswith("    "):
            inputs.append(line.strip().lstrip("| ").replace("\\", "/"))
    if any(inputs.count(name) != 1 for name in required) or any(name in inputs for name in forbidden):
        raise ValueError("PROBE-BUILD-LINK-ISOLATION")
    return inputs


def inspect_probe_symbols(symbols, probe):
    definitions = [line for line in symbols.splitlines() if SYMBOL + "arm(" in line or SYMBOL + "inspect(" in line]
    if not probe:
        if definitions:
            raise ValueError("PROBE-BUILD-ORDINARY-SYMBOL")
        return []
    for method in ("arm", "inspect"):
        for owner in ("WebGpuOriginalTypedCallbackCustody", "WebGpuCanonicalCallbackCustody"):
            if sum(SYMBOL + method + "(" in line and owner in line for line in definitions) != 1:
                raise ValueError("PROBE-BUILD-EXPORTED-SYMBOL")
    if len(definitions) != 4:
        raise ValueError("PROBE-BUILD-EXPORTED-SYMBOL")
    return definitions


def inspect_build_log(text, commands):
    # Ninja -v prefixes commands with its progress counter. No rewrite of the
    # command itself is permitted; a configured recipe is not an executed build.
    lines = [re.sub(r"^\[\d+/\d+\] ", "", line).strip() for line in text.splitlines()]
    if any(lines.count(command.strip()) != 1 for command in commands):
        raise ValueError("PROBE-BUILD-EXECUTED-COMMAND")
    if any(line.startswith(("FAILED:", "ninja: build stopped:")) for line in lines):
        raise ValueError("PROBE-BUILD-FAILED-COMMAND")


def inspect(build, mode, ninja, nm, build_log=None):
    if mode not in ("ordinary", "probe"):
        raise ValueError("PROBE-BUILD-MODE")
    build = build.resolve()
    enabled = mode == "probe"
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
    for setting in (MACRO + ":BOOL=" + ("ON" if enabled else "OFF"),
                    "BUILD_SHARED_LIBS:BOOL=OFF", "CMAKE_BUILD_TYPE:STRING=Release"):
        if cache.count(setting) != 1:
            raise ValueError("PROBE-BUILD-CONFIGURATION")
    systems = list((build / "CMakeFiles").glob("*/CMakeSystem.cmake"))
    if len(systems) != 1 or 'set(CMAKE_SYSTEM_NAME "Emscripten")' not in systems[0].read_text(encoding="utf-8"):
        raise ValueError("PROBE-BUILD-PLATFORM")
    files = {build / name for name in ("CMakeCache.txt", "build.ninja", ".ninja_log", ".ninja_deps",
                                      "CMakeFiles/rules.ninja", "libarp_rhi_webgpu.a")}
    files.add(systems[0])
    files.update((pathlib.Path(__file__).resolve(),
                  pathlib.Path(__file__).with_name("bindless_webgpu_probe_build_evidence_test.py")))
    files.update(ROOT / name for name in ("CMakeLists.txt", ORDINARY, MAIN, PROBE,
                                         "tests/fixtures/webgpu_post_suppression_failure_probe_fixture.h"))
    objects = {"ordinary": "CMakeFiles/arp_rhi_webgpu.dir/" + ORDINARY + ".o",
               "main": "CMakeFiles/" + TARGET + ".dir/" + MAIN + ".o"}
    if enabled:
        objects["probe"] = "CMakeFiles/" + TARGET + ".dir/" + PROBE + ".o"
    commands, query_outputs, symbol_outputs, dependencies = {}, {}, {}, {}
    def ninja_tool(tool, target):
        return subprocess.check_output([str(ninja), "-C", str(build), "-t", tool, target], encoding="utf-8")
    def symbols(path):
        return subprocess.check_output([str(nm), "--defined-only", "--demangle", str(path)], encoding="utf-8")
    for label, output in objects.items():
        source = dict(ordinary=ORDINARY, main=MAIN, probe=PROBE)[label]
        command_list = ninja_tool("commands", output).splitlines()
        matches = [command for command in command_list if " -c " in command and " -o " in command
                   and output in command.replace("\\", "/")]
        if len(matches) != 1:
            raise ValueError("PROBE-BUILD-COMPILE-COMMAND")
        command = matches[0]
        inspect_compile_command(command, source, output, enabled and label != "ordinary", label != "ordinary")
        commands[label] = command
        path = build / output
        files.add(path)
        symbols_output = symbols(path)
        inspect_probe_symbols(symbols_output, enabled and label == "probe")
        symbol_outputs[label] = symbols_output
        deps = ninja_tool("deps", output)
        if not deps.splitlines() or not deps.splitlines()[0].endswith("(VALID)"):
            raise ValueError("PROBE-BUILD-OBJECT-DEPS")
        source_paths = [pathlib.Path(line.strip()) for line in deps.splitlines()[1:] if line.startswith("    ")]
        source_paths = [path.resolve() if path.is_absolute() else (build / path).resolve() for path in source_paths]
        object_mtime = path.stat().st_mtime_ns
        if not source_paths or any(not dependency.is_file() or dependency.stat().st_mtime_ns > object_mtime
                                   for dependency in source_paths):
            raise ValueError("PROBE-BUILD-STALE-OBJECT")
        dependencies[label] = dict(count=len(source_paths), ninjaOutput=deps)
    library_query = ninja_tool("query", "libarp_rhi_webgpu.a")
    inspect_query(library_query, [objects["ordinary"]], ["CMakeFiles/" + TARGET + ".dir/" + PROBE + ".o"])
    query_outputs["library"] = library_query
    library_symbols = symbols(build / "libarp_rhi_webgpu.a")
    inspect_probe_symbols(library_symbols, False)
    symbol_outputs["library"] = library_symbols
    target_query = ninja_tool("query", TARGET + ".js")
    probe_object = "CMakeFiles/" + TARGET + ".dir/" + PROBE + ".o"
    inputs = inspect_query(target_query, [objects["main"]] + ([probe_object] if enabled else []),
                           [] if enabled else [probe_object])
    if {name for name in inputs if name.endswith(".o")} != {objects["main"]} | ({probe_object} if enabled else set()):
        raise ValueError("PROBE-BUILD-EXTRA-LINKED-OBJECT")
    query_outputs["browser"] = target_query
    link_matches = [command for command in ninja_tool("commands", TARGET + ".js").splitlines()
                    if re.search(r'(?:^|\s)-o\s+"?' + re.escape(TARGET + ".js") + r'"?(?:\s|$)', command)]
    if len(link_matches) != 1:
        raise ValueError("PROBE-BUILD-LINK-COMMAND")
    commands["link"] = link_matches[0]
    payload = [build / (TARGET + suffix) for suffix in (".js", ".wasm", ".data")]
    files.update(payload)
    link_inputs = [build / name for name in inputs if name.endswith((".o", ".a"))]
    if any(not path.is_file() for path in link_inputs) or any(
            path.stat().st_mtime_ns > payload[0].stat().st_mtime_ns for path in link_inputs):
        raise ValueError("PROBE-BUILD-STALE-LINK")
    files.update(link_inputs)
    executed = build_log is not None
    if executed:
        inspect_build_log(build_log.read_text(encoding="utf-8"), list(commands.values()))
        files.add(build_log.resolve())
    return dict(contract="WebGpuProbeBuildEvidence/1", complete=executed, mode=mode,
        probeEnabled=enabled, actualLinkExecutionValidated=executed,
        configuredObjectGraphValidated=True, originalBackendProbeDefinitionsAbsent=True,
        assertionsEnabledOnFixture=True, objectDependenciesFresh=True,
        transitiveSdkProvenanceComplete=False, commands=commands, queries=query_outputs,
        symbolOutputs=symbol_outputs, dependencies=dependencies,
        payload={path.name: fingerprint(path) for path in payload},
        files={path: fingerprint(path) for path in files}, **CLOSED_GATES)


def run(args):
    result = inspect(args.build, args.mode, args.ninja, args.nm, args.build_log)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    artifacts = []
    for index, (source, expected) in enumerate(sorted(result.pop("files").items())):
        destination = output / "artifacts" / str(index) / source.name
        destination.parent.mkdir(parents=True)
        shutil.copyfile(source, destination)
        if fingerprint(destination) != expected or fingerprint(source) != expected:
            raise ValueError("PROBE-BUILD-CHANGED-INPUT")
        artifacts.append(dict(source=str(source), path=str(destination.relative_to(output)), **expected))
    result["artifacts"] = artifacts
    name = "validation.json" if result["complete"] else "inspection.json"
    (output / name).write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("Build graph inspected:", args.mode, "; executed verbose provenance:", result["complete"],
          "; all GPU and aggregate gates closed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--mode", choices=("ordinary", "probe"), required=True)
    parser.add_argument("--ninja", type=pathlib.Path, default=pathlib.Path("C:/mingw64/bin/ninja.exe"))
    parser.add_argument("--nm", type=pathlib.Path, default=pathlib.Path("C:/emsdk/upstream/bin/llvm-nm.exe"))
    parser.add_argument("--build-log", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        run(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
