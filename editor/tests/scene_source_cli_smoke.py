"""Real Interpreter CLI commands through native source Scene admission."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

from project_io_cli_smoke import run


def workflow(invoke, state):
    def load(path):
        accepted = invoke("scene.load", path)
        task = accepted["snapshot"]["scenePersistenceTask"]["taskId"]
        assert task > 0, accepted
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            receipt = invoke("scene.task.get", str(task))["snapshot"]["scenePersistenceTask"]
            if receipt["status"] != "pending":
                assert receipt["status"] == "succeeded", receipt
                assert not state()["sceneDirty"]
                return
            time.sleep(0.02)
        raise AssertionError("Scene task timeout")

    invoke("activate", "workspace_3D")
    load("scene.yaml")
    hierarchy = invoke("scene.hierarchy.get")
    assert "Shell script root" in json.dumps(hierarchy), hierarchy
    assert "Local A" in json.dumps(hierarchy), hierarchy
    load("plain.yaml")
    assert "Mounted sky scene" in json.dumps(invoke("scene.hierarchy.get"))
    load("bom.yaml")
    assert "Shell script root" in json.dumps(invoke("scene.hierarchy.get"))
    assert state()["scenePath"] == "bom.yaml"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="arp-scene-cli-") as folder:
        subprocess.run([str(args.fixture.resolve()), "--emit", folder], check=True, timeout=30)
        run(args.editor.resolve(), args.cli.resolve(), Path(folder), args.output, workflow)
    print("PASS actual Interpreter CLI Scene v3/v4/BOM source loads and exact task queries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
