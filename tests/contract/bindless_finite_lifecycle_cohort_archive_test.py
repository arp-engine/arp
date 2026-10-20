"""Hostile copied-cohort tests. Synthetic fixtures confer no native GPU proof."""
import contextlib
import copy
import io
import pathlib
import shutil
import tempfile
import types
import unittest
from unittest import mock

import bindless_webgpu_post_suppression_archive_audit_test as post_fixtures
from bindless_webgpu_post_suppression_archive_audit import freeze as freeze_post

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, ROOT, audit, read_json
from bindless_finite_lifecycle_cohort_archive import expected_cohort, freeze, replay
import bindless_finite_lifecycle_evidence_test as native_fixtures
import bindless_finite_lifecycle_predecessor_evidence_test as package_fixtures


class CohortArchiveTest(unittest.TestCase):
    @staticmethod
    def probe_tools(fake_tools):
        def run(args, **kwargs):
            arguments = list(args)
            if pathlib.Path(arguments[0]).name == "ninja.exe": arguments[0] = "unit-ninja"
            if pathlib.Path(arguments[0]).name == "llvm-nm.exe": arguments[0] = "unit-nm"
            return fake_tools(arguments, **kwargs)
        return run

    def test_dedicated_native_post_attempt_replays_as_exact_predecessor_after_original_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); prior = root / "prior"; prior.mkdir()
            snapshot, log_source, fake_tools = post_fixtures.PostSuppressionArchiveTest().fixture(prior)
            stage = root / "post-audit"; archive = root / "PostSuppression4"
            with mock.patch("subprocess.check_output", side_effect=self.probe_tools(fake_tools)):
                freeze_post(snapshot, stage, log_source, post_fixtures.NINJA, post_fixtures.NM)
                accepted = freeze(stage, "PostSuppression4", archive)
                self.assertEqual(accepted["cases"], 12)
                self.assertTrue(accepted["shaderPackagesBound"])
                shutil.rmtree(snapshot); shutil.rmtree(stage)
                self.assertEqual(replay(archive), accepted)
                for gate in CLOSED_GATES: self.assertIs(accepted[gate], False)
                self.assertFalse(accepted["aggregatePrerequisitesValidated"])

    def test_post_attempt_cannot_supply_other_cohorts_and_compares_independent_final_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); prior = root / "prior"; prior.mkdir()
            snapshot, log_source, fake_tools = post_fixtures.PostSuppressionArchiveTest().fixture(prior)
            stage = root / "post-audit"; archive = root / "PostSuppression4"
            with mock.patch("subprocess.check_output", side_effect=self.probe_tools(fake_tools)):
                freeze_post(snapshot, stage, log_source, post_fixtures.NINJA, post_fixtures.NM)
                for cohort in ("Execution7", "Recording4", "Submission4", "Claim4"):
                    with self.subTest(cohort=cohort), self.assertRaisesRegex(ValueError, "MISSING-CASES"):
                        freeze(stage, cohort, root / cohort)
                freeze(stage, "PostSuppression4", archive)
                unused, final = self.stage_fixture(root / "final", "compute-final")
                result = replay(archive, final)
                self.assertTrue(result["finalCohortProfileCompared"])
                self.assertFalse(result["finalPayloadRerunValidated"])
                self.assertFalse(result["finiteLifecycle4Validated"])

    @staticmethod
    def stage_fixture(root, stage="claim", packages=True, package_payload=None):
        original = root / "original"; original.mkdir(parents=True)
        factory = native_fixtures.FiniteArchiveEvidenceTest()
        manifest = factory.snapshot(original)
        identities = {}
        if packages:
            arguments = {} if package_payload is None else dict(payload=package_payload)
            packet, unused = package_fixtures.PredecessorEvidenceTest.package_fixture(
                original / "package-fixture", **arguments)
            identities = read_json(original / "package-fixture/report.json")
            for entry in packet["inputs"]:
                copied = copy.deepcopy(entry)
                copied["path"] = str(pathlib.Path("package-fixture") / copied["path"])
                manifest["inputs"].append(copied)
        if stage == "compute-final":
            path = original / "inputs/cache/CMakeCache.txt"
            path.parent.mkdir(parents=True)
            path.write_text("ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES:BOOL=ON\nBUILD_SHARED_LIBS:BOOL=OFF\n"
                + "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(ROOT.resolve()) + "\n"
                + "CMAKE_CACHEFILE_DIR:INTERNAL=" + str((ROOT / "build/unit-fixture").resolve()) + "\n", encoding="utf-8")
            manifest["inputs"].append(dict(source=str(ROOT / "build/unit-fixture/CMakeCache.txt"),
                path=str(path.relative_to(original)), **fingerprint(path)))
        records, tests = [], []
        fixture = native_fixtures.FiniteNativeEvidenceTest()
        selectors = dict(recording_terminal="--general-recording-terminal",
            submission_terminal="--general-submission-terminal", physical_loss="--general-physical-loss",
            map_terminal="--general-map-terminal", claim_terminal="--general-claim-terminal",
            post_suppression="--general-post-suppression-failure", execution="--general-finite-execution")
        for name, identity in expected_cases(stage).items():
            group, buffer_type, interval, terminal = identity
            command = ["unit-fixture-python", "unit-fixture-driver", "--general-resource-projection-scenario"]
            if group != "execution": command.append("--general-finite-lifecycle")
            command.append(selectors[group])
            if terminal is not None: command.append(terminal)
            command += ["--general-content-buffer-type", buffer_type, "--output", str(original / "cases" / name)]
            if interval == "dynamic": command.append("--general-content-plan-dynamic")
            if interval == "whole": command.append("--general-source-whole")
            report = fixture.finite(identity); report.update(identities)
            path = original / "cases" / name / "report.json"
            factory.write(path, report); factory.write(path.with_name("driver.log"), report)
            path.with_name("browser.log").write_text("synthetic fixture, no GPU execution", encoding="utf-8")
            records.append(dict(name=name, identity=list(identity), command=command, exitCode=0, accepted=True,
                report=dict(path=str(path.relative_to(original)), **fingerprint(path))))
            tests.append(dict(name=name, command=command))
        manifest.update(stage=stage, cases=len(records), results=records, finiteMatrixValidated=stage == "compute-final")
        factory.write(original / "validation.json", manifest)
        factory.write(original / "inputs.json", manifest["inputs"])
        factory.write(original / "tests.json", dict(tests=tests))
        factory.write(original / "attempt.json", dict(stage=stage, expected=len(records), results=records,
            complete=False, publicBindlessExecutionAvailable=False))
        stage_root = root / "stage-audit"
        with contextlib.redirect_stdout(io.StringIO()):
            audit(types.SimpleNamespace(snapshot=original, output=stage_root, compare_snapshot=None, verify_live=False))
        return original, stage_root

    def test_exact_cohorts_cover_204_terminals_and_six_numerical_cases(self):
        expected = {name: len(expected_cohort(name)) for name in
                    ("Execution7", "Recording4", "Submission4", "Claim4", "PostSuppression4")}
        self.assertEqual(expected, dict(Execution7=96, Recording4=24, Submission4=48, Claim4=30, PostSuppression4=12))
        with self.assertRaises(ValueError): expected_cohort("arbitrary")

    def test_copied_archive_replays_after_both_original_directories_disappear(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); original, stage = self.stage_fixture(root)
            archive = root / "cohort"
            result = freeze(stage, "Claim4", archive)
            self.assertEqual(result["cases"], 30)
            self.assertTrue(result["shaderPackagesBound"])
            for gate in CLOSED_GATES: self.assertIs(result[gate], False)
            self.assertFalse(result["affectedRegressionsValidated"])
            shutil.rmtree(original); shutil.rmtree(stage)
            self.assertEqual(replay(archive), result)

    def test_missing_package_or_unrepresented_cohort_never_creates_acceptance(self):
        for packages, cohort in ((False, "Claim4"), (True, "Recording4"), (True, "PostSuppression4")):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); unused, stage = self.stage_fixture(root, packages=packages)
                output = root / "refused"
                with self.subTest(packages=packages, cohort=cohort), self.assertRaises(ValueError):
                    freeze(stage, cohort, output)
                self.assertFalse(output.exists())

    def test_complete_default_stage_yields_four_separate_exact_cohorts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); unused, stage = self.stage_fixture(root, "compute-default")
            for cohort, count in (("Execution7", 96), ("Recording4", 24), ("Submission4", 48), ("Claim4", 30)):
                with self.subTest(cohort=cohort):
                    result = freeze(stage, cohort, root / cohort)
                    self.assertEqual(result["cases"], count)
                    self.assertFalse(result["finiteLifecycle4Validated"])

    def test_changed_artifact_profile_identity_count_or_gate_refuses(self):
        for mutation in ("artifact", "profile", "identity", "count", "duplicate", "gate", "packages", "acceptance"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); unused, stage = self.stage_fixture(root)
                archive = root / "cohort"; freeze(stage, "Claim4", archive)
                path = archive / "validation.json"; manifest = read_json(path)
                if mutation == "artifact":
                    artifact = archive / manifest["artifacts"][0]["path"]
                    artifact.write_bytes(artifact.read_bytes() + b" altered")
                if mutation == "profile": manifest["semanticProfiles"] = {}
                if mutation == "identity": manifest["identities"] = {}
                if mutation == "count": manifest["cases"] -= 1
                if mutation == "duplicate":
                    manifest["artifacts"].append(manifest["artifacts"][0]); manifest["artifactCount"] += 1
                if mutation == "gate": manifest["finiteLifecycle4Validated"] = True
                if mutation == "packages":
                    manifest["packageIdentities"][next(iter(manifest["packageIdentities"]))] = "f" * 64
                if mutation == "acceptance": manifest["sourceAcceptance"]["sha256"] = "f" * 64
                native_fixtures.FiniteArchiveEvidenceTest.write(path, manifest)
                with self.subTest(mutation=mutation), self.assertRaises(ValueError): replay(archive)

    def test_rehashed_semantic_profile_in_parent_still_replays_native_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); unused, stage = self.stage_fixture(root)
            archive = root / "cohort"; freeze(stage, "Claim4", archive)
            manifest_path = archive / "validation.json"; manifest = read_json(manifest_path)
            parent_path = archive / "stage/validation.json"; parent = read_json(parent_path)
            name = next(iter(parent["semanticProfiles"]))
            parent["semanticProfiles"][name]["originalGeneralFiniteLifecycleVersion"] = 3
            manifest["semanticProfiles"] = parent["semanticProfiles"]
            native_fixtures.FiniteArchiveEvidenceTest.write(parent_path, parent)
            manifest["stageAudit"].update(fingerprint(parent_path))
            next(entry for entry in manifest["artifacts"] if entry["path"] == str(pathlib.Path("stage/validation.json"))).update(fingerprint(parent_path))
            native_fixtures.FiniteArchiveEvidenceTest.write(manifest_path, manifest)
            with self.assertRaises(ValueError): replay(archive)

    def test_output_inside_stage_or_existing_archive_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); unused, stage = self.stage_fixture(root)
            with self.assertRaises(ValueError): freeze(stage, "Claim4", stage / "nested")
            output = root / "cohort"; freeze(stage, "Claim4", output)
            with self.assertRaises(FileExistsError): freeze(stage, "Claim4", output)
            self.assertTrue(replay(output)["complete"])

    def test_same_final_run_cannot_become_its_own_predecessor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); unused, final = self.stage_fixture(root, "compute-final")
            archive = root / "post"; freeze(final, "PostSuppression4", archive)
            with self.assertRaisesRegex(ValueError, "SELF-PREDECESSOR"):
                replay(archive, final)

    def test_independent_final_profile_comparison_keeps_all_aggregate_gates_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); unused, prior = self.stage_fixture(root / "prior")
            archive = root / "cohort"; freeze(prior, "Claim4", archive)
            unused, final = self.stage_fixture(root / "final", "compute-final")
            result = replay(archive, final)
            self.assertTrue(result["finalCohortProfileCompared"])
            self.assertFalse(result["finalPayloadRerunValidated"])
            self.assertFalse(result["aggregatePrerequisitesValidated"])
            self.assertFalse(result["finiteLifecycle4Validated"])


if __name__ == "__main__":
    unittest.main()
