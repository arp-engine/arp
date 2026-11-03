"""Run sealed pre-migration tooling with the repository's original source paths."""
import hashlib
import importlib.util
from pathlib import Path
import sys

import yaml

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "scripts"))
metadata = yaml.safe_load((FIXTURE / "original.yaml").read_text(encoding="utf-8"))


def load(name, filename, expected):
    path = FIXTURE / filename
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError("SHADER-INPUT-ORIGINAL-FIXTURE-IDENTITY")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


for item in metadata["dependencies"]:
    load(Path(item["source"]).stem, item["fixture"], item["sha256"])
compiler = load("original_shader_compiler", "original_compile_shaders.py", metadata["sha256"])
compiler.ROOT = str(ROOT)
compiler.SHADER_INCLUDE_DIR = str(ROOT / "shaders")
compiler.WOCC_SHADER_DIR = str(ROOT / "shaders/wocc")
# This baseline deliberately covers ordinary and single-entry invocations only.
# Graphics recursion is checked independently through the new real child CLI.
if "--graphics-module" in sys.argv:
    raise RuntimeError("SHADER-INPUT-ORIGINAL-GRAPHICS-BASELINE-NOT-DEFINED")
compiler.main()
