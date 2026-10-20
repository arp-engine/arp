"""Synthetic aggregate invariants; mocked tools confer no GPU authority."""
import contextlib
import io
import json
import pathlib
import tempfile
import types
import unittest
from unittest import mock

import bindless_finite_lifecycle_aggregate as aggregate
from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, audit, read_json
from bindless_finite_lifecycle_cohort_archive import freeze
import bindless_finite_lifecycle_cohort_archive_test as cohort_fixtures
from bindless_finite_lifecycle_predecessor_evidence import COHORTS
from bindless_webgpu_post_suppression_archive_audit import freeze as freeze_post
import bindless_webgpu_post_suppression_archive_audit_test as post_fixtures
import bindless_webgpu_probe_archive_audit_test as probe_fixtures
from bindless_webgpu_probe_build_evidence import TARGET


class FiniteAggregateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="arp-finite-aggregate-unit-")
        cls.root = pathlib.Path(cls.temporary.name).resolve()
        probe_parent = cls.root / "probe"; probe_parent.mkdir()
        cls.probe, build, cls.log, probe_files, tools = probe_fixtures.ProbeArchiveAuditTest().fixture(probe_parent)
        cls.tools = staticmethod(cohort_fixtures.CohortArchiveTest.probe_tools(tools))
        cls.cohorts = {}
        with contextlib.redirect_stdout(io.StringIO()), mock.patch("subprocess.check_output", side_effect=cls.tools):
            unused, ordinary = cohort_fixtures.CohortArchiveTest.stage_fixture(cls.root / "ordinary", "compute-default")
            for name in COHORTS:
                if name != "PostSuppression4":
                    cls.cohorts[name] = cls.root / name
                    freeze(ordinary, name, cls.cohorts[name])
            post_parent = cls.root / "post"; post_parent.mkdir()
            snapshot, log, tools = post_fixtures.PostSuppressionArchiveTest().fixture(post_parent)
            post_stage = cls.root / "post-stage"
            with mock.patch("subprocess.check_output", side_effect=cohort_fixtures.CohortArchiveTest.probe_tools(tools)):
                freeze_post(snapshot, post_stage, log, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))
                cls.cohorts["PostSuppression4"] = cls.root / "PostSuppression4"
                freeze(post_stage, "PostSuppression4", cls.cohorts["PostSuppression4"])
            original, unused = cohort_fixtures.CohortArchiveTest.stage_fixture(cls.root / "final", "compute-final")
            manifest = read_json(original / "validation.json")
            names = {"CMakeCache.txt", *(TARGET + suffix for suffix in (".js", ".wasm", ".data"))}
            manifest["inputs"] = [row for row in manifest["inputs"]
                                  if pathlib.Path(row["source"]).name not in names
                                  and pathlib.Path(row["source"]).resolve() not in probe_files]
            for index, (source, archived) in enumerate(probe_files.items()):
                path = original / "inputs/probe" / str(index) / source.name
                path.parent.mkdir(parents=True); path.write_bytes(archived.read_bytes())
                manifest["inputs"].append(dict(source=str(source.resolve()),
                    path=str(path.relative_to(original)), **fingerprint(path)))
            (original / "validation.json").write_text(json.dumps(manifest), encoding="utf-8")
            (original / "inputs.json").write_text(json.dumps(manifest["inputs"]), encoding="utf-8")
            cls.final = cls.root / "final-corresponding-stage"
            audit(types.SimpleNamespace(snapshot=original, output=cls.final,
                compare_snapshot=None, verify_live=False))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def inspect(self, **kwargs):
        with mock.patch("subprocess.check_output", side_effect=self.tools):
            return aggregate.FiniteComputeLifecycleAggregate.inspect(self.cohorts, self.final,
                self.probe, self.log, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"), **kwargs)

    def test_complete_join_validates_only_selected_finite_compute_lifecycle(self):
        result = self.inspect()
        self.assertTrue(result["complete"])
        self.assertTrue(result["finiteLifecycle4Validated"])
        self.assertTrue(result["aggregatePrerequisitesValidated"])
        self.assertEqual(result["terminalCases"], 204)
        self.assertEqual(result["numericalCases"], 6)
        for gate in set(CLOSED_GATES) - {"finiteLifecycle4Validated"}:
            self.assertIs(result[gate], False, gate)
        self.assertFalse(result["transitiveSdkProvenanceComplete"])
        self.assertFalse(result["affectedRegressionsValidated"])

    def test_empty_available_only_reports_missing_without_acceptance(self):
        result = aggregate.FiniteComputeLifecycleAggregate.inspect({}, None, None, None,
            pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"), available_only=True)
        self.assertFalse(result["complete"])
        self.assertFalse(result["aggregatePrerequisitesValidated"])
        for gate in CLOSED_GATES: self.assertIs(result[gate], False)
        self.assertIn("final", result["missingInputs"])
        self.assertIn("probe", result["missingInputs"])

    def test_available_only_with_complete_evidence_cannot_promote_acceptance(self):
        result = self.inspect(available_only=True)
        self.assertEqual(result["missingInputs"], [])
        self.assertTrue(result["actualProbeBuildEvidenceValidated"])
        self.assertFalse(result["complete"])
        self.assertFalse(result["aggregatePrerequisitesValidated"])
        self.assertFalse(result["finalPayloadRerunValidated"])
        self.assertFalse(result["selfContainedArchive"])
        for gate in CLOSED_GATES: self.assertIs(result[gate], False, gate)

    def test_unknown_cohort_and_nested_archive_owner_refuse_before_inspection(self):
        for cohorts, final, diagnostic in (
                ({"Unknown": self.root}, self.final, "COHORT-NAME"),
                (self.cohorts, self.cohorts["Execution7"] / "nested", "INPUT-OWNER")):
            with self.subTest(diagnostic=diagnostic), self.assertRaisesRegex(ValueError, diagnostic):
                aggregate.FiniteComputeLifecycleAggregate.inspect(cohorts, final, self.probe,
                    self.log, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_missing_final_or_probe_cannot_be_filled_from_predecessors(self):
        for final, probe in ((None, self.probe), (self.final, None),
                             (self.final / "absent", self.probe)):
            with self.subTest(final=final, probe=probe), self.assertRaisesRegex(ValueError, "MISSING-INPUTS"):
                aggregate.FiniteComputeLifecycleAggregate.inspect(self.cohorts, final, probe,
                    self.log, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_final_and_probe_owners_cannot_alias(self):
        with self.assertRaisesRegex(ValueError, "INPUT-OWNER"):
            aggregate.FiniteComputeLifecycleAggregate.inspect(self.cohorts, self.final, self.final,
                self.log, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_probe_correspondence_refuses_unrelated_rehashed_payload(self):
        original = aggregate.inspect_audited_stage
        def substitute(*args, **kwargs):
            inventory = original(*args, **kwargs)
            for source, stamp in inventory["frozenInputIdentities"].items():
                if pathlib.Path(source).name == TARGET + ".wasm":
                    stamp["sha256"] = "f" * 64
            return inventory
        with mock.patch.object(aggregate, "inspect_audited_stage", side_effect=substitute), self.assertRaisesRegex(ValueError, "BUILD-CORRESPONDENCE"):
            self.inspect()

    def test_artifact_drift_after_final_probe_replay_refuses_before_publication(self):
        original = aggregate.inspect_final_build
        row = read_json(self.final / "validation.json")["artifacts"][0]
        path = self.final / row["path"]; before = path.read_bytes()
        def drift(*args, **kwargs):
            result = original(*args, **kwargs)
            path.write_bytes(before + b"changed during aggregate inspection")
            return result
        try:
            with mock.patch.object(aggregate, "inspect_final_build", side_effect=drift), self.assertRaisesRegex(ValueError, "CHANGED-DURING-INSPECTION"):
                self.inspect()
        finally:
            path.write_bytes(before)

    def test_report_owner_cannot_mutate_any_input(self):
        for owner in (*self.cohorts.values(), self.final, self.probe):
            with self.subTest(owner=owner), self.assertRaisesRegex(ValueError, "REPORT-OWNER"):
                aggregate.validate_report_owner(owner / "new-report.json", self.cohorts,
                    self.final, self.probe)


if __name__ == "__main__":
    unittest.main()
