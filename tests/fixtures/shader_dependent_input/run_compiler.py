"""Real compiler driver; snapshots retain their own provenance identities."""
import builtins
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = Path(__file__).resolve().parent
original = "--original" in sys.argv
if original:
    sys.argv.remove("--original")
    metadata = yaml.safe_load((FIXTURE / "original.yaml").read_text(encoding="utf-8"))
    for item in metadata["sources"]:
        path = FIXTURE / item["path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            raise RuntimeError("SHADER-DEPENDENT-ORIGINAL-IDENTITY")
    # Load the sealed producers under their original import names. Their own
    # __file__ continues to identify the actual sealed bytes, not today's source.
    sys.path.insert(0, str(FIXTURE / "scripts"))
    for name in ("spirv_layout", "shader_bindings_codegen", "slang_resource_switch", "buffer_vector_record",
                 "bindless_zero_base_identity", "gpu_visibility_codegen", "spirv_cross_api",
                 "webgl_shader_artifact", "compile_shaders", "bindless_codegen"):
        path = FIXTURE / "scripts" / (name + ".py")
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    compiler = sys.modules["compile_shaders"]
    compiler.ROOT = str(ROOT)
    compiler.SHADER_INCLUDE_DIR = str(ROOT / "shaders")
    compiler.WOCC_SHADER_DIR = str(ROOT / "shaders/wocc")
    visibility = sys.modules["gpu_visibility_codegen"]
    # Rebase only explicit fixture source roots; generated executable bytes and
    # tool identities remain untouched. The compiler uses the same authored files.
    for name in ("ROOT", "SOURCE", "LIBRARIES", "CONSUMER_SOURCE", "CONSUMER_LIBRARIES"):
        value = getattr(visibility, name)
        def rebase(path):
            return ROOT / path.relative_to(FIXTURE)
        setattr(visibility, name, tuple(rebase(path) for path in value) if isinstance(value, tuple) else rebase(value))
else:
    sys.path.insert(0, str(ROOT / "scripts"))
    import compile_shaders as compiler
    import bindless_codegen
    import webgl_shader_artifact
    import spirv_cross_api
    import gpu_visibility_codegen

native_run = subprocess.run


def run(command, *args, **kwargs):
    # Re-enter this test driver for Python shader children so the old parent
    # really uses old children, and selected children retain the bypass guards.
    # Slang, Reflect and every other native process execute their original argv.
    if (isinstance(command, (list, tuple)) and len(command) > 1
            and str(command[0]) == sys.executable and str(command[1]) == str(Path(compiler.__file__).resolve())):
        command = [sys.executable, str(Path(__file__).resolve()),
                   *(["--original"] if original else []), *command[2:]]
    return native_run(command, *args, **kwargs)


def output_only(path, mode="r", *args, **kwargs):
    if "r" in mode or not any(value in mode for value in ("w", "a", "x")) or "+" in mode:
        raise AssertionError("SHADER-DEPENDENT-PYTHON-READ-BYPASS: " + str(path))
    return builtins.open(path, mode, *args, **kwargs)


with patch.object(subprocess, "run", run):
    if original:
        compiler.main()
    else:
        if "--io-tool" not in sys.argv:
            raise RuntimeError("SHADER-DEPENDENT-EXPLICIT-SOURCE-REQUIRED")
        with patch.object(compiler, "open", output_only, create=True), \
                patch.object(Path, "read_bytes", side_effect=AssertionError("SHADER-DEPENDENT-PATH-BYTES-BYPASS")), \
                patch.object(Path, "read_text", side_effect=AssertionError("SHADER-DEPENDENT-PATH-TEXT-BYPASS")):
            compiler.main()
