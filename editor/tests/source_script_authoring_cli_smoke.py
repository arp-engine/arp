"""Actual Interpreter CLI source preparation, attachment, fields and exact history."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time

from project_io_cli_smoke import run

RESOURCE = "00000000-0000-4000-8000-000000000096"


def workflow(invoke, state):
    def finish():
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            task = state()["projectIoTask"]
            if task["status"] != "pending":
                assert task["status"] == "succeeded", task
                return
            time.sleep(0.02)
        raise AssertionError("source authoring timeout")

    def apply(*arguments):
        accepted = invoke(*arguments)
        assert accepted["snapshot"]["projectIoTask"]["taskId"] > 0, accepted
        finish()

    def components():
        return invoke("scene.inspector.selection.get")["sceneInspectorSelection"]["objects"][0]["components"]

    invoke("activate", "workspace_3D")
    accepted = invoke("scene.load", "plain.yaml")
    task = accepted["snapshot"]["scenePersistenceTask"]["taskId"]
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        receipt = invoke("scene.task.get", str(task))["snapshot"]["scenePersistenceTask"]
        if receipt["status"] != "pending":
            assert receipt["status"] == "succeeded", receipt
            break
        time.sleep(0.02)
    else:
        raise AssertionError("source Scene timeout")
    root = next(e["objectId"] for e in invoke("scene.hierarchy.get")["sceneHierarchy"]["entries"]
                if e["parentId"] == 0)
    invoke("scene.select", str(root))
    apply("scene.script.prepare")
    before = {c["componentId"] for c in components()}
    apply("scene.script.add", str(root), RESOURCE, "Mover", "1", "false", "amount: 11\n")
    after = {c["componentId"] for c in components()}
    attached = after - before
    assert len(attached) == 1, (before, after)
    component = next(iter(attached))
    apply("scene.script.fields.set", str(root), str(component), RESOURCE, "Mover", "1", "true", "amount: 35\n")
    apply("scene.undo")
    apply("scene.redo")
    apply("scene.component.remove", str(root), str(component))
    assert {c["componentId"] for c in components()} == before
    apply("scene.undo")
    assert {c["componentId"] for c in components()} == after
    apply("scene.redo")
    assert {c["componentId"] for c in components()} == before
    apply("scene.undo")
    assert {c["componentId"] for c in components()} == after
    assert state()["sceneDirty"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="arp-source-authoring-cli-") as folder:
        subprocess.run([str(args.fixture.resolve()), "--emit", folder], check=True, timeout=30)
        run(args.editor.resolve(), args.cli.resolve(), Path(folder), args.output, workflow, discard_exit=True)
    print("PASS Interpreter source prepare/add/fields/undo/redo/remove/exact restore")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
