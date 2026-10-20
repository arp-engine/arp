"""Actual browser shader execution; NOT an RHI/WOCC integration readiness claim."""
import argparse
import json
import pathlib
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from bindless_codegen import (compile_bindless_package, read_bindless_publication,
                              zero_base_indirect_producer_schema)
from compile_shaders import run_slangc, write_shader_reflection
from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--browser")
    parser.add_argument("--compile-only", action="store_true")
    parser.add_argument("--spirv-reflect")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    reflect = args.spirv_reflect or str(pathlib.Path(args.slangc).with_name(
        "spirv-reflect.exe" if sys.platform == "win32" else "spirv-reflect"))
    schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_variant.yaml").read_text())
    schema["faultLatchBinding"] = 14
    schema["tables"][4]["stages"] = ["fragment", "compute"]
    source = args.output / "bindless_graphics_fixture.slang"
    source.write_text(
        (ROOT / "tests/fixtures/bindless_access.slang").read_text(encoding="utf-8")
        + (ROOT / "tests/fixtures/bindless_vertex.slang").read_text(encoding="utf-8")
        + (ROOT / "tests/fixtures/bindless_fragment_buffer.slang").read_text(encoding="utf-8"),
        encoding="utf-8")
    package = compile_bindless_package(
        schema, source, ["computeMain", "fragmentMain", "vertexMain", "fragmentBufferMain"],
        "Emulated", args.output, args.slangc, reflect)
    manifest = read_bindless_publication(package)
    public_schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_webgpu_public_compute.yaml").read_text())
    public_source = ROOT / "tests/fixtures/bindless_webgpu_public_compute.slang"
    public_output = args.output / "public-compute"
    public_output.mkdir(parents=True, exist_ok=True)
    public_package = compile_bindless_package(
        public_schema, public_source, ["computeMain", "indirectFaultMain"], "Emulated", public_output,
        args.slangc, reflect)
    public_manifest = read_bindless_publication(public_package)
    public_graphics_schema = yaml.safe_load(
        (ROOT / "tests/fixtures/bindless_webgpu_public_graphics.yaml").read_text())
    public_graphics_source = ROOT / "tests/fixtures/bindless_webgpu_public_graphics.slang"
    public_graphics_output = args.output / "public-graphics"
    public_graphics_output.mkdir(parents=True, exist_ok=True)
    public_graphics_package = compile_bindless_package(
        public_graphics_schema, public_graphics_source, ["vertexMain", "fragmentMain", "instanceBaseMain"],
        "Emulated", public_graphics_output, args.slangc, reflect)
    public_graphics_manifest = read_bindless_publication(public_graphics_package)
    producer_package = compile_bindless_package(zero_base_indirect_producer_schema(), None,
        ["arpZeroBaseIndirectMain"], "Emulated", args.output / "zero-base-producer", args.slangc, reflect)
    producer_manifest = read_bindless_publication(producer_package)
    indirect_consumer_schema = yaml.safe_load(
        (ROOT / "tests/fixtures/bindless_indirect_consumer.yaml").read_text())
    indirect_consumer_package = compile_bindless_package(indirect_consumer_schema,
        ROOT / "tests/fixtures/bindless_indirect_consumer.slang", ["vertexMain", "fragmentMain"],
        "Emulated", args.output / "indirect-consumer", args.slangc, reflect)
    if args.compile_only:
        print(json.dumps({
            "privatePackage": str(package),
            "publicComputePackage": str(public_package),
            "publicComputePackageIdentity": public_manifest["shaderPackageIdentity"],
            "publicGraphicsPackage": str(public_graphics_package),
            "publicGraphicsPackageIdentity": public_graphics_manifest["shaderPackageIdentity"],
            "zeroBaseProducerPackage": str(producer_package),
            "zeroBaseProducerPackageIdentity": producer_manifest["shaderPackageIdentity"],
            "indirectConsumerPackage": str(indirect_consumer_package),
        }, indent=2))
        return 0
    if not args.browser:
        parser.error("--browser is required unless --compile-only is set")
    (args.output / "metadata-layout.json").write_text(json.dumps({
        "layout": manifest["snapshotMetadata"], "faultLatch": manifest["faultLatch"],
        "tables": manifest["tables"]}), encoding="utf-8")
    for stage in ("compute", "fragment"):
        (args.output / (stage + ".wgsl")).write_bytes(
            (args.output / ("BindlessFixture.emulated." + stage + "Main.wgsl")).read_bytes())
    vertex_source = str(ROOT / "tests/fixtures/bindless_vertex.slang")
    for target, suffix in (("spirv", "spv"), ("metal", "metal"), ("wgsl", "wgsl")):
        run_slangc(args.slangc, vertex_source, target,
            "spirv_1_5" if target == "spirv" else "sm_6_0",
            ["vertexMain"], str(args.output / ("vertex." + suffix)))
    write_shader_reflection(vertex_source, "BindlessFixtureVertex",
        str(args.output / "vertex.spv"), str(args.output / "vertex.metal"),
        str(args.output / "vertex.wgsl"), str(args.output / "vertex.reflection.yaml"),
        None, spirv_reflect_exe=reflect, descriptor_stage_masks={}, entry_names=["vertexMain"],
        metal_binding_space=[])
    (args.output / "index.html").write_bytes((ROOT / "tests/fixtures/bindless_webgpu_runner.html").read_bytes())
    result = BrowserContractRunner(args.browser, args.output, "index.html", args.output, 45).run({
        "shaderPackageIdentity": manifest["shaderPackageIdentity"],
        "scope": "standalone-shader-execution-not-RHI-or-WOCC"})
    (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result.get("success") else 1


if __name__ == "__main__":
    raise SystemExit(main())
