"""VolumetricCloud/1 binary uniform, storage and matrix ABI contract."""
import argparse
import re
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from spirv_layout import SpirvLayoutFacts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    directory = parser.parse_args().directory
    source = (Path(__file__).resolve().parents[2] / "shaders/renderer/cloud_common.slang").read_text()
    assert "row_major float4x4" not in source and "column_major float4x4" not in source
    for field in ("inverseViewProjection", "previousViewProjection"):
        assert f"    float4x4 {field};" in source
    expected = {"cloud_density_slice": {1: 16, 3: 32}, "cloud_raymarch": {1: 16, 3: 32},
                "cloud_temporal": {1: 32, 2: 32, 3: 32}, "cloud_composite": {1: 32},
                "cloud_spatial_resolve": {1: 32, 2: 32, 3: 32, 4: 16},
                "cloud_spatial_composite": {1: 32},
                "cloud_compact_resolve": {1: 32, 2: 24, 3: 24, 4: 16},
                "cloud_compact_composite": {1: 24}}
    for name, strides in expected.items():
        facts = SpirvLayoutFacts(directory / (name + ".spv"))
        layouts = facts.buffer_layouts()
        depth_binding = {"cloud_raymarch": 2, "cloud_spatial_resolve": 5,
                         "cloud_composite": 3, "cloud_spatial_composite": 3, "cloud_compact_resolve": 5,
                         "cloud_compact_composite": 3}.get(name)
        if depth_binding is not None:
            image = facts.descriptor_image_types()[(0, depth_binding)]
            assert image["descriptorCount"] == 1 and image["dimension"] == 1
            assert image["depth"] == 1 and not image["arrayed"] and not image["multisampled"]
            assert image["sampled"] == 1 and image["scalarKind"] == "Float" and image["scalarWidth"] == 32
            assert "depth2d<float" in (directory / (name + ".metal")).read_text()
            assert "texture_depth_2d" in (directory / (name + ".wgsl")).read_text()
            authored = (Path(__file__).resolve().parents[2] / "shaders/renderer" / (name.replace("compact", "spatial") + ".slang")).read_text()
            assert "#define ARP_SAMPLED_DEPTH(name) DepthTexture2D name" in authored
        assert layouts[(0, 0)]["staticOccupiedExtentBytes"] == 368, (name, layouts)
        if name in ("cloud_spatial_resolve", "cloud_compact_resolve"):
            assert layouts[(0, 6)]["staticOccupiedExtentBytes"] == 16, layouts
            assert set(layouts) == {(0, i) for i in (0, 1, 2, 3, 4, 6)}, layouts
        for binding, stride in strides.items():
            assert layouts[(0, binding)]["runtimeArrayStrideBytes"] == stride, (name, binding)
        names, members = {}, {}
        for _, opcode, operands in facts.instructions:
            if opcode in (5, 6):
                prefix = 1 if opcode == 5 else 2
                data = struct.pack("<" + "I" * len(operands[prefix:]), *operands[prefix:])
                text = data.split(b"\0")[0].decode()
                if opcode == 5:
                    names[operands[0]] = text
                else:
                    members[(operands[0], operands[1])] = text
        constants = next(i for i, value in names.items() if value == "CloudGpuConstants_std140")
        if name in ("cloud_spatial_resolve", "cloud_compact_resolve"):
            policy = next(i for i, value in names.items()
                          if value == "CloudReconstructionGpuConstants_std140")
            assert [members[(policy, i)] for i in range(4)] == [
                "contractVersion", "maxRadianceRange", "maxTransmittanceRange", "reserved"]
            for i in range(4):
                assert facts.member_decoration(policy, i, 35) == 4 * i
        offsets = [0, 64] + list(range(128, 368, 16))
        assert len([1 for identifier, _ in members if identifier == constants]) == len(offsets)
        for index, offset in enumerate(offsets):
            assert facts.member_decoration(constants, index, 35) == offset, (name, index)
        for index in (0, 1):
            facts.member_decoration(constants, index, 5, True)
            assert (constants, index, 4) not in facts.members
            assert facts.member_decoration(constants, index, 7) == 16
        assert not facts.push_constant_layouts()
        assert "pushConstantSizeBytes: 0" in (directory / (name + ".reflection.yaml")).read_text()
        for suffix in (".metal", ".wgsl"):
            assert (directory / (name + suffix)).stat().st_size > 0
        if name.startswith("cloud_compact_"):
            sample = next(i for i, value in names.items() if value == "CloudGpuCompactSample_std430")
            fields = ("red", "green", "blue", "transmittance", "distance", "opaqueDistance")
            assert tuple(members[(sample, i)] for i in range(6)) == fields
            _, declaration = facts.definition(sample, 30)
            assert len(declaration) == 7
            for i, field_type in enumerate(declaration[1:]):
                assert facts.definition(field_type, 22, 2)[1][1] == 32
                assert facts.member_decoration(sample, i, 35) == i * 4
            # Scalar-only native records have 4-byte alignment/size per field;
            # require all six lanes, no vector padding or implicit packed ABI.
            for suffix in (".metal", ".wgsl"):
                target = (directory / (name + suffix)).read_text()
                records = re.findall(r"struct\s+CloudGpuCompactSample\w*\s*\{([^{}]*)\}", target)
                assert records, (name, suffix)
                scalar = r"float\s+\w+\s*;" if suffix == ".metal" else r"(?:@align\(4\)\s*)?\w+\s*:\s*f32\s*,"
                for body in records:
                    assert re.fullmatch(r"\s*(?:" + scalar + r"\s*){6}", body), (name, suffix, body)
                    scalar_fields = re.findall(scalar, body)
                    for field, declaration in zip(fields, scalar_fields):
                        assert re.search(r"\b" + field + r"(?:_\d+)?\b", declaration), (field, declaration)
    slice_facts = SpirvLayoutFacts(directory / "cloud_asset_slice.spv")
    slice_layouts = slice_facts.buffer_layouts()
    assert set(slice_layouts) == {(0, 0), (0, 1), (0, 3)}
    assert slice_layouts[(0, 0)]["staticOccupiedExtentBytes"] == 48
    assert slice_layouts[(0, 1)]["runtimeArrayStrideBytes"] == 16
    assert slice_layouts[(0, 3)]["runtimeArrayStrideBytes"] == 16
    assert not slice_facts.push_constant_layouts()
    for suffix in (".metal", ".wgsl", ".reflection.yaml"):
        assert (directory / ("cloud_asset_slice" + suffix)).stat().st_size > 0
    print("VolumetricCloud/1 + Reconstruction/1,2: uniform368 + policy16, storage16/24/32, matrix ABI, three targets; CloudAssetSlice/1 uniform48/storage16")


if __name__ == "__main__":
    main()
