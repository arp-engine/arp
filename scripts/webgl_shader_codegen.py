#!/usr/bin/env python3
"""WebGl1Graphics/1 compiler feasibility probe; not a production package grant.

Use the existing Slang invocation and SPIRV-Reflect layout reader. SPIRV-Cross
reflection is an external tool response, immediately normalized into YAML.
No generated GLSL is patched or authored by hand.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import subprocess

import yaml

from compile_shaders import run_slangc
from shader_bindings_codegen import reflect_push_constant_block
from spirv_layout import SpirvLayoutFacts


class WebGl1ShaderProbeCompiler:
    def __init__(self, slangc: Path, spirv_cross: Path, spirv_reflect: Path):
        self.slangc = slangc.resolve()
        self.spirv_cross = spirv_cross.resolve()
        self.spirv_reflect = spirv_reflect.resolve()

    @staticmethod
    def tool_identity(path: Path, version_args: list[str] | None) -> dict:
        identity = {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        if version_args is None:
            identity["versionStatus"] = "NoVersionQueryInterface"
        else:
            result = subprocess.run([str(path), *version_args], capture_output=True, text=True, check=True)
            version = (result.stdout + result.stderr).strip()
            if not version:
                raise RuntimeError("RHI-WEBGL1-PROBE-TOOL-VERSION-MISSING")
            identity.update(version=version, versionArguments=version_args)
        return identity

    def compile(self, source: Path, output: Path) -> dict:
        output.mkdir(parents=True, exist_ok=True)
        source = source.resolve()
        direct = subprocess.run([str(self.slangc), str(source),
            "-matrix-layout-row-major", "-target", "glsl", "-profile", "glsl_100",
            "-entry", "vsMain", "-stage", "vertex", "-o", str(output / "direct-es100.vert")],
            capture_output=True, text=True)
        (output / "direct-es100.log").write_text(direct.stdout + direct.stderr, encoding="utf-8")
        report = {"type": "WebGl1ShaderFeasibility", "contractVersion": 1,
            "status": "ProbeOnly", "executablePackage": False,
            "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "generatorSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "matrixLayout": "row-major", "glslVersion": 100,
            "tools": {"slangc": self.tool_identity(self.slangc, ["-version"]),
                "spirvCross": self.tool_identity(self.spirv_cross, ["--revision"]),
                "spirvReflect": self.tool_identity(self.spirv_reflect, None)},
            "directEs100ExitCode": direct.returncode,
            "route": "Slang-SPIRV-SPIRVCross-ESSL100", "stages": {}}
        for stage, entry in (("vert", "vsMain"), ("frag", "fsMain")):
            spv = output / f"probe.{stage}.spv"
            run_slangc(str(self.slangc), str(source), "spirv", "spirv_1_3", [entry], str(spv))
            # The original SPIR-V remains authoritative. Flattening UBOs is
            # deliberately not selected: transparent array wrappers in Slang
            # can be unflattenable. ES100 ordinary uniform structs preserve them.
            reflection = json.loads(subprocess.check_output(
                [str(self.spirv_cross), str(spv), "--reflect"], text=True))
            facts = SpirvLayoutFacts(spv)
            buffer_facts = [{"set": key[0], "binding": key[1], **value}
                for key, value in sorted(facts.buffer_layouts().items())]
            if stage == "vert":
                flatten = subprocess.run([str(self.spirv_cross), str(spv), "--es", "--version", "100",
                    "--flatten-ubo"], capture_output=True, text=True)
                (output / "flatten-ubo.log").write_text(flatten.stdout + flatten.stderr, encoding="utf-8")
                report["flattenUboProbeExitCode"] = flatten.returncode
            cmd = [str(self.spirv_cross), str(spv), "--es", "--version", "100"]
            for direction, key in (("out", "outputs"),) if stage == "vert" else (("in", "inputs"),):
                for item in reflection.get(key, []):
                    location = item["location"]
                    cmd += ["--rename-interface-variable", direction, str(location), f"arpVarying{location}"]
            glsl = output / f"probe.{stage}"
            subprocess.run(cmd + ["--output", str(glsl)], check=True, capture_output=True, text=True)
            # MSL and WGSL are compiled from the same Slang source and layout.
            run_slangc(str(self.slangc), str(source), "metal", "sm_6_0", [entry], str(output / f"probe.{stage}.metal"))
            run_slangc(str(self.slangc), str(source), "wgsl", "sm_6_0", [entry], str(output / f"probe.{stage}.wgsl"))
            push = reflect_push_constant_block(str(self.spirv_reflect), str(spv))
            report["stages"][stage] = {"entry": entry, "spirvReflection": reflection,
                "bufferLayoutFacts": buffer_facts,
                "pushConstant": asdict(push) if push else None,
                "files": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in (spv, glsl, output / f"probe.{stage}.metal", output / f"probe.{stage}.wgsl")}}
        vs = report["stages"]["vert"]["spirvReflection"].get("outputs", [])
        fs = report["stages"]["frag"]["spirvReflection"].get("inputs", [])
        if {(x["location"], x["type"]) for x in vs} != {(x["location"], x["type"]) for x in fs}:
            raise RuntimeError("RHI-WEBGL1-PROBE-VARYING-ABI")
        (output / "reflection.yaml").write_text(yaml.safe_dump(report, sort_keys=False), encoding="utf-8")
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--slangc", type=Path, required=True)
    parser.add_argument("--spirv-cross", type=Path, required=True)
    parser.add_argument("--spirv-reflect", type=Path, required=True)
    args = parser.parse_args()
    WebGl1ShaderProbeCompiler(args.slangc, args.spirv_cross, args.spirv_reflect).compile(args.source, args.out_dir)


if __name__ == "__main__":
    main()
