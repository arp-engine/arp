"""Real native Editor + Interpreter CLI source-open/navigation smoke."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def run(editor, cli, root, output):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    environment = os.environ.copy()
    environment["ARP_EDITOR_REMOTE_TOKEN"] = "source-io-cli-smoke-local-token-20261030"
    environment["ARP_EDITOR_REMOTE_URL"] = "http://127.0.0.1:" + str(port)
    base = environment["ARP_EDITOR_REMOTE_URL"]
    evidence = []
    def request(command, **fields):
        data = json.dumps(dict(schemaVersion=1, command=command, **fields)).encode()
        req = urllib.request.Request(base + "/api/v1/commands", data=data,
            headers={"Content-Type": "application/json", "Authorization": "Bearer " + environment["ARP_EDITOR_REMOTE_TOKEN"]})
        with urllib.request.urlopen(req, timeout=10) as result:
            return json.load(result)
    def invoke(*arguments):
        result = subprocess.run([str(cli), *arguments], env=environment,
            capture_output=True, text=True, encoding="utf-8", timeout=40)
        assert result.returncode == 0, result.stdout + result.stderr
        value = json.loads(result.stdout)
        evidence.append(dict(arguments=list(arguments), response=value))
        return value
    def state():
        value = request("editor.state.get")
        return value["snapshot"]
    def finish():
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            current = state()
            if current["projectIoTask"]["status"] != "pending":
                assert current["projectIoTask"]["status"] == "succeeded", current
                return current
            time.sleep(0.02)
        raise AssertionError("source task did not complete")
    with tempfile.TemporaryDirectory(prefix="arp-source-cli-") as logs:
        startup = None
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
        with open(Path(logs) / "editor.log", "w", encoding="utf-8") as log:
            process = subprocess.Popen([str(editor), "--smoke", "--project-root", str(root),
                "--remote-port", str(port)], env=environment, stdout=log, stderr=log,
                startupinfo=startup)
            try:
                deadline = time.monotonic() + 40
                while time.monotonic() < deadline:
                    assert process.poll() is None, "Editor exited before accepting CLI: " + (Path(logs) / "editor.log").read_text(encoding="utf-8", errors="replace")
                    try:
                        initial = state()
                        break
                    except (OSError, urllib.error.URLError):
                        time.sleep(0.05)
                else:
                    raise AssertionError("Editor remote startup timed out")
                assert initial["availableProjectSource"] == "workspace", initial
                invoke("project.open.source", "workspace", "workspace")
                opened = finish()
                assert opened["projectSource"] == "workspace" and opened["hasProject"], opened
                invoke("activate", "workspace_3D")
                invoke("project.directory.open", "scripts")
                assert finish()["projectDirectory"] == "workspace/scripts"
                invoke("project.directory.back")
                assert finish()["projectDirectory"] == "workspace"
                invoke("project.directory.forward")
                assert finish()["projectDirectory"] == "workspace/scripts"
                invoke("project.directory.refresh")
                assert finish()["projectDirectory"] == "workspace/scripts"
                invoke("script.open", "scripts/gameplay.arp")
                document = finish()
                assert document["hasActiveDocument"] and document["openDocumentCount"] == 1, document
                assert document["scriptRelativePath"] == "scripts/gameplay.arp", document
                assert document["scriptText"].startswith("//") and not document["scriptText"].startswith("\ufeff") and "\r" not in document["scriptText"]
                document_id = str(document["activeDocumentId"])
                invoke("script.replace", document_id, "// source buffer edited through CLI\n")
                assert state()["scriptDirty"]
                invoke("script.open", "scripts/gameplay.arp")
                assert state()["scriptText"] == "// source buffer edited through CLI\n"
                invoke("script.undo", document_id)
                assert state()["scriptText"] == document["scriptText"] and not state()["scriptDirty"]
                invoke("script.redo", document_id)
                assert state()["scriptDirty"]
                invoke("script.reload", document_id, "discard")
                reloaded = finish()
                assert reloaded["activeDocumentId"] == document["activeDocumentId"]
                assert reloaded["scriptText"] == document["scriptText"] and not reloaded["scriptDirty"]
                invoke("script.undo", document_id)
                invoke("script.redo", document_id)
                assert state()["scriptText"] == document["scriptText"] and not state()["scriptDirty"]
                invoke("script.close", document_id, "discard")
                assert state()["openDocumentCount"] == 0
                invoke("state")
                invoke("editor.exit")
                assert process.wait(timeout=20) == 0
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
                log.flush()
                if output:
                    output.mkdir(parents=True, exist_ok=True)
                    (output / "native-cli.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
                    (output / "native-cli-editor.log").write_text((Path(logs) / "editor.log").read_text(encoding="utf-8", errors="replace"), encoding="utf-8")
    print("PASS real native Editor and Interpreter CLI source project/script open/edit/history/close/navigation/exit")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    run(args.editor.resolve(), args.cli.resolve(),
        Path(__file__).resolve().parents[2] / "tests/fixtures/project_resources", args.output)
