"""Check authoring ownership against the generated Ninja dependency graph."""
import argparse
import subprocess
import unittest


parser = argparse.ArgumentParser()
parser.add_argument("--ninja", required=True)
parser.add_argument("--build-dir", required=True)
parser.add_argument("--engine-target", required=True)
parser.add_argument("--editor", choices=("enabled", "disabled"), required=True)
arguments = parser.parse_args()


class ProjectBuildBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        command = [arguments.ninja, "-C", arguments.build_dir, "-t"]
        cls.targets = subprocess.check_output(
            [*command, "targets", "all"], text=True, timeout=30)
        cls.inputs = subprocess.check_output(
            [*command, "inputs", arguments.engine_target], text=True, timeout=30)

    def test_authoring_target_obeys_editor_option(self):
        self.assertEqual("arp_editor_project:" in self.targets,
                         arguments.editor == "enabled")
        if arguments.editor == "disabled":
            self.assertNotIn("arp_editor_project", self.targets)

    def test_engine_has_no_project_authoring_dependency(self):
        forbidden = ("/editor/project/", "/editor/tools/project_migrate",
                     "arp_editor_project", "/src/engine/project/")
        inputs = ["/" + line.replace("\\", "/") for line in self.inputs.splitlines()]
        self.assertFalse([line for line in inputs if any(part in line for part in forbidden)])

    def test_removed_engine_project_target_is_absent(self):
        self.assertNotIn("arp_engine_project", self.targets)


if __name__ == "__main__":
    unittest.main(argv=[__file__])
