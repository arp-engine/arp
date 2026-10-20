"""Run and archive the exact finite WebGPU lifecycle matrix on one frozen payload.

This is conformance tooling. An accepted stage never enables public execution,
NativeSync, Graphics, WOCC or platform/production capabilities.
"""
import argparse
import hashlib
import itertools
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TYPES = ("uniform", "readonly-storage")
RANGES = ("static", "dynamic", "whole")
STAGES = ("claim", "compute-default", "compute-final")
TERMINALS = {
    "recording_terminal": ("cancel", "drop", "native-error", "reverse-scopes"),
    "submission_terminal": ("cancel", "retired", "shutdown", "throw-before", "throw-after",
                            "native-error", "reverse-scopes", "partial-scope"),
    "physical_loss": ("queue-uncertain", "recording-shutdown", "partial-push", "recording-exception"),
    "map_terminal": ("cancel", "native-error", "read-error", "scope-native-error", "throw-before",
                     "throw-after", "scope-registration", "unmap-throw", "pending-loss",
                     "reverse-scopes", "published-loss"),
    "claim_terminal": ("cancel", "drop", "retired", "shutdown", "physical-loss"),
    "post_suppression": ("commit", "publication"),
}


def expected_cases(stage):
    if stage not in STAGES:
        raise ValueError("FINITE-EVIDENCE-STAGE")
    groups = ("claim_terminal",) if stage == "claim" else tuple(TERMINALS)
    if stage == "compute-default":
        groups = tuple(group for group in groups if group != "post_suppression")
    cases = {}
    for group in groups:
        for buffer_type, interval, terminal in itertools.product(TYPES, RANGES, TERMINALS[group]):
            name = f"arp_rhi_webgpu_general_finite_{group}_{buffer_type}_{interval}_{terminal}_browser_smoke"
            cases[name] = (group, buffer_type, interval, terminal)
    if stage != "claim":
        for buffer_type, interval in itertools.product(TYPES, RANGES):
            name = f"arp_rhi_webgpu_general_finite_execution_{buffer_type}_{interval}_browser_smoke"
            cases[name] = ("execution", buffer_type, interval, None)
    return cases


def fingerprint(path):
    data = pathlib.Path(path).read_bytes()
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def inspect_report(report, identity):
    from bindless_finite_lifecycle_evidence import inspect_finite_report
    return inspect_finite_report(report, identity)


def run(args):
    output = args.output.resolve()
    # A failed attempt is evidence too; never overwrite or merge it with a rerun.
    output.mkdir(parents=True, exist_ok=False)
    tests = json.loads(subprocess.check_output(
        ["ctest", "--test-dir", str(args.build), "--show-only=json-v1"], encoding="utf-8-sig"))
    inventory = {test["name"]: test for test in tests["tests"]}
    if len(inventory) != len(tests["tests"]):
        raise ValueError("FINITE-EVIDENCE-DUPLICATE-REGISTERED-CASE")
    cases = expected_cases(args.stage)
    missing = sorted(set(cases) - set(inventory))
    if missing:
        raise ValueError("FINITE-EVIDENCE-MISSING-REGISTERED-CASES: " + ", ".join(missing))
    (output / "tests.json").write_text(json.dumps(tests, indent=2), encoding="utf-8")
    prefix = "arp_rhi_webgpu_bindless_pipeline_browser_smoke"
    paths = [args.build / (prefix + suffix) for suffix in (".wasm", ".js", ".data")]
    paths += [args.build / name for name in ("bindless_webgpu_rhi_runner.html", "browser_progress_transport.js")]
    tracked = subprocess.check_output(["git", "ls-files", "src/rhi", "src/core", "src/shader", "scripts",
                                       "tests/fixtures", "tests/integration", "tests/contract", "CMakeLists.txt",
                                       "CMakePresets.json", ".gitattributes"], cwd=ROOT, text=True).splitlines()
    paths += [ROOT / name for name in tracked if (ROOT / name).is_file()]
    # These files are read outside the .data archive by the actual browser driver.
    # Freeze package publications and their payloads, configured probe policy and
    # all explicitly used untracked successor evidence modules as well.
    paths += [path for path in (args.build / "bindless-rhi-fixture").rglob("*") if path.is_file()]
    paths += [args.build / "CMakeCache.txt"]
    paths += [ROOT / "tests/contract" / name for name in (
        "bindless_finite_lifecycle_evidence.py", "bindless_post_suppression_evidence.py",
        "bindless_finite_lifecycle_archive_audit.py", "bindless_finite_lifecycle_evidence_test.py",
        "bindless_webgpu_lifecycle_driver_test.py", "bindless_finite_lifecycle_acceptance_test.py",
        "bindless_post_suppression_evidence_test.py")]
    paths += [pathlib.Path(__file__).resolve()]
    paths = sorted(set(path.resolve() for path in paths))
    frozen = {str(path): fingerprint(path) for path in paths}
    archived = []
    for index, path in enumerate(paths):
        target = output / "inputs" / str(index) / path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        archived.append(dict(source=str(path), path=str(target.relative_to(output)), **frozen[str(path)]))
    (output / "inputs.json").write_text(json.dumps(archived, indent=2), encoding="utf-8")
    results = []
    for name, identity in cases.items():
        command = list(inventory[name]["command"])
        case_output = output / "cases" / name
        case_output.mkdir(parents=True)
        position = command.index("--output")
        command[position + 1] = str(case_output)
        print(f"[{len(results) + 1}/{len(cases)}] {name}", flush=True)
        with (case_output / "driver.log").open("w", encoding="utf-8") as log:
            code = subprocess.call(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        record = dict(name=name, identity=identity, command=command, exitCode=code, accepted=False)
        results.append(record)
        try:
            if code:
                raise ValueError("FINITE-EVIDENCE-DRIVER-FAILED")
            report_path = case_output / "report.json"
            inspect_report(json.loads(report_path.read_text(encoding="utf-8")), identity)
            record.update(accepted=True, report=dict(path=str(report_path.relative_to(output)), **fingerprint(report_path)))
        finally:
            (output / "attempt.json").write_text(json.dumps(dict(stage=args.stage, expected=len(cases),
                results=results, complete=False, publicBindlessExecutionAvailable=False), indent=2), encoding="utf-8")
    if any(fingerprint(path) != frozen[str(path)] for path in paths):
        raise ValueError("FINITE-EVIDENCE-FROZEN-INPUT-CHANGED")
    summary = dict(contract="FiniteLifecycleAcceptance/1", stage=args.stage, scope="private finite Compute only",
                   complete=True, cases=len(cases), results=results, inputs=archived,
                   transitiveSdkProvenanceComplete=False, publicBindlessExecutionAvailable=False,
                   nativeSyncAvailable=False, graphicsValidated=False, woccValidated=False,
                   finiteMatrixValidated=args.stage == "compute-final", finiteLifecycle4Validated=False,
                   fullFiniteExecutionValidated=False,
                   platformValidated=False, productionReady=False)
    (output / "validation.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"Accepted {len(cases)} exact cases; public/Graphics/WOCC/platform/production gates remain closed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--stage", choices=STAGES, required=True)
    args = parser.parse_args()
    args.build = args.build.resolve()
    try:
        run(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
