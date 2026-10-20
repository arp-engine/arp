"""Execute the registered12 native probe cases with frozen selected evidence.

CTest executes the exact registered commands in an independent probe build. The
original ordinary matrix continues unchanged. This scoped attempt cannot close
affected regressions, aggregate Lifecycle4, public execution or production gates.
"""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as xml

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json, verify_file, verify_selector
from bindless_finite_lifecycle_evidence import inspect_finite_report
from bindless_webgpu_probe_archive_audit import replay as replay_probe

TARGET = "arp_rhi_webgpu_bindless_pipeline_browser_smoke"


def select_tests(inventory, build):
    expected = {name: identity for name, identity in expected_cases("compute-final").items()
                if identity[0] == "post_suppression"}
    tests = inventory.get("tests")
    if not isinstance(tests, list) or len({test["name"] for test in tests}) != len(tests):
        raise ValueError("POST-ATTEMPT-REGISTERED-INVENTORY")
    selected = {test["name"]: test for test in tests if test["name"] in expected}
    if set(selected) != set(expected):
        raise ValueError("POST-ATTEMPT-MISSING-CASE")
    for name, test in selected.items():
        command = test.get("command")
        if not isinstance(command, list):
            raise ValueError("POST-ATTEMPT-COMMAND")
        verify_selector(command, expected[name])
        if command.count("--output") != 1 or command.count("--runner") != 1:
            raise ValueError("POST-ATTEMPT-COMMAND")
        output = pathlib.Path(command[command.index("--output") + 1]).resolve()
        if (not output.is_relative_to(build.resolve())
                or pathlib.Path(command[command.index("--runner") + 1]).resolve() != build.resolve()
                or (output / "report.json").exists()):
            raise ValueError("POST-ATTEMPT-OUTPUT-OWNER")
    return expected, selected


def inspect_junit(path, expected):
    suite = xml.parse(path).getroot()
    cases = suite.findall("testcase")
    if (suite.tag != "testsuite" or suite.get("tests") != str(len(expected))
            or suite.get("failures") != "0" or suite.get("disabled") != "0"
            or len(cases) != len(expected)
            or {case.get("name") for case in cases} != set(expected)):
        raise ValueError("POST-ATTEMPT-JUNIT")
    if any(case.get("status") != "run" or case.find("failure") is not None or case.find("error") is not None
           or case.find("skipped") is not None for case in cases):
        raise ValueError("POST-ATTEMPT-JUNIT-TERMINAL")
    return {case.get("name"): case.findtext("system-out") for case in cases}


def capture_case_outputs(output, selected, stdout):
    """Retain failed/partial outputs too, without assigning native authority."""
    captured = []
    for name, test in selected.items():
        command = test["command"]
        case_root = pathlib.Path(command[command.index("--output") + 1])
        destination = output / "cases" / name; destination.mkdir(parents=True, exist_ok=True)
        files = []
        for filename in ("report.json", "browser.log"):
            source = case_root / filename
            if not source.is_file(): continue
            stamp = fingerprint(source); target = destination / filename
            if target.exists(): raise ValueError("POST-ATTEMPT-CAPTURE-EXISTS")
            shutil.copyfile(source, target)
            if fingerprint(source) != stamp or fingerprint(target) != stamp:
                raise ValueError("POST-ATTEMPT-CAPTURE-CHANGED")
            files.append(dict(source=str(source), path=str(target.relative_to(output)), **stamp))
        if isinstance(stdout.get(name), str):
            target = destination / "junit-output.log"
            with target.open("x", encoding="utf-8") as stream: stream.write(stdout[name])
            files.append(dict(source="ctest.xml:system-out:" + name,
                path=str(target.relative_to(output)), **fingerprint(target)))
        captured.append(dict(name=name, command=command, files=files, accepted=False))
    manifest = dict(contract="FinitePostSuppressionCaseCapture/1", complete=False,
        scope="captured registered CTest outputs only; no native acceptance",
        cases=len(selected), captured=captured, **CLOSED_GATES)
    with (output / "case-files.json").open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
    return manifest


def selection_expression(expected):
    # CTest uses CMake's regular-expression grammar, without Python's (?:...) form.
    return "^(" + "|".join(re.escape(name) for name in sorted(expected)) + ")$"


def verify_filtered_selection(expected, selected, filtered, build):
    unused, selected_filter = select_tests(filtered, build)
    # CTest rebuilds backtrace-graph node IDs for each inventory query. Only
    # that diagnostic reference may differ; commands and execution properties
    # must remain identical to the full registered inventory.
    registered_execution = {name: {key: value for key, value in test.items() if key != "backtrace"}
                            for name, test in selected.items()}
    filtered_execution = {name: {key: value for key, value in test.items() if key != "backtrace"}
                          for name, test in selected_filter.items()}
    if filtered_execution != registered_execution or len(filtered["tests"]) != len(expected):
        raise ValueError("POST-ATTEMPT-CTEST-SELECTION")


def run(args):
    build = args.build.resolve(); output = args.output.resolve()
    if type(args.test_output_bytes) is not int or args.test_output_bytes <= 0:
        raise ValueError("POST-ATTEMPT-LOG-BUDGET")
    if output == build or output.is_relative_to(build):
        raise ValueError("POST-ATTEMPT-OUTPUT-OVERLAP")
    probe = replay_probe(args.probe_archive, args.build_log_source, args.ninja, args.nm)
    for suffix in (".js", ".wasm", ".data"):
        if fingerprint(build / (TARGET + suffix)) != probe["payload"][TARGET + suffix]:
            raise ValueError("POST-ATTEMPT-PROBE-PAYLOAD")
    inventory = json.loads(subprocess.check_output(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"], encoding="utf-8-sig"))
    expected, selected = select_tests(inventory, build)
    expression = selection_expression(expected)
    filtered = json.loads(subprocess.check_output(["ctest", "--test-dir", str(build), "-R", expression,
                                                  "--show-only=json-v1"], encoding="utf-8-sig"))
    verify_filtered_selection(expected, selected, filtered, build)
    inputs = read_json(args.ordinary_frozen_inputs)
    old_payloads = [pathlib.Path(entry["source"]).resolve() for entry in inputs
                    if pathlib.Path(entry["source"]).name == TARGET + ".wasm"]
    if len(old_payloads) != 1:
        raise ValueError("POST-ATTEMPT-ORDINARY-INPUTS")
    for entry in inputs:
        if fingerprint(entry["source"]) != dict(bytes=entry["bytes"], sha256=entry["sha256"]):
            raise ValueError("POST-ATTEMPT-ORDINARY-INPUT-CHANGED")
    ordinary_build = old_payloads[0].parent
    files = {pathlib.Path(entry["source"]).resolve() for entry in inputs
             if not pathlib.Path(entry["source"]).resolve().is_relative_to(ordinary_build)}
    files.update(build / name for name in ("CMakeCache.txt", TARGET + ".js", TARGET + ".wasm", TARGET + ".data",
                                           "bindless_webgpu_rhi_runner.html", "browser_progress_transport.js"))
    files.update(path for path in (build / "bindless-rhi-fixture").rglob("*") if path.is_file())
    files.update(path for path in (build / "ordinary-pipeline-fixture").rglob("*") if path.is_file())
    files.add(pathlib.Path(__file__).resolve())
    files.add(pathlib.Path(__file__).with_name("bindless_webgpu_post_suppression_attempt_test.py"))
    for name in ("bindless_webgpu_probe_archive_audit.py", "bindless_webgpu_probe_archive_audit_test.py",
                 "bindless_webgpu_probe_build_evidence.py", "bindless_webgpu_probe_build_evidence_test.py"):
        files.add(pathlib.Path(__file__).with_name(name).resolve())
    frozen = {path.resolve(): fingerprint(path) for path in files}
    output.mkdir(parents=True, exist_ok=False)
    (output / "attempt.json").write_text(json.dumps(dict(complete=False, **CLOSED_GATES), indent=2), encoding="utf-8")
    archived = []
    for index, (source, stamp) in enumerate(sorted(frozen.items())):
        destination = output / "inputs" / str(index) / source.name
        destination.parent.mkdir(parents=True); shutil.copyfile(source, destination)
        if fingerprint(destination) != stamp:
            raise ValueError("POST-ATTEMPT-CHANGED-COPY")
        archived.append(dict(source=str(source), path=str(destination.relative_to(output)), **stamp))
    # Keep the complete verified build archive, not a boolean reference to it.
    probe_root = args.probe_archive.resolve(); parent = read_json(probe_root / "validation.json")
    probe_files = [(probe_root / "validation.json", pathlib.Path("validation.json"))]
    probe_files.extend((verify_file(probe_root, entry), pathlib.Path(entry["path"])) for entry in parent["artifacts"])
    for source, relative in probe_files:
        destination = output / "probe-build" / relative
        destination.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, destination)
        if fingerprint(destination) != fingerprint(source):
            raise ValueError("POST-ATTEMPT-PROBE-COPY")
    (output / "inputs.json").write_text(json.dumps(archived, indent=2), encoding="utf-8")
    (output / "tests.json").write_text(json.dumps(inventory, indent=2), encoding="utf-8")
    (output / "filtered-tests.json").write_text(json.dumps(filtered, indent=2), encoding="utf-8")
    preflight = dict(contract="FinitePostSuppressionNativeAttempt/1", complete=False, scope="private finite Compute probe12 only",
        cases=len(expected), probeBuildReplay=probe, inputs=archived, selectedTests=selected,
        perTestOutputBytes=args.test_output_bytes,
        ordinaryFrozenInputsReference=dict(path=str(args.ordinary_frozen_inputs.resolve()), **fingerprint(args.ordinary_frozen_inputs)),
        affectedRegressionsValidated=False, aggregatePrerequisitesValidated=False, **CLOSED_GATES)
    (output / "preflight.json").write_text(json.dumps(preflight, indent=2), encoding="utf-8")
    command = ["ctest", "--test-dir", str(build), "--parallel", "1", "-R", expression,
               "--no-tests=error",
               "--test-output-size-passed", str(args.test_output_bytes),
               "--test-output-size-failed", str(args.test_output_bytes),
               "--output-on-failure", "--output-junit", str(output / "ctest.xml")]
    (output / "invocation.json").write_text(json.dumps(dict(command=command), indent=2), encoding="utf-8")
    print("Executing exact registered native probe cases:", len(expected), flush=True)
    with (output / "ctest.log").open("w", encoding="utf-8") as log:
        code = subprocess.call(command, stdout=log, stderr=subprocess.STDOUT)
    terminal = dict(complete=False, exitCode=code, selectedNativeReportsChecked=False, **CLOSED_GATES)
    (output / "attempt.json").write_text(json.dumps(terminal, indent=2), encoding="utf-8")
    if (output / "ctest.xml").exists():
        suite = xml.parse(output / "ctest.xml").getroot()
        case_stdout = {case.get("name"): case.findtext("system-out") for case in suite.findall("testcase")}
    else:
        case_stdout = {}
    capture_case_outputs(output, selected, case_stdout)
    if any(fingerprint(source) != stamp for source, stamp in frozen.items()):
        raise ValueError("POST-ATTEMPT-FROZEN-INPUT-CHANGED")
    if code:
        raise ValueError("POST-ATTEMPT-CTEST-FAILED")
    stdout = inspect_junit(output / "ctest.xml", expected)
    results = []
    for name, identity in expected.items():
        command = selected[name]["command"]
        destination = output / "cases" / name
        report = read_json(destination / "report.json")
        if (not isinstance(stdout[name], str) or len(stdout[name].encode("utf-8")) > args.test_output_bytes
                or json.loads(stdout[name]) != report):
            raise ValueError("POST-ATTEMPT-JUNIT-REPORT")
        inspect_finite_report(report, identity)
        (destination / "driver.log").write_text(stdout[name], encoding="utf-8")
        results.append(dict(name=name, identity=list(identity), command=command, exitCode=0, accepted=True,
            report=dict(path=str((destination / "report.json").relative_to(output)), **fingerprint(destination / "report.json"))))
    if any(fingerprint(source) != stamp for source, stamp in frozen.items()):
        raise ValueError("POST-ATTEMPT-FROZEN-INPUT-CHANGED")
    result = dict(preflight, complete=True, results=results, exitCode=0, selectedNativeReportsChecked=True)
    (output / "validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("Native probe reports checked:", len(results), "; affected/aggregate/public/Graphics/WOCC gates closed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--probe-archive", type=pathlib.Path, required=True)
    parser.add_argument("--build-log-source", type=pathlib.Path, required=True)
    parser.add_argument("--ordinary-frozen-inputs", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--test-output-bytes", type=int, required=True)
    parser.add_argument("--ninja", type=pathlib.Path, default=pathlib.Path("C:/mingw64/bin/ninja.exe"))
    parser.add_argument("--nm", type=pathlib.Path, default=pathlib.Path("C:/emsdk/upstream/bin/llvm-nm.exe"))
    args = parser.parse_args()
    try:
        run(args)
    except (ValueError, OSError, KeyError, TypeError, IndexError, subprocess.CalledProcessError, xml.ParseError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
