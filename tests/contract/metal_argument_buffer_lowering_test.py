#!/usr/bin/env python3
"""Compiler artifact boundary tests; native compilation/pixels use the Metal fixture."""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))
from scripts.compile_shaders import (
    get_metal_argument_buffer_lowering,
    get_metal_binding_space_lowering,
    get_shader_bindings,
    get_webgpu_binding_space_lowering,
    lower_metal_argument_buffer_artifact,
    set_wgsl_depth_texture_type,
)


TEXTURE = "texture2d<float, access::sample>"
BINDING = dict(name="Inputs", set=0, binding=7, type="SampledTexture", arrayCount=3)
HEADER = "#include <metal_stdlib>\nusing namespace metal;\n"


def entry(stage, name, parameter, body="return float4(0);"):
    return f"[[{stage}]] float4 {name}({parameter}) {{ {body} }}\n"


class ArgumentBufferLoweringTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="arp-metal-array-")
        self.addCleanup(self.directory.cleanup)
        self.path = pathlib.Path(self.directory.name) / "probe.metal"

    def lower(self, source, bindings=None):
        self.path.write_text(source, encoding="utf-8")
        result = lower_metal_argument_buffer_artifact(self.path, bindings or [BINDING])
        self.assertEqual(result, get_metal_argument_buffer_lowering(bindings or [BINDING], self.path))
        return self.path.read_text(encoding="utf-8"), result

    def reject(self, source, diagnostic, bindings=None):
        self.path.write_text(source, encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, diagnostic):
            lower_metal_argument_buffer_artifact(self.path, bindings or [BINDING])
        self.assertEqual(self.path.read_text(encoding="utf-8"), source,
                         "a failed lowering must not publish a partial artifact")

    def test_current_slang_multiple_entries_and_context_member(self):
        member = f"array<{TEXTURE}, int(3)> Inputs_0;"
        scalar = f"{TEXTURE} Scalar_1 [[texture(2)]]"
        source = HEADER + "struct KernelContext { " + member + " };\n"
        source += entry("vertex", "vertexMain", f"uint id [[vertex_id]], array<{TEXTURE}, int(3)> Inputs_1, {scalar}")
        source += entry("fragment", "fragmentMain", f"float4 p [[position]], array<{TEXTURE}, int(3)> Inputs_2, {scalar}")
        output, result = self.lower(source)
        self.assertIn(member, output)
        self.assertIn(scalar, output)
        self.assertNotIn("#define", output)
        for name in ("Inputs_1", "Inputs_2"):
            self.assertEqual(output.count(f"auto {name} = arpMetalArgs_Inputs.Inputs;"), 1)
        self.assertEqual(result[0]["argumentBufferSlot"], 0)
        self.assertEqual(result[0]["capacity"], 3)
        scalar_binding = dict(name="Scalar", set=0, binding=2, type="SampledTexture")
        direct = get_metal_binding_space_lowering([scalar_binding], self.path)
        self.assertEqual(direct[0]["metalSlot"], 2)

    def test_legacy_array_forms(self):
        for parameter in (f"array<{TEXTURE}, 3> Inputs [[texture(7)]]",
                          f"{TEXTURE} Inputs[3] [[texture(7)]]"):
            with self.subTest(parameter=parameter):
                output, _ = self.lower(HEADER + entry("fragment", "main", parameter))
                self.assertIn(f"array<{TEXTURE}, 3> Inputs [[id(0)]]", output)

    def test_multiple_arrays_avoid_existing_buffer_slots(self):
        parameters = (f"constant float4& pc [[buffer(0)]], device float* data [[buffer(2)]], "
                      f"array<{TEXTURE}, 3> Inputs_1, array<sampler, 2> Filters_1")
        bindings = [BINDING, dict(name="Filters", set=1, binding=4, type="Sampler", arrayCount=2)]
        output, result = self.lower(HEADER + entry("fragment", "main", parameters), bindings)
        self.assertEqual([item["argumentBufferSlot"] for item in result], [1, 3])
        self.assertIn("auto Filters_1 = arpMetalArgs_Filters.Filters;", output)
        self.assertIn("auto Inputs_1 = arpMetalArgs_Inputs.Inputs;", output)

    def test_wrong_capacity_is_not_published(self):
        for parameter in (f"array<{TEXTURE}, int(4)> Inputs", f"{TEXTURE} Inputs[4]"):
            with self.subTest(parameter=parameter):
                self.reject(HEADER + entry("fragment", "main", parameter), "capacity")

    def test_uniform_pointee_defined_between_entries(self):
        bindings = [BINDING, dict(name="Constants", set=0, binding=8, type="UniformBuffer", arrayCount=2)]
        source = HEADER + entry("vertex", "vertexMain", f"array<{TEXTURE}, 3> Inputs")
        source += "struct UniformData { float4 value; };\n"
        source += entry("fragment", "fragmentMain", "array<UniformData constant*, 2> Constants")
        output, _ = self.lower(source, bindings)
        self.assertLess(output.index("vertexMain("), output.index("struct UniformData"))
        self.assertLess(output.index("struct UniformData"), output.index("struct ArpMetalArgumentBuffer_Constants"))
        self.assertLess(output.index("struct ArpMetalArgumentBuffer_Constants"), output.index("fragmentMain("))

    def test_readonly_buffer_array_const_contract(self):
        binding = dict(name="Buffers", set=0, binding=3, type="ReadOnlyStorageBuffer", arrayCount=2)
        source = HEADER + "struct Values { float4 value; };\nstruct Context { array<Values device*, int(2)> Buffers_0; };\n"
        source += entry("fragment", "main", "array<Values device*, int(2)> Buffers_1")
        output, _ = self.lower(source, [binding])
        self.assertIn("array<const Values device*, 2> Buffers [[id(0)]]", output)
        self.assertIn("array<const Values device*, 2> Buffers_0;", output)
        self.assertNotIn("array<Values device*", output)
        self.assertIn("auto Buffers_1 = arpMetalArgs_Buffers.Buffers", output)
        writable = dict(binding, type="StorageBuffer")
        output, _ = self.lower(source, [writable])
        self.assertIn("array<Values device*, 2> Buffers [[id(0)]]", output)
        self.reject(HEADER + entry("fragment", "main", "array<Values*, 2> Buffers"), "pointer type", [binding])

    def test_malformed_capacity_is_rejected(self):
        for count in ("3)", "int(3", "int(3))"):
            with self.subTest(count=count):
                self.reject(HEADER + entry("fragment", "main", f"array<{TEXTURE}, {count}> Inputs"),
                            "Metal|capacity")

    def test_scalar_cannot_claim_array(self):
        self.reject(HEADER + entry("fragment", "main", f"{TEXTURE} Inputs [[texture(7)]]"), "capacity")

    def test_inconsistent_entry_type_or_slot(self):
        first = entry("vertex", "vertexMain", f"array<{TEXTURE}, 3> Inputs_1 [[texture(7)]]")
        for parameter in ("array<texture2d<half, access::sample>, 3> Inputs_2 [[texture(7)]]",
                          f"array<{TEXTURE}, 3> Inputs_2 [[texture(8)]]"):
            with self.subTest(parameter=parameter):
                self.reject(HEADER + first + entry("fragment", "fragmentMain", parameter), "inconsistent")

    def test_native_kind_must_match_logical_resource(self):
        self.reject(HEADER + entry("fragment", "main", f"array<{TEXTURE}, 3> Inputs [[buffer(7)]]"),
                    "kind")

    def test_missing_entry_or_namespace(self):
        self.reject(HEADER + f"struct Context {{ array<{TEXTURE}, 3> Inputs; }};", "no emitted")
        self.reject(entry("fragment", "main", f"array<{TEXTURE}, 3> Inputs"), "namespace")

    def test_inactive_array_requires_target_reflection_proof(self):
        source = HEADER + entry("vertex", "vertexMain", "uint id [[vertex_id]]")
        self.path.write_text(source, encoding="utf-8")
        for masks in (None, {(0, 7): 1}):
            with self.assertRaisesRegex(RuntimeError, "no emitted"):
                lower_metal_argument_buffer_artifact(self.path, [BINDING], masks)
            self.assertEqual(self.path.read_text(), source)
        lower_metal_argument_buffer_artifact(self.path, [BINDING], {})
        self.assertEqual(self.path.read_text(), source)
        self.assertFalse(get_metal_argument_buffer_lowering([BINDING], self.path)[0]["active"])

    def test_slot_exhaustion_is_not_published(self):
        occupied = ", ".join(f"constant float4& pc{i} [[buffer({i})]]" for i in range(31))
        self.reject(HEADER + entry("fragment", "main", occupied + f", array<{TEXTURE}, 3> Inputs"),
                    "no Metal buffer slot")

    def test_depth_array_source_identity(self):
        bindings = get_shader_bindings("[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(Inputs)[3];")
        self.assertEqual(len(bindings), 1)
        self.assertTrue(bindings[0]["depthTexture"])
        self.assertEqual(bindings[0]["arrayCount"], 3)

    def test_depth_array_wgsl_reads_scalar_and_preserves_color(self):
        self.path.write_text(
            "@binding(0) @group(0) var Inputs_0 : binding_array<texture_2d<f32>, 3>;\n"
            "@binding(1) @group(0) var Color_0 : texture_2d<f32>;\n"
            "fn probe() { let d = textureLoad((Inputs_0[u32(p.x) % u32(3)]), xy, 0).x; "
            "let c = textureLoad((Color_0), xy, 0).x; }\n", encoding="utf-8")
        set_wgsl_depth_texture_type(self.path, "Inputs_0")
        output = self.path.read_text(encoding="utf-8")
        self.assertIn("binding_array<texture_depth_2d, 3>", output)
        self.assertIn("textureLoad((Inputs_0[u32(p.x) % u32(3)]), xy, 0);", output)
        self.assertIn("textureLoad((Color_0), xy, 0).x;", output)
        bindings = get_shader_bindings("[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(Inputs)[3];")
        self.assertTrue(get_webgpu_binding_space_lowering(bindings, self.path)[0]["depthTexture"])

    def test_explicit_depth_cannot_be_overwritten_by_color_artifact(self):
        self.path.write_text("@binding(0) @group(0) var Inputs_0 : binding_array<texture_2d<f32>, 3>;\n",
                             encoding="utf-8")
        bindings = get_shader_bindings("[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(Inputs)[3];")
        with self.assertRaisesRegex(RuntimeError, "depth"):
            get_webgpu_binding_space_lowering(bindings, self.path)


if __name__ == "__main__":
    unittest.main()
