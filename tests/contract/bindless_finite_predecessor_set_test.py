"""Synthetic predecessor joins only; no GPU run or native authorization."""
import contextlib
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import bindless_finite_lifecycle_cohort_archive as cohort_archive
import bindless_finite_lifecycle_cohort_archive_test as cohort_fixtures
import bindless_webgpu_post_suppression_archive_audit_test as post_fixtures
from bindless_webgpu_post_suppression_archive_audit import freeze as freeze_post
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json
from bindless_finite_lifecycle_predecessor_evidence import COHORTS, PACKAGES
from bindless_finite_predecessor_set import FiniteLifecyclePredecessorSet


class FinitePredecessorSetTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arp-finite-set-contract-")
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name).resolve()

    def cohort(self, name="Claim4", stage="claim", payload=None):
        with contextlib.redirect_stdout(io.StringIO()):
            unused, source = cohort_fixtures.CohortArchiveTest.stage_fixture(
                self.root / (name + "-source"), stage, package_payload=payload)
            output = self.root / name
            cohort_archive.freeze(source, name, output)
        return output

    def inspect(self, cohorts, final=None):
        return FiniteLifecyclePredecessorSet.inspect(cohorts, final)

    def assert_closed(self, result):
        for gate in (*CLOSED_GATES, "complete", "aggregatePrerequisitesValidated",
                     "affectedRegressionsValidated", "finalPayloadRerunValidated",
                     "actualProbeBuildEvidenceValidated", "transitiveSdkProvenanceComplete"):
            self.assertIs(result[gate], False, gate)

    def test_empty_and_partial_sets_report_missing_without_authority(self):
        result = self.inspect({})
        self.assertEqual(result["missingCohorts"], sorted(COHORTS))
        self.assertEqual(result["coveredCases"], 0)
        self.assertIs(result["suppliedCohortPackagesBound"], False)
        self.assert_closed(result)
        result = self.inspect(dict(Claim4=self.cohort()))
        self.assertEqual(result["coveredCases"], 30)
        self.assertEqual(result["coveredNumericalCases"], 0)
        self.assertEqual(result["coveredTerminalCases"], 30)
        self.assertEqual(set(result["missingCohorts"]), set(COHORTS) - {"Claim4"})
        self.assertEqual(set(result["packageIdentities"]), set(PACKAGES))
        self.assertIs(result["exactCaseCoverageVerified"], False)
        self.assert_closed(result)

    def test_unknown_aliased_and_nested_owners_refuse_before_replay(self):
        for cohorts, diagnostic in (({"unknown": self.root}, "COHORT-NAME"),
            (dict(Claim4=self.root, Recording4=self.root), "SET-OWNER"),
            (dict(Claim4=self.root, Recording4=self.root / "child"), "SET-OWNER")):
            with self.subTest(cohorts=cohorts), self.assertRaisesRegex(ValueError, diagnostic):
                self.inspect(cohorts)

    def test_wrong_cohort_identity_refuses(self):
        with self.assertRaises(ValueError):
            self.inspect(dict(Recording4=self.cohort()))

    def test_partial_set_cannot_compare_final_or_use_final_as_missing_predecessor(self):
        with self.assertRaisesRegex(ValueError, "FINAL-REQUIRES-COMPLETE-SET"):
            self.inspect(dict(Claim4=self.root / "missing"), self.root / "not-a-final")

    def test_mutated_artifact_after_replay_cannot_enter_set(self):
        root = self.cohort()
        original = cohort_archive.replay
        def mutate_after_replay(*args, **kwargs):
            result = original(*args, **kwargs)
            row = read_json(root / "validation.json")["artifacts"][0]
            path = root / row["path"]
            path.write_bytes(path.read_bytes() + b"\nchanged after stage inspection\n")
            return result
        with mock.patch("bindless_finite_predecessor_set.replay", side_effect=mutate_after_replay):
            with self.assertRaises(ValueError):
                self.inspect(dict(Claim4=root))

    def test_individually_valid_cohorts_cannot_mix_package_identities(self):
        claim = self.cohort()
        recording = self.cohort("Recording4", "compute-default",
            b"testOnly: true\nnoGpuAuthority: true\nvariant: different-workload\n")
        # Both retain complete stage and publication proof; only the join fails.
        self.assertTrue(cohort_archive.replay(claim)["shaderPackagesBound"])
        self.assertTrue(cohort_archive.replay(recording)["shaderPackagesBound"])
        with self.assertRaisesRegex(ValueError, "SET-PACKAGE-MISMATCH"):
            self.inspect(dict(Claim4=claim, Recording4=recording))

    def test_all_five_cohorts_cover_exact_204_plus_six_without_aggregate_promotion(self):
        with contextlib.redirect_stdout(io.StringIO()):
            unused, source = cohort_fixtures.CohortArchiveTest.stage_fixture(
                self.root / "ordinary", "compute-default")
        archives = {}
        for name in COHORTS:
            if name != "PostSuppression4":
                output = self.root / name
                cohort_archive.freeze(source, name, output)
                archives[name] = output
        post_root = self.root / "synthetic-post"
        post_root.mkdir()
        snapshot, log_source, fake_tools = post_fixtures.PostSuppressionArchiveTest().fixture(post_root)
        with mock.patch("subprocess.check_output", side_effect=cohort_fixtures.CohortArchiveTest.probe_tools(fake_tools)):
            post_stage = self.root / "post-stage"
            freeze_post(snapshot, post_stage, log_source, post_fixtures.NINJA, post_fixtures.NM)
            archives["PostSuppression4"] = self.root / "PostSuppression4"
            cohort_archive.freeze(post_stage, "PostSuppression4", archives["PostSuppression4"])
            result = self.inspect(archives)
            self.assertEqual(result["coveredCases"], 210)
            self.assertEqual(result["coveredTerminalCases"], 204)
            self.assertEqual(result["coveredNumericalCases"], 6)
            self.assertEqual(result["missingCohorts"], [])
            self.assertIs(result["exactCaseCoverageVerified"], True)
            self.assertIs(result["allCohortsComparedToIndependentFinal"], False)
            self.assert_closed(result)
            with contextlib.redirect_stdout(io.StringIO()):
                unused, final = cohort_fixtures.CohortArchiveTest.stage_fixture(
                    self.root / "independent-final", "compute-final")
            compared = self.inspect(archives, final)
            self.assertIs(compared["allCohortsComparedToIndependentFinal"], True)
            self.assertEqual(compared["comparedFinalStageAudit"]["path"], str(final / "validation.json"))
            self.assert_closed(compared)
            original = cohort_archive.replay
            def change_final_after_last_cohort(*args, **kwargs):
                result = original(*args, **kwargs)
                if args[0] == archives["PostSuppression4"]:
                    path = final / "validation.json"
                    path.write_bytes(path.read_bytes() + b"\n")
                return result
            with mock.patch("bindless_finite_predecessor_set.replay", side_effect=change_final_after_last_cohort):
                with self.assertRaisesRegex(ValueError, "SET-CHANGED-DURING-INSPECTION"):
                    self.inspect(archives, final)

    def test_cli_duplicate_declarations_do_not_create_report(self):
        report = self.root / "must-not-exist.json"
        process = subprocess.run([sys.executable, "-B", "-X", "utf8",
            str(pathlib.Path(__file__).with_name("bindless_finite_predecessor_set.py")),
            "--cohort", "Claim4=first", "--cohort", "Claim4=second", "--report", str(report)],
            capture_output=True, text=True, encoding="utf-8")
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("SET-DECLARATION", process.stderr)
        self.assertFalse(report.exists())

    def test_cli_report_cannot_modify_an_archive_owner(self):
        archive = self.root / "input"
        archive.mkdir()
        report = archive / "new-report.json"
        process = subprocess.run([sys.executable, "-B", "-X", "utf8",
            str(pathlib.Path(__file__).with_name("bindless_finite_predecessor_set.py")),
            "--cohort", "Claim4=" + str(archive), "--report", str(report)],
            capture_output=True, text=True, encoding="utf-8")
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("SET-REPORT-OWNER", process.stderr)
        self.assertFalse(report.exists())


if __name__ == "__main__":
    unittest.main()
