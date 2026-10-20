"""Synthetic offline archive contracts; no genuine native evidence or GPU work."""
import copy
import json
import pathlib
import subprocess
import sys
import unittest

import bindless_frozen_reference_archive_audit_test as fixture_contract
import bindless_frozen_reference_stage_archive as subject
from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json


class FrozenReferenceStageArchiveTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture_contract.FrozenReferenceSourceAuditTests(methodName="runTest")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.original = self.fixture.fixture
        self.output = self.original.root / "archive"

    def freeze(self, native=False):
        return subject.FrozenReferenceStageArchive.freeze(self.original.output,
            self.original.reference, self.original.materialized, self.output, native)

    def replay(self):
        return subject.FrozenReferenceStageArchive.replay(self.output)

    def change_manifest(self, edit):
        path = self.output / "validation.json"
        value = read_json(path)
        edit(value)
        path.write_text(json.dumps(value), encoding="utf-8")

    def remove_original_namespaces(self):
        # Retain the synthetic inputs under distinct names. Every target is a
        # verified child of this test's temporary root; no repository is moved.
        for path in (self.original.output, self.original.reference, self.original.materialized):
            self.assertTrue(path.resolve().is_relative_to(self.original.root))
            path.rename(path.with_name(path.name + "-preserved"))

    def test_namespace_archive_replays_after_all_original_namespaces_are_absent(self):
        result = self.freeze()
        self.assertEqual(result["contract"], subject.NAMESPACE_CONTRACT)
        self.assertIs(result["nativeAcceptanceInspected"], False)
        self.assertEqual(result["nativeCasesInspected"], 0)
        self.remove_original_namespaces()
        self.assertEqual(self.replay(), result)
        self.assertFalse((self.output / "snapshot/attempt.json").exists())
        self.assertFalse((self.output / "snapshot/validation.json").exists())
        for gate in (*CLOSED_GATES, "eligibleForAggregatePredecessor", "aggregatePrerequisitesValidated"):
            self.assertIs(result[gate], False)

    def test_complete_synthetic_stage_replays_through_canonical_checks_offline(self):
        self.fixture.complete_synthetic_snapshot()
        result = self.freeze(True)
        self.assertEqual(result["contract"], subject.STAGE_CONTRACT)
        self.assertEqual(result["nativeCasesInspected"], 30)
        self.remove_original_namespaces()
        self.assertEqual(self.replay(), result)

    def test_independent_copied_tool_process_needs_no_original_paths(self):
        self.fixture.complete_synthetic_snapshot()
        self.freeze(True)
        self.remove_original_namespaces()
        process = subprocess.run([sys.executable, "-X", "utf8",
            str(self.output / "tools/bindless_frozen_reference_stage_archive.py"),
            "replay", "--archive", str(self.output)], capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(process.returncode, 0, process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual(result["nativeCasesInspected"], 30)
        self.assertIs(result["publicBindlessExecutionAvailable"], False)

    def test_incomplete_native_attempt_cannot_create_complete_stage_archive(self):
        with self.assertRaises(FileNotFoundError):
            self.freeze(True)
        self.assertFalse(self.output.exists())

    def test_inputs_only_archive_cannot_promote_itself_to_complete_stage(self):
        self.freeze()
        self.change_manifest(lambda data: data.update(contract=subject.STAGE_CONTRACT,
                                                       nativeAcceptanceInspected=True))
        with self.assertRaises(FileNotFoundError):
            self.replay()

    def test_adding_undeclared_acceptance_to_inputs_only_archive_refuses(self):
        self.freeze()
        (self.output / "snapshot/validation.json").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "UNDECLARED-NATIVE-STAGE"):
            self.replay()

    def test_promoted_and_coerced_gates_refuse(self):
        self.freeze()
        original = read_json(self.output / "validation.json")
        for field, value in (("complete", 1), ("nativeAcceptanceInspected", 0),
                             ("publicBindlessExecutionAvailable", True),
                             ("eligibleForAggregatePredecessor", True)):
            data = dict(original, **{field: value})
            (self.output / "validation.json").write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(ValueError):
                self.replay()

    def test_missing_duplicate_or_escaped_archive_member_refuses(self):
        self.freeze()
        original = read_json(self.output / "validation.json")
        for edit in (lambda data: data["artifacts"].pop(),
                     lambda data: data["artifacts"].append(copy.deepcopy(data["artifacts"][0])),
                     lambda data: data["artifacts"][0].update(path="../outside")):
            data = copy.deepcopy(original)
            edit(data)
            data["artifactCount"] = len(data["artifacts"])
            (self.output / "validation.json").write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(ValueError):
                self.replay()

    def test_original_copy_corruption_cannot_be_filled_from_live_reference(self):
        self.freeze()
        manifest = read_json(self.output / "validation.json")
        entry = next(item for item in manifest["artifacts"] if item["path"].startswith("reference/"))
        (self.output / entry["path"]).write_bytes(b"corrupted copy")
        with self.assertRaises(ValueError):
            self.replay()

    def test_reauthored_artifact_source_cannot_replace_recorded_owner(self):
        self.freeze()
        self.change_manifest(lambda data: data["artifacts"][0].update(
            source=str(self.original.root / "forged-source")))
        with self.assertRaisesRegex(ValueError, "SOURCE-IDENTITY"):
            self.replay()

    def test_changed_tool_with_reauthored_hash_still_refuses(self):
        self.freeze()
        name = "tools/bindless_finite_lifecycle_archive_audit.py"
        path = self.output / name
        path.write_bytes(path.read_bytes() + b"\n# forged inspector\n")

        def edit(data):
            next(item for item in data["artifacts"] if item["path"] == name).update(fingerprint(path))

        self.change_manifest(edit)
        with self.assertRaisesRegex(ValueError, "LOADED-TOOL"):
            self.replay()

    def test_wrong_roots_and_duplicate_validator_sources_refuse(self):
        self.freeze()
        original = read_json(self.output / "validation.json")
        for edit in (lambda data: data.update(referenceRoot=str(self.original.root / "forged")),
                     lambda data: data["mapping"].update(mappedRoot=str(self.original.root / "forged")),
                     lambda data: data["validatorSources"].update(
                         bindless_finite_lifecycle_archive_audit=data["validatorSources"]["bindless_finite_lifecycle_acceptance"])):
            data = copy.deepcopy(original)
            edit(data)
            (self.output / "validation.json").write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(ValueError):
                self.replay()

    def test_native_profiles_cannot_replace_copied_report_semantics(self):
        self.fixture.complete_synthetic_snapshot()
        self.freeze(True)
        self.change_manifest(lambda data: data.update(semanticProfiles={}))
        with self.assertRaisesRegex(ValueError, "SEMANTIC-PROFILES"):
            self.replay()

    def test_complete_archive_copied_report_corruption_refuses(self):
        self.fixture.complete_synthetic_snapshot()
        self.freeze(True)
        path = next((self.output / "snapshot/cases").glob("*/report.json"))
        path.write_text("{}", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.replay()

    def test_outputs_cannot_overlap_inputs_or_replace_an_earlier_failure(self):
        for owner in (self.original.output, self.original.reference, self.original.materialized, self.original.source):
            with self.assertRaisesRegex(ValueError, "OUTPUT-OWNER"):
                subject.FrozenReferenceStageArchive.freeze(self.original.output,
                    self.original.reference, self.original.materialized, owner / "new")
            self.assertFalse((owner / "new").exists())
        self.output.mkdir()
        marker = self.output / "prior-failed-attempt"
        marker.write_text("preserve", encoding="utf-8")
        with self.assertRaises(FileExistsError):
            self.freeze()
        self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")


if __name__ == "__main__":
    unittest.main()
