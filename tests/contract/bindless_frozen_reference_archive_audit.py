"""Inspect a frozen-reference attempt under its proven source namespace.

The running workflow and canonical validators remain unmodified. Only the
required-source root of a private copy of the canonical inspection function is
mapped, after proving original/copy/attempt hashes and exact registrations.
Mapping preflight is not native acceptance; this inspection is not a predecessor
archive, an SDK provenance proof, or public execution authorization.
"""
import argparse
import json
import pathlib
import sys
import types

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import (
    CLOSED_GATES, inspect_snapshot, read_json, verify_file)
from bindless_finite_lifecycle_evidence import require_fields
from bindless_frozen_reference_acceptance import FrozenReferenceFiniteAttempt

VALIDATORS = ("bindless_frozen_reference_acceptance", "bindless_finite_lifecycle_acceptance",
    "bindless_finite_lifecycle_archive_audit", "bindless_finite_lifecycle_evidence",
    "bindless_post_suppression_evidence", "browser_contract_runner")
REQUIRED_SOURCES = ("CMakeLists.txt", "tests/contract/bindless_webgpu_rhi_smoke.py",
    "tests/contract/bindless_finite_lifecycle_acceptance.py",
    "tests/fixtures/bindless_webgpu_rhi_runner.html",
    "tests/fixtures/original_general_finite_claim_terminal_fixture.h",
    "scripts/browser_contract_runner.py")


class FrozenReferenceSourceAudit:
    def __init__(self, snapshot, reference, materialized):
        self.snapshot = snapshot.resolve()
        self.reference = reference.resolve()
        self.materialized = materialized.resolve()

    @staticmethod
    def index_inputs(root, entries):
        if not isinstance(entries, list) or not entries:
            raise ValueError("FINITE-REFERENCE-AUDIT-INPUTS")
        sources, copies = {}, set()
        for entry in entries:
            if not isinstance(entry, dict) or not isinstance(entry.get("source"), str):
                raise ValueError("FINITE-REFERENCE-AUDIT-INPUTS")
            source = pathlib.Path(entry["source"])
            if not source.is_absolute():
                raise ValueError("FINITE-REFERENCE-AUDIT-SOURCE")
            source = source.resolve()
            path = verify_file(root, entry)
            if source in sources or path in copies:
                raise ValueError("FINITE-REFERENCE-AUDIT-DUPLICATE-INPUT")
            sources[source] = path
            copies.add(path)
        return sources

    def archived(self, source):
        path = self.sources.get(source.resolve())
        if path is None:
            raise ValueError("FINITE-REFERENCE-AUDIT-MISSING-SOURCE: " + str(source))
        return path

    @staticmethod
    def inspect_copy_proof(original_root, materialized, original_sources, attempt_sources,
                           rows, copied_sources=None):
        """Reuse the identical copy proof for live preflight and offline archives."""
        if not isinstance(rows, list) or len(rows) != len(original_sources):
            raise ValueError("FINITE-REFERENCE-AUDIT-INPUT-COUNT")
        copied, paths, files = {}, set(), {}
        for row in rows:
            if not isinstance(row, dict) or not isinstance(row.get("originalSource"), str):
                raise ValueError("FINITE-REFERENCE-AUDIT-COPY")
            source = pathlib.Path(row["originalSource"])
            if not source.is_absolute():
                raise ValueError("FINITE-REFERENCE-AUDIT-SOURCE")
            source = source.resolve()
            if not source.is_relative_to(original_root):
                raise ValueError("FINITE-REFERENCE-AUDIT-SOURCE-ROOT")
            relative = row.get("path")
            if not isinstance(relative, str):
                raise ValueError("FINITE-REFERENCE-AUDIT-COPY")
            relative = pathlib.Path(relative.replace("\\", "/"))
            if (relative.is_absolute() or ".." in relative.parts
                    or relative != source.relative_to(original_root)):
                raise ValueError("FINITE-REFERENCE-AUDIT-COPY-PATH")
            logical = materialized / relative
            path = (verify_file(materialized, row) if copied_sources is None
                    else copied_sources.get(logical))
            original_path, attempt_path = original_sources.get(source), attempt_sources.get(logical)
            if path is None or original_path is None or attempt_path is None:
                raise ValueError("FINITE-REFERENCE-AUDIT-COPY-IDENTITY")
            require_fields(row, fingerprint(path))
            if source in copied or logical in paths:
                raise ValueError("FINITE-REFERENCE-AUDIT-DUPLICATE-COPY")
            if (fingerprint(original_path) != fingerprint(path)
                    or fingerprint(attempt_path) != fingerprint(path)):
                raise ValueError("FINITE-REFERENCE-AUDIT-COPY-IDENTITY")
            copied[source] = logical
            paths.add(logical)
            files.update({item: fingerprint(item) for item in (path, original_path, attempt_path)})
        if set(copied) != set(original_sources):
            raise ValueError("FINITE-REFERENCE-AUDIT-INPUT-COVERAGE")
        return paths, files

    @staticmethod
    def inspect_registration(original_tests, tests, cases, original_root, materialized,
                             available, snapshot_owner):
        inventories = []
        for inventory in (original_tests, tests):
            if not isinstance(inventory, list):
                raise ValueError("FINITE-REFERENCE-AUDIT-INVENTORY")
            indexed = {}
            for test in inventory:
                if not isinstance(test, dict) or not isinstance(test.get("name"), str) or test["name"] in indexed:
                    raise ValueError("FINITE-REFERENCE-AUDIT-INVENTORY")
                indexed[test["name"]] = test
            inventories.append(indexed)
        original_tests, tests = inventories
        if not set(cases) <= set(original_tests) or set(tests) != set(cases):
            raise ValueError("FINITE-REFERENCE-AUDIT-REGISTERED-COVERAGE")
        for name, identity in cases.items():
            expected = FrozenReferenceFiniteAttempt.relocate(original_tests[name].get("command"),
                identity, original_root, materialized, available, snapshot_owner / "cases" / name)
            if tests[name].get("command") != expected:
                raise ValueError("FINITE-REFERENCE-AUDIT-REGISTERED-COMMAND")

    def prepare(self):
        self.entries = read_json(self.snapshot / "inputs.json")
        self.sources = self.index_inputs(self.snapshot, self.entries)
        preflight = read_json(self.snapshot / "preflight.json")
        require_fields(preflight, dict(contract="FrozenReferenceFinitePreflight/1", complete=False,
            exactRegisteredSelectorsAndDeadlinesPreserved=True,
            aggregatePrerequisitesValidated=False, ordinaryComputePredecessorEligible=False,
            transitiveSdkProvenanceComplete=False, **CLOSED_GATES))
        self.stage = preflight.get("stage")
        if self.stage not in ("claim", "compute-default", "compute-final"):
            raise ValueError("FINITE-REFERENCE-AUDIT-STAGE")
        cases = expected_cases(self.stage)
        require_fields(preflight, dict(expected=len(cases)))
        materialization = read_json(self.archived(self.materialized / "reference-manifest.json"))
        require_fields(materialization, dict(contract="FrozenInputReferenceMaterialization/1",
            publicBindlessExecutionAvailable=False, ordinaryComputePredecessorEligible=False,
            platformValidated=False, productionReady=False))
        if pathlib.Path(materialization["reference"]).resolve() != self.reference:
            raise ValueError("FINITE-REFERENCE-AUDIT-PARENT")
        original_root = pathlib.Path(materialization["sourceRoot"])
        if not original_root.is_absolute():
            raise ValueError("FINITE-REFERENCE-AUDIT-SOURCE-ROOT")
        original_root = original_root.resolve()
        roots = (self.snapshot, self.reference, self.materialized, original_root)
        if any(a == b or a.is_relative_to(b) or b.is_relative_to(a)
               for index, a in enumerate(roots[:3]) for b in roots[index + 1:]
               if not (a == self.reference and b == original_root)):
            raise ValueError("FINITE-REFERENCE-AUDIT-ROOT-OWNERSHIP")
        original = read_json(self.archived(self.reference / "inputs.json"))
        rows = materialization.get("inputs")
        if not isinstance(original, list) or not original or not isinstance(rows, list):
            raise ValueError("FINITE-REFERENCE-AUDIT-INPUTS")
        require_fields(materialization, dict(copiedInputs=len(original)))
        require_fields(preflight, dict(materializedInputsVerified=len(original)))
        original_sources = self.index_inputs(self.reference, original)
        paths, proof_files = self.inspect_copy_proof(original_root, self.materialized,
            original_sources, self.sources, rows)
        if not {self.materialized / name for name in REQUIRED_SOURCES} <= paths:
            raise ValueError("FINITE-REFERENCE-AUDIT-REQUIRED-SOURCES")
        # Bind the inspector's actual loaded helper files to the workflow copies.
        # A newer live validator cannot silently replace the frozen contract.
        self.validator_files = {}
        for name in VALIDATORS:
            source = pathlib.Path(sys.modules[name].__file__).resolve()
            expected = fingerprint(self.archived(source))
            if fingerprint(source) != expected:
                raise ValueError("FINITE-REFERENCE-AUDIT-VALIDATOR-CHANGED")
            self.validator_files[source] = expected
        registered_path = self.archived(self.reference / "tests.json")
        require_fields(preflight.get("registrationSource", {}), dict(
            source=str(self.reference / "tests.json"), **fingerprint(registered_path)))
        original_tests = read_json(registered_path).get("tests")
        tests = read_json(self.snapshot / "tests.json").get("tests")
        self.inspect_registration(original_tests, tests, cases, original_root,
            self.materialized, paths, self.snapshot)
        self.stamps = {path: fingerprint(path) for path in self.sources.values()}
        self.stamps.update(proof_files)
        self.stamps.update(self.validator_files)
        self.stamps.update({self.snapshot / name: fingerprint(self.snapshot / name)
                            for name in ("inputs.json", "preflight.json", "tests.json")})
        self.mapping = dict(contract="FrozenReferenceSourceRootMapping/1", complete=False,
            scope="private frozen-reference Compute source mapping; no predecessor publication",
            stage=self.stage, sourceRoot=str(original_root), mappedRoot=str(self.materialized),
            materializedInputsVerified=len(rows), attemptInputsVerified=len(self.entries),
            registeredCasesVerified=len(cases), sourceRootMappingVerified=True,
            exactRegisteredSelectorsAndDeadlinesPreserved=True, nativeAcceptanceInspected=False,
            aggregatePrerequisitesValidated=False, ordinaryComputePredecessorEligible=False,
            transitiveSdkProvenanceComplete=False, **CLOSED_GATES)
        return self.mapping

    def verify_unchanged(self):
        if any(fingerprint(path) != expected for path, expected in self.stamps.items()):
            raise ValueError("FINITE-REFERENCE-AUDIT-CHANGED-DURING-INSPECTION")

    def inspect(self):
        mapping = self.prepare()
        acceptance_path = self.snapshot / "validation.json"
        acceptance_stamp = fingerprint(acceptance_path)
        manifest = read_json(acceptance_path)
        require_fields(manifest, dict(frozenReferenceAttempt=True, ordinaryComputePredecessorEligible=False))
        if manifest.get("stage") != self.stage:
            raise ValueError("FINITE-REFERENCE-AUDIT-STAGE")
        # Clone only the canonical function's namespace; never mutate its module
        # ROOT or rewrite the manifest. Every report/hash/selector check is intact.
        globals_ = dict(inspect_snapshot.__globals__, ROOT=self.materialized)
        inspector = types.FunctionType(inspect_snapshot.__code__, globals_,
            inspect_snapshot.__name__, inspect_snapshot.__defaults__, inspect_snapshot.__closure__)
        inspected = inspector(self.snapshot)
        self.inspected = inspected
        self.verify_unchanged()
        if fingerprint(acceptance_path) != acceptance_stamp:
            raise ValueError("FINITE-REFERENCE-AUDIT-CHANGED-DURING-INSPECTION")
        return dict(mapping, contract="FrozenReferenceFiniteStageInspection/1", complete=True,
            nativeAcceptanceInspected=True, shaderMetadataFrozen=inspected["shaderMetadataFrozen"],
            selectedFilesInspected=len(inspected["files"]), semanticProfiles=inspected["profiles"],
            acceptance=dict(source=str(acceptance_path), **acceptance_stamp))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=pathlib.Path, required=True)
    parser.add_argument("--reference", type=pathlib.Path, required=True)
    parser.add_argument("--materialized", type=pathlib.Path, required=True)
    parser.add_argument("--report", type=pathlib.Path, required=True)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    try:
        audit = FrozenReferenceSourceAudit(args.snapshot, args.reference, args.materialized)
        result = audit.prepare() if args.prepare_only else audit.inspect()
        audit.verify_unchanged()
        with args.report.open("x", encoding="utf-8") as output:
            json.dump(result, output, indent=2)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
