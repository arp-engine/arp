"""Synthetic reference publication/cohort bridge; never native execution proof."""
import contextlib
import copy
import io
import json
import pathlib
import shutil
import subprocess
import sys
import unittest

import bindless_frozen_reference_stage_archive_test as stage_fixtures
import bindless_finite_lifecycle_predecessor_evidence_test as package_fixtures
import bindless_finite_lifecycle_cohort_archive_test as cohort_fixtures
from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import read_json, CLOSED_GATES
from bindless_finite_lifecycle_predecessor_evidence import inspect_audited_stage, PACKAGES
from bindless_finite_lifecycle_cohort_archive import freeze, replay, inspect_source


class FrozenReferencePredecessorTests(unittest.TestCase):
    def setUp(self):
        self.fixture = stage_fixtures.FrozenReferenceStageArchiveTests(methodName="runTest")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.original = self.fixture.original
        self.cohort = self.original.root / "cohort"

    def add_reference_inputs(self, extra):
        originals = read_json(self.original.reference / "inputs.json")
        metadata = read_json(self.original.materialized / "reference-manifest.json")
        for relative, data in extra.items():
            source = self.original.source / relative
            materialized = self.original.materialized / relative
            materialized.parent.mkdir(parents=True, exist_ok=True)
            materialized.write_bytes(data)
            archived = self.original.reference / "inputs" / str(len(originals)) / materialized.name
            archived.parent.mkdir(parents=True)
            shutil.copyfile(materialized, archived)
            stamp = fingerprint(materialized)
            originals.append(dict(source=str(source), path=str(archived.relative_to(self.original.reference)), **stamp))
            metadata["inputs"].append(dict(originalSource=str(source), path=relative, **stamp))
        metadata["copiedInputs"] = len(originals)
        self.original.write(self.original.reference / "inputs.json", originals)
        self.original.write(self.original.materialized / "reference-manifest.json", metadata)
        self.original.output = self.original.root / "bound-attempt"
        self.original.attempt().prepare()

    def prepare_stage(self, packages=True, changed_reader=False, changed_payload=False, stage="claim"):
        root = pathlib.Path(__file__).resolve().parents[2]
        extra = {"scripts/" + name: (root / "scripts" / name).read_bytes()
                 for name in ("bindless_codegen.py", "slang_resource_switch.py")}
        if changed_reader:
            extra["scripts/bindless_codegen.py"] += b"\n# substituted reader\n"
        identities = {}
        if packages:
            package_root = self.original.root / "synthetic-packages"
            packet, unused = package_fixtures.PredecessorEvidenceTest.package_fixture(package_root)
            identities = read_json(package_root / "report.json")
            for entry in packet["inputs"]:
                relative = "generated/shaders/reference-packages/" + pathlib.Path(entry["path"]).as_posix()
                data = (package_root / entry["path"]).read_bytes()
                if changed_payload and relative.endswith("payload.yaml"):
                    data += b"changed: true\n"
                extra[relative] = data
        self.original.stage = stage
        registrations = [dict(name=name, command=self.original.command(buffer, interval, terminal, group))
                         for name, (group, buffer, interval, terminal) in expected_cases(stage).items()]
        self.original.write(self.original.reference / "tests.json", dict(tests=registrations))
        self.add_reference_inputs(extra)
        self.fixture.fixture.complete_synthetic_snapshot()
        for name in ("validation.json", "attempt.json"):
            path = self.original.output / name
            manifest = read_json(path)
            for record in manifest["results"]:
                report_path = self.original.output / record["report"]["path"]
                report = read_json(report_path)
                report.update(identities)
                report_path.write_text(json.dumps(report), encoding="utf-8")
                report_path.with_name("driver.log").write_text(json.dumps(report), encoding="utf-8")
                record["report"].update(fingerprint(report_path))
            path.write_text(json.dumps(manifest), encoding="utf-8")

    def archive(self):
        self.fixture.freeze(True)
        return self.fixture.output

    def test_complete_stage_and_reader_proof_bind_all_four_report_identities(self):
        self.prepare_stage()
        stage = self.archive()
        result = inspect_audited_stage(stage)
        self.assertEqual(set(result["packageIdentities"]), set(PACKAGES))
        self.assertIs(result["frozenPublicationReaderVerified"], True)
        self.assertIs(result["eligibleForAggregatePredecessor"], True)
        self.assertIs(read_json(stage / "validation.json")["eligibleForAggregatePredecessor"], False)
        for gate in CLOSED_GATES:
            self.assertIs(result[gate], False)

    def test_inputs_only_namespace_refuses_even_if_publications_are_frozen(self):
        self.prepare_stage()
        self.fixture.freeze(False)
        with self.assertRaisesRegex(ValueError, "REFERENCE-NATIVE-STAGE-MISSING"):
            inspect_audited_stage(self.fixture.output)

    def test_absent_publications_leave_complete_stage_ineligible(self):
        self.prepare_stage(packages=False)
        stage = self.archive()
        result = inspect_audited_stage(stage)
        self.assertIs(result["shaderPackagesBound"], False)
        self.assertIs(result["eligibleForAggregatePredecessor"], False)
        with self.assertRaisesRegex(ValueError, "PACKAGE-PROVENANCE"):
            inspect_source(stage, "Claim4")

    def test_substituted_reader_refuses(self):
        self.prepare_stage(changed_reader=True)
        stage = self.archive()
        with self.assertRaisesRegex(ValueError, "FROZEN-READER-IDENTITY"):
            inspect_audited_stage(stage)

    def test_missing_frozen_reader_never_uses_available_live_helper(self):
        self.fixture.fixture.complete_synthetic_snapshot()
        stage = self.archive()
        with self.assertRaisesRegex(ValueError, "FROZEN-READER-IDENTITY"):
            inspect_audited_stage(stage)

    def test_changed_payload_cannot_borrow_old_publication_identity(self):
        self.prepare_stage(changed_payload=True)
        stage = self.archive()
        with self.assertRaisesRegex(ValueError, "PACKAGE-HASH-MISMATCH"):
            inspect_audited_stage(stage)

    def test_rehashed_report_package_substitution_refuses(self):
        self.prepare_stage()
        for name in ("validation.json", "attempt.json"):
            path = self.original.output / name
            manifest = read_json(path)
            record = manifest["results"][0]
            report_path = self.original.output / record["report"]["path"]
            report = read_json(report_path)
            report["publicComputePackageIdentity"] = "f" * 64
            report_path.write_text(json.dumps(report), encoding="utf-8")
            report_path.with_name("driver.log").write_text(json.dumps(report), encoding="utf-8")
            record["report"].update(fingerprint(report_path))
            path.write_text(json.dumps(manifest), encoding="utf-8")
        stage = self.archive()
        with self.assertRaises(ValueError):
            inspect_audited_stage(stage)

    def test_exact_claim_cohort_freezes_without_manufacturing_other_cases(self):
        self.prepare_stage()
        stage = self.archive()
        result = freeze(stage, "Claim4", self.cohort)
        self.assertEqual(result["cases"], 30)
        self.assertIs(result["shaderPackagesBound"], True)
        self.assertEqual(len(read_json(self.cohort / "validation.json")["replayTools"]), 13)
        with self.assertRaisesRegex(ValueError, "MISSING-CASES"):
            inspect_source(stage, "Execution7")

    def test_independent_cohort_process_replays_after_original_namespaces_disappear(self):
        self.prepare_stage()
        stage = self.archive()
        freeze(stage, "Claim4", self.cohort)
        self.fixture.remove_original_namespaces()
        self.assertTrue(stage.is_relative_to(self.original.root))
        stage.rename(stage.with_name(stage.name + "-preserved"))
        process = subprocess.run([sys.executable, "-X", "utf8",
            str(self.cohort / "tools/bindless_finite_lifecycle_cohort_archive.py"),
            "replay", "--archive", str(self.cohort)], capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(process.returncode, 0, process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual(result["cases"], 30)
        self.assertIs(result["publicBindlessExecutionAvailable"], False)

    def test_reference_cohort_compares_to_independent_legacy_final_stage(self):
        self.prepare_stage()
        freeze(self.archive(), "Claim4", self.cohort)
        with contextlib.redirect_stdout(io.StringIO()):
            unused, final = cohort_fixtures.CohortArchiveTest.stage_fixture(self.original.root / "final", "compute-final")
            result = replay(self.cohort, final)
        self.assertIs(result["finalCohortProfileCompared"], True)
        self.assertIs(result["finalPayloadRerunValidated"], False)

    def test_changed_replay_tool_list_refuses(self):
        self.prepare_stage()
        freeze(self.archive(), "Claim4", self.cohort)
        path = self.cohort / "validation.json"
        manifest = read_json(path)
        manifest["replayTools"].pop()
        path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "REPLAY-TOOLS"):
            replay(self.cohort)

    def test_full_compute_reference_projects_four_exact_cohorts_without_post_suppression(self):
        # Exercise all six numerical and 192 terminal schemas through the
        # reference namespace, not only the Claim30 fixture. Still synthetic.
        self.prepare_stage(stage="compute-default")
        stage = self.archive()
        inventory = inspect_audited_stage(stage)
        self.assertEqual(inventory["cases"], 198)
        self.assertEqual(inventory["cohorts"]["PostSuppression4"], [])
        cases = set()
        for cohort, count in (("Execution7", 96), ("Recording4", 24),
                              ("Submission4", 48), ("Claim4", 30)):
            with self.subTest(cohort=cohort):
                output = self.original.root / cohort
                result = freeze(stage, cohort, output)
                self.assertEqual(result["cases"], count)
                names = set(read_json(output / "validation.json")["identities"])
                self.assertFalse(cases & names)
                cases.update(names)
                self.assertEqual(replay(output), result)
                for gate in CLOSED_GATES:
                    self.assertIs(result[gate], False)
        self.assertEqual(cases, set(expected_cases("compute-default")))
        with self.assertRaisesRegex(ValueError, "MISSING-CASES"):
            freeze(stage, "PostSuppression4", self.original.root / "missing-post")
        self.assertFalse((self.original.root / "missing-post").exists())


if __name__ == "__main__":
    unittest.main()
