"""Synthetic provenance/namespace negatives; no native GPU evidence is created."""
import copy
import json
import pathlib
import sys
import unittest
from unittest import mock

import bindless_finite_lifecycle_archive_audit as canonical
import bindless_frozen_reference_archive_audit as subject
from bindless_finite_lifecycle_acceptance import fingerprint
import bindless_frozen_reference_acceptance_test as fixture_contract
import bindless_finite_lifecycle_evidence_test as report_contract


class FrozenReferenceSourceAuditTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture_contract.FrozenReferenceAttemptTests(methodName="runTest")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        for name in ("CMakeLists.txt", "tests/contract/bindless_finite_lifecycle_acceptance.py",
                     "tests/fixtures/bindless_webgpu_rhi_runner.html",
                     "tests/fixtures/original_general_finite_claim_terminal_fixture.h",
                     "scripts/browser_contract_runner.py"):
            self.fixture.names.append(name)
        self.fixture.available = {self.fixture.materialized / name for name in self.fixture.names}
        self.fixture.fixture()
        self.fixture.attempt().prepare()

    def audit(self):
        return subject.FrozenReferenceSourceAudit(
            self.fixture.output, self.fixture.reference, self.fixture.materialized)

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def frozen_json(self, source):
        rows = canonical.read_json(self.fixture.output / "inputs.json")
        entry = next(row for row in rows if pathlib.Path(row["source"]).resolve() == source.resolve())
        return self.fixture.output / entry["path"]

    def replace_frozen(self, source, edit):
        path = self.frozen_json(source)
        value = canonical.read_json(path)
        edit(value)
        self.write(path, value)
        rows = canonical.read_json(self.fixture.output / "inputs.json")
        entry = next(row for row in rows if pathlib.Path(row["source"]).resolve() == source.resolve())
        entry.update(fingerprint(path))
        self.write(self.fixture.output / "inputs.json", rows)

    def complete_synthetic_snapshot(self):
        # Reuse the existing hostile-report fixture, never GPU work or readback.
        generator = report_contract.FiniteNativeEvidenceTest(methodName="runTest")
        inventory = canonical.read_json(self.fixture.output / "tests.json")["tests"]
        stage = self.fixture.stage
        identities = subject.expected_cases(stage)
        records = []
        for test in inventory:
            name, command = test["name"], test["command"]
            identity = identities[name]
            report = generator.finite(identity)
            directory = self.fixture.output / "cases" / name
            directory.mkdir(parents=True)
            self.write(directory / "report.json", report)
            self.write(directory / "driver.log", report)
            (directory / "browser.log").write_text("synthetic trace only", encoding="utf-8")
            records.append(dict(name=name, identity=list(identity), command=command,
                exitCode=0, accepted=True, report=dict(path=str((directory / "report.json").relative_to(
                    self.fixture.output)), **fingerprint(directory / "report.json"))))
        self.write(self.fixture.output / "attempt.json", dict(stage=stage, expected=len(identities),
            results=records, complete=False, publicBindlessExecutionAvailable=False))
        self.write(self.fixture.output / "validation.json", dict(contract="FiniteLifecycleAcceptance/1",
            complete=True, scope="private finite Compute only", stage=stage, cases=len(identities),
            inputs=canonical.read_json(self.fixture.output / "inputs.json"), results=records,
            finiteMatrixValidated=False, frozenReferenceAttempt=True,
            ordinaryComputePredecessorEligible=False, transitiveSdkProvenanceComplete=False,
            **canonical.CLOSED_GATES))
        return records

    def test_mapping_preserves_commands_and_all_gates_without_native_launch(self):
        original_root = canonical.ROOT
        with mock.patch("subprocess.call", side_effect=AssertionError("native launch forbidden")):
            result = self.audit().prepare()
        self.assertEqual(result["registeredCasesVerified"], 30)
        self.assertEqual(result["materializedInputsVerified"], len(self.fixture.names))
        self.assertIs(result["sourceRootMappingVerified"], True)
        self.assertIs(result["nativeAcceptanceInspected"], False)
        self.assertIs(result["complete"], False)
        for gate in canonical.CLOSED_GATES:
            self.assertIs(result[gate], False)
        self.assertEqual(canonical.ROOT, original_root)
        self.assertFalse((self.fixture.output / "validation.json").exists())

    def test_wrong_parent_and_forged_source_root_refuse(self):
        source = self.fixture.materialized / "reference-manifest.json"
        original = self.frozen_json(source).read_bytes()
        for field, value in (("reference", str(self.fixture.root / "forged-parent")),
                             ("sourceRoot", str(self.fixture.root / "forged-root"))):
            self.frozen_json(source).write_bytes(original)
            self.replace_frozen(source, lambda data: data.update({field: value}))
            with self.assertRaises(ValueError):
                self.audit().prepare()

    def test_duplicate_materialized_source_and_path_escape_refuse(self):
        source = self.fixture.materialized / "reference-manifest.json"
        original = self.frozen_json(source).read_bytes()
        for edit in (lambda data: data["inputs"].__setitem__(1, copy.deepcopy(data["inputs"][0])),
                     lambda data: data["inputs"][0].update(path="../outside")):
            self.frozen_json(source).write_bytes(original)
            self.replace_frozen(source, edit)
            with self.assertRaises(ValueError):
                self.audit().prepare()

    def test_copied_bytes_and_reference_archive_mutation_refuse(self):
        path = self.fixture.materialized / self.fixture.names[0]
        original = path.read_bytes()
        path.write_bytes(b"changed-native-driver")
        with self.assertRaises(ValueError):
            self.audit().prepare()
        path.write_bytes(original)
        reference = canonical.read_json(self.fixture.reference / "inputs.json")[0]
        (self.fixture.reference / reference["path"]).write_bytes(b"changed-reference")
        with self.assertRaises(ValueError):
            self.audit().prepare()

    def test_missing_attempt_source_and_duplicate_attempt_input_refuse(self):
        path = self.fixture.output / "inputs.json"
        original = canonical.read_json(path)
        missing = [row for row in original if pathlib.Path(row["source"]).name != "CMakeLists.txt"]
        for rows in (missing, original + [original[0]]):
            self.write(path, rows)
            with self.assertRaises(ValueError):
                self.audit().prepare()

    def test_deadline_browser_selector_and_output_mutation_refuse(self):
        path = self.fixture.output / "tests.json"
        original = canonical.read_json(path)
        for option in ("--timeout-seconds", "--browser", "--general-claim-terminal", "--output"):
            wrong = copy.deepcopy(original)
            command = wrong["tests"][0]["command"]
            command[command.index(option) + 1] = "forged"
            self.write(path, wrong)
            with self.assertRaisesRegex(ValueError, "REGISTERED-COMMAND"):
                self.audit().prepare()

    def test_registered_missing_or_duplicate_case_refuse(self):
        path = self.fixture.output / "tests.json"
        original = canonical.read_json(path)
        for tests in (original["tests"][:-1], original["tests"] + [original["tests"][0]]):
            self.write(path, dict(tests=tests))
            with self.assertRaises(ValueError):
                self.audit().prepare()

    def test_bool_coercion_does_not_authorize_mapping(self):
        path = self.fixture.output / "preflight.json"
        original = canonical.read_json(path)
        for field, value in (("complete", 0), ("exactRegisteredSelectorsAndDeadlinesPreserved", 1),
                             ("publicBindlessExecutionAvailable", True),
                             ("materializedInputsVerified", True)):
            wrong = dict(original, **{field: value})
            self.write(path, wrong)
            with self.assertRaises(ValueError):
                self.audit().prepare()

    def test_loaded_validator_must_match_frozen_workflow_source(self):
        source = pathlib.Path(sys.modules["bindless_finite_lifecycle_archive_audit"].__file__).resolve()
        original = subject.fingerprint

        def altered(path):
            return dict(bytes=0, sha256="0" * 64) if path.resolve() == source else original(path)

        with mock.patch.object(subject, "fingerprint", side_effect=altered):
            with self.assertRaisesRegex(ValueError, "VALIDATOR-CHANGED"):
                self.audit().prepare()

    def test_mapping_does_not_inspect_incomplete_attempt(self):
        with self.assertRaises(FileNotFoundError):
            self.audit().inspect()

    def test_canonical_complete_case_requirement_remains_enforced(self):
        path = self.fixture.output / "validation.json"
        inputs = canonical.read_json(self.fixture.output / "inputs.json")
        self.write(path, dict(contract="FiniteLifecycleAcceptance/1", complete=True,
            scope="private finite Compute only", stage="claim", cases=30, inputs=inputs, results=[],
            finiteMatrixValidated=False, frozenReferenceAttempt=True,
            ordinaryComputePredecessorEligible=False, transitiveSdkProvenanceComplete=False,
            **canonical.CLOSED_GATES))
        original_root = canonical.ROOT
        with self.assertRaisesRegex(ValueError, "FINITE-ARCHIVE-RESULTS"):
            self.audit().inspect()
        self.assertEqual(canonical.ROOT, original_root)

    def test_changes_after_preflight_refuse(self):
        audit = self.audit()
        audit.prepare()
        (self.fixture.output / "tests.json").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "CHANGED-DURING-INSPECTION"):
            audit.verify_unchanged()

    def test_complete_synthetic_reports_reuse_canonical_inspection_without_global_root_mutation(self):
        self.complete_synthetic_snapshot()
        original_root = canonical.ROOT
        with mock.patch("subprocess.call", side_effect=AssertionError("native launch forbidden")):
            result = self.audit().inspect()
        self.assertEqual(len(result["semanticProfiles"]), 30)
        self.assertEqual(result["contract"], "FrozenReferenceFiniteStageInspection/1")
        self.assertEqual(canonical.ROOT, original_root)
        for field in (*canonical.CLOSED_GATES, "ordinaryComputePredecessorEligible",
                      "aggregatePrerequisitesValidated", "transitiveSdkProvenanceComplete"):
            self.assertIs(result[field], False)

    def test_canonical_driver_report_and_native_progress_checks_are_not_bypassed(self):
        records = self.complete_synthetic_snapshot()
        report_path = self.fixture.output / records[0]["report"]["path"]
        driver_path = report_path.with_name("driver.log")
        self.write(driver_path, {})
        with self.assertRaisesRegex(ValueError, "FINITE-ARCHIVE-DRIVER-REPORT"):
            self.audit().inspect()
        report = canonical.read_json(report_path)
        report["progress"] = []
        self.write(report_path, report)
        self.write(driver_path, report)
        manifest = canonical.read_json(self.fixture.output / "validation.json")
        manifest["results"][0]["report"].update(fingerprint(report_path))
        self.write(self.fixture.output / "validation.json", manifest)
        attempt = canonical.read_json(self.fixture.output / "attempt.json")
        attempt["results"] = manifest["results"]
        self.write(self.fixture.output / "attempt.json", attempt)
        with self.assertRaisesRegex(ValueError, "FINITE-NATIVE-PROGRESS"):
            self.audit().inspect()

    def test_attempt_command_must_match_frozen_registration(self):
        self.complete_synthetic_snapshot()
        for name in ("validation.json", "attempt.json"):
            path = self.fixture.output / name
            data = canonical.read_json(path)
            command = data["results"][0]["command"]
            command[command.index("--timeout-seconds") + 1] = "360"
            self.write(path, data)
        with self.assertRaisesRegex(ValueError, "FINITE-ARCHIVE-COMMAND"):
            self.audit().inspect()

    def test_materialized_mutation_after_mapping_is_detected(self):
        audit = self.audit()
        audit.prepare()
        path = self.fixture.materialized / self.fixture.names[0]
        path.write_text("changed-after-mapping", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "CHANGED-DURING-INSPECTION"):
            audit.verify_unchanged()


if __name__ == "__main__":
    unittest.main()
