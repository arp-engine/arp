"""ScenePbrProducer/1 selected entry layout; generated targets grant no execution."""
import argparse
from pathlib import Path
import struct
import sys
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from spirv_layout import SpirvLayoutFacts


def named_types(facts):
    names, members = {}, {}
    for _, opcode, operands in facts.instructions:
        if opcode in (5, 6):
            start = 1 if opcode == 5 else 2
            name = struct.pack("<" + "I" * len(operands[start:]), *operands[start:]).split(b"\0")[0].decode()
            if opcode == 5:
                names[operands[0]] = name
            else:
                members[(operands[0], operands[1])] = name
    return names, members


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--directory", type=Path, required=True)
    directory = ap.parse_args().directory
    root = Path(__file__).resolve().parents[2]
    source = (root / "shaders/renderer/scene_pbr.slang").read_text(encoding="utf-8")
    assert "    float4x4 viewProjection;" in source
    assert "row_major float4x4" not in source and "column_major float4x4" not in source
    assert "mul(SceneFrame.viewProjection, vertex.position)" in source
    for stage in ("", "Vertex", "Fragment"):
        prefix = directory / stage / "scene_pbr"
        facts = SpirvLayoutFacts(prefix.with_suffix(".spv"))
        assert not facts.push_constant_layouts()
        layouts = facts.buffer_layouts()
        names, members = named_types(facts)
        frame = next(i for i, name in names.items() if name == "ScenePbrFrameConstants_std140")
        assert layouts[(0, 0)]["staticOccupiedExtentBytes"] == 128
        for i, offset in enumerate((0, 64, 80, 96, 112)):
            assert facts.member_decoration(frame, i, 35) == offset
        assert [members[(frame, i)] for i in range(5)] == [
            "viewProjection", "directionIntensity", "lightColor", "ambientColor", "cameraPosition"]
        facts.member_decoration(frame, 0, 5, True)
        assert (frame, 0, 4) not in facts.members
        assert facts.member_decoration(frame, 0, 7) == 16
        if stage != "Fragment":
            assert layouts[(0, 1)]["runtimeArrayStrideBytes"] == 80
            vertex = next(i for i, name in names.items() if name == "ScenePbrVertex_std430")
            for i, offset in enumerate((0, 16, 32, 48, 64)):
                assert facts.member_decoration(vertex, i, 35) == offset
            assert [members[(vertex, i)] for i in range(5)] == ["position", "normal", "tangent", "uv", "tint"]
        if stage != "Vertex":
            assert layouts[(0, 2)]["staticOccupiedExtentBytes"] == 64
            material = next(i for i, name in names.items() if name == "ScenePbrMaterialConstants_std140")
            for i, offset in enumerate((0, 16, 32, 48)):
                assert facts.member_decoration(material, i, 35) == offset
            assert [members[(material, i)] for i in range(4)] == ["baseColor", "emissive", "factors", "flags"]
        expected = {(0, 0), (0, 1)} if stage == "Vertex" else {(0, 0), (0, 2)} if stage == "Fragment" else {(0, 0), (0, 1), (0, 2)}
        assert set(layouts) == expected, layouts
        for suffix in (".metal", ".wgsl", ".reflection.yaml"):
            assert prefix.with_suffix(suffix).stat().st_size > 0
        reflection = prefix.with_suffix(".reflection.yaml").read_text(encoding="utf-8")
        assert "pushConstantSizeBytes: 0" in reflection
        document = yaml.safe_load(reflection)
        expected_bindings = {
            0: ("UniformBuffer", 3 if stage != "Vertex" else 1),
            1: ("ReadOnlyStorageBuffer", 1),
            2: ("UniformBuffer", 2),
            **{i: ("SampledTexture", 2) for i in range(3, 8)},
            8: ("Sampler", 2),
        }
        if stage == "Vertex":
            expected_bindings = {i: expected_bindings[i] for i in (0, 1)}
        elif stage == "Fragment":
            expected_bindings.pop(1)
            expected_bindings[0] = ("UniformBuffer", 2)
        actual = {}
        for binding in document["bindings"]:
            assert binding["set"] == 0 and binding["arrayCount"] == 1
            assert binding["binding"] not in actual
            actual[binding["binding"]] = (binding["type"], binding["stageMask"])
        assert actual == expected_bindings, (stage, actual, expected_bindings)
    # Shared extraction preserves the selected WOCC model in both consumers.
    shared = (root / "shaders/renderer/pbr_brdf.slang").read_text(encoding="utf-8")
    for name in ("pbr_scene.slang", "pbr_scene_terrain.slang"):
        text = (root / "shaders/wocc" / name).read_text(encoding="utf-8")
        assert '#include "renderer/pbr_brdf.slang"' in text
        assert "float distributionGGX(" not in text
    assert "float distributionGGX(" in shared and "max(3.14159265 * denom * denom, 0.001)" in shared
    print("ScenePbrProducer/1: UBO128/material64/storage80, ColMajor/stride16, push0; SPIR-V/MSL/WGSL")


if __name__ == "__main__":
    main()
