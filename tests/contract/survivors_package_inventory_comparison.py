"""Original/new offline producer byte oracle; no game execution or GPU readiness claim."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
from unittest.mock import patch
import zipfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ROOT = ROOT  # Preserve the original location-derived repository root.
    return module


def tree(path):
    result = {}
    for file in sorted(path.rglob("*")):
        if file.is_file():
            with file.open("rb") as stream:
                result[file.relative_to(path).as_posix()] = hashlib.file_digest(stream, "sha256").hexdigest()
    return result


def archive(path):
    with zipfile.ZipFile(path) as value:
        return {name: hashlib.sha256(value.read(name)).hexdigest() for name in value.namelist()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--source-build", type=Path, required=True)
    parser.add_argument("--pe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    source = work / "input"
    source.mkdir(parents=True)
    shutil.copy2(args.pe, source / "arp_survivors.exe")
    shutil.copytree(ROOT / "examples/survivors/assets", source / "assets")
    (source / "assets/empty.bin").write_bytes(b"")
    (source / "assets/raw.bin").write_bytes(b"\x00\xffbinary\r\n")
    for folder in ("shaders", "generated/survivors-aot"):
        (source / folder).mkdir(parents=True)
    for suffix in (".slang", ".spv", ".metal", ".wgsl", ".reflection.yaml", ".artifact-set.yaml"):
        name = "scene_2d_sprites" + suffix
        shutil.copy2(args.source_build / "shaders" / name, source / "shaders" / name)
    shutil.copy2(args.source_build / "generated/survivors-aot/survivors.manifest.yaml",
                 source / "generated/survivors-aot/survivors.manifest.yaml")
    fixture = ROOT / "tests/fixtures/survivors_package"
    metadata = yaml.safe_load((fixture / "original.yaml").read_text(encoding="utf-8"))
    original_path = fixture / metadata["fixture"]
    assert hashlib.sha256(original_path.read_bytes()).hexdigest() == metadata["sha256"]
    before = work / "before/package"
    after = work / "after/package"
    sys.argv = [str(original_path), "--build", str(source), "--output", str(before)]
    load(original_path, "original_survivors_package").main()
    original = tree(before)
    manifest = yaml.safe_load((before / "package.yaml").read_text(encoding="utf-8"))
    assert {row["path"]: row["sha256"] for row in manifest["files"]} == {
        name: digest for name, digest in original.items() if name != "package.yaml"}
    if args.baseline_only:
        (work / "baseline.yaml").write_text(yaml.safe_dump({"files": original}, sort_keys=False), encoding="utf-8")
        print("original_package_files=", len(original))
        return
    sys.argv = [str(ROOT / "scripts/package_survivors.py"), "--build", str(source),
                "--output", str(after), "--io-tool", str(args.executable)]
    # A direct Python hash/read must fail. The child process owns real FileService IO.
    with patch.object(Path, "read_bytes", side_effect=AssertionError("Python package read bypass")):
        load(ROOT / "scripts/package_survivors.py", "current_survivors_package").main()
    assert tree(after) == original
    assert archive(before.with_suffix(".zip")) == archive(after.with_suffix(".zip"))
    print("package_files=", len(original), "all_file_bytes_and_manifest_equal=true zip_entry_bytes_equal=true")


if __name__ == "__main__":
    main()
