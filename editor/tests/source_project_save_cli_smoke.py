"""Actual Interpreter CLI project source save, conflict and fresh-process reopen."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import shutil
import tempfile
import time

from project_io_cli_smoke import run
from source_scene_save_cli_smoke import terminal as scene_terminal


def terminal(state, expected="succeeded"):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        value = state()
        if value["projectIoTask"]["status"] != "pending":
            assert value["projectIoTask"]["status"] == expected, value
            return value
        time.sleep(0.02)
    raise AssertionError("project source save deadline")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve() if args.output else None
    with tempfile.TemporaryDirectory(prefix="arp-source-project-save-") as directory:
        project = Path(directory) / "project"
        subprocess.run([str(args.fixture.resolve()), "--emit", str(project)], check=True, timeout=45)
        manifest = project / "workspace/project.arp.yaml"
        # Independent text document: changing a declared module intentionally
        # invalidates a v4 Scene's explicit source fingerprint until reauthoring.
        (project / "workspace/scratch.arp").write_text("# Independent source document\n", encoding="utf-8")

        def workflow(invoke, state):
            scene_terminal(invoke, state, invoke("scene.load", "scene.yaml", "discard"))
            invoke("project.rename", "CLI saved project settings")
            accepted = invoke("project.save")["snapshot"]
            assert accepted["projectSavePending"] and not accepted["projectSaveReceipt"]["submitted"]
            saved = terminal(state)
            assert saved["projectSaveReceipt"]["published"] and not saved["projectDirty"]
            assert bytes(saved["projectSaveReceipt"]["versionDigest"]) == hashlib.sha256(manifest.read_bytes()).digest()
            external = manifest.read_bytes() + b"\n# external project writer\n"
            manifest.write_bytes(external)
            invoke("project.rename", "CLI local draft retained")
            invoke("project.save")
            failed = terminal(state, "failed")
            assert failed["projectDirty"] and not failed["projectSaveReceipt"]["published"]
            assert failed["projectName"] == "CLI local draft retained" and manifest.read_bytes() == external
            invoke("project.open.source", "workspace", "workspace", "discard")
            assert terminal(state)["projectName"] == "CLI saved project settings"
            invoke("project.settings.set", "CLI project restored after restart", "none")
            invoke("project.save")
            assert terminal(state)["projectSaveReceipt"]["published"]
            scene_terminal(invoke, state, invoke("scene.load", "scene.yaml", "discard"))
            root = next(entry["objectId"] for entry in invoke("scene.hierarchy.get")["sceneHierarchy"]["entries"]
                        if entry["parentId"] == 0)
            invoke("scene.rename", str(root), "Scene after project settings save")
            assert scene_terminal(invoke, state, invoke("scene.save", "scene.yaml"))["sceneSaveReceipt"]["published"]
            invoke("script.open", "scratch.arp")
            script = terminal(state)
            doc = str(script["activeDocumentId"])
            invoke("script.replace", doc, script["scriptText"] + "\n# saved after project settings\n")
            invoke("script.save", doc)
            assert terminal(state)["scriptSaveReceipt"]["published"]

        def restart(invoke, state):
            value = state()
            assert value["projectName"] == "CLI project restored after restart" and not value["projectDirty"]
            scene_terminal(invoke, state, invoke("scene.load", "scene.yaml", "discard"))
            root = next(entry for entry in invoke("scene.hierarchy.get")["sceneHierarchy"]["entries"]
                        if entry["parentId"] == 0)
            assert root["name"] == "Scene after project settings save"
            invoke("script.open", "scratch.arp")
            assert "# saved after project settings" in terminal(state)["scriptText"]

        run(args.editor.resolve(), args.cli.resolve(), project,
            output / "save" if output else None, workflow, source_writes=True)
        run(args.editor.resolve(), args.cli.resolve(), project,
            output / "restart" if output else None, restart)
        if output:
            shutil.copytree(project, output / "project", dirs_exist_ok=True)
    print("PASS actual Interpreter CLI project settings save, conflict, scene/script continuation and restart")


if __name__ == "__main__":
    main()
