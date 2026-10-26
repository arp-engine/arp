"""Exercise the Editor's actual v2 -> v3 codec and exclusive bundle publisher."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

import yaml

TOOL = pathlib.Path(sys.argv.pop(1)).resolve()
IDENTITY = "a75b3294-d283-4b12-92d8-7d522b14eae2"


class SceneMigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="arp-scene-migrate-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / "制作场景-v2.yaml"
        self.original = "type: arp.editor-scene\ncontractVersion: 2\nname: 原场景\nobjects: []\nselected: 0\n"
        self.source.write_text(self.original, encoding="utf-8")
        self.output = self.root / "独立迁移结果"

    def run_tool(self, identity=IDENTITY):
        return subprocess.run([str(TOOL), str(self.source), str(self.output),
            "--runtime-scene", identity], capture_output=True, text=True,
            encoding="utf-8", timeout=20)

    def test_explicit_identity_and_preserve_source(self):
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        document = yaml.safe_load((self.output / "scene.arp.yaml").read_text(encoding="utf-8"))
        self.assertEqual(document["contractVersion"], 3)
        self.assertEqual(document["runtimeSceneId"], IDENTITY)
        self.assertEqual(document["name"], "原场景")
        self.assertEqual(self.source.read_text(encoding="utf-8"), self.original)

    def test_none_is_explicitly_unbound(self):
        result = self.run_tool("none")
        self.assertEqual(result.returncode, 0, result.stderr)
        document = yaml.safe_load((self.output / "scene.arp.yaml").read_text(encoding="utf-8"))
        self.assertIsNone(document["runtimeSceneId"])

    def test_existing_destination_is_never_changed(self):
        self.output.mkdir()
        sentinel = self.output / "scene.arp.yaml"
        sentinel.write_text("keep this file", encoding="utf-8")
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep this file")
        self.assertEqual(self.source.read_text(encoding="utf-8"), self.original)

    def test_invalid_identity_and_source_never_publish(self):
        for identity in ["not-a-uuid", "", "../scene", "场景"]:
            with self.subTest(identity=identity):
                self.assertNotEqual(self.run_tool(identity).returncode, 0)
                self.assertFalse(self.output.exists())
        for value in [self.original + "unknown: 2\n",
                      self.original.replace("objects: []", "objects: invalid"),
                      self.original.replace("selected: 0", "selected: 1"),
                      self.original + "runtimeSceneId: null\n",
                      self.original + "name: duplicate\n"]:
            with self.subTest(source=value):
                self.source.write_text(value, encoding="utf-8")
                self.assertNotEqual(self.run_tool().returncode, 0)
                self.assertFalse(self.output.exists())
                self.assertEqual(self.source.read_text(encoding="utf-8"), value)

    def test_other_versions_and_runtime_asset_type_rejected(self):
        for version in [1, 3, 99]:
            value = self.original.replace("contractVersion: 2", f"contractVersion: {version}")
            self.source.write_text(value, encoding="utf-8")
            self.assertNotEqual(self.run_tool().returncode, 0)
            self.assertFalse(self.output.exists())
        self.source.write_text(self.original.replace("arp.editor-scene", "arp.scene-asset"), encoding="utf-8")
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_identity_argument_must_be_explicit(self):
        result = subprocess.run([str(TOOL), str(self.source), str(self.output)],
            capture_output=True, text=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
