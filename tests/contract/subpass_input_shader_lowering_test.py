#!/usr/bin/env python3
"""Contract test for compiler-owned WebGPU subpass shader lowering."""

import argparse
import os
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
sys.path.insert(0, REPO_ROOT)

from scripts.compile_shaders import (
    get_shader_bindings,
    get_webgpu_binding_space_lowering,
    get_logical_subpass_bindings,
    lower_subpass_input_source_for_wgsl,
    lower_subpass_input_source_for_spirv,
    lower_wgsl_subpass_input_current_fragment,
    lower_wgsl_subpass_input_depth_types,
    read_text,
    run_slangc,
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc", required=True)
    args = parser.parse_args()

    source_path = os.path.join(REPO_ROOT, "tests", "fixtures", "subpass_input_probe.slang")
    source = read_text(source_path)
    transformed, lowered = lower_subpass_input_source_for_wgsl(source, source_path)
    assert lowered
    assert "__WGSL__" not in transformed
    assert "[[vk::binding(0, 0)]] Texture2D<float4> SceneInput" in transformed
    assert "arpSubpassLoadColor(SceneInput, int2(0, 0))" in transformed

    spirv_transformed, spirv_lowered = lower_subpass_input_source_for_spirv(
        source, source_path)
    assert spirv_lowered
    assert "__WGSL__" not in spirv_transformed
    assert "[[vk::binding(0, 0)]]" in spirv_transformed
    assert "[[vk::input_attachment_index(4)]]" in spirv_transformed
    assert "SubpassInput<float4> SceneInput" in spirv_transformed

    collision_source = (
        "[[vk::binding(0, 0)]] Texture2D<float4> Existing;\n"
        "SUBPASS_INPUT_COLOR(SceneInput, 4);\n")
    assert get_logical_subpass_bindings(collision_source) == {"SceneInput": 1}
    collision_bindings = get_shader_bindings(collision_source)
    assert next(binding for binding in collision_bindings
                if binding["name"] == "SceneInput")["binding"] == 1

    with tempfile.TemporaryDirectory(prefix="arp-subpass-contract-") as temp_dir:
        transformed_path = os.path.join(temp_dir, "subpass_input_probe.slang")
        spirv_transformed_path = os.path.join(
            temp_dir, "subpass_input_probe.spirv.slang")
        spirv_path = os.path.join(temp_dir, "subpass_input_probe.spv")
        wgsl_path = os.path.join(temp_dir, "subpass_input_probe.wgsl")
        with open(transformed_path, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(transformed)
        with open(spirv_transformed_path, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(spirv_transformed)
        assert run_slangc(
            args.slangc,
            spirv_transformed_path,
            "spirv",
            "spirv_1_5",
            ["vsMain", "fsMain"],
            spirv_path,
        )
        assert run_slangc(
            args.slangc,
            transformed_path,
            "wgsl",
            "sm_6_0",
            ["vsMain", "fsMain"],
            wgsl_path,
        )
        lower_wgsl_subpass_input_current_fragment(wgsl_path)
        wgsl = read_text(wgsl_path)
        assert "texture_2d<f32>" in wgsl
        assert "textureLoad" in wgsl
        assert "@builtin(position)" in wgsl
        assert "i32(arpSubpassFragmentPosition_0.x)" in wgsl
        assert "i32(arpSubpassFragmentPosition_0.y)" in wgsl

        logical = get_shader_bindings(source)
        artifact = get_webgpu_binding_space_lowering(logical, wgsl_path)
        assert artifact == [{
            "name": "SceneInput",
            "set": 0,
            "binding": 0,
            "group": 0,
            "webGpuBinding": 0,
            "active": True,
            "physicalType": "SampledTexture",
            "loadSemantics": "CurrentFragment",
            "requiresSampler": False,
            "depthTexture": False,
        }]

    depth_source_path = os.path.join(
        REPO_ROOT, "tests", "fixtures", "subpass_input_depth_probe.slang")
    depth_source = read_text(depth_source_path)
    depth_transformed, depth_lowered = lower_subpass_input_source_for_wgsl(
        depth_source, depth_source_path)
    assert depth_lowered
    assert "Texture2D<float> DepthInput" in depth_transformed
    assert "arpSubpassLoadDepth(DepthInput, int2(0, 0))" in depth_transformed
    assert get_shader_bindings(depth_source) == [{
        "name": "DepthInput",
        "type": "InputAttachment",
        "set": 0,
        "binding": 0,
        "attachmentIndex": 7,
        "depthTexture": True,
    }]

    with tempfile.TemporaryDirectory(prefix="arp-subpass-depth-contract-") as temp_dir:
        transformed_path = os.path.join(temp_dir, "subpass_input_depth_probe.slang")
        wgsl_path = os.path.join(temp_dir, "subpass_input_depth_probe.wgsl")
        with open(transformed_path, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(depth_transformed)
        assert run_slangc(
            args.slangc,
            transformed_path,
            "wgsl",
            "sm_6_0",
            ["vsMain", "fsMain"],
            wgsl_path,
        )
        lower_wgsl_subpass_input_current_fragment(wgsl_path)
        lower_wgsl_subpass_input_depth_types(wgsl_path, depth_source)
        depth_wgsl = read_text(wgsl_path)
        assert "texture_depth_2d" in depth_wgsl
        assert "texture_2d<f32>" not in depth_wgsl.split("fn arpSubpassLoadDepth", 1)[0]
        depth_artifact = get_webgpu_binding_space_lowering(
            get_shader_bindings(depth_source), wgsl_path)
        assert depth_artifact == [{
            "name": "DepthInput",
            "set": 0,
            "binding": 0,
            "group": 0,
            "webGpuBinding": 0,
            "active": True,
            "physicalType": "SampledTexture",
            "loadSemantics": "CurrentFragment",
            "requiresSampler": False,
            "depthTexture": True,
        }]

    print("[ok] compiler-owned WebGPU subpass shader lowering contract")


if __name__ == "__main__":
    main()
