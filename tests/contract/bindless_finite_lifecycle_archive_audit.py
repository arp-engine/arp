"""Audit a complete immutable finite matrix and copy its selected evidence.

This stage audit grants neither aggregate Lifecycle4 nor public execution.
The independent predecessor and final-payload aggregate remains a separate gate.
"""
import argparse
import json
import pathlib
import shutil
import sys

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_evidence import inspect_finite_report, require_fields

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLOSED_GATES = dict(publicBindlessExecutionAvailable=False, nativeSyncAvailable=False,
                    finiteLifecycle4Validated=False, fullFiniteExecutionValidated=False,
                    graphicsValidated=False, woccValidated=False, platformValidated=False, productionReady=False)


def read_json(path):
    # A duplicate key cannot silently replace evidence inside an authored manifest.
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("FINITE-ARCHIVE-DUPLICATE-KEY: " + key)
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique)


def archived_path(root, relative):
    if not isinstance(relative, str) or not relative or pathlib.Path(relative).is_absolute():
        raise ValueError("FINITE-ARCHIVE-PATH")
    path = (root / relative.replace("\\", "/")).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError("FINITE-ARCHIVE-PATH")
    return path


def verify_file(root, entry):
    if not isinstance(entry, dict):
        raise ValueError("FINITE-ARCHIVE-ENTRY")
    path = archived_path(root, entry.get("path"))
    require_fields(entry, fingerprint(path))
    return path


def verify_selector(command, identity):
    group, buffer_type, interval, terminal = identity
    selectors = dict(recording_terminal="--general-recording-terminal",
                     submission_terminal="--general-submission-terminal", physical_loss="--general-physical-loss",
                     map_terminal="--general-map-terminal", claim_terminal="--general-claim-terminal",
                     post_suppression="--general-post-suppression-failure", execution="--general-finite-execution")
    expected = selectors[group]
    if (command.count("--general-resource-projection-scenario") != 1
            or command.count(expected) != 1
            or sum(command.count(selector) for selector in selectors.values()) != 1
            or command.count("--general-finite-lifecycle") != (0 if group == "execution" else 1)
            or command.count("--general-content-plan-dynamic") != (1 if interval == "dynamic" else 0)
            or command.count("--general-source-whole") != (1 if interval == "whole" else 0)
            or command.count("--general-content-buffer-type") != 1):
        raise ValueError("FINITE-ARCHIVE-SELECTOR")
    try:
        if (command[command.index("--general-content-buffer-type") + 1] != buffer_type
                or group != "execution" and command[command.index(expected) + 1] != terminal):
            raise ValueError("FINITE-ARCHIVE-SELECTOR")
    except IndexError as error:
        raise ValueError("FINITE-ARCHIVE-SELECTOR") from error


def inspect_snapshot(root, verify_live=False):
    root = root.resolve()
    manifest = read_json(root / "validation.json")
    require_fields(manifest, dict(contract="FiniteLifecycleAcceptance/1", complete=True,
                                 scope="private finite Compute only", transitiveSdkProvenanceComplete=False,
                                 **CLOSED_GATES))
    stage = manifest.get("stage")
    if stage not in ("claim", "compute-default", "compute-final"):
        raise ValueError("FINITE-ARCHIVE-STAGE")
    cases = expected_cases(stage)
    require_fields(manifest, dict(cases=len(cases), finiteMatrixValidated=stage == "compute-final"))
    inputs = read_json(root / "inputs.json")
    if not isinstance(inputs, list) or inputs != manifest.get("inputs") or not inputs:
        raise ValueError("FINITE-ARCHIVE-INPUTS")
    paths = [root / name for name in ("validation.json", "inputs.json", "tests.json", "attempt.json")]
    sources = set()
    archived = set()
    for entry in inputs:
        if not isinstance(entry, dict) or not isinstance(entry.get("source"), str):
            raise ValueError("FINITE-ARCHIVE-INPUTS")
        source = pathlib.Path(entry["source"]).resolve()
        path = verify_file(root, entry)
        if source in sources or path in archived:
            raise ValueError("FINITE-ARCHIVE-DUPLICATE-INPUT")
        sources.add(source)
        archived.add(path)
        paths.append(path)
        if verify_live:
            require_fields(entry, fingerprint(source))
    required = {ROOT / "CMakeLists.txt", ROOT / "tests/contract/bindless_webgpu_rhi_smoke.py",
                ROOT / "tests/contract/bindless_finite_lifecycle_acceptance.py",
                ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html",
                ROOT / "tests/fixtures/original_general_finite_claim_terminal_fixture.h",
                ROOT / "scripts/browser_contract_runner.py"}
    if not required <= sources:
        raise ValueError("FINITE-ARCHIVE-REQUIRED-SOURCES")
    prefix = "arp_rhi_webgpu_bindless_pipeline_browser_smoke"
    artifacts = {path.name for path in sources}
    if not {prefix + suffix for suffix in (".data", ".js", ".wasm")} <= artifacts or not {
            "bindless_webgpu_rhi_runner.html", "browser_progress_transport.js"} <= artifacts:
        raise ValueError("FINITE-ARCHIVE-REQUIRED-PAYLOAD")
    shader_metadata_frozen = {
        "WebGpuPublicCompute.emulated.bindless.yaml", "WebGpuPublicGraphics.emulated.bindless.yaml",
        "ArpZeroBaseIndirectProducer.emulated.bindless.yaml", "BindlessIndirectConsumer.emulated.bindless.yaml"
    } <= artifacts
    if stage != "claim" and not shader_metadata_frozen:
        raise ValueError("FINITE-ARCHIVE-SHADER-METADATA")
    if stage == "compute-final":
        caches = [path for path in archived if path.name == "CMakeCache.txt"]
        if len(caches) != 1:
            raise ValueError("FINITE-ARCHIVE-PROBE-CONFIGURATION")
        cache = caches[0].read_text(encoding="utf-8")
        if ("ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES:BOOL=ON" not in cache.splitlines()
                or "BUILD_SHARED_LIBS:BOOL=OFF" not in cache.splitlines()):
            raise ValueError("FINITE-ARCHIVE-PROBE-CONFIGURATION")
    inventory = read_json(root / "tests.json").get("tests")
    if not isinstance(inventory, list):
        raise ValueError("FINITE-ARCHIVE-INVENTORY")
    registered = {}
    for test in inventory:
        if not isinstance(test, dict) or test.get("name") in registered:
            raise ValueError("FINITE-ARCHIVE-INVENTORY")
        registered[test.get("name")] = test
    if not set(cases) <= set(registered):
        raise ValueError("FINITE-ARCHIVE-MISSING-REGISTERED-CASE")
    records = manifest.get("results")
    if not isinstance(records, list) or len(records) != len(cases):
        raise ValueError("FINITE-ARCHIVE-RESULTS")
    attempt = read_json(root / "attempt.json")
    require_fields(attempt, dict(stage=stage, expected=len(cases), publicBindlessExecutionAvailable=False))
    if attempt.get("results") != records:
        raise ValueError("FINITE-ARCHIVE-ATTEMPT-MISMATCH")
    profiles = {}
    for record in records:
        name = record.get("name")
        if name not in cases or name in profiles or record.get("identity") != list(cases[name]):
            raise ValueError("FINITE-ARCHIVE-CASE-IDENTITY")
        require_fields(record, dict(exitCode=0, accepted=True))
        path = verify_file(root, record.get("report", {}))
        if path != (root / "cases" / name / "report.json").resolve():
            raise ValueError("FINITE-ARCHIVE-REPORT-OWNER")
        command = record.get("command")
        expected_command = list(registered[name].get("command", []))
        if not isinstance(command, list) or expected_command.count("--output") != 1:
            raise ValueError("FINITE-ARCHIVE-COMMAND")
        position = expected_command.index("--output") + 1
        if position >= len(expected_command) or len(command) != len(expected_command):
            raise ValueError("FINITE-ARCHIVE-COMMAND")
        # Output roots may be relocated when copying a snapshot; all test inputs
        # and selector arguments must match the captured CTest command exactly.
        expected_command[position] = command[position]
        if command != expected_command or pathlib.Path(command[position]).name != name:
            raise ValueError("FINITE-ARCHIVE-COMMAND")
        verify_selector(command, cases[name])
        report = read_json(path)
        log = path.with_name("driver.log")
        if read_json(log) != report:
            raise ValueError("FINITE-ARCHIVE-DRIVER-REPORT")
        profiles[name] = inspect_finite_report(report, cases[name])
        paths.extend((path, log, path.with_name("browser.log")))
    if set(profiles) != set(cases):
        raise ValueError("FINITE-ARCHIVE-MISSING-CASE")
    # Snapshot payload/artifact hashes are scoped selected provenance. This is
    # intentionally not a claim about all SDK dependencies or native authenticity.
    files = {path: fingerprint(path) for path in paths}
    return dict(stage=stage, profiles=profiles, files=files,
                shaderMetadataFrozen=shader_metadata_frozen,
                publicBindlessExecutionAvailable=False, finiteLifecycle4Validated=False)


def audit(args):
    snapshot = inspect_snapshot(args.snapshot, args.verify_live)
    comparison = None
    if args.compare_snapshot:
        comparison = inspect_snapshot(args.compare_snapshot)
        shared = set(snapshot["profiles"]) & set(comparison["profiles"])
        if not shared or any(snapshot["profiles"][name] != comparison["profiles"][name] for name in shared):
            raise ValueError("FINITE-ARCHIVE-REGRESSION-PROFILE")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    # Preserve inputs and failed copy attempts separately from accepted output.
    (output / "attempt.json").write_text(json.dumps(dict(complete=False, **CLOSED_GATES), indent=2), encoding="utf-8")
    files = dict(snapshot["files"])
    if comparison:
        files.update(comparison["files"])
    for name in ("bindless_finite_lifecycle_archive_audit.py", "bindless_finite_lifecycle_evidence.py",
                 "bindless_post_suppression_evidence.py", "bindless_finite_lifecycle_acceptance.py"):
        path = pathlib.Path(__file__).with_name(name)
        files[path] = fingerprint(path)
    copies = []
    for index, (source, expected) in enumerate(sorted(files.items())):
        destination = output / "artifacts" / str(index) / source.name
        destination.parent.mkdir(parents=True)
        shutil.copyfile(source, destination)
        if fingerprint(destination) != expected:
            raise ValueError("FINITE-ARCHIVE-COPY-CHANGED")
        copies.append(dict(source=str(source), path=str(destination.relative_to(output)), **expected))
    if any(fingerprint(source) != expected for source, expected in files.items()):
        raise ValueError("FINITE-ARCHIVE-CHANGED-DURING-AUDIT")
    result = dict(contract="FiniteLifecycleStageAudit/1", complete=True, stage=snapshot["stage"],
                  cases=len(snapshot["profiles"]), artifactCount=len(copies),
                  artifacts=copies, semanticProfiles=snapshot["profiles"],
                  comparedCases=len(shared) if comparison else 0,
                  selectedLiveInputsVerified=args.verify_live, transitiveSdkProvenanceComplete=False,
                  shaderMetadataFrozen=snapshot["shaderMetadataFrozen"],
                  aggregatePrerequisitesValidated=False, **CLOSED_GATES)
    (output / "validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("Strict stage audit accepted", result["cases"], "cases and", len(copies),
          "artifacts; aggregate/public/Graphics/WOCC/platform/production gates remain closed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--compare-snapshot", type=pathlib.Path)
    parser.add_argument("--verify-live", action="store_true")
    args = parser.parse_args()
    try:
        audit(args)
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
