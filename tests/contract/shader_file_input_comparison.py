"""Original/current real compiler bytes; no new GPU/platform execution claims."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
parser.add_argument("--slangc", type=Path, required=True)
parser.add_argument("--spirv-reflect", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--smoke", action="store_true")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
evidence_root = args.output.resolve()
args.output = Path(tempfile.mkdtemp(prefix="run-", dir=evidence_root))
args.output.resolve().relative_to(evidence_root)


def inventory(directory):
    return {path.relative_to(directory).as_posix(): {"bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            for path in sorted(directory.rglob("*")) if path.is_file()}


def invoke(script, output, extra, log):
    command = [sys.executable, "-X", "utf8", str(script), "--slangc", str(args.slangc),
               "--spirv-reflect", str(args.spirv_reflect), "--out-dir", str(output), *extra]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=900)
    (args.output / log).write_bytes(result.stdout + b"\n--- stderr ---\n" + result.stderr)
    if result.returncode:
        raise RuntimeError("SHADER-INPUT-COMPILER-FAILED: " + log + "\n" + result.stderr.decode("utf-8", errors="replace")[-3000:])
    return result.stdout.decode("utf-8")


# TemporaryDirectory owns only a freshly-created child of this explicit output root.
with tempfile.TemporaryDirectory(prefix="compiled-", dir=args.output.resolve()) as temporary:
    compiled = Path(temporary).resolve()
    compiled.relative_to(args.output.resolve())
    selection = ["--module", "scene_2d_sprites"] if args.smoke else []
    invoke(ROOT / "tests/fixtures/shader_tool_input/run_original.py", compiled, selection, "original.log")
    original = inventory(compiled)
    # Keep a hash table, plus the exact original byte set for independent review.
    baseline = args.output / ("original-smoke" if args.smoke else "original-output")
    if baseline.exists():
        raise RuntimeError("SHADER-INPUT-EVIDENCE-ALREADY-EXISTS: " + str(baseline))
    shutil.copytree(compiled, baseline)
    current_log = invoke(ROOT / "tests/contract/shader_file_input_runner.py", compiled,
                         [*selection, "--io-tool", str(args.executable)], "file-service.log")
    current = inventory(compiled)
    if original != current:
        changed = sorted(name for name in original.keys() | current.keys() if original.get(name) != current.get(name))
        (args.output / "differences.yaml").write_text(yaml.safe_dump(changed), encoding="utf-8")
        raise RuntimeError("SHADER-INPUT-ARTIFACT-BYTES-CHANGED: " + repr(changed))
    if "FileService/1" not in current_log or "LegacyHostFile" in current_log:
        raise RuntimeError("SHADER-INPUT-SOURCE-SELECTION")
    (args.output / "artifact-comparison.yaml").write_text(yaml.safe_dump({
        "type": "ShaderFileInputComparison", "contractVersion": 1,
        "fileCount": len(original), "byteIdentical": True, "smoke": args.smoke,
        "files": original}, sort_keys=False), encoding="utf-8")
    if args.smoke:
        graphics_log = invoke(ROOT / "tests/contract/shader_file_input_runner.py", compiled / "graphics",
            ["--fixture", "ordinary_graphics_matrix_push", "--graphics-module", "--io-tool", str(args.executable)],
            "graphics-children.log")
        if graphics_log.count("FileService/1") != 3 or "LegacyHostFile" in graphics_log:
            raise RuntimeError("SHADER-INPUT-CHILD-SOURCE-SELECTION")
        publication_path = compiled / "graphics/ordinary_graphics_matrix_push.graphics.module.yaml"
        publication = yaml.safe_load(publication_path.read_text(encoding="utf-8"))
        if publication["matrixLayout"] != "row-major" or len(publication["entries"]) != 2:
            raise RuntimeError("SHADER-INPUT-GRAPHICS-PUBLICATION")
        for entry in publication["entries"]:
            record = entry["publication"]
            data = (publication_path.parent / record["path"]).read_bytes()
            if hashlib.sha256(data).hexdigest() != record["sha256"]:
                raise RuntimeError("SHADER-INPUT-CHILD-PUBLICATION-HASH")
        (args.output / "graphics-smoke.yaml").write_text(yaml.safe_dump({
            "type": "ShaderFileInputGraphicsSmoke", "contractVersion": 1,
            "realChildInvocations": 2, "fileServiceInvocations": 3,
            "matrixLayout": "row-major", "childPublicationHashesValid": True}), encoding="utf-8")
    (evidence_root / "latest.yaml").write_text(yaml.safe_dump({"run": args.output.name,
        "comparison": "artifact-comparison.yaml", "smoke": args.smoke}), encoding="utf-8")
    print("byte_identical_files=" + str(len(original)) + " evidence=" + str(args.output))
