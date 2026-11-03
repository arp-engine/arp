"""Exercise selected tooling while rejecting Python resource-read bypasses."""
import builtins
from pathlib import Path
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import compile_shaders as compiler


def output_only(path, mode="r", *args, **kwargs):
    if "r" in mode or not any(value in mode for value in ("w", "a", "x")) or "+" in mode:
        raise AssertionError("SHADER-INPUT-LEGACY-READ-BYPASS: " + str(path))
    return builtins.open(path, mode, *args, **kwargs)


if "--io-tool" not in sys.argv:
    raise RuntimeError("SHADER-INPUT-EXPLICIT-TOOL-REQUIRED")
with patch.object(compiler, "open", output_only, create=True), \
        patch.object(Path, "read_bytes", side_effect=AssertionError("SHADER-INPUT-PATH-READ-BYPASS")):
    compiler.main()
