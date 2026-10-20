"""Synthetic missing/stale/substituted input tests; no compiler or GPU proof."""
import copy
import json
import os
import pathlib
import tempfile
import unittest

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES
from bindless_webgpu_probe_archive_audit import OBJECTS
from bindless_webgpu_probe_build_evidence import TARGET
from bindless_webgpu_probe_dependency_evidence import selected_dependencies, replay


class ProbeDependencyEvidenceTest(unittest.TestCase):
    def inventory(self, paths, status="VALID"):
        return {label: target + f": #deps {len(paths)}, deps mtime 42 ({status})\n"
                + "".join("    " + str(path) + "\n" for path in paths)
                for label, target in OBJECTS.items()}

    def test_resolved_shared_inputs_and_missing_object_reject(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).resolve()
            source = root / "source.cpp"; source.write_text("source", encoding="utf-8")
            inventories = self.inventory([source])
            mtimes = {label: source.stat().st_mtime_ns for label in OBJECTS}
            self.assertEqual(selected_dependencies(root, inventories, mtimes), {source: list(OBJECTS)})
            del inventories["main"]
            with self.assertRaisesRegex(ValueError, "OBJECT-SET"):
                selected_dependencies(root, inventories, mtimes)

    def test_missing_stale_duplicate_and_newer_input_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).resolve()
            source = root / "source.cpp"; source.write_text("source", encoding="utf-8")
            mtimes = {label: source.stat().st_mtime_ns for label in OBJECTS}
            for paths, status, diagnostic in (([root / "missing.h"], "VALID", "MISSING-SOURCE"),
                    ([source], "STALE", "STALE-INVENTORY"),
                    ([source, source], "VALID", "DUPLICATE-SOURCE")):
                with self.subTest(diagnostic=diagnostic), self.assertRaisesRegex(ValueError, diagnostic):
                    selected_dependencies(root, self.inventory(paths, status), mtimes)
            os.utime(source, ns=(mtimes["main"] + 1000000, mtimes["main"] + 1000000))
            with self.assertRaisesRegex(ValueError, "NEWER-THAN-OBJECT"):
                selected_dependencies(root, self.inventory([source]), mtimes)

    def fixture(self, root):
        archive = root / "archive"; archive.mkdir()
        build = root / "original-build-absent"
        dependency = build / "include/shared.h"
        inventories = self.inventory([dependency])
        texts = {dependency: "synthetic-header", **{build / target: "synthetic-object" for target in OBJECTS.values()}}
        texts.update({build / (TARGET + suffix): "synthetic-payload" for suffix in (".js", ".wasm", ".data")})
        log = "# ninja log v7\n" + "".join("0\t1\t42\t" + target + "\tabcd\n" for target in OBJECTS.values())
        log += "0\t1\t43\t" + TARGET + ".js\tabcd\n"
        texts[build / ".ninja_log"] = log
        rows = []
        for index, (source, text) in enumerate(texts.items()):
            relative = f"files/{index}/{source.name}"
            path = archive / relative; path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
            rows.append(dict(source=str(source), path=relative, observedMtimeNs=1 if source == dependency else 42,
                             dependencyOwners=list(OBJECTS) if source == dependency else [], **fingerprint(path)))
        payload = {TARGET + suffix: fingerprint(archive / row["path"])
                   for suffix in (".js", ".wasm", ".data") for row in rows
                   if pathlib.Path(row["source"]).name == TARGET + suffix}
        manifest = dict(contract="SelectedProbeDependencyBytes/1", complete=True,
            logicalBuildRoot=str(build), files=rows, inventories=inventories,
            objectMtimes={label: 42 for label in OBJECTS}, uniqueDependencies=1, payload=payload,
            originalCompilerInputBytesVerified=False, sdkCompilerAndLinkInputsVerified=False,
            transitiveSdkProvenanceComplete=False, gpuExecuted=False, **CLOSED_GATES)
        return archive, manifest

    def write(self, archive, manifest):
        (archive / "validation.json").write_text(json.dumps(manifest), encoding="utf-8")

    def test_archive_replay_uses_copied_inputs_with_original_tree_absent(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, manifest = self.fixture(pathlib.Path(directory).resolve())
            self.write(archive, manifest)
            result = replay(archive)
            self.assertTrue(result["complete"])
            self.assertEqual(result["uniqueDependencies"], 1)
            self.assertFalse(result["liveSourcesRead"])
            self.assertFalse(result["transitiveSdkProvenanceComplete"])

    def test_missing_bytes_owner_mutation_or_incomplete_payload_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, manifest = self.fixture(pathlib.Path(directory).resolve())
            for edit, diagnostic in (
                    (lambda m: m["files"].pop(0), "ARCHIVE-MISSING"),
                    (lambda m: m["files"][0].update(dependencyOwners=["main"]), "ARCHIVE-OWNER"),
                    (lambda m: m.update(payload={}), "ARCHIVE-SHAPE"),
                    (lambda m: m["files"][0].update(observedMtimeNs=43), "NEWER-THAN-OBJECT")):
                changed = copy.deepcopy(manifest); edit(changed); self.write(archive, changed)
                with self.subTest(diagnostic=diagnostic), self.assertRaisesRegex(ValueError, diagnostic):
                    replay(archive)

    def test_tampered_bytes_and_sdk_or_execution_promotion_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, manifest = self.fixture(pathlib.Path(directory).resolve())
            for field in ("transitiveSdkProvenanceComplete", "originalCompilerInputBytesVerified",
                          "publicBindlessExecutionAvailable", "productionReady"):
                changed = copy.deepcopy(manifest); changed[field] = True; self.write(archive, changed)
                with self.subTest(field=field), self.assertRaisesRegex(ValueError, "ARCHIVE-GATE"):
                    replay(archive)
            self.write(archive, manifest)
            (archive / manifest["files"][0]["path"]).write_text("substitution", encoding="utf-8")
            with self.assertRaises(ValueError):
                replay(archive)

    def test_malformed_rows_unknown_object_and_missing_inventory_log_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, manifest = self.fixture(pathlib.Path(directory).resolve())
            for edit, diagnostic in (
                    (lambda m: m["files"][0].update(dependencyOwners=[{}]), "ARCHIVE-ROW"),
                    (lambda m: m["files"][0].update(observedMtimeNs=True), "ARCHIVE-ROW"),
                    (lambda m: m["inventories"].update(foreign="forged"), "ARCHIVE-SHAPE"),
                    (lambda m: m["files"].pop(), "ARCHIVE-MISSING")):
                changed = copy.deepcopy(manifest); edit(changed); self.write(archive, changed)
                with self.subTest(diagnostic=diagnostic), self.assertRaisesRegex(ValueError, diagnostic):
                    replay(archive)
            self.write(archive, [])
            with self.assertRaisesRegex(ValueError, "ARCHIVE-CONTRACT"):
                replay(archive)


if __name__ == "__main__":
    unittest.main()
