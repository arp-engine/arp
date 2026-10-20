"""Copied evidence and package provenance tests; synthetic traces grant no GPU proof."""
import contextlib
import hashlib
import io
import json
import pathlib
import shutil
import tempfile
import types
import unittest
from unittest import mock

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import audit, read_json
from bindless_finite_lifecycle_predecessor_evidence import PACKAGES, COHORTS, bind_packages, inspect_audited_stage


class PredecessorEvidenceTest(unittest.TestCase):
    def test_final_stage_relocation_uses_verified_cache_source_labels(self):
        from bindless_finite_lifecycle_cohort_archive_test import CohortArchiveTest
        import bindless_finite_lifecycle_predecessor_evidence as predecessor
        with tempfile.TemporaryDirectory() as directory:
            unused, stage = CohortArchiveTest.stage_fixture(pathlib.Path(directory), "compute-final")
            with mock.patch.dict(predecessor.inspect_snapshot.__globals__, ROOT=pathlib.Path(directory) / "relocated-reader"):
                result = inspect_audited_stage(stage)
            self.assertEqual(result["cases"], 210)
            self.assertTrue(result["shaderPackagesBound"])

    def test_rehashed_forged_final_source_root_cannot_borrow_other_source_labels(self):
        from bindless_finite_lifecycle_cohort_archive_test import CohortArchiveTest
        with tempfile.TemporaryDirectory() as directory:
            original, unused = CohortArchiveTest.stage_fixture(pathlib.Path(directory), "compute-final")
            path = original / "validation.json"; manifest = read_json(path)
            entry = next(row for row in manifest["inputs"] if pathlib.Path(row["source"]).name == "CMakeCache.txt")
            archived = original / entry["path"]
            lines = archived.read_text(encoding="utf-8").splitlines()
            lines = ["CMAKE_HOME_DIRECTORY:INTERNAL=" + str(pathlib.Path(directory).resolve() / "forged")
                     if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=") else line for line in lines]
            archived.write_text("\n".join(lines) + "\n", encoding="utf-8")
            from bindless_finite_lifecycle_acceptance import fingerprint
            entry.update(fingerprint(archived)); path.write_text(json.dumps(manifest), encoding="utf-8")
            (original / "inputs.json").write_text(json.dumps(manifest["inputs"]), encoding="utf-8")
            stage = pathlib.Path(directory) / "forged-stage"
            with contextlib.redirect_stdout(io.StringIO()):
                audit(types.SimpleNamespace(snapshot=original, output=stage, compare_snapshot=None, verify_live=False))
            with self.assertRaises(ValueError):
                inspect_audited_stage(stage)

    @staticmethod
    def package_fixture(root, payload=b"testOnly: true\nnoGpuAuthority: true\n"):
        inputs = []
        identity = hashlib.sha256(payload).hexdigest()
        report = {}
        for index, (filename, field) in enumerate(PACKAGES.items()):
            original = root / "not-an-archive" / str(index)
            parent = root / "inputs" / str(index)
            parent.mkdir(parents=True)
            for name, data in (("payload.yaml", payload), (filename,
                ("type: BindlessPackagePublication\ncontractVersion: 1\npayload: payload.yaml\nshaderPackageIdentity: "
                 + identity + "\n").encode())):
                path = parent / name; path.write_bytes(data)
                inputs.append(dict(source=str(original / name), path=str(path.relative_to(root)), **fingerprint(path)))
            report[field] = identity
        path = root / "report.json"; path.write_text(json.dumps(report), encoding="utf-8")
        manifest = dict(inputs=inputs, results=[dict(report=dict(path="report.json", **fingerprint(path)))])
        return manifest, identity

    @staticmethod
    def stage_fixture(root):
        from bindless_finite_lifecycle_evidence_test import FiniteArchiveEvidenceTest
        original = root / "original"; original.mkdir()
        FiniteArchiveEvidenceTest().snapshot(original)
        stage = root / "audit"
        with contextlib.redirect_stdout(io.StringIO()):
            audit(types.SimpleNamespace(snapshot=original, output=stage, compare_snapshot=None, verify_live=False))
        return original, stage

    def test_cohorts_partition_exact_204_terminals_and_six_numerical_cases(self):
        cases = expected_cases("compute-final")
        represented = set()
        counts = {}
        for cohort, groups in COHORTS.items():
            names = {name for name, identity in cases.items() if identity[0] in groups}
            self.assertTrue(names.isdisjoint(represented))
            represented.update(names); counts[cohort] = len(names)
        self.assertEqual(represented, set(cases))
        self.assertEqual(counts, dict(Execution7=96, Recording4=24, Submission4=48, Claim4=30, PostSuppression4=12))

    def test_all_four_driver_identities_bind_to_archived_payload_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest, identity = self.package_fixture(pathlib.Path(directory))
            self.assertEqual(bind_packages(pathlib.Path(directory), manifest),
                             {filename: identity for filename in PACKAGES})

    def test_unfrozen_publications_are_not_aggregate_eligible(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); manifest, unused = self.package_fixture(root)
            manifest["inputs"] = [entry for entry in manifest["inputs"] if entry["source"].endswith("payload.yaml")]
            self.assertIsNone(bind_packages(root, manifest))

    def test_any_driver_package_substitution_refuses_even_with_a_rehashed_report(self):
        for field in PACKAGES.values():
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); manifest, unused = self.package_fixture(root)
                path = root / "report.json"; report = read_json(path); report[field] = "f" * 64
                path.write_text(json.dumps(report), encoding="utf-8")
                manifest["results"][0]["report"].update(fingerprint(path))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    bind_packages(root, manifest)

    def test_rehashed_payload_cannot_borrow_the_old_publication_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); manifest, unused = self.package_fixture(root)
            entry = next(entry for entry in manifest["inputs"] if entry["source"].endswith("payload.yaml"))
            path = root / entry["path"]; path.write_bytes(b"testOnly: changed\n")
            entry.update(fingerprint(path))
            with self.assertRaises(ValueError):
                bind_packages(root, manifest)

    def test_missing_archived_payload_never_reads_a_live_original_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); manifest, unused = self.package_fixture(root)
            removed = next(entry for entry in manifest["inputs"] if entry["source"].endswith("payload.yaml"))
            original = pathlib.Path(removed["source"]); original.parent.mkdir(parents=True)
            original.write_bytes((root / removed["path"]).read_bytes())
            manifest["inputs"].remove(removed)
            with self.assertRaises(OSError):
                bind_packages(root, manifest)

    def test_copied_claim_replays_after_original_archive_is_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            original, stage = self.stage_fixture(pathlib.Path(directory))
            shutil.rmtree(original)
            result = inspect_audited_stage(stage)
            self.assertEqual(result["cases"], 30)
            self.assertEqual(len(result["cohorts"]["Claim4"]), 30)
            self.assertFalse(result["complete"])
            self.assertFalse(result["eligibleForAggregatePredecessor"])
            self.assertFalse(result["finiteLifecycle4Validated"])
            self.assertFalse(result["publicBindlessExecutionAvailable"])

    def test_changed_artifact_duplicated_source_or_forged_semantic_profile_refuses(self):
        for mutation in ("artifact", "duplicate", "profile", "gate", "count"):
            with tempfile.TemporaryDirectory() as directory:
                unused, stage = self.stage_fixture(pathlib.Path(directory))
                path = stage / "validation.json"; manifest = read_json(path)
                if mutation == "artifact":
                    artifact = stage / manifest["artifacts"][0]["path"]
                    artifact.write_bytes(artifact.read_bytes() + b" changed")
                if mutation == "duplicate":
                    manifest["artifacts"].append(manifest["artifacts"][0]); manifest["artifactCount"] += 1
                if mutation == "profile": manifest["semanticProfiles"] = {}
                if mutation == "gate": manifest["finiteLifecycle4Validated"] = True
                if mutation == "count": manifest["artifactCount"] -= 1
                path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    inspect_audited_stage(stage)


if __name__ == "__main__":
    unittest.main()
