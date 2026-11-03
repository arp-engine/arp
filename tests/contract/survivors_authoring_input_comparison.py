"""Original CPU authoring oracle; all output stays in isolated build fixtures."""
import argparse
import hashlib
import importlib.util
from io import BytesIO
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

from PIL import Image
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def hashes(root):
    return {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(root.rglob("*")) if path.is_file()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=args.output))
    source = ROOT / "examples/survivors/assets/source/tiny_dungeon.png"
    original_png = source.read_bytes()
    fixture = ROOT / "tests/fixtures/survivors_authoring"
    metadata = yaml.safe_load((fixture / "original.yaml").read_text(encoding="utf-8"))
    producer = fixture / metadata["fixture"]
    assert hashlib.sha256(producer.read_bytes()).hexdigest() == metadata["sha256"]
    before = load(producer, "original_authoring")
    before.OUT, before.SOURCE = work / "before", source
    before.build()
    original = hashes(before.OUT)
    assert len(original) == 11
    after = load(ROOT / "scripts/build_survivors_assets.py", "current_authoring")
    after.OUT, after.SOURCE = work / "after", source
    original_open = Image.open
    def memory_only(value, *positional, **keyword):
        assert isinstance(value, BytesIO), "producer must not open a physical image path"
        return original_open(value, *positional, **keyword)
    with patch.object(Path, "read_bytes", side_effect=AssertionError("Python source read bypass")):
        with patch.object(Image, "open", memory_only):
            after.build(args.executable)
    assert hashes(after.OUT) == original
    assert source.read_bytes() == original_png
    provenance = yaml.safe_load((after.OUT / "PROVENANCE.yaml").read_text(encoding="utf-8"))
    assert provenance["sourcePngSha256"] == hashlib.sha256(original_png).hexdigest()

    # Mutating the physical source after acquisition cannot change the bytes
    # used for decoding or the recorded provenance of this producer invocation.
    mutable = work / "mutable.png"
    mutable.write_bytes(original_png)
    after.OUT, after.SOURCE = work / "single-receipt", mutable
    def acquired_snapshot(value, *positional, **keyword):
        assert isinstance(value, BytesIO)
        mutable.write_bytes(b"source changed after acquisition")
        return original_open(value, *positional, **keyword)
    with patch.object(Image, "open", acquired_snapshot):
        after.build(args.executable)
    assert hashes(after.OUT) == original
    assert mutable.read_bytes() != original_png

    # Missing source/helper and invalid images fail before creating output files.
    for label, source_path, tool in (("missing-source", work / "absent.png", args.executable),
                                     ("missing-tool", source, work / "absent.exe")):
        after.OUT, after.SOURCE = work / label, source_path
        try:
            after.build(tool)
        except (OSError, RuntimeError):
            pass
        else:
            raise AssertionError("missing source/tool accepted")
        assert not after.OUT.exists()
    wrong = work / "wrong-grid.png"
    Image.new("RGBA", (16, 16)).save(wrong)
    malformed = work / "malformed.png"
    malformed.write_bytes(b"not a PNG")
    wrong_format = work / "jpeg-named-png.png"
    Image.new("RGB", (192, 176)).save(wrong_format, format="JPEG")
    for label, source_path in (("wrong-grid", wrong), ("malformed", malformed),
                               ("wrong-format", wrong_format)):
        after.OUT, after.SOURCE = work / (label + "-output"), source_path
        try:
            after.build(args.executable)
        except (OSError, ValueError):
            pass
        else:
            raise AssertionError("invalid authoring input accepted")
        assert not after.OUT.exists()
        # Existing outputs are equally protected by input admission.
        after.OUT.mkdir()
        sentinel = after.OUT / "keep.bin"
        sentinel.write_bytes(b"previous output")
        try:
            after.build(args.executable)
        except (OSError, ValueError):
            pass
        else:
            raise AssertionError("invalid source overwrote existing output")
        assert list(after.OUT.iterdir()) == [sentinel]
        assert sentinel.read_bytes() == b"previous output"

    # Use the actual command-line entry, with its declared output override.
    result = subprocess.run([sys.executable, "-X", "utf8", str(ROOT / "scripts/build_survivors_assets.py"),
                             "--io-tool", str(args.executable), "--output", str(work / "cli")],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert hashes(work / "cli") == original
    print("authoring_files=11 original_new_and_cli_equal=true consumed_source_sha_equal=true single_acquisition_snapshot=true input_failures_preserve_output=true")
    print(work)


if __name__ == "__main__":
    main()
