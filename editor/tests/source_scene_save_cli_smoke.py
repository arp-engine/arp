"""Actual Interpreter CLI: mounted v4 Scene save/conflict and process restart."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
import time

from project_io_cli_smoke import run


def terminal(invoke, state, accepted, expected="succeeded"):
    task = accepted["snapshot"]["scenePersistenceTask"]["taskId"]
    assert task
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if not state()["sceneIoPending"]:
            result = invoke("scene.task.get", str(task))["snapshot"]
            assert result["scenePersistenceTask"]["status"] == expected, result
            return result
        time.sleep(0.02)
    raise AssertionError("scene source task deadline")


def root_object(invoke):
    return next(entry for entry in invoke("scene.hierarchy.get")["sceneHierarchy"]["entries"]
                if entry["parentId"] == 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve() if args.output else None
    with tempfile.TemporaryDirectory(prefix="arp-source-scene-save-") as directory:
        project = Path(directory) / "project"
        subprocess.run([str(args.fixture.resolve()), "--emit", str(project)], check=True, timeout=40)
        path = project / "workspace/cli-copy.yaml"

        def save_workflow(invoke, state):
            terminal(invoke, state, invoke("scene.load", "scene.yaml", "discard"))
            root = str(root_object(invoke)["objectId"])
            invoke("scene.rename", root, "CLI saved scripted scene")
            accepted = invoke("scene.save", "scene.yaml")
            assert not accepted["snapshot"]["sceneSaveReceipt"]["submitted"]
            saved = terminal(invoke, state, accepted)
            assert saved["sceneSaveReceipt"]["published"] and not saved["sceneDirty"]
            saved = terminal(invoke, state, invoke("scene.save", "cli-copy.yaml"))
            receipt = saved["sceneSaveReceipt"]
            assert receipt["commitState"] == "committed" and receipt["published"]
            assert bytes(receipt["versionDigest"]) == hashlib.sha256(path.read_bytes()).digest()
            external = path.read_bytes() + b"\n# external CLI smoke writer\n"
            path.write_bytes(external)
            invoke("scene.rename", root, "Keep local CLI scene")
            conflict = terminal(invoke, state, invoke("scene.save", "cli-copy.yaml"), "failed")
            assert conflict["sceneDirty"] and not conflict["sceneSaveReceipt"]["published"]
            assert conflict["sceneSaveReceipt"]["commitState"] == "not_committed"
            assert root_object(invoke)["name"] == "Keep local CLI scene" and path.read_bytes() == external
            terminal(invoke, state, invoke("scene.load", "cli-copy.yaml", "discard"))
            root = str(root_object(invoke)["objectId"])
            invoke("scene.rename", root, "CLI scene restored after restart")
            final = terminal(invoke, state, invoke("scene.save", "cli-copy.yaml"))
            assert not final["sceneDirty"] and final["sceneSaveReceipt"]["published"]
            assert bytes(final["sceneSaveReceipt"]["versionDigest"]) == hashlib.sha256(path.read_bytes()).digest()

        def reopen_workflow(invoke, state):
            loaded = terminal(invoke, state, invoke("scene.load", "cli-copy.yaml", "discard"))
            assert root_object(invoke)["name"] == "CLI scene restored after restart"
            assert not loaded["sceneDirty"] and loaded["scenePath"] == "cli-copy.yaml"

        run(args.editor.resolve(), args.cli.resolve(), project,
            output / "save" if output else None, save_workflow, source_writes=True)
        run(args.editor.resolve(), args.cli.resolve(), project,
            output / "restart" if output else None, reopen_workflow)
    print("PASS actual Interpreter CLI v4 source Scene save, conflict and process restart")


if __name__ == "__main__":
    main()
