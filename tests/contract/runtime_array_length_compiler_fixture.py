"""Test-only Slang length programs; exact Metal failure is retained, never faked."""
import argparse
import contextlib
import io
import pathlib
import sys
import subprocess
import shutil

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import compile_shaders
import shader_bindings_codegen


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    variants = ("structured", "rwstructured", "byteaddress", "rwbyteaddress")
    results = {}
    for name in variants:
        source = args.out_dir / (name + ".slang")
        shutil.copyfile(ROOT / "tests/fixtures" / ("runtime_length_" + name + ".slang"), source)
        roles = {}
        for target, extension, profile in (("spirv", "spv", "spirv_1_5"), ("metal", "metal", "sm_6_0"), ("wgsl", "wgsl", "sm_6_0")):
            log = io.StringIO()
            with contextlib.redirect_stdout(log):
                success = compile_shaders.run_slangc(args.slangc, str(source), target, profile, ["computeMain"],
                    str(args.out_dir / (name + "." + extension)), diagnostic_failure_code="E36107" if target == "metal" else None)
            (args.out_dir / (name + "." + target + ".log")).write_text(log.getvalue())
            roles[target] = "Generated" if success else "DiagnosticFailure E36107 - MetalRuntimeLengthDiagnostic/1"
            # This versioned compiler fixture expects the audited unavailable
            # target. A compiler upgrade must review its new native ABI.
            if target == "metal" and success:
                raise RuntimeError("SHADER-METAL-RUNTIME-LENGTH-CONTRACT-REVIEW-REQUIRED")
        layouts = shader_bindings_codegen.reflect_buffer_declaration_layouts(args.spirv_reflect, str(args.out_dir / (name + ".spv")))
        (args.out_dir / (name + ".reflection.yaml")).write_text(yaml.safe_dump({
            "type": "RuntimeLengthCompilerFixtureReflection", "contractVersion": 1,
            "status": "Prototype", "roles": roles,
            "bindings": [{"set": key[0], "binding": key[1], **value} for key, value in layouts.items()]}, sort_keys=False))
        results[name] = roles
    # Same sources now travel through canonical role/reflection publication.
    publications = {}
    for name in (*variants, "graphics", "graphics_probe"):
        fixture = "runtime_length_" + name
        destination = args.out_dir / "original" / name
        command = [sys.executable, str(ROOT / "scripts/compile_shaders.py"), "--slangc", args.slangc,
                   "--spirv-reflect", args.spirv_reflect, "--out-dir", str(destination),
                   "--fixture", fixture, "--metal-runtime-length-diagnostic"]
        command += ["--graphics-module"] if name.startswith("graphics") else ["--single-entry", "computeMain"]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (args.out_dir / (name + ".publication.log")).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(result.stdout)
        publications[name] = str(destination)
    (args.out_dir / "report.yaml").write_text(yaml.safe_dump({"type": "RuntimeLengthCompilerFixture", "contractVersion": 1,
        "productionShader": False, "nativeLengthAuthority": False, "variants": results, "originalPublications": publications}, sort_keys=False))


if __name__ == "__main__":
    main()
