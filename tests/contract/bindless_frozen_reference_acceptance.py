"""Run an independent finite matrix using byte-exact materialized reference inputs.

Relocation changes only the registered driver, runner and output paths. Native
payloads, selectors, timeouts and failed attempts remain unchanged. A reference
attempt grants no public execution, SDK provenance or aggregate readiness.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import (
    CLOSED_GATES, read_json, verify_file, verify_selector)
from bindless_finite_lifecycle_evidence import require_fields


class FrozenReferenceFiniteAttempt:
    def __init__(self, reference, materialized, output, stage):
        self.reference = reference.resolve()
        self.materialized = materialized.resolve()
        self.output = output.resolve()
        self.stage = stage
        self.cases = expected_cases(stage)
        self.inputs = []

    @staticmethod
    def relocate(command, identity, source_root, materialized, available, output):
        if not isinstance(command, list) or not all(isinstance(i, str) for i in command):
            raise ValueError("FINITE-REFERENCE-COMMAND")
        verify_selector(command, identity)
        if (len(command) < 2 or command.count("--runner") != 1
                or command.count("--output") != 1
                or command.count("--timeout-seconds") != 1):
            raise ValueError("FINITE-REFERENCE-COMMAND")
        runner = command.index("--runner") + 1
        destination = command.index("--output") + 1
        deadline = command.index("--timeout-seconds") + 1
        if max(runner, destination, deadline) >= len(command):
            raise ValueError("FINITE-REFERENCE-COMMAND")
        if not command[deadline].isdecimal() or int(command[deadline]) <= 0:
            raise ValueError("FINITE-REFERENCE-DEADLINE")
        source_root = source_root.resolve()
        original_driver = source_root / "tests/contract/bindless_webgpu_rhi_smoke.py"
        original_build = source_root / "build/web-webgpu-release"
        if (pathlib.Path(command[1]).resolve() != original_driver
                or pathlib.Path(command[runner]).resolve() != original_build):
            raise ValueError("FINITE-REFERENCE-REGISTERED-INPUT")
        driver = materialized / original_driver.relative_to(source_root)
        build = materialized / original_build.relative_to(source_root)
        prefix = "arp_rhi_webgpu_bindless_pipeline_browser_smoke"
        required = {driver, build / "bindless_webgpu_rhi_runner.html",
                    build / "browser_progress_transport.js"}
        required.update(build / (prefix + suffix) for suffix in (".js", ".wasm", ".data"))
        if not required <= available:
            raise ValueError("FINITE-REFERENCE-MISSING-INPUT")
        # No general string substitution: only these three declared positions
        # change. Browser executable, every selector and deadline stay exact.
        relocated = list(command)
        relocated[1] = str(driver)
        relocated[runner] = str(build)
        relocated[destination] = str(output)
        if any(source_root.as_posix().lower() in value.replace("\\", "/").lower()
               for index, value in enumerate(relocated)
               if index not in (1, runner, destination)):
            raise ValueError("FINITE-REFERENCE-UNDECLARED-SOURCE-PATH")
        return relocated

    def prepare(self):
        materialization = read_json(self.materialized / "reference-manifest.json")
        require_fields(materialization, dict(contract="FrozenInputReferenceMaterialization/1",
            publicBindlessExecutionAvailable=False, ordinaryComputePredecessorEligible=False,
            platformValidated=False, productionReady=False))
        if pathlib.Path(materialization["reference"]).resolve() != self.reference:
            raise ValueError("FINITE-REFERENCE-PARENT")
        source_root = pathlib.Path(materialization["sourceRoot"]).resolve()
        if any(self.output.is_relative_to(root) for root in (
                self.reference, self.materialized, source_root)):
            raise ValueError("FINITE-REFERENCE-OUTPUT-OWNERSHIP")
        original = read_json(self.reference / "inputs.json")
        rows = materialization["inputs"]
        if not isinstance(rows, list) or len(rows) != len(original) or materialization["copiedInputs"] != len(rows):
            raise ValueError("FINITE-REFERENCE-INPUT-COUNT")
        indexed = {}
        for row in rows:
            path = verify_file(self.materialized, row)
            if row["originalSource"] in indexed:
                raise ValueError("FINITE-REFERENCE-DUPLICATE-INPUT")
            indexed[row["originalSource"]] = (row, path)
        for row in original:
            archived = verify_file(self.reference, row)
            source = pathlib.Path(row["source"]).resolve()
            if not source.is_relative_to(source_root) or row["source"] not in indexed:
                raise ValueError("FINITE-REFERENCE-SOURCE")
            copied, path = indexed[row["source"]]
            if (path != self.materialized / source.relative_to(source_root)
                    or fingerprint(archived) != fingerprint(path)):
                raise ValueError("FINITE-REFERENCE-INPUT-IDENTITY")
            self.inputs.append((path, fingerprint(path)))
        if len({path for path, _ in self.inputs}) != len(original):
            raise ValueError("FINITE-REFERENCE-DUPLICATE-INPUT")
        tests = read_json(self.reference / "tests.json")
        inventory = {}
        for test in tests["tests"]:
            if not isinstance(test, dict) or test.get("name") in inventory:
                raise ValueError("FINITE-REFERENCE-INVENTORY")
            inventory[test.get("name")] = test
        if not set(self.cases) <= set(inventory):
            raise ValueError("FINITE-REFERENCE-MISSING-CASE")
        available = {path for path, _ in self.inputs}
        self.commands = {}
        for name, identity in self.cases.items():
            self.commands[name] = self.relocate(inventory[name]["command"], identity,
                source_root, self.materialized, available, self.output / "cases" / name)
        if self.stage == "compute-final":
            cache = (self.materialized / "build/web-webgpu-release/CMakeCache.txt").read_text(encoding="utf-8")
            if "ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES:BOOL=ON" not in cache.splitlines():
                raise ValueError("FINITE-REFERENCE-FINAL-PROBE-CONFIGURATION")
        self.output.mkdir(parents=True, exist_ok=False)
        # Freeze the selected registration and this independent workflow too.
        self.inputs.extend((path, fingerprint(path)) for path in (
            self.reference / "tests.json", self.reference / "inputs.json",
            self.materialized / "reference-manifest.json", pathlib.Path(__file__).resolve()))
        for name in ("bindless_finite_lifecycle_acceptance", "bindless_finite_lifecycle_archive_audit",
                     "bindless_finite_lifecycle_evidence", "bindless_post_suppression_evidence",
                     "browser_contract_runner"):
            path = pathlib.Path(sys.modules[name].__file__).resolve()
            if path not in {source for source, _ in self.inputs}:
                self.inputs.append((path, fingerprint(path)))
        copies = []
        for index, (source, expected) in enumerate(self.inputs):
            target = self.output / "inputs" / str(index) / source.name
            target.parent.mkdir(parents=True)
            shutil.copyfile(source, target)
            require_fields(fingerprint(target), expected)
            copies.append(dict(source=str(source), path=str(target.relative_to(self.output)), **expected))
        self.verify_unchanged()
        self.archived_inputs = copies
        (self.output / "inputs.json").write_text(json.dumps(copies, indent=2), encoding="utf-8")
        registrations = dict(tests=[dict(name=name, command=command) for name, command in self.commands.items()])
        (self.output / "tests.json").write_text(json.dumps(registrations, indent=2), encoding="utf-8")
        result = dict(contract="FrozenReferenceFinitePreflight/1", complete=False,
            stage=self.stage, expected=len(self.cases), materializedInputsVerified=len(original),
            exactRegisteredSelectorsAndDeadlinesPreserved=True,
            registrationSource=dict(source=str(self.reference / "tests.json"), **fingerprint(self.reference / "tests.json")),
            aggregatePrerequisitesValidated=False, ordinaryComputePredecessorEligible=False,
            transitiveSdkProvenanceComplete=False, **CLOSED_GATES)
        (self.output / "preflight.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result

    def verify_unchanged(self):
        for path, expected in self.inputs:
            if fingerprint(path) != expected:
                raise ValueError("FINITE-REFERENCE-INPUT-CHANGED")

    def execute(self):
        results = []
        # The native driver and its report validator execute from the copied
        # original namespace. Current live engine files cannot fill a gap.
        for name, identity in self.cases.items():
            self.verify_unchanged()
            command = self.commands[name]
            directory = self.output / "cases" / name
            directory.mkdir(parents=True)
            print(f"[{len(results)+1}/{len(self.cases)}] {name}", flush=True)
            with (directory / "driver.log").open("w", encoding="utf-8") as log:
                code = subprocess.call(command, cwd=self.materialized, stdout=log, stderr=subprocess.STDOUT)
            record = dict(name=name, identity=identity, command=command, exitCode=code, accepted=False)
            results.append(record)
            try:
                if code:
                    raise ValueError("FINITE-REFERENCE-DRIVER-FAILED")
                report_path = directory / "report.json"
                # A second independent check uses the existing canonical report
                # inspector; the copied driver has already validated its report.
                from bindless_finite_lifecycle_evidence import inspect_finite_report
                inspect_finite_report(read_json(report_path), identity)
                record.update(accepted=True, report=dict(path=str(report_path.relative_to(self.output)), **fingerprint(report_path)))
            finally:
                attempt = dict(stage=self.stage, expected=len(self.cases), results=results,
                    complete=False, publicBindlessExecutionAvailable=False)
                (self.output / "attempt.json").write_text(json.dumps(attempt, indent=2), encoding="utf-8")
        self.verify_unchanged()
        summary = dict(contract="FiniteLifecycleAcceptance/1", stage=self.stage,
            scope="private finite Compute only", complete=True, cases=len(self.cases),
            results=results, inputs=self.archived_inputs, finiteMatrixValidated=self.stage=="compute-final",
            transitiveSdkProvenanceComplete=False, ordinaryComputePredecessorEligible=False,
            frozenReferenceAttempt=True, **CLOSED_GATES)
        (self.output / "validation.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"Accepted {len(results)} reference cases; aggregate/public gates remain closed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=pathlib.Path, required=True)
    parser.add_argument("--materialized", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--stage", choices=("claim", "compute-default", "compute-final"), required=True)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    try:
        attempt = FrozenReferenceFiniteAttempt(args.reference, args.materialized, args.output, args.stage)
        preflight = attempt.prepare()
        print(json.dumps(preflight, indent=2), flush=True)
        if not args.prepare_only:
            attempt.execute()
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
