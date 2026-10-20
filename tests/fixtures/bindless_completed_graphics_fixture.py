"""Build Graphics resource families from authored Slang and typed variants."""
import argparse
from pathlib import Path
import sys

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from bindless_codegen import compile_bindless_package


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    parser.add_argument("--spirv-reflect", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--geometry-only", choices=("Vertex", "Instance"))
    parser.add_argument("--mode", choices=("Native", "Emulated"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    fixture = ROOT / "tests/fixtures"
    document = yaml.safe_load((fixture / "bindless_completed_graphics_root.yaml").read_text(encoding="utf-8"))
    modes = (args.mode,) if args.mode else ("Native", "Emulated")
    if args.geometry_only:
        for mode in modes:
            geometry = args.geometry_only
            compile_bindless_package(dict(document, name="RootGeometry" + geometry),
                fixture / ("bindless_original_geometry_" + geometry.lower() + ".slang"),
                ["vertexMain", "fragmentMain", "faultFragment"], mode,
                args.output, args.slangc, args.spirv_reflect)
        return
    source = args.output / "completed_graphics.slang"
    source.write_text((fixture / "bindless_completed_graphics_root.slang").read_text(encoding="utf-8"), encoding="utf-8")
    graphics_buffers = yaml.safe_load((fixture / "bindless_original_graphics_buffers.yaml").read_text(encoding="utf-8"))
    graphics_array = yaml.safe_load((fixture / "bindless_original_graphics_array.yaml").read_text(encoding="utf-8"))
    graphics_records = yaml.safe_load((fixture / "bindless_original_graphics_records.yaml").read_text(encoding="utf-8"))
    buffer_document = yaml.safe_load((fixture / "bindless_metal_graphics.yaml").read_text(encoding="utf-8"))
    buffer_document["name"] = "RootVertexBuffer"
    buffer_document["tables"] = [table for table in buffer_document["tables"] if table["name"] == "Inputs"]
    buffer_document["tables"][0]["stages"] = ["vertex"]
    buffer_document["tables"][0]["operations"] = ["load"]
    buffer_source = args.output / "vertex_buffer.slang"
    buffer_source.write_text((fixture / "bindless_vertex_buffer.slang").read_text(encoding="utf-8")
                             + '\n[shader("fragment")]\nfloat4 cleanFragment() : SV_Target0 { return float4(1.0, 1.0, 1.0, 1.0); }\n',
                             encoding="utf-8")
    for mode in modes:
        for geometry in ("Vertex", "Instance"):
            geometry_document = dict(document, name="RootGeometry" + geometry)
            compile_bindless_package(geometry_document, fixture / ("bindless_original_geometry_" + geometry.lower() + ".slang"),
                                     ["vertexMain", "fragmentMain", "faultFragment"], mode,
                                     args.output, args.slangc, args.spirv_reflect)
        # These indexed Vertex entries report faults through writable storage.
        # WGSL cannot execute that stage contract. Keep their Native evidence;
        # explicit refusal tests cover Emulated instead of publishing it.
        if mode == "Native":
            compile_bindless_package(graphics_records, fixture / "bindless_original_graphics_records.slang",
                                     ["vertexMain", "cleanFragment", "faultFragment"], mode,
                                     args.output, args.slangc, args.spirv_reflect)
        compile_bindless_package(graphics_array, fixture / "bindless_original_graphics_array.slang",
                                 ["vertexMain", "cleanFragment", "faultFragment"], mode,
                                 args.output, args.slangc, args.spirv_reflect)
        if mode == "Native":
            compile_bindless_package(graphics_buffers, fixture / "bindless_original_graphics_buffers.slang",
                                     ["vertexMain", "cleanFragment", "faultFragment"], mode,
                                     args.output, args.slangc, args.spirv_reflect)
        compile_bindless_package(document, source, ["vertexMain", "fragmentMain", "faultFragment"], mode,
                                 args.output, args.slangc, args.spirv_reflect)
        if mode == "Native":
            compile_bindless_package(buffer_document, buffer_source, ["vertexMain", "cleanFragment"], mode,
                                     args.output, args.slangc, args.spirv_reflect)


if __name__ == "__main__":
    main()
