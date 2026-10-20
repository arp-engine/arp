"""Independently replay the dedicated twelve-case native probe attempt.

Copied build recipes, objects, execution logs, shader publications, CTest/JUnit
and native report markers must agree. No live original file fills a missing
archive member. This private prerequisite cannot authorize public execution.
"""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, ROOT, read_json, verify_file, verify_selector
from bindless_finite_lifecycle_evidence import inspect_finite_report, require_fields
from bindless_finite_lifecycle_predecessor_evidence import bind_packages
from bindless_webgpu_post_suppression_attempt import inspect_junit, selection_expression
from bindless_webgpu_probe_archive_audit import artifact_inventory, cache_directory, configuration, replay as replay_probe
from bindless_webgpu_probe_build_evidence import TARGET, ORDINARY, MAIN, PROBE

CONTRACT = "FinitePostSuppressionStageAudit/1"
SCOPE = "private finite Compute PostSuppression12 prerequisite only"
TOOLS = ("bindless_webgpu_post_suppression_archive_audit.py",
         "bindless_webgpu_post_suppression_archive_audit_test.py")
PROBE_DERIVED_FIELDS = frozenset(("logicalSourceRoot", "latestExecutionRecords", "dependencyMtimes"))


def compare_probe_capture(captured, current, by_source, probe_root, build_log_source, ninja, nm, source_root):
    """Bind the historical schema to its frozen reader; keep current checks."""
    if not isinstance(captured, dict):
        raise ValueError("POST-AUDIT-PROBE-CAPTURE")
    missing = set(current) - set(captured)
    # Legacy replay supplements current checks; it cannot override any common
    # payload, archive, closed-gate or dependency observation.
    for key in set(current) & set(captured):
        if key == "sourceArchive":
            require_fields(captured.get(key, {}), {name: current[key][name] for name in ("bytes", "sha256")})
        else:
            require_fields(captured, {key: current[key]})
    expected = current
    if missing:
        if missing != PROBE_DERIVED_FIELDS or set(captured) != set(current) - PROBE_DERIVED_FIELDS:
            raise ValueError("POST-AUDIT-PROBE-READER-SCHEMA")
        if pathlib.Path(current["logicalSourceRoot"]).resolve() != source_root:
            raise ValueError("POST-AUDIT-PROBE-READER-SOURCE-OWNER")
        namespaces = []
        for filename in ("bindless_webgpu_probe_build_evidence.py", "bindless_webgpu_probe_archive_audit.py"):
            source = source_root / "tests/contract" / filename
            archived = by_source.get(source)
            if archived is None:
                raise ValueError("POST-AUDIT-FROZEN-PROBE-READER-MISSING")
            # The caller has already verified each attempt-input size/hash.
            # Use an isolated globals dictionary, as the reference-stage reader
            # does for explicit ROOT relocation; do not modify sys.modules.
            namespace = dict(__name__="_arp_frozen_probe_reader", __file__=str(archived))
            exec(compile(archived.read_bytes(), str(archived), "exec"), namespace)
            namespace["ROOT"] = source_root
            namespaces.append(namespace)
        build_reader, archive_reader = namespaces
        for key in ("MACRO", "TARGET", "ORDINARY", "MAIN", "PROBE"):
            if build_reader.get(key) != archive_reader.get(key):
                raise ValueError("POST-AUDIT-FROZEN-PROBE-READER-CONTRACT")
        for name in ("inspect_compile_command", "inspect_query", "inspect_probe_symbols", "inspect_build_log"):
            archive_reader[name] = build_reader[name]
        expected = archive_reader["replay"](probe_root, build_log_source, ninja, nm)
        if set(expected) != set(captured):
            raise ValueError("POST-AUDIT-FROZEN-PROBE-READER-SCHEMA")
    for key, value in expected.items():
        if key == "sourceArchive":
            require_fields(captured.get(key, {}), {name: value[name] for name in ("bytes", "sha256")})
        else:
            require_fields(captured, {key: value})


def cases():
    return {name: identity for name, identity in expected_cases("compute-final").items()
            if identity[0] == "post_suppression"}


def unique_inventory(inventory):
    tests = inventory.get("tests")
    if not isinstance(tests, list) or any(not isinstance(test, dict) for test in tests):
        raise ValueError("POST-AUDIT-INVENTORY")
    result = {test.get("name"): test for test in tests}
    if len(result) != len(tests) or any(not isinstance(name, str) for name in result):
        raise ValueError("POST-AUDIT-INVENTORY")
    return result


def inspect_attempt(root, build_log_source, ninja, nm, verify_live=False):
    root = root.resolve(); expected = cases()
    manifest = read_json(root / "validation.json")
    require_fields(manifest, dict(contract="FinitePostSuppressionNativeAttempt/1", complete=True,
        scope="private finite Compute probe12 only", cases=len(expected), exitCode=0,
        selectedNativeReportsChecked=True, affectedRegressionsValidated=False,
        aggregatePrerequisitesValidated=False, **CLOSED_GATES))
    preflight = read_json(root / "preflight.json")
    require_fields(preflight, dict(complete=False))
    for key, value in preflight.items():
        if key != "complete": require_fields(manifest, {key: value})
    require_fields(read_json(root / "attempt.json"), dict(complete=False, exitCode=0,
        selectedNativeReportsChecked=False, **CLOSED_GATES))
    inputs = read_json(root / "inputs.json")
    if not isinstance(inputs, list) or not inputs or inputs != manifest.get("inputs"):
        raise ValueError("POST-AUDIT-INPUTS")
    by_source, paths = {}, set()
    for entry in inputs:
        if not isinstance(entry.get("source"), str): raise ValueError("POST-AUDIT-INPUT-SOURCE")
        source = pathlib.Path(entry["source"]).resolve(); path = verify_file(root, entry)
        if source in by_source or path in paths: raise ValueError("POST-AUDIT-DUPLICATE-INPUT")
        by_source[source] = path; paths.add(path)
        if verify_live: require_fields(entry, fingerprint(source))
    caches = [source for source in by_source if source.name == "CMakeCache.txt"]
    if len(caches) != 1: raise ValueError("POST-AUDIT-CACHE")
    cache_text = by_source[caches[0]].read_text(encoding="utf-8")
    build = configuration(cache_text)
    source_root = cache_directory(cache_text, "CMAKE_HOME_DIRECTORY", "POST-AUDIT-SOURCE-OWNER")
    if caches[0] != build / "CMakeCache.txt": raise ValueError("POST-AUDIT-CACHE-OWNER")
    required = {source_root / name for name in ("CMakeLists.txt", ORDINARY, MAIN, PROBE,
        "tests/fixtures/webgpu_post_suppression_failure_probe_fixture.h",
        "tests/contract/bindless_webgpu_rhi_smoke.py",
        "tests/contract/bindless_webgpu_post_suppression_attempt.py",
        "tests/contract/bindless_webgpu_post_suppression_attempt_test.py",
        "scripts/browser_contract_runner.py")}
    if not required <= set(by_source): raise ValueError("POST-AUDIT-REQUIRED-SOURCES")
    payload_sources = {build / (TARGET + suffix) for suffix in (".js", ".wasm", ".data")}
    if not (payload_sources | {build / name for name in (
            "bindless_webgpu_rhi_runner.html", "browser_progress_transport.js")}) <= set(by_source):
        raise ValueError("POST-AUDIT-REQUIRED-PAYLOAD")
    probe_root = root / "probe-build"
    probe_manifest = read_json(probe_root / "validation.json")
    probe_sources = artifact_inventory(probe_root, probe_manifest)
    probe = replay_probe(probe_root, build_log_source, ninja, nm)
    require_fields(probe, dict(logicalSourceRoot=str(source_root)))
    captured_probe = manifest.get("probeBuildReplay", {})
    compare_probe_capture(captured_probe, probe, by_source, probe_root, build_log_source, ninja, nm, source_root)
    # The actually built source/cache/payload must be the same frozen inputs
    # used by these reports, not a separately rehashed probe-build summary.
    for source in required & set(probe_sources) | payload_sources | {caches[0]}:
        if source not in probe_sources or fingerprint(by_source[source]) != fingerprint(probe_sources[source]):
            raise ValueError("POST-AUDIT-BUILD-INPUT-CORRESPONDENCE")
    for source in payload_sources:
        if fingerprint(by_source[source]) != probe["payload"][source.name]:
            raise ValueError("POST-AUDIT-PAYLOAD")
    paths.add(probe_root / "validation.json"); paths.update(probe_sources.values())
    inventory = read_json(root / "tests.json")
    registered = unique_inventory(inventory)
    selected = {name: registered[name] for name in expected if name in registered}
    if set(selected) != set(expected) or selected != manifest.get("selectedTests"):
        raise ValueError("POST-AUDIT-REGISTERED-SELECTION")
    filtered = unique_inventory(read_json(root / "filtered-tests.json"))
    execution = lambda tests: {name: {key: value for key, value in test.items() if key != "backtrace"}
                               for name, test in tests.items()}
    if execution(filtered) != execution(selected): raise ValueError("POST-AUDIT-FILTERED-SELECTION")
    output_roots = set()
    for name, test in selected.items():
        command = test.get("command")
        if not isinstance(command, list): raise ValueError("POST-AUDIT-REGISTERED-COMMAND")
        verify_selector(command, expected[name])
        if command.count("--runner") != 1 or command.count("--output") != 1:
            raise ValueError("POST-AUDIT-REGISTERED-COMMAND")
        output = pathlib.Path(command[command.index("--output") + 1]).resolve()
        if (pathlib.Path(command[command.index("--runner") + 1]).resolve() != build
                or output == build or not output.is_relative_to(build) or output in output_roots):
            raise ValueError("POST-AUDIT-REGISTERED-OWNER")
        output_roots.add(output)
    budget = manifest.get("perTestOutputBytes")
    if type(budget) is not int or budget <= 0: raise ValueError("POST-AUDIT-LOG-BUDGET")
    invocation = read_json(root / "invocation.json").get("command")
    if not isinstance(invocation, list) or not invocation: raise ValueError("POST-AUDIT-INVOCATION")
    junit_source = pathlib.Path(invocation[-1])
    if not junit_source.is_absolute() or junit_source.name != "ctest.xml":
        raise ValueError("POST-AUDIT-JUNIT-OWNER")
    require_fields(read_json(root / "invocation.json"), dict(command=["ctest", "--test-dir", str(build),
        "--parallel", "1", "-R", selection_expression(expected), "--no-tests=error",
        "--test-output-size-passed", str(budget), "--test-output-size-failed", str(budget),
        "--output-on-failure", "--output-junit", str(junit_source)]))
    stdout = inspect_junit(root / "ctest.xml", expected)
    log = (root / "ctest.log").read_text(encoding="utf-8")
    completed = re.findall(r"^\s*(\d+)/12 Test #\d+: (\S+)\s+\.+\s+Passed\s+[\d.]+ sec\s*$", log, re.MULTILINE)
    if (len(completed) != len(expected) or {name for unused, name in completed} != set(expected)
            or {int(index) for index, unused in completed} != set(range(1, len(expected) + 1))
            or "100% tests passed, 0 tests failed out of 12" not in log):
        raise ValueError("POST-AUDIT-CTEST-TERMINAL-LOG")
    records = manifest.get("results")
    if not isinstance(records, list) or len(records) != len(expected): raise ValueError("POST-AUDIT-RESULTS")
    profiles = {}
    for record in records:
        name = record.get("name")
        if name not in expected or name in profiles: raise ValueError("POST-AUDIT-CASE")
        require_fields(record, dict(identity=list(expected[name]), command=selected[name]["command"], exitCode=0, accepted=True))
        report_path = verify_file(root, record.get("report", {}))
        if report_path != (root / "cases" / name / "report.json").resolve(): raise ValueError("POST-AUDIT-REPORT-OWNER")
        report = read_json(report_path); driver = report_path.with_name("driver.log")
        if (not isinstance(stdout[name], str) or len(stdout[name].encode("utf-8")) > budget
                or driver.read_text(encoding="utf-8") != stdout[name] or read_json(driver) != report):
            raise ValueError("POST-AUDIT-DRIVER-JUNIT-REPORT")
        profiles[name] = inspect_finite_report(report, expected[name])
        paths.update((report_path, driver, report_path.with_name("browser.log")))
    packages = bind_packages(root, manifest)
    if packages is None: raise ValueError("POST-AUDIT-SHADER-PACKAGES")
    capture_path = root / "case-files.json"
    if capture_path.exists():
        capture = read_json(capture_path)
        require_fields(capture, dict(contract="FinitePostSuppressionCaseCapture/1", complete=False,
            scope="captured registered CTest outputs only; no native acceptance", cases=len(expected), **CLOSED_GATES))
        captured = capture.get("captured")
        if not isinstance(captured, list) or len(captured) != len(expected):
            raise ValueError("POST-AUDIT-CASE-CAPTURE")
        captured_names = set()
        for record in captured:
            name = record.get("name")
            if name not in expected or name in captured_names: raise ValueError("POST-AUDIT-CASE-CAPTURE")
            captured_names.add(name)
            require_fields(record, dict(command=selected[name]["command"], accepted=False))
            files = record.get("files")
            if not isinstance(files, list) or len(files) != 3: raise ValueError("POST-AUDIT-CASE-CAPTURE")
            command = selected[name]["command"]
            source = pathlib.Path(command[command.index("--output") + 1])
            expected_paths = {root / "cases" / name / filename for filename in ("report.json", "browser.log", "junit-output.log")}
            captured_paths = set()
            for entry in files:
                path = verify_file(root, entry)
                if path not in expected_paths or path in captured_paths: raise ValueError("POST-AUDIT-CASE-CAPTURE")
                captured_paths.add(path)
                origin = "ctest.xml:system-out:" + name if path.name == "junit-output.log" else str(source / path.name)
                require_fields(entry, dict(source=origin))
                if path.name == "junit-output.log" and path.read_text(encoding="utf-8") != stdout[name]:
                    raise ValueError("POST-AUDIT-CASE-CAPTURE")
            paths.update(captured_paths)
        paths.add(capture_path)
    paths.update(root / name for name in ("validation.json", "preflight.json", "attempt.json", "inputs.json",
        "tests.json", "filtered-tests.json", "invocation.json", "ctest.xml", "ctest.log"))
    return dict(cases=len(expected), semanticProfiles=profiles, packageIdentities=packages,
        sourceAcceptance=dict(path=str(root / "validation.json"), **fingerprint(root / "validation.json")),
        probeBuildReplay=probe, files={path: fingerprint(path) for path in paths})


def replay(root, build_log_source, ninja, nm):
    root = root.resolve(); manifest_path = root / "validation.json"; stamp = fingerprint(manifest_path)
    manifest = read_json(manifest_path)
    require_fields(manifest, dict(contract=CONTRACT, complete=True, scope=SCOPE, cases=12,
        shaderPackagesBound=True, actualProbeBuildEvidenceValidated=True,
        postSuppressionRuntimeValidated=True, affectedRegressionsValidated=False,
        aggregatePrerequisitesValidated=False, transitiveSdkProvenanceComplete=False, **CLOSED_GATES))
    require_fields(manifest, dict(buildLogSource=str(build_log_source.resolve())))
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts: raise ValueError("POST-AUDIT-ARTIFACTS")
    require_fields(manifest, dict(artifactCount=len(artifacts)))
    sources, paths = set(), set()
    for entry in artifacts:
        if not isinstance(entry.get("source"), str): raise ValueError("POST-AUDIT-ARTIFACT-SOURCE")
        source = pathlib.Path(entry["source"]).resolve(); path = verify_file(root, entry)
        if source in sources or path in paths: raise ValueError("POST-AUDIT-DUPLICATE-ARTIFACT")
        sources.add(source); paths.add(path)
    inspected = inspect_attempt(root / "snapshot", build_log_source, ninja, nm)
    tools = {root / "tools" / name for name in TOOLS}
    if set(inspected["files"]) | tools != paths: raise ValueError("POST-AUDIT-MISSING-SNAPSHOT-ARTIFACT")
    require_fields(manifest, {key: inspected[key] for key in ("cases", "semanticProfiles", "packageIdentities")})
    require_fields(manifest.get("sourceAcceptance", {}), {key: inspected["sourceAcceptance"][key] for key in ("bytes", "sha256")})
    if fingerprint(manifest_path) != stamp or any(
            fingerprint(verify_file(root, entry)) != dict(bytes=entry["bytes"], sha256=entry["sha256"])
            for entry in artifacts): raise ValueError("POST-AUDIT-CHANGED-DURING-REPLAY")
    return dict(contract=CONTRACT, complete=True, cases=12, selectedArtifactsVerified=len(artifacts),
        semanticProfiles=inspected["semanticProfiles"], packageIdentities=inspected["packageIdentities"],
        shaderPackagesBound=True, actualProbeBuildEvidenceValidated=True, postSuppressionRuntimeValidated=True,
        affectedRegressionsValidated=False, aggregatePrerequisitesValidated=False, **CLOSED_GATES)


def freeze(snapshot, output, build_log_source, ninja, nm, verify_live=False):
    snapshot = snapshot.resolve(); output = output.resolve()
    if snapshot == output or output.is_relative_to(snapshot) or snapshot.is_relative_to(output):
        raise ValueError("POST-AUDIT-OUTPUT-OVERLAP")
    inspected = inspect_attempt(snapshot, build_log_source, ninja, nm, verify_live)
    output.mkdir(parents=True, exist_ok=False)
    (output / "attempt.json").write_text(json.dumps(dict(complete=False, **CLOSED_GATES), indent=2), encoding="utf-8")
    artifacts = []
    files = dict(inspected["files"])
    for name in TOOLS:
        source = pathlib.Path(__file__).with_name(name).resolve(); files[source] = fingerprint(source)
    for source, expected in sorted(files.items()):
        destination = (output / "snapshot" / source.relative_to(snapshot) if source.is_relative_to(snapshot)
                       else output / "tools" / source.name)
        destination.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, destination)
        if fingerprint(destination) != expected: raise ValueError("POST-AUDIT-COPY-CHANGED")
        artifacts.append(dict(source=str(source), path=str(destination.relative_to(output)), **expected))
    if any(fingerprint(source) != expected for source, expected in files.items()):
        raise ValueError("POST-AUDIT-SOURCE-CHANGED")
    manifest = dict(contract=CONTRACT, complete=True, scope=SCOPE,
        **{key: inspected[key] for key in ("cases", "semanticProfiles", "packageIdentities", "sourceAcceptance")},
        artifacts=artifacts, artifactCount=len(artifacts), shaderPackagesBound=True,
        actualProbeBuildEvidenceValidated=True, postSuppressionRuntimeValidated=True,
        buildLogSource=str(build_log_source.resolve()),
        selectedLiveInputsVerified=verify_live, affectedRegressionsValidated=False,
        aggregatePrerequisitesValidated=False, transitiveSdkProvenanceComplete=False, **CLOSED_GATES)
    path = output / "validation.json"; path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    try:
        return replay(output, build_log_source, ninja, nm)
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError):
        path.rename(output / "rejected.json")
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("freeze", "replay"))
    parser.add_argument("--snapshot", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--verify-live", action="store_true")
    parser.add_argument("--build-log-source", type=pathlib.Path, required=True)
    parser.add_argument("--ninja", type=pathlib.Path, default=pathlib.Path("C:/mingw64/bin/ninja.exe"))
    parser.add_argument("--nm", type=pathlib.Path, default=pathlib.Path("C:/emsdk/upstream/bin/llvm-nm.exe"))
    args = parser.parse_args()
    try:
        if args.action == "freeze" and args.output is None: raise ValueError("POST-AUDIT-OUTPUT-REQUIRED")
        result = (freeze(args.snapshot, args.output, args.build_log_source, args.ninja, args.nm, args.verify_live)
                  if args.action == "freeze" else replay(args.snapshot, args.build_log_source, args.ninja, args.nm))
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
