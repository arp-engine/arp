"""Exercise the real offline converter; no parser or filesystem mock."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

TOOL = pathlib.Path(sys.argv.pop(1)).resolve()


class ProjectMigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="arp-project-migrate-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / "legacy.yaml"
        self.original = "format: ArpEditorProject\nversion: 1\nassetPackages: [Assets/base.arpkg]\n"
        self.source.write_text(self.original, encoding="utf-8")
        self.output = self.root / "converted"

    def run_tool(self, *extra, entry="none"):
        return subprocess.run([str(TOOL), str(self.source), str(self.output),
            "--project-id", "a75b3294-d283-4b12-92d8-7d522b14eae2",
            "--name", "迁移工程", "--engine-contract", "migration-fixture/1",
            "--entry-scene", entry, *extra], capture_output=True, text=True,
            encoding="utf-8", errors="strict", timeout=20)

    def test_converts_without_changing_source_or_existing_destination(self):
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        converted = (self.output / "project.arp.yaml").read_text(encoding="utf-8")
        self.assertIn('"Interpreter"', converted)
        self.assertIn('"Assets/base.arpkg"', converted)
        self.assertIn('迁移工程', converted)
        self.assertEqual(self.source.read_text(encoding="utf-8"), self.original)
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertEqual((self.output / "project.arp.yaml").read_text(encoding="utf-8"), converted)

    def test_requires_explicit_metadata_destination_and_preserves_previews(self):
        self.source.write_text(self.original + "terrainPreviews: [terrain/preview.yaml]\n", encoding="utf-8")
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertFalse(self.output.exists())
        result = self.run_tool("--editor-metadata", ".arp/editor-project.yaml")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"terrain/preview.yaml"', (self.output / ".arp/editor-project.yaml").read_text(encoding="utf-8"))
        self.assertIn('".arp/editor-project.yaml"', (self.output / "project.arp.yaml").read_text(encoding="utf-8"))

    def test_invalid_input_and_identity_never_publish(self):
        result = self.run_tool(entry="guessed.scene.yaml")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PROJECT-ENTRY-SCENE-INVALID", result.stderr)
        self.assertFalse(self.output.exists())
        self.source.write_text(self.original + "unknown: true\n", encoding="utf-8")
        self.assertNotEqual(self.run_tool().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_metadata_cannot_replace_manifest_or_escape_bundle(self):
        self.source.write_text(self.original + "terrainPreviews: []\n", encoding="utf-8")
        for path in ["project.arp.yaml", "../outside.yaml"]:
            with self.subTest(path=path):
                result = self.run_tool("--editor-metadata", path)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.output.exists())
                self.assertFalse(pathlib.Path(str(self.output) + ".arp-staging").exists())

    def test_unicode_source_and_destination_paths(self):
        self.source = self.root / '原项目.yaml'
        self.source.write_text(self.original, encoding='utf-8')
        self.output = self.root / '转换结果'
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.output / 'project.arp.yaml').is_file())
        self.assertEqual(self.source.read_text(encoding='utf-8'), self.original)

    def test_duplicate_preview_paths_are_rejected_before_publication(self):
        self.source.write_text(self.original + "terrainPreviews: [a.yaml, a.yaml]\n", encoding="utf-8")
        result = self.run_tool("--editor-metadata", ".arp/editor-project.yaml")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PROJECT-METADATA-PATH-DUPLICATE", result.stderr)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
