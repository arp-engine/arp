"""Real native Editor + Interpreter CLI, source script save/conflict/restart."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from project_io_cli_smoke import run


def terminal(state, expected="succeeded"):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        value = state()
        if value["projectIoTask"]["status"] != "pending":
            assert value["projectIoTask"]["status"] == expected, value
            return value
        time.sleep(0.02)
    raise AssertionError("script save task deadline")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    invalid = subprocess.run([str(args.editor.resolve()), "--source-write"],
        capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert invalid.returncode == 2 and "explicit absolute --project-root" in invalid.stderr
    engine = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="arp-source-script-save-") as directory:
        root = Path(directory) / "project"
        shutil.copytree(engine / "tests/fixtures/project_resources", root)
        path = root / "workspace/scripts/gameplay.arp"
        original = path.read_bytes()
        assert original.startswith(b"\xef\xbb\xbf") and b"\r\n" in original
        original_text = original.decode("utf-8-sig").replace("\r\n", "\n")
        first_text = original_text + "// Source saved through CLI\n"
        final_text = original_text + "// Saved source and reopened through the Interpreter CLI\n"
        external_text = original_text + "// External editor change\n"
        external_bytes = external_text.replace("\n", "\r\n").encode("utf-8")

        def save_workflow(invoke, state):
            invoke("script.open", "scripts/gameplay.arp")
            opened = terminal(state)
            document = str(opened["activeDocumentId"])
            invoke("script.replace", document, first_text)
            accepted = invoke("script.save", document)
            receipt = accepted["snapshot"]["scriptSaveReceipt"]
            assert not receipt["submitted"] and not receipt["terminal"]
            saved = terminal(state)
            receipt = saved["scriptSaveReceipt"]
            assert receipt["submitted"] and receipt["terminal"] and receipt["published"]
            assert receipt["commitState"] == "committed" and not saved["scriptDirty"]
            expected = b"\xef\xbb\xbf" + first_text.replace("\n", "\r\n").encode("utf-8")
            assert path.read_bytes() == expected
            assert bytes(receipt["versionDigest"]) == hashlib.sha256(expected).digest()
            invoke("project.directory.refresh")
            refreshed = terminal(state)
            assert refreshed["scriptSaveReceipt"]["taskId"] == receipt["taskId"]

            invoke("script.replace", document, original_text + "// Conflicting local edits\n")
            path.write_bytes(external_bytes)
            invoke("script.save", document)
            conflicted = terminal(state, "failed")
            assert conflicted["scriptDirty"]
            assert conflicted["scriptSaveReceipt"]["commitState"] == "not_committed"
            assert not conflicted["scriptSaveReceipt"]["published"]
            assert path.read_bytes() == external_bytes
            invoke("script.reload", document, "discard")
            assert not terminal(state)["scriptDirty"]
            invoke("script.replace", document, final_text)
            invoke("script.save", document)
            assert not terminal(state)["scriptDirty"]
            assert path.read_bytes() == final_text.replace("\n", "\r\n").encode("utf-8")

        def reopen_workflow(invoke, state):
            invoke("script.open", "scripts/gameplay.arp")
            reopened = terminal(state)
            assert reopened["scriptText"] == final_text and not reopened["scriptDirty"]

        output = args.output.resolve() if args.output else None
        run(args.editor.resolve(), args.cli.resolve(), root,
            output / "save" if output else None, save_workflow, source_writes=True)
        run(args.editor.resolve(), args.cli.resolve(), root,
            output / "restart" if output else None, reopen_workflow)
    print("PASS real native source save, exact bytes, conflict and exit/restart")


if __name__ == "__main__":
    main()
