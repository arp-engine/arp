"""Typed compiler contract and real Slang cross-target compilation gates."""
import argparse
import copy
import contextlib
import hashlib
import io
import pathlib
import subprocess
import struct
import sys
import tempfile
import unittest
from unittest import mock

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from bindless_codegen import (BindlessContractError, generate_bindless, compile_bindless_package,
                              read_bindless_publication, zero_base_indirect_producer_schema,
                              zero_base_indirect_consumer_schema, _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES)
from compile_shaders import (lower_wgsl_atomic_storage_buffers, lower_metal_argument_buffer_artifact,
                             validate_wgsl_atomic_storage_buffers, lower_wgsl_depth_integer_mip_sampling,
                             get_shader_bindings)
from buffer_vector_record import BufferVectorRecord
from spirv_layout import SpirvLayoutFacts
sys.path.insert(0, str(ROOT / "tests/fixtures"))
from bindless_prepared_fixture import PreparedBindingFixture


def spirv_reflect_tool():
    if SPIRV_REFLECT:
        return pathlib.Path(SPIRV_REFLECT)
    return pathlib.Path(SLANGC).with_name("spirv-reflect.exe" if sys.platform == "win32" else "spirv-reflect")


def spirv_validator_tool():
    if SPIRV_VAL:
        return pathlib.Path(SPIRV_VAL)
    return pathlib.Path(SLANGC).with_name("spirv-val.exe" if sys.platform == "win32" else "spirv-val")


class BindlessCodegenTests(unittest.TestCase):
    def setUp(self):
        self.schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_variant.yaml").read_text())

    def test_pbr_geometry_family_stage_and_shared_deformation(self):
        from compile_shaders import derive_entry_point_reflection
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pbr_geometry_family.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_pbr_geometry_family.slang"
        entries = [f"{family}{render_pass}Vertex" for family in ("mesh", "skin", "grass", "canopy")
                   for render_pass in ("Color", "Depth", "Shadow")]
        entries += ["familyColorMain", "familyDepthMain", "familyShadowMain"]
        geometry_library = ROOT / "shaders/renderer/bindless_pbr_geometry.slang"
        reflected = derive_entry_point_reflection(geometry_library.read_text() + source.read_text(), entries, "pbr-geometry")
        self.assertEqual([entry["stage"] for entry in reflected], ["Vertex"] * 12 + ["Fragment"] * 3)
        libraries = [ROOT / "shaders" / name for name in (
            "core/leaf_surface.slang", "core/pbr_material.slang", "core/instance_transform.slang",
            "core/foliage_geometry.slang", "core/skinning_geometry.slang", "core/pbr_geometry.slang",
            "renderer/bindless_pbr_material.slang", "renderer/bindless_pbr_geometry.slang")]
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["switchCases"], 23 if mode == "Emulated" else 0)
            self.assertEqual(generated.reflection["tables"][2]["bufferLayout"]["elementStrideBytes"], 96)
            if not SLANGC:
                continue
            with tempfile.TemporaryDirectory(prefix="arp-pbr-geometry-") as directory:
                publication = compile_bindless_package(schema, source, entries, mode, directory,
                    SLANGC, spirv_reflect_tool(), libraries)
                payload = read_bindless_publication(publication)
                self.assertEqual([module["entry"] for module in payload["modules"]], entries)
                for index, module in enumerate(payload["modules"]):
                    reflection = yaml.safe_load((publication.parent / module["reflection"]).read_text())
                    facts = SpirvLayoutFacts(publication.parent / reflection["targets"]["spirv"])
                    layouts = facts.buffer_layouts()
                    if index < 12:
                        self.assertEqual(facts.push_constant_layouts()[0]["occupiedExtentBytes"], 16)
                        self.assertEqual(layouts[(0, 0)]["staticOccupiedExtentBytes"], 400)
                        self.assertTrue(reflection["vertexAttributes"], module["entry"])
                        self.assertEqual(reflection["vertexAttributes"][0]["format"], "Float3")
                        if 3 <= index < 6:
                            self.assertEqual(layouts[(0, 2)]["staticOccupiedExtentBytes"], 32864)
                        if index >= 6:
                            self.assertEqual(layouts[(0, 1)]["runtimeArrayStrideBytes"], 64)
                    if mode == "Emulated":
                        wgsl = (publication.parent / reflection["targets"]["wgsl"]).read_text()
                        self.assertNotIn("binding_array", wgsl)
                        if index < 12:
                            self.assertNotRegex(wgsl, r"var\s*<\s*storage\s*,\s*read_write\s*>")
                            self.assertNotRegex(wgsl, r"\batomic\w*\b")

    def test_terrain_geometry_family_stages_and_frame_layout(self):
        from compile_shaders import derive_entry_point_reflection
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_terrain_geometry_family.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_terrain_geometry_family.slang"
        libraries = [ROOT / "shaders" / name for name in (
            "core/instance_transform.slang", "core/foliage_geometry.slang", "core/skinning_geometry.slang",
            "core/pbr_geometry.slang", "renderer/bindless_pbr_geometry.slang", "core/terrain_surface.slang",
            "renderer/bindless_terrain_material.slang", "renderer/bindless_terrain_geometry.slang")]
        entries = ["terrainColorVertex", "terrainDepthVertex", "terrainShadowVertex",
                   "terrainColorMain", "terrainDepthMain", "terrainShadowMain"]
        reflected = derive_entry_point_reflection("\n".join(p.read_text() for p in libraries) + source.read_text(), entries, "terrain-geometry")
        self.assertEqual([entry["stage"] for entry in reflected], ["Vertex"] * 3 + ["Fragment"] * 3)
        self.assertEqual(sum(t["capacity"] for t in schema["tables"] if t["kind"] == "SampledTexture"), 27)
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["switchCases"], 34 if mode == "Emulated" else 0)
            if not SLANGC:
                continue
            with tempfile.TemporaryDirectory(prefix="arp-terrain-geometry-") as directory:
                publication = compile_bindless_package(schema, source, entries, mode, directory,
                    SLANGC, spirv_reflect_tool(), libraries)
                payload = read_bindless_publication(publication)
                for index, module in enumerate(payload["modules"]):
                    reflection = yaml.safe_load((publication.parent / module["reflection"]).read_text())
                    facts = SpirvLayoutFacts(publication.parent / reflection["targets"]["spirv"])
                    layouts = facts.buffer_layouts()
                    if index < 3:
                        self.assertEqual(facts.push_constant_layouts()[0]["occupiedExtentBytes"], 16)
                        self.assertEqual(layouts[(0, 0)]["staticOccupiedExtentBytes"], 400)
                        self.assertTrue(reflection["vertexAttributes"])
                    else:
                        self.assertEqual(layouts[(0, 3)]["staticOccupiedExtentBytes"], 64)
                    if mode == "Emulated":
                        wgsl = (publication.parent / reflection["targets"]["wgsl"]).read_text()
                        self.assertNotIn("binding_array", wgsl)
                        if index < 3:
                            self.assertNotRegex(wgsl, r"var\s*<\s*storage\s*,\s*read_write\s*>")
                            self.assertNotRegex(wgsl, r"\batomic\w*\b")

    def test_terrain_production_record_layout_and_resource_demand(self):
        from compile_shaders import derive_entry_point_reflection
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_terrain_material_record.yaml").read_text())
        record = BufferVectorRecord(schema["tables"][3]["record"])
        pages = BufferVectorRecord(schema["tables"][4]["record"])
        self.assertEqual(record.stride, 240)
        self.assertEqual(pages.stride, 32)
        self.assertEqual(record.fields[:6], tuple((f"layer{i}", "uint4") for i in range(6)))
        self.assertEqual(record.fields[6:9], (("routing", "uint4"), ("domains", "uint4"), ("generation", "uint4")))
        self.assertEqual(pages.fields, (("worldBounds", "float4"), ("routing", "uint4")))
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["snapshotMetadata"]["wordCount"], 64)
            self.assertEqual(generated.reflection["switchCases"], 33 if mode == "Emulated" else 0)
            self.assertEqual(generated.reflection["tables"][3]["bufferLayout"]["identity"], record.layout()["identity"])
            self.assertEqual(generated.reflection["tables"][4]["bufferLayout"]["identity"], pages.layout()["identity"])
            self.assertIn("uint layer, float2 gradientX, float2 gradientY", generated.source)
        for budget in ("maxSwitchCases", "snapshotBytes", "maxBindings"):
            bad = copy.deepcopy(schema)
            bad[budget] = {"maxSwitchCases": 32, "snapshotBytes": 255, "maxBindings": 33}[budget]
            with self.assertRaises(BindlessContractError):
                generate_bindless(bad, "Emulated")
        source = (ROOT / "tests/fixtures/bindless_terrain_material_record.slang").read_text()
        entries = derive_entry_point_reflection(source,
            ["vertexMain", "colorMain", "depthMain", "shadowMain"], "terrain-record")
        self.assertEqual([entry["stage"] for entry in entries], ["Vertex", "Fragment", "Fragment", "Fragment"])

    def test_pbr_production_record_layout_and_depth_entry_stages(self):
        from compile_shaders import derive_entry_point_reflection
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pbr_material_record.yaml").read_text())
        expected = [("baseColorAlpha", "float4"), ("factors", "float4"),
                    ("coverageLeaf", "float4"), ("textures", "uint4"),
                    ("routing", "uint4"), ("generation", "uint4")]
        record = BufferVectorRecord(schema["tables"][2]["record"])
        self.assertEqual(record.fields, tuple(expected))
        self.assertEqual(record.stride, 96)
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            material = next(table for table in generated.reflection["tables"] if table["tableId"] == 3)
            self.assertEqual(material["bufferLayout"]["elementStrideBytes"], 96)
            self.assertEqual(material["bufferLayout"]["identity"], record.layout()["identity"])
            self.assertIn("uint2 arpBindlessSnapshotGeneration()", generated.source)
        source = (ROOT / "tests/fixtures/bindless_pbr_material_record.slang").read_text()
        entries = derive_entry_point_reflection(source,
            ["vertexMain", "colorMain", "depthMain", "shadowMain"], "material-record")
        self.assertEqual([entry["stage"] for entry in entries], ["Vertex", "Fragment", "Fragment", "Fragment"])

    def test_visibility_closed_schema_and_atomic_or_domain(self):
        import gpu_visibility_codegen as visibility
        schema = visibility.schema()
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["gpuVisibilityKernelContractVersion"], 1)
            self.assertEqual(generated.reflection["snapshotMetadata"]["wordCount"], 54)
            self.assertEqual(generated.reflection["snapshotBytes"], 224)
            self.assertEqual(generated.reflection["switchCases"], 16 if mode == "Emulated" else 0)
            self.assertIn("uint arpBindlessFaultBits()", generated.source)
            self.assertIn("VisibilityControl_atomicOr", generated.source)
            self.assertRegex(generated.source, r"InterlockedOr\(VisibilityControl(?:_0)?\[location\]")
        for edit in (lambda value: value["tables"][0].update(binding=11),
                     lambda value: value["tables"][3].update(operations=["store"]),
                     lambda value: value.update(gpuVisibilityKernelContractVersion=2)):
            bad = copy.deepcopy(schema)
            edit(bad)
            with self.assertRaisesRegex(BindlessContractError, "GPU-VISIBILITY-KERNEL-SCHEMA"):
                generate_bindless(bad, "Emulated")
        unsealed = copy.deepcopy(schema)
        unsealed.pop("gpuVisibilityKernelContractVersion")
        unsealed["tables"][3]["operations"].append("atomicOr")
        with self.assertRaisesRegex(BindlessContractError, "ATOMIC-OPERATION-DOMAIN"):
            generate_bindless(unsealed, "Emulated")

    def test_visibility_authored_source_cannot_claim_engine_provenance(self):
        import gpu_visibility_codegen as visibility
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "wrong.slang"
            source.write_text(visibility.SOURCE.read_text(encoding="utf-8") + "\n// changed\n", encoding="utf-8")
            with self.assertRaisesRegex(BindlessContractError, "GPU-VISIBILITY-KERNEL-ENTRY-OWNERSHIP"):
                compile_bindless_package(visibility.schema(), source, list(visibility.ENTRIES),
                    "Emulated", directory, None, None, visibility.LIBRARIES)
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))

    def test_visibility_consumer_requires_closed_selector_and_shared_producer(self):
        import gpu_visibility_codegen as visibility
        schema = visibility.schema(True)
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["gpuVisibilityConsumerContractVersion"], 2)
            self.assertEqual(generated.reflection["switchCases"], 18 if mode == "Emulated" else 0)
        for edit in (lambda value: value["pushConstants"].update(stages=["fragment"]),
                     lambda value: value["tables"][4].update(stages=["compute", "vertex"]),
                     lambda value: value.pop("gpuVisibilityKernelContractVersion")):
            bad = copy.deepcopy(schema)
            edit(bad)
            with self.assertRaises(BindlessContractError):
                generate_bindless(bad, "Emulated")
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "changed.slang"
            source.write_text(visibility.CONSUMER_SOURCE.read_text(encoding="utf-8")
                .replace("parameters.counts.w != 0u", "false"), encoding="utf-8")
            with self.assertRaisesRegex(BindlessContractError, "GPU-VISIBILITY-KERNEL-ENTRY-OWNERSHIP"):
                compile_bindless_package(schema, source, list(visibility.CONSUMER_ENTRIES),
                    "Emulated", directory, None, None, visibility.CONSUMER_LIBRARIES)
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))

    def test_visibility_consumer_real_interfaces_and_push_stage_coverage(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for target reflection")
        import gpu_visibility_codegen as visibility
        for mode in ("Native", "Emulated"):
            with tempfile.TemporaryDirectory(prefix="arp-visibility-consumer-") as directory:
                path = compile_bindless_package(visibility.schema(True), visibility.CONSUMER_SOURCE,
                    list(visibility.CONSUMER_ENTRIES), mode, directory, SLANGC, spirv_reflect_tool(),
                    visibility.CONSUMER_LIBRARIES)
                payload = read_bindless_publication(path)
                self.assertEqual([m["entry"] for m in payload["modules"]], list(visibility.CONSUMER_ENTRIES))
                for i, module in enumerate(payload["modules"]):
                    reflection = yaml.safe_load((path.parent / module["reflection"]).read_text(encoding="utf-8"))
                    self.assertEqual(reflection["pushConstantSizeBytes"], 16 if i == 4 else 0)
                    spv = path.parent / reflection["targets"]["spirv"]
                    subprocess.run([str(spirv_validator_tool()), str(spv)], check=True,
                        capture_output=True, text=True)
                    if i == 4:
                        facts = SpirvLayoutFacts(spv)
                        self.assertEqual(facts.push_constant_layouts()[0]["occupiedExtentBytes"], 16)
                        layouts = facts.buffer_layouts()
                        self.assertEqual(layouts[(2, 1)]["runtimeArrayStrideBytes"], 64)
                        self.assertEqual(layouts[(2, 2)]["runtimeArrayStrideBytes"], 16)
                        self.assertEqual(reflection["vertexInputContractVersion"], "VertexInputInterface/1")
                        self.assertEqual(reflection["vertexAttributes"][0]["location"], 0)
                        self.assertEqual(reflection["vertexAttributes"][0]["format"], "Float3")
                    if mode == "Emulated":
                        wgsl = (path.parent / reflection["targets"]["wgsl"]).read_text(encoding="utf-8")
                        self.assertNotIn("binding_array", wgsl)
                        if i == 4:
                            self.assertNotRegex(wgsl, r"var\s*<\s*storage\s*,\s*read_write\s*>")
                            self.assertNotRegex(wgsl, r"\batomic\w*\b")
                if RUNTIME_VERIFIER:
                    subprocess.run([RUNTIME_VERIFIER, str(path), "valid", "gpu-visibility-consumer"],
                        check=True, capture_output=True, text=True)

    def test_visibility_real_targets_and_runtime_provenance(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for target reflection")
        import gpu_visibility_codegen as visibility
        for mode in ("Native", "Emulated"):
            with tempfile.TemporaryDirectory(prefix="arp-visibility-contract-") as directory:
                path = compile_bindless_package(visibility.schema(), visibility.SOURCE,
                    list(visibility.ENTRIES), mode, directory, SLANGC, spirv_reflect_tool(), visibility.LIBRARIES)
                payload = read_bindless_publication(path)
                self.assertEqual([module["entry"] for module in payload["modules"]], list(visibility.ENTRIES))
                if mode == "Emulated":
                    for phase in ("Reset", "Cull", "Emit", "Finalize"):
                        target = pathlib.Path(directory) / ("ArpGpuVisibility.emulated.visibility" + phase + "ComputeMain.wgsl")
                        self.assertNotIn("binding_array", target.read_text(encoding="utf-8"))
                if RUNTIME_VERIFIER:
                    subprocess.run([RUNTIME_VERIFIER, str(path), "valid", "gpu-visibility"], check=True,
                        capture_output=True, text=True)
                    # Rehashing the publication cannot certify a changed
                    # producer. Runtime pins the complete compiler package.
                    payload.pop("shaderPackageIdentity")
                    payload["authoredSourceSha256"] = "0" * 64
                    publication = yaml.safe_load(path.read_text())
                    payload_path = path.parent / publication["payload"]
                    encoded = yaml.safe_dump(payload, sort_keys=False).encode("utf-8")
                    payload_path.write_bytes(encoded)
                    publication["shaderPackageIdentity"] = hashlib.sha256(encoded).hexdigest()
                    path.write_text(yaml.safe_dump(publication), encoding="utf-8")
                    subprocess.run([RUNTIME_VERIFIER, str(path), "RHI-BINDLESS-PACKAGE-GPU-VISIBILITY-PROVENANCE"],
                        check=True, capture_output=True, text=True)

    def test_depth_pyramid_shared_image_schema_and_exact_budgets(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_depth_pyramid.yaml").read_text())
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["sampledDepthContractVersion"], 1)
            self.assertEqual(generated.reflection["storageImageContractVersion"], 1)
            self.assertEqual(generated.reflection["snapshotMetadata"]["wordCount"], 34)
            self.assertEqual(generated.reflection["snapshotBytes"], 144)
            self.assertEqual(generated.reflection["switchCases"], 9 if mode == "Emulated" else 0)
            self.assertEqual(sum(b["arrayCount"] for b in generated.reflection["bindings"]), 8)
            # Each independent declaration remains required in a mixed package.
            for field, reason in (("sampledDepthContractVersion", "SAMPLED-DEPTH"),
                                  ("storageImageContractVersion", "STORAGE-FORMAT")):
                broken = copy.deepcopy(schema); broken.pop(field)
                with self.assertRaisesRegex(BindlessContractError, reason):
                    generate_bindless(broken, mode)
            broken = copy.deepcopy(schema); broken["snapshotBytes"] -= 1
            with self.assertRaisesRegex(BindlessContractError, "SNAPSHOT-BUDGET"):
                generate_bindless(broken, mode)
        for field, reason in (("maxBindings", "BINDING-BUDGET"), ("maxSwitchCases", "SWITCH-BUDGET")):
            broken = copy.deepcopy(schema); broken[field] -= 1
            with self.assertRaisesRegex(BindlessContractError, reason):
                generate_bindless(broken, "Emulated")

    def test_native_single_element_tables_use_the_reflected_scalar_abi(self):
        schema = copy.deepcopy(self.schema)
        for table in schema["tables"]:
            table["capacity"] = 1
        generated = generate_bindless(schema, "Native")
        bindings = {b["name"]: b for b in get_shader_bindings(generated.source)}
        for table in schema["tables"]:
            name = table["name"]
            self.assertEqual(bindings[name]["arrayCount"], 1)
            self.assertNotIn(name + "[1]", generated.source)
            self.assertNotIn(name + "[arpBindlessResourceIndex(", generated.source)
        # Scalar physical resources retain active-prefix and invalid-index
        # fault guards; a declared table capacity is never silently expanded.
        self.assertIn("index >= activeCount", generated.source)
        original = generate_bindless(self.schema, "Native")
        for table in self.schema["tables"]:
            if table["capacity"] > 1:
                self.assertIn(table["name"] + "[" + str(table["capacity"]) + "]", original.source)

    def test_depth_pyramid_shared_image_cross_target_package(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        if not spirv_validator_tool().is_file():
            self.skipTest("Pass --spirv-val for independent SPIR-V validation")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_depth_pyramid.yaml").read_text())
        entries = ["depthPyramidCopyMain", "depthPyramidReduceMain", "depthPyramidReadMain"]
        with tempfile.TemporaryDirectory(prefix="arp-depth-pyramid-") as directory:
            for mode in ("Native", "Emulated"):
                publication = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_depth_pyramid.slang",
                    entries, mode, directory, SLANGC, spirv_reflect_tool())
                result = subprocess.run([RUNTIME_VERIFIER, str(publication), "valid", "depth-pyramid"], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                package = read_bindless_publication(publication)
                self.assertEqual([m["entry"] for m in package["modules"]], entries)
                for module in package["modules"]:
                    reflection = yaml.safe_load((publication.parent / module["reflection"]).read_bytes())
                    spv = publication.parent / reflection["targets"]["spirv"]
                    validated = subprocess.run([str(spirv_validator_tool()), "--target-env", "vulkan1.2", str(spv)],
                        capture_output=True, text=True)
                    self.assertEqual(validated.returncode, 0, validated.stdout + validated.stderr)

    def test_storage_vector_domain_is_versioned_and_preserves_four_lanes(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_storage_vector.yaml").read_text())
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["storageImageContractVersion"], 1)
            self.assertIn('[[vk::image_format("rgba16f")]]', generated.source)
            self.assertIn('WTexture2D<float4>', generated.source)
            self.assertNotIn('Pyramids_load(', generated.source)
            self.assertIn('uint2 location, float4 value, out bool fault', generated.source)
        for mutate, reason in (
                (lambda s: s.pop("storageImageContractVersion"), "STORAGE-FORMAT"),
                (lambda s: s.update(storageImageContractVersion=True), "STORAGE-IMAGE-VERSION"),
                (lambda s: s.update(storageImageContractVersion=2), "STORAGE-IMAGE-VERSION"),
                (lambda s: s["tables"][0].update(access="ReadWrite"), "STORAGE-IMAGE-ACCESS"),
                (lambda s: s["tables"][0]["operations"].append("load"), "STORAGE-IMAGE-OPERATION"),
                (lambda s: s["tables"][0].update(format="rgba32float"), "STORAGE-FORMAT"),
                (lambda s: s["tables"].pop(0), "STORAGE-IMAGE-UNUSED")):
            broken = copy.deepcopy(schema); mutate(broken)
            with self.assertRaisesRegex(BindlessContractError, reason): generate_bindless(broken, "Emulated")

    def test_storage_vector_cross_target_package(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_storage_vector.yaml").read_text())
        with tempfile.TemporaryDirectory(prefix="arp-storage-vector-") as directory:
            for mode in ("Native", "Emulated"):
                publication = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_storage_vector.slang",
                    ["storeMain", "readMain"], mode, directory, SLANGC, spirv_reflect_tool())
                result = subprocess.run([RUNTIME_VERIFIER, str(publication), "valid", "storage-vector"], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                envelope = yaml.safe_load(publication.read_bytes())
                payload_path = publication.parent / envelope["payload"]
                payload = yaml.safe_load(payload_path.read_bytes())
                def verify(manifest, expected):
                    data = yaml.safe_dump(manifest, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    publication.write_text(yaml.safe_dump(dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest())))
                    result = subprocess.run([RUNTIME_VERIFIER, str(publication), expected, "storage-vector"], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                broken = copy.deepcopy(payload); broken.pop("storageImageContractVersion")
                verify(broken, "RHI-BINDLESS-PACKAGE-STORAGE-FORMAT")
                broken = copy.deepcopy(payload); broken["storageImageContractVersion"] = 2
                verify(broken, "RHI-BINDLESS-PACKAGE-STORAGE-IMAGE-VERSION")
                broken = copy.deepcopy(payload); broken["tables"][0]["operations"].append("load")
                verify(broken, "RHI-BINDLESS-PACKAGE-STORAGE-IMAGE-OPERATION")
                reflection = yaml.safe_load((publication.parent / payload["modules"][0]["reflection"]).read_bytes())
                spirv_path = publication.parent / reflection["targets"]["spirv"]
                original = spirv_path.read_bytes()
                words = list(struct.unpack(f"<{len(original)//4}I", original))
                for offset, opcode, _ in SpirvLayoutFacts(spirv_path).instructions:
                    if opcode == 25: words[offset + 8] = 3  # R32f instead of Rgba16f
                changed = struct.pack(f"<{len(words)}I", *words); spirv_path.write_bytes(changed)
                broken = copy.deepcopy(payload); broken["artifacts"][spirv_path.name] = hashlib.sha256(changed).hexdigest()
                verify(broken, "RHI-BINDLESS-PACKAGE-IMAGE-SHAPE-MISMATCH")
                spirv_path.write_bytes(original)
                if mode == "Emulated":
                    wgsl_path = publication.parent / reflection["targets"]["wgsl"]
                    original = wgsl_path.read_bytes()
                    for changed in (original.replace(b"rgba16float, write", b"r32float, write"),
                                    original.replace(b"rgba16float, write", b"rgba16float, read_write")):
                        self.assertNotEqual(changed, original); wgsl_path.write_bytes(changed)
                        broken = copy.deepcopy(payload); broken["artifacts"][wgsl_path.name] = hashlib.sha256(changed).hexdigest()
                        verify(broken, "RHI-BINDLESS-PACKAGE-STORAGE-IMAGE-WGSL-MISMATCH")
                    wgsl_path.write_bytes(original)
                verify(payload, "valid")

    def test_pipeline_interface_version_is_explicit_and_typed(self):
        for value in (True, "1", -1, 2, None):
            schema = copy.deepcopy(self.schema)
            schema["pipelineInterfaceContractVersion"] = value
            with self.assertRaisesRegex(BindlessContractError, "PIPELINE-INTERFACE-VERSION"):
                generate_bindless(schema, "Emulated")
        for mode in ("Native", "Emulated"):
            schema = copy.deepcopy(self.schema)
            schema["pipelineInterfaceContractVersion"] = 1
            self.assertEqual(generate_bindless(schema, mode).reflection["pipelineInterfaceContractVersion"], 1)

    def push_schema(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_graphics_matrix_ordinary.yaml").read_text())
        schema["name"] = "GraphicsMatrixPush"
        schema["pushConstants"] = dict(contractVersion=1, sizeBytes=224,
                                       stages=["vertex", "fragment"], webGpuGroup=3, webGpuBinding=0)
        return schema

    def test_push_intent_is_explicit_closed_and_budgeted(self):
        schema = self.push_schema()
        for mode in ("Native", "Emulated"):
            self.assertEqual(generate_bindless(schema, mode).reflection["pushConstants"],
                             dict(contractVersion=1, sizeBytes=224, stageMask=3,
                                  webGpuGroup=3, webGpuBinding=0))
            for field, value, reason in [("contractVersion", True, "VERSION"),
                                         ("contractVersion", 2, "VERSION"),
                                         ("sizeBytes", True, "SIZE"),
                                         ("sizeBytes", 0, "SIZE"),
                                         ("sizeBytes", 257, "SIZE"),
                                         ("sizeBytes", 223, "SIZE"),
                                         ("stages", ["vertex", "vertex"], "STAGES"),
                                         ("stages", ["vertex", "compute"], "STAGES"),
                                         ("stages", [], "STAGES"),
                                         ("webGpuGroup", True, "INVALID-PUSH"),
                                         ("webGpuGroup", 0x100000000, "INVALID-PUSH"),
                                         ("webGpuBinding", -1, "INVALID-PUSH")]:
                broken = copy.deepcopy(schema); broken["pushConstants"][field] = value
                with self.assertRaisesRegex(BindlessContractError, reason):
                    generate_bindless(broken, mode)
            for mutate in (lambda s: s.pop("pipelineInterfaceContractVersion"),
                           lambda s: s["pushConstants"].update(offsetBytes=0),
                           lambda s: s["pushConstants"].pop("webGpuBinding")):
                broken = copy.deepcopy(schema); mutate(broken)
                with self.assertRaisesRegex(BindlessContractError, "PUSH-SCHEMA"):
                    generate_bindless(broken, mode)

    def test_push_matrix_cross_target_publication_and_runtime_refusal(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for actual push target compilation")
        schema = self.push_schema()
        # Reuse the existing 224-byte shadow-shaped matrix/push declaration
        # and operation fixture, with a guarded bindless result side effect.
        source = (ROOT / "tests/fixtures/ordinary_graphics_matrix_push.slang").read_text()
        before = "return mul(Push.lightViewProjection[min(Push.cascadeIndex, 2u)], input.value) + Push.depthBiasAndFlags;"
        self.assertEqual(source.count(before), 1)
        source = source.replace(before,
            "float4 value = mul(Push.lightViewProjection[min(Push.cascadeIndex, 2u)], input.value) + Push.depthBiasAndFlags;\n"
            "    bool fault; Results_store(0u, 0u, asuint(value.x), fault);\n    return value;")
        with tempfile.TemporaryDirectory(prefix="arp-bindless-push-") as directory:
            authored = pathlib.Path(directory) / "push.slang"; authored.write_text(source)
            for mode in ("Native", "Emulated"):
                publication = compile_bindless_package(schema, authored, ["vertexMain", "fragmentMain"],
                    mode, directory, SLANGC, spirv_reflect_tool())
                payload = read_bindless_publication(publication)
                self.assertEqual(payload["pushConstants"]["stageMask"], 3)
                for module in payload["modules"]:
                    reflection = yaml.safe_load((pathlib.Path(directory) / module["reflection"]).read_text())
                    self.assertEqual(reflection["pushConstantSizeBytes"], 224)
                    self.assertEqual(reflection["pushConstantContractVersion"], "AtomicFullBlockPush/1")
                    self.assertIsInstance(reflection["pushConstantMetalBufferIndex"], int)
                    spirv = pathlib.Path(directory) / reflection["targets"]["spirv"]
                    self.assertEqual(SpirvLayoutFacts(spirv).push_constant_layouts()[0]["occupiedExtentBytes"], 224)
                    subprocess.run([str(spirv_validator_tool()), str(spirv)], check=True, capture_output=True)
                    if mode == "Emulated":
                        self.assertEqual(reflection["pushConstantWebGpuBinding"], dict(group=3, binding=0))
                if RUNTIME_VERIFIER:
                    checked = subprocess.run([RUNTIME_VERIFIER, str(publication),
                        "RHI-BINDLESS-PACKAGE-ENTRY-ABI", "pipeline-domains"], capture_output=True, text=True)
                    self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
                for mutate, reason in [(lambda s: s.pop("pushConstants"), "SOURCE-CONTRACT-UNSUPPORTED"),
                                       (lambda s: s["pushConstants"].update(sizeBytes=208), "PUSH-SOURCE-SIZE"),
                                       (lambda s: s["pushConstants"].update(stages=["vertex"]), "PUSH-UNDECLARED-STAGE"),
                                       (lambda s: s.update(maxBindings=3), "PIPELINE-BINDING-BUDGET")]:
                    broken = copy.deepcopy(schema); mutate(broken)
                    with self.assertRaisesRegex(BindlessContractError, reason):
                        compile_bindless_package(broken, authored, ["vertexMain", "fragmentMain"],
                            mode, directory, SLANGC, spirv_reflect_tool())
                    self.assertFalse(publication.exists())
                if mode == "Emulated":
                    broken = copy.deepcopy(schema); broken["pushConstants"]["webGpuGroup"] = schema["set"]
                    with self.assertRaisesRegex(BindlessContractError, "PUSH-WEBGPU-GROUP-COLLISION"):
                        compile_bindless_package(broken, authored, ["vertexMain", "fragmentMain"],
                            mode, directory, SLANGC, spirv_reflect_tool())
                    self.assertFalse(publication.exists())

    def test_pipeline_opaque_schema_is_explicit_and_closed(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pipeline_images.yaml").read_text())
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(len(generated.reflection["ordinaryOpaqueBindings"]), 7)
            self.assertEqual(generated.reflection["ordinaryOpaqueBindings"][0]["stageMask"], 6)
        bindings = get_shader_bindings("[[vk::binding(3, 2)]] DepthTexture2D ShadowMap;\n"
                                       "[[vk::binding(4, 2)]] Texture2D<float> ColorMap;")
        self.assertTrue(bindings[0]["depthTexture"])
        self.assertFalse(bindings[1].get("depthTexture", False))
        for change in (
            lambda s: s.pop("pipelineInterfaceContractVersion"),
            lambda s: s["ordinaryOpaqueBindings"][0].update(depthTexture="false"),
            lambda s: s["ordinaryOpaqueBindings"][0].update(sampledTextureFilterable=1),
            lambda s: s["ordinaryOpaqueBindings"][0].update(textureViewDimension="TextureCubeArray"),
            lambda s: s["ordinaryOpaqueBindings"][0].update(textureViewDimension=[]),
            lambda s: s["ordinaryOpaqueBindings"][0].update(type="StorageTexture"),
            lambda s: s["ordinaryOpaqueBindings"][0].update(arrayCount=2),
            lambda s: s["ordinaryOpaqueBindings"][0].update(set=1),
            lambda s: s["ordinaryOpaqueBindings"][0].update(stages=["compute", "compute"]),
            lambda s: s["ordinaryOpaqueBindings"][2].update(samplerFiltering="Unknown"),
            lambda s: s["ordinaryOpaqueBindings"][6].update(depthTexture=True),
            lambda s: s["ordinaryOpaqueBindings"].append(copy.deepcopy(s["ordinaryOpaqueBindings"][0])),
        ):
            broken = copy.deepcopy(schema); change(broken)
            with self.assertRaisesRegex(BindlessContractError, "PIPELINE-OPAQUE"):
                generate_bindless(broken, "Emulated")

    def test_graphics_color_gi_stage_and_target_contract(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_graphics_color_gi.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_color_gi.slang"
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            ordinary = generated.reflection["ordinaryOpaqueBindings"]
            self.assertEqual(len(ordinary), 4)
            self.assertEqual([b["stageMask"] for b in ordinary], [3, 2, 3, 2])
            self.assertEqual(sum(b["arrayCount"] for b in generated.reflection["bindings"]), 3)
            if SLANGC:
                with tempfile.TemporaryDirectory(prefix="arp-bindless-graphics-color-gi-") as directory:
                    path = compile_bindless_package(schema, source,
                        ["vertexMain", "cleanFragment", "faultFragment"], mode, directory,
                        SLANGC, spirv_reflect_tool())
                    payload = read_bindless_publication(path)
                    self.assertEqual(len(payload["modules"]), 3)
                    for module in payload["modules"]:
                        reflection = yaml.safe_load((path.parent / module["reflection"]).read_bytes())
                        subprocess.run([str(spirv_validator_tool()),
                            str(path.parent / reflection["targets"]["spirv"])],
                            check=True, capture_output=True)

    def test_graphics_depth_color_stage_and_target_contract(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_graphics_depth_color.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_graphics_depth_color.slang"
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            ordinary = generated.reflection["ordinaryOpaqueBindings"]
            self.assertEqual([b["stageMask"] for b in ordinary], [2] * 5)
            self.assertEqual([b["name"] for b in ordinary],
                             ["ColorTexture", "ColorSampler", "ShadowMap", "LessSampler", "GreaterSampler"])
            self.assertTrue(ordinary[2]["depthTexture"])
            self.assertEqual([b["samplerFiltering"] for b in ordinary if b["type"] == "Sampler"],
                             ["Filtering", "Comparison", "Comparison"])
            if SLANGC:
                with tempfile.TemporaryDirectory(prefix="arp-bindless-graphics-depth-color-") as directory:
                    path = compile_bindless_package(schema, source,
                        ["vertexMain", "cleanFragment", "faultFragment"], mode, directory,
                        SLANGC, spirv_reflect_tool())
                    payload = read_bindless_publication(path)
                    self.assertEqual(len(payload["modules"]), 3)
                    if RUNTIME_VERIFIER:
                        checked = subprocess.run(
                            [RUNTIME_VERIFIER, str(path), "valid", "graphics-depth-color"],
                            capture_output=True, text=True)
                        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
                        self.assertIn("BINDLESS_GRAPHICS_DEPTH_COLOR_PACKAGE_OK", checked.stdout)
                    for module in payload["modules"]:
                        reflection = yaml.safe_load((path.parent / module["reflection"]).read_bytes())
                        targets = reflection["targets"]
                        self.assertIn("metal", targets)
                        binary = path.parent / targets["spirv"]
                        subprocess.run([str(spirv_validator_tool()), str(binary)],
                                       check=True, capture_output=True)
                        facts = SpirvLayoutFacts(binary)
                        # SPIR-V OpImageSampleDrefExplicitLod must survive. No
                        # implicit derivative dependency is introduced by the
                        # authored SampleCmpLevelZero in either fragment entry.
                        opcodes = [op for _, op, _ in facts.instructions]
                        fragment = reflection["entryPoints"][0]["stage"] == "Fragment"
                        if fragment:
                            self.assertGreaterEqual(opcodes.count(90), 2)
                        else:
                            self.assertNotIn(90, opcodes)
                        self.assertNotIn(89, opcodes)  # OpImageSampleDrefImplicitLod
                        if mode == "Emulated":
                            self.assertIn("wgsl", targets)
                            wgsl = (path.parent / targets["wgsl"]).read_text()
                            if fragment:
                                self.assertIn("texture_depth_2d", wgsl)
                                self.assertEqual(wgsl.count("sampler_comparison"), 2)
                                self.assertEqual(wgsl.count("textureSampleCompareLevel("), 2)

    def test_graphics_hdr_gi_stage_and_target_contract(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_graphics_hdr_gi.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_hdr_gi.slang"
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            ordinary = generated.reflection["ordinaryOpaqueBindings"]
            self.assertEqual(len(ordinary), 4)
            self.assertEqual([b["stageMask"] for b in ordinary], [3, 2, 3, 2])
            self.assertEqual(sum(b["arrayCount"] for b in generated.reflection["bindings"]), 3)
            if SLANGC:
                with tempfile.TemporaryDirectory(prefix="arp-bindless-graphics-hdr-gi-") as directory:
                    path = compile_bindless_package(schema, source,
                        ["vertexMain", "cleanFragment", "faultFragment"], mode, directory,
                        SLANGC, spirv_reflect_tool())
                    payload = read_bindless_publication(path)
                    self.assertEqual(len(payload["modules"]), 3)
                    for module in payload["modules"]:
                        reflection = yaml.safe_load((path.parent / module["reflection"]).read_bytes())
                        subprocess.run([str(spirv_validator_tool()),
                            str(path.parent / reflection["targets"]["spirv"])],
                            check=True, capture_output=True)

    def test_graphics_wocc_ibl_stage_and_target_contract(self):
        self.assert_wocc_ibl_stage_and_target_contract(False)

    def test_graphics_wocc_ibl_direct_reference_contract(self):
        self.assert_wocc_ibl_stage_and_target_contract(True)

    def assert_wocc_ibl_stage_and_target_contract(self, reference):
        schema_name = "bindless_graphics_wocc_ibl_reference.yaml" if reference else "bindless_graphics_wocc_ibl.yaml"
        source_name = "ordinary_wocc_ibl_reference.slang" if reference else "bindless_wocc_ibl.slang"
        schema = yaml.safe_load((ROOT / "tests/fixtures" / schema_name).read_text())
        source = ROOT / "tests/fixtures" / source_name
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            ordinary = generated.reflection["ordinaryOpaqueBindings"]
            self.assertEqual(len(ordinary), 4)
            self.assertEqual([b["stageMask"] for b in ordinary], [3, 2, 3, 3])
            self.assertEqual(sum(b["arrayCount"] for b in generated.reflection["bindings"]), 3)
            if SLANGC:
                with tempfile.TemporaryDirectory(prefix="arp-bindless-graphics-wocc-ibl-") as directory:
                    path = compile_bindless_package(schema, source,
                        ["vertexMain", "cleanFragment", "faultFragment"], mode, directory,
                        SLANGC, spirv_reflect_tool())
                    payload = read_bindless_publication(path)
                    self.assertEqual(len(payload["modules"]), 3)
                    for module in payload["modules"]:
                        reflection = yaml.safe_load((path.parent / module["reflection"]).read_bytes())
                        self.assertIn("spirv", reflection["targets"])
                        self.assertIn("metal", reflection["targets"])
                        if mode == "Emulated":
                            self.assertIn("wgsl", reflection["targets"])
                        direct = [b for b in reflection["bindings"] if b["name"] == "DirectResults"]
                        if reference and reflection["entryPoints"][0]["stage"] == "Fragment":
                            self.assertEqual([(b["set"], b["binding"], b["type"], b["arrayCount"], b["stageMask"])
                                              for b in direct], [(0, 4, "StorageBuffer", 1, 2)])
                        else:
                            self.assertEqual(direct, [])
                        subprocess.run([str(spirv_validator_tool()),
                            str(path.parent / reflection["targets"]["spirv"])],
                            check=True, capture_output=True)

    def test_pipeline_opaque_graphics_selected_entry(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass shader tools and runtime verifier for original Graphics proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pipeline_images.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_pipeline_images.slang"
        for binding in schema["ordinaryOpaqueBindings"]:
            if binding["binding"] in (0, 2):
                binding["stages"] = ["vertex", "compute", "fragment"]
        with tempfile.TemporaryDirectory(prefix="arp-bindless-graphics-images-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, source,
                    ["computeMain", "vertexMain", "fragmentMain"], mode, directory,
                    SLANGC, spirv_reflect_tool())
                result = subprocess.run([RUNTIME_VERIFIER, str(path), "valid", "graphics-root-images"],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("BINDLESS_ORDINARY_OPAQUE_GRAPHICS_ROOT1_OK images5 samplers2", result.stdout)
                self.assertIn("selected-fragment vertex-cube-sampler inactive-vertex generation-range hot0 execution-closed", result.stdout)
                payload = read_bindless_publication(path)
                for module in payload["modules"]:
                    reflection = yaml.safe_load((path.parent / module["reflection"]).read_bytes())
                    subprocess.run([str(spirv_validator_tool()), str(path.parent / reflection["targets"]["spirv"])],
                        check=True, capture_output=True)

    def test_pipeline_opaque_targets_and_resealed_refusals(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass shader tools and runtime verifier for opaque declaration proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pipeline_images.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_pipeline_images.slang"
        with tempfile.TemporaryDirectory(prefix="arp-bindless-pipeline-images-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, source, ["computeMain", "fragmentMain"],
                    mode, directory, SLANGC, spirv_reflect_tool())
                def verify(expected="valid", option="pipeline-images"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, option], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    if expected == "valid" and option == "pipeline-root-images":
                        self.assertIn("BINDLESS_ORDINARY_OPAQUE_ROOT1_OK images5 samplers2", result.stdout)
                verify()
                verify(option="pipeline-root-images")
                verify("RHI-BINDLESS-PACKAGE-BINDING-BUDGET", "bindings")
                envelope = yaml.safe_load(path.read_bytes())
                payload_path = path.parent / envelope["payload"]
                payload = yaml.safe_load(payload_path.read_bytes())
                def publish(manifest):
                    data = yaml.safe_dump(manifest, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    path.write_text(yaml.safe_dump(dict(envelope,
                        shaderPackageIdentity=hashlib.sha256(data).hexdigest()), sort_keys=False))
                broken = copy.deepcopy(payload)
                broken["ordinaryOpaqueBindings"][0]["textureViewDimension"] = "Texture2D"
                publish(broken); verify("RHI-BINDLESS-PACKAGE-OPAQUE-LOWERING-MISMATCH")
                publish(payload)
                module = payload["modules"][0]
                reflected = yaml.safe_load((path.parent / module["reflection"]).read_bytes())
                binary_path = path.parent / reflected["targets"]["spirv"]
                original_binary = binary_path.read_bytes()
                facts = SpirvLayoutFacts(binary_path)
                cubes = [(o, a) for o, op, a in facts.instructions if op == 25 and a[2] == 3]
                self.assertEqual(len(cubes), 1)
                words = list(struct.unpack(f"<{len(original_binary) // 4}I", original_binary))
                words[cubes[0][0] + 3] = 1  # Cube -> 2D, without changing reflection.
                binary_path.write_bytes(struct.pack(f"<{len(words)}I", *words))
                broken = copy.deepcopy(payload)
                broken["artifacts"][binary_path.name] = hashlib.sha256(binary_path.read_bytes()).hexdigest()
                publish(broken); verify("RHI-BINDLESS-PACKAGE-ORDINARY-IMAGE-BINARY")
                binary_path.write_bytes(original_binary); publish(payload)
                if mode == "Emulated":
                    wgsl_path = path.parent / reflected["targets"]["wgsl"]
                    original = wgsl_path.read_bytes()
                    for before, after in ((b"texture_cube<f32>", b"texture_2d<f32>"),
                                          (b"texture_2d_array<f32>", b"texture_2d<f32>"),
                                          (b"texture_3d<f32>", b"texture_3d<u32>"),
                                          (b"texture_depth_2d", b"texture_2d<f32>"),
                                          (b"sampler_comparison", b"sampler")):
                        self.assertEqual(original.count(before), 1, original.decode())
                        wgsl_path.write_bytes(original.replace(before, after))
                        broken = copy.deepcopy(payload)
                        broken["artifacts"][wgsl_path.name] = hashlib.sha256(wgsl_path.read_bytes()).hexdigest()
                        publish(broken); verify("RHI-BINDLESS-PACKAGE-ORDINARY-OPAQUE-WGSL")
                    wgsl_path.write_bytes(original); publish(payload)
                verify()
            for change in (lambda s: s["ordinaryOpaqueBindings"].pop(),
                           lambda s: s["ordinaryOpaqueBindings"][0].update(stages=["compute"]),
                           lambda s: s["ordinaryOpaqueBindings"][0].update(textureViewDimension="Texture2D")):
                broken = copy.deepcopy(schema); change(broken)
                with self.assertRaisesRegex(BindlessContractError, "PIPELINE-OPAQUE"):
                    compile_bindless_package(broken, source, ["computeMain", "fragmentMain"],
                        "Emulated", directory, SLANGC, spirv_reflect_tool())
                self.assertFalse((path.parent / "PipelineImages.emulated.bindless.yaml").exists())
            ambiguous = pathlib.Path(directory) / "ambiguous.slang"
            ambiguous.write_text(source.read_text().replace("DepthTexture2D ShadowMap", "Texture2D<float> ShadowMap"))
            with self.assertRaisesRegex(BindlessContractError, "PIPELINE-OPAQUE-BINARY"):
                compile_bindless_package(schema, ambiguous, ["computeMain", "fragmentMain"],
                    "Native", directory, SLANGC, spirv_reflect_tool())

    def test_color_gi_cross_target_and_original_root(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_color_gi.yaml").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="arp-bindless-color-gi-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_color_gi.slang",
                    ["cleanMain", "faultMain"], mode, directory, SLANGC, spirv_reflect_tool())
                for option in ("color-gi", "color-gi-root"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), "valid", option], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_graphics_domains_cross_target_and_original_root(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_graphics_domains.yaml").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="arp-bindless-graphics-domains-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_graphics_domains.slang",
                    ["vertexMain", "cleanFragment", "faultFragment"], mode, directory, SLANGC, spirv_reflect_tool())
                for option in ("graphics-domains", "graphics-root-domains"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), "valid", option], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pipeline_domains_cross_target_and_runtime_inspection(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pipeline_domains.yaml").read_text())
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-domains-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_pipeline_domains.slang",
                    ["computeMain", "fragmentMain"], mode, directory, SLANGC, reflect)
                def verify(expected="valid", option="pipeline-domains"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, option], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                verify()
                verify(option="pipeline-root-domains")
                verify("RHI-BINDLESS-PACKAGE-BINDING-BUDGET", "bindings")
                envelope = yaml.safe_load(path.read_bytes())
                payload_path = path.parent / envelope["payload"]
                payload = yaml.safe_load(payload_path.read_bytes())
                def publish(manifest):
                    data = yaml.safe_dump(manifest, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    path.write_text(yaml.safe_dump(dict(envelope,
                        shaderPackageIdentity=hashlib.sha256(data).hexdigest()), sort_keys=False))
                # Resealing all file hashes must not turn a forged Uniform /
                # StorageBuffer class or descriptor count into a valid ABI.
                module = payload["modules"][0]
                reflected_path = path.parent / module["reflection"]
                original_reflected = reflected_path.read_bytes()
                for change in (dict(type="StorageBuffer"), dict(arrayCount=2)):
                    reflected = yaml.safe_load(original_reflected)
                    next(b for b in reflected["bindings"] if b["set"] == 0).update(change)
                    reflected_path.write_text(yaml.safe_dump(reflected, sort_keys=False))
                    broken = copy.deepcopy(payload)
                    broken["modules"][0]["bindings"] = reflected["bindings"]
                    broken["artifacts"][reflected_path.name] = hashlib.sha256(reflected_path.read_bytes()).hexdigest()
                    publish(broken)
                    verify("RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-BINARY"
                           if mode == "Native" or "type" in change else "RHI-BINDLESS-PACKAGE-ORDINARY-BINDING-SHAPE")
                    reflected_path.write_bytes(original_reflected); publish(payload)
                if mode == "Emulated":
                    wgsl_path = path.parent / yaml.safe_load(original_reflected)["targets"]["wgsl"]
                    original_wgsl = wgsl_path.read_bytes()
                    self.assertEqual(original_wgsl.count(b"var<uniform>"), 1)
                    wgsl_path.write_bytes(original_wgsl.replace(b"var<uniform>", b"var<storage, read>"))
                    broken = copy.deepcopy(payload)
                    broken["artifacts"][wgsl_path.name] = hashlib.sha256(wgsl_path.read_bytes()).hexdigest()
                    publish(broken); verify("RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-WGSL")
                    wgsl_path.write_bytes(original_wgsl); publish(payload)
                    # Binding kind/address space alone cannot establish frame /
                    # instance member ABI. Fully reseal same-size scalar and
                    # vector mutations and a runtime element-stride mutation.
                    for declaration, replacement in (
                        (b"FrameValues_0 : vec4<f32>", b"FrameValues_0 : vec4<i32>"),
                        (b"FrameValues_0 : vec4<f32>", b"FrameValues_0 : vec2<f32>"),
                        (b"SharedValues_0 : array<u32>", b"SharedValues_0 : array<i32>"),
                        (b"SharedValues_0 : array<u32>", b"SharedValues_0 : array<vec2<u32>>"),
                    ):
                        self.assertEqual(original_wgsl.count(declaration), 1)
                        wgsl_path.write_bytes(original_wgsl.replace(declaration, replacement))
                        broken = copy.deepcopy(payload)
                        broken["artifacts"][wgsl_path.name] = hashlib.sha256(wgsl_path.read_bytes()).hexdigest()
                        publish(broken); verify("RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY")
                        wgsl_path.write_bytes(original_wgsl); publish(payload)
                payload["pipelineInterfaceContractVersion"] = 2
                data = yaml.safe_dump(payload, sort_keys=False).encode()
                payload_path.write_bytes(data)
                envelope["shaderPackageIdentity"] = hashlib.sha256(data).hexdigest()
                path.write_text(yaml.safe_dump(envelope, sort_keys=False))
                verify("RHI-BINDLESS-PACKAGE-PIPELINE-INTERFACE-VERSION")

    def test_pipeline_external_domains_require_proven_interface(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for compiler publication gates")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pipeline_domains.yaml").read_text())
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-domains-negative-") as directory:
            schema.pop("pipelineInterfaceContractVersion")
            with self.assertRaisesRegex(BindlessContractError, "PIPELINE-INTERFACE-REQUIRED"):
                compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_pipeline_domains.slang",
                    ["computeMain", "fragmentMain"], "Native", directory, SLANGC, reflect)
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))
            schema["pipelineInterfaceContractVersion"] = 1
            source = pathlib.Path(directory) / "unsupported.slang"
            source.write_text("[[vk::binding(0, 2)]] TextureCube<float4> Environment;\n")
            with self.assertRaisesRegex(BindlessContractError, "PIPELINE-ORDINARY-SHAPE-UNAVAILABLE"):
                compile_bindless_package(schema, source, ["computeMain"], "Native", directory, SLANGC, reflect)
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))

    def test_pipeline_frame_instance_members_require_exact_memory(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for member proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_pipeline_domains.yaml").read_text())
        schema["name"] = "PipelineMembers"
        with tempfile.TemporaryDirectory(prefix="arp-bindless-members-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_pipeline_members.slang",
                    ["computeMain", "fragmentMain"], mode, directory, SLANGC, spirv_reflect_tool())
                def verify(expected="valid"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, "pipeline-domains"],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                verify()
                envelope = yaml.safe_load(path.read_bytes())
                payload_path = path.parent / envelope["payload"]
                payload = yaml.safe_load(payload_path.read_bytes())
                # A shared binding must retain its complete binary member ABI
                # across separately compiled selected entries in both modes.
                fragment = yaml.safe_load((path.parent / payload["modules"][1]["reflection"]).read_bytes())
                binary_path = path.parent / fragment["targets"]["spirv"]
                original_binary = binary_path.read_bytes()
                binary_facts = SpirvLayoutFacts(binary_path)
                block = binary_facts.buffer_layouts()[(0, 3)]["blockTypeId"]
                words = list(struct.unpack(f"<{len(original_binary) // 4}I", original_binary))
                offsets = [(offset, operands) for offset, opcode, operands in binary_facts.instructions
                           if opcode == 72 and operands[:3] == (block, 1, 35)]
                self.assertEqual(len(offsets), 1)
                offset, operands = offsets[0]
                self.assertEqual(operands[3], 16)
                words[offset + 4] = 32
                binary_path.write_bytes(struct.pack(f"<{len(words)}I", *words))
                broken = copy.deepcopy(payload)
                broken["artifacts"][binary_path.name] = hashlib.sha256(binary_path.read_bytes()).hexdigest()
                def publish(manifest):
                    data = yaml.safe_dump(manifest, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    path.write_text(yaml.safe_dump(dict(envelope,
                        shaderPackageIdentity=hashlib.sha256(data).hexdigest()), sort_keys=False))
                publish(broken)
                verify("RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-STAGE-MEMORY")
                binary_path.write_bytes(original_binary); publish(payload)
                if mode == "Native":
                    continue
                reflected = yaml.safe_load((path.parent / payload["modules"][0]["reflection"]).read_bytes())
                target_path = path.parent / reflected["targets"]["wgsl"]
                original = target_path.read_bytes()
                # Same byte size but different scalar identity, and a valid
                # declaration that moves the second member without changing
                # the descriptor kind or address space.
                for before, after in ((b"indices_0 : vec4<u32>", b"indices_0 : vec4<i32>"),
                                      (b"indices_0 : vec4<u32>", b"@size(32) indices_0 : vec4<u32>"),
                                      (b"tint_0 : vec4<f32>", b"tint_0 : vec4<i32>")):
                    self.assertEqual(original.count(before), 1, original.decode())
                    target_path.write_bytes(original.replace(before, after))
                    broken = copy.deepcopy(payload)
                    broken["artifacts"][target_path.name] = hashlib.sha256(target_path.read_bytes()).hexdigest()
                    data = yaml.safe_dump(broken, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    path.write_text(yaml.safe_dump(dict(envelope,
                        shaderPackageIdentity=hashlib.sha256(data).hexdigest()), sort_keys=False))
                    verify("RHI-BINDLESS-PACKAGE-ORDINARY-BUFFER-MEMORY")
                target_path.write_bytes(original)

    def test_sampled_depth_integer_mip_schema_and_refusals(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_sampled_depth.yaml").read_text())
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["sampledDepthContractVersion"], 1)
            self.assertIn("#define ARP_SAMPLED_DEPTH(name) DepthTexture2D name", generated.source)
            self.assertIn("float Depths_load", generated.source)
            self.assertIn("float Depths_sampleLevel(uint index, uint samplerIndex, float2 uv, ArpBindlessDepthMip mip", generated.source)
            self.assertIn("uint level = mip.value;", generated.source)
            self.assertIn("if (level >= levels)", generated.source)
            self.assertTrue(all(b["depthTexture"] for b in generated.reflection["bindings"] if b["type"] == "SampledTexture"))
            self.assertTrue(all(b["samplerFiltering"] == "NonFiltering"
                                for b in generated.reflection["bindings"] if b["type"] == "Sampler"))
        for mutate, diagnostic in (
            (lambda s: s.pop("sampledDepthContractVersion"), "SAMPLED-DEPTH-VERSION"),
            (lambda s: s.update(sampledDepthContractVersion=True), "SAMPLED-DEPTH-VERSION"),
            (lambda s: s.update(sampledDepthContractVersion=2), "SAMPLED-DEPTH-VERSION"),
            (lambda s: s["tables"][0].update(depthTexture=1), "SAMPLED-DEPTH-DOMAIN"),
            (lambda s: s["tables"][1].update(depthTexture=True), "SAMPLED-DEPTH-DOMAIN"),
            (lambda s: s["tables"][0].update(dimension="Texture2DArray"), "SAMPLED-DEPTH-DOMAIN"),
            (lambda s: s["tables"][0]["operations"].append("sampleGrad"), "SAMPLED-DEPTH-OPERATION"),
            (lambda s: s["tables"][0]["operations"].append("sampleCompare"), "OPERATION-UNSUPPORTED"),
            (lambda s: s["tables"][0].pop("depthTexture"), "SAMPLED-DEPTH-UNUSED"),
        ):
            broken = copy.deepcopy(schema); mutate(broken)
            with self.assertRaisesRegex(BindlessContractError, diagnostic):
                generate_bindless(broken, "Emulated")

    def test_depth_integer_mip_lowering_preserves_color_and_nested_expressions(self):
        source = ("@binding(0) @group(0) var Depths_0_0 : texture_depth_2d;\n"
                  "@binding(1) @group(0) var Colors_0 : texture_2d<f32>;\n"
                  "let depth = textureSampleLevel((Depths_0_0), s, vec2<f32>(x, y), (f32(level)));\n"
                  "let color = textureSampleLevel((Colors_0), s, vec2<f32>(x, y), 0.5);\n")
        with tempfile.TemporaryDirectory(prefix="arp-depth-mip-") as directory:
            path = pathlib.Path(directory) / "depth.wgsl"
            path.write_text(source)
            lower_wgsl_depth_integer_mip_sampling(path, ["Depths_0"])
            self.assertIn("i32((f32(level)))", path.read_text())
            self.assertIn("vec2<f32>(x, y), 0.5)", path.read_text())
            for broken in (source.replace("texture_depth_2d", "texture_2d<f32>"),
                           source.replace("Depths_0_0", "Missing_0"),
                           source.replace("(f32(level))", "(f32(level)), vec2<i32>(0, 0)")):
                path.write_text(broken)
                with self.assertRaises(RuntimeError): lower_wgsl_depth_integer_mip_sampling(path, ["Depths_0"])
                self.assertEqual(path.read_text(), broken)

    def test_sampled_depth_cross_target_package_and_resealed_mismatch(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_sampled_depth.yaml").read_text())
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-depth-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_sampled_depth.slang",
                    ["cleanMain", "faultMain", "publicCleanMain", "nativeSingleCellMain"],
                    mode, directory, SLANGC, reflect)
                envelope = yaml.safe_load(path.read_bytes())
                payload_path = path.parent / envelope["payload"]
                payload = yaml.safe_load(payload_path.read_bytes())
                def publish(manifest):
                    data = yaml.safe_dump(manifest, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    path.write_text(yaml.safe_dump(dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest()), sort_keys=False))
                def verify(expected="valid"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, "sampled-depth"], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                verify()
                for module in payload["modules"]:
                    reflection = yaml.safe_load((path.parent / module["reflection"]).read_bytes())
                    spirv_path = path.parent / reflection["targets"]["spirv"]
                    facts = SpirvLayoutFacts(spirv_path)
                    images = facts.descriptor_image_types()
                    self.assertTrue(images)
                    self.assertTrue(all(f["depth"] == 1 for f in images.values() if f["kind"] == "Image"))
                    metal = (path.parent / reflection["targets"]["metal"]).read_text()
                    self.assertIn("depth2d<float", metal)
                    if mode == "Emulated":
                        wgsl = (path.parent / reflection["targets"]["wgsl"]).read_text()
                        self.assertIn("texture_depth_2d", wgsl)
                        self.assertRegex(wgsl, r"textureSampleLevel\([^\n]+,\s*i32\(")
                    original = spirv_path.read_bytes()
                    sampler_offsets = [offset for offset, opcode, _ in facts.instructions if opcode == 26]
                    if sampler_offsets:
                        if mode == "Emulated":
                            wgsl_path = path.parent / reflection["targets"]["wgsl"]
                            original_wgsl = wgsl_path.read_bytes()
                            forged_wgsl = original_wgsl.replace(b": sampler;", b": sampler_comparison;")
                            self.assertNotEqual(forged_wgsl, original_wgsl)
                            wgsl_path.write_bytes(forged_wgsl)
                            broken = copy.deepcopy(payload)
                            broken["artifacts"][wgsl_path.name] = hashlib.sha256(forged_wgsl).hexdigest()
                            publish(broken); verify("RHI-BINDLESS-PACKAGE-SAMPLER-WGSL-MISMATCH")
                            wgsl_path.write_bytes(original_wgsl); publish(payload)
                        sampler_words = list(struct.unpack(f"<{len(original)//4}I", original))
                        sampler_offset = sampler_offsets[0]
                        sampler_words[sampler_offset] = (sampler_words[sampler_offset] & 0xffff0000) | 25
                        spirv_path.write_bytes(struct.pack(f"<{len(sampler_words)}I", *sampler_words))
                        broken = copy.deepcopy(payload)
                        broken["artifacts"][spirv_path.name] = hashlib.sha256(spirv_path.read_bytes()).hexdigest()
                        publish(broken); verify("RHI-BINDLESS-PACKAGE-SAMPLER-SHAPE-MISMATCH")
                        spirv_path.write_bytes(original); publish(payload)
                    words = list(struct.unpack(f"<{len(original)//4}I", original))
                    image_offsets = [offset for offset, opcode, _ in facts.instructions if opcode == 25]
                    self.assertTrue(image_offsets)
                    for offset in image_offsets: words[offset + 4] = 0  # Depth=0 color counterexample
                    spirv_path.write_bytes(struct.pack(f"<{len(words)}I", *words))
                    broken = copy.deepcopy(payload)
                    broken["artifacts"][spirv_path.name] = hashlib.sha256(spirv_path.read_bytes()).hexdigest()
                    publish(broken)
                    verify("RHI-BINDLESS-PACKAGE-IMAGE-SHAPE-MISMATCH")
                    spirv_path.write_bytes(original); publish(payload)
                    reflection_path = path.parent / module["reflection"]
                    original_reflection = reflection_path.read_bytes()
                    changed_reflection = copy.deepcopy(reflection)
                    for binding in changed_reflection["bindings"]:
                        if binding["type"] == "SampledTexture": binding["depthTexture"] = False
                    changed = yaml.safe_dump(changed_reflection, sort_keys=False).encode()
                    reflection_path.write_bytes(changed)
                    broken = copy.deepcopy(payload)
                    next(m for m in broken["modules"] if m["entry"] == module["entry"])["bindings"] = changed_reflection["bindings"]
                    broken["artifacts"][reflection_path.name] = hashlib.sha256(changed).hexdigest()
                    publish(broken); verify("RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-REFLECTION")
                    reflection_path.write_bytes(original_reflection); publish(payload)
                broken = copy.deepcopy(payload); broken["sampledDepthContractVersion"] = 2
                publish(broken); verify("RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-VERSION")
                broken = copy.deepcopy(payload)
                next(b for b in broken["bindings"] if b["type"] == "Sampler")["samplerFiltering"] = "Filtering"
                publish(broken); verify("RHI-BINDLESS-PACKAGE-SAMPLER-FILTERING")
                broken = copy.deepcopy(payload)
                lowering = yaml.safe_load(broken["loweringYaml"]); lowering.pop("sampledDepthContractVersion")
                broken["loweringYaml"] = yaml.safe_dump(lowering, sort_keys=False)
                broken["loweringSha256"] = hashlib.sha256(broken["loweringYaml"].encode()).hexdigest()
                publish(broken); verify("RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-VERSION")
                broken = copy.deepcopy(payload)
                for binding in broken["bindings"]:
                    if binding["type"] == "SampledTexture": binding["depthTexture"] = False
                publish(broken); verify("RHI-BINDLESS-PACKAGE-SAMPLED-DEPTH-DOMAIN")
                publish(payload); verify()

    def test_sampled_depth_rejects_implicit_fractional_lod_and_invalidates_publication(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for target type checking")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_sampled_depth.yaml").read_text())
        reflect = spirv_reflect_tool()
        original = (ROOT / "tests/fixtures/bindless_sampled_depth.slang").read_text()
        with tempfile.TemporaryDirectory(prefix="arp-depth-fractional-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_sampled_depth.slang",
                    ["cleanMain", "faultMain"], mode, directory, SLANGC, reflect)
                source = pathlib.Path(directory) / "fractional.slang"
                source.write_text(original.replace("ArpBindlessDepthMip(tid.x)", "0.5f"))
                errors = io.StringIO()
                with contextlib.redirect_stderr(errors), self.assertRaises(SystemExit) as failure:
                    compile_bindless_package(schema, source, ["cleanMain", "faultMain"], mode, directory, SLANGC, reflect)
                self.assertNotEqual(failure.exception.code, 0)
                self.assertIn("expected an expression of type 'ArpBindlessDepthMip', got 'float'", errors.getvalue())
                self.assertFalse(path.exists())

    def test_atomic_storage_preserves_emitted_clones_and_rejects_partial_rewrites(self):
        source = "void helper() { InterlockedAdd(Counters[0], 1u, old); }"
        emitted = ("var<storage, read_write> Counters_0 : array<u32>;\n"
                   "fn helper() {\n"
                   "  atomicAdd(&(Counters_0[0u]), 1u);\n"
                   "  Counters_0[0u] = 2u;\n"
                   "  let x = Counters_0[0u];\n}\n"
                   "fn cloned() {\n  atomicAdd(&(Counters_0[0u]), 3u);\n}\n")
        with tempfile.TemporaryDirectory(prefix="arp-atomic-clone-") as directory:
            path = pathlib.Path(directory) / "cloned.wgsl"
            path.write_text(emitted, encoding="utf-8")
            lower_wgsl_atomic_storage_buffers(path, source)
            lowered = path.read_text(encoding="utf-8")
            self.assertIn("array<atomic<u32>>", lowered)
            self.assertIn("atomicStore(&(Counters_0[0u]), 2u)", lowered)
            self.assertIn("atomicLoad(&(Counters_0[0u]))", lowered)
            self.assertEqual(lowered.count("atomicAdd("), 2)
            expected = {"Counters_0": {"Add": 2, "Or": 0}}
            validate_wgsl_atomic_storage_buffers(path, lowered, expected)
            with self.assertRaisesRegex(RuntimeError, "expected 2"):
                validate_wgsl_atomic_storage_buffers(path,
                    lowered.replace("atomicAdd(&(Counters_0[0u]), 3u);", ""), expected)
            for malformed, diagnostic in (
                (emitted.replace("atomicAdd", "atomicOr"), "unsupported or missing"),
                (emitted.replace("  let x =", "  Counters_0[0u] += 1u;\n  let x ="), "unsupported indexed write"),
            ):
                path.write_text(malformed, encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, diagnostic):
                    lower_wgsl_atomic_storage_buffers(path, source)
                self.assertEqual(path.read_text(encoding="utf-8"), malformed)

    def test_atomic_single_entry_keeps_remaining_accesses_and_rejects_unauthorized_operations(self):
        source = ("void cull() { InterlockedAdd(Counts[0], 1u, old); } "
                  "void fault() { InterlockedOr(Control[0], 1u, old); }")
        reset = ("var<storage, read_write> Counts_0 : array<u32>;\n"
                 "var<storage, read_write> Control_0 : array<u32>;\n"
                 "fn reset() {\n  Counts_0[0u] = 0u;\n  Control_0[0u] = 0u;\n}\n")
        with tempfile.TemporaryDirectory(prefix="arp-atomic-entry-") as directory:
            path = pathlib.Path(directory) / "entry.wgsl"
            path.write_text(reset, encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "unsupported or missing"):
                lower_wgsl_atomic_storage_buffers(path, source)
            lower_wgsl_atomic_storage_buffers(path, source, entry_filtered=True)
            lowered = path.read_text(encoding="utf-8")
            self.assertEqual(lowered.count("array<atomic<u32>>"), 2)
            self.assertEqual(lowered.count("atomicStore("), 2)
            self.assertNotIn("atomicAdd(", lowered)
            # An inactive resource may disappear; a malformed remaining resource may not.
            path.write_text("fn noResources() {}\n", encoding="utf-8")
            lower_wgsl_atomic_storage_buffers(path, source, entry_filtered=True)
            path.write_text(reset.replace("Counts_0 : array<u32>", "Counts_0 : u32"), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "no WGSL array"):
                lower_wgsl_atomic_storage_buffers(path, source, entry_filtered=True)
            path.write_text(reset.replace("  Counts_0[0u] = 0u;",
                "  atomicOr(&(Counts_0[0u]), 1u);"), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "unsupported or missing"):
                lower_wgsl_atomic_storage_buffers(path, source, entry_filtered=True)

    def test_vector_record_schema_rejects_unproven_shapes_and_atomics(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_original_graphics_records.yaml").read_text())
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual([t["bufferLayout"]["elementStrideBytes"] for t in generated.reflection["tables"]], [32, 64, 48])
            self.assertEqual(generated.reflection["vectorRecordContractVersion"], 1)
            self.assertIn("RWStructuredBuffer<ArpBindlessRecord_Outputs>", generated.source)
        for mutation, diagnostic in (
            (lambda s: s.pop("vectorRecordContractVersion"), "RECORD-DOMAIN"),
            (lambda s: s.update(vectorRecordContractVersion=2), "RECORD-VERSION"),
            (lambda s: s["tables"][0]["record"]["fields"][0].update(type="float3"), "RECORD-FIELD"),
            (lambda s: s["tables"][0]["record"]["fields"][0].update(type="float4x4"), "RECORD-FIELD"),
            (lambda s: s["tables"][0]["record"]["fields"][1].update(name="color"), "RECORD-FIELD"),
            (lambda s: s["tables"][2]["operations"].append("atomicAdd"), "RECORD-ATOMIC-UNSUPPORTED"),
        ):
            broken = copy.deepcopy(schema); mutation(broken)
            with self.assertRaisesRegex(BindlessContractError, diagnostic): generate_bindless(broken, "Emulated")

    def test_vector_record_runtime_independently_checks_rehashed_binary_and_wgsl(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_original_graphics_records.yaml").read_text())
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-record-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_original_graphics_records.slang",
                    ["vertexUnindexed" if mode == "Emulated" else "vertexMain", "cleanFragment", "faultFragment"],
                    mode, directory, SLANGC, reflect)
                def verify(expected="valid"):
                    profile = "root-record-fragment-package" if mode == "Emulated" else "root-graphics-records"
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, profile], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                verify()
                envelope = yaml.safe_load(path.read_text())
                payload_path = path.parent / envelope["payload"]
                payload = yaml.safe_load(payload_path.read_bytes())
                originals = {module["reflection"]: (path.parent / module["reflection"]).read_bytes()
                             for module in payload["modules"]}
                def publish(manifest):
                    data = yaml.safe_dump(manifest, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    path.write_text(yaml.safe_dump(dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest()), sort_keys=False))
                broken = copy.deepcopy(payload)
                layout = broken["tables"][2]["bufferLayout"]
                layout["record"]["fields"][2]["type"] = "uint4"
                layout.update(BufferVectorRecord(layout["record"]).layout())
                lowering = yaml.safe_load(broken["loweringYaml"])
                lowering["tables"] = copy.deepcopy(broken["tables"])
                broken["loweringYaml"] = yaml.safe_dump(lowering, sort_keys=False)
                broken["loweringSha256"] = hashlib.sha256(broken["loweringYaml"].encode()).hexdigest()
                # Keep the source/reflection lowering stamp aligned, so rejection
                # cannot be attributed merely to the outer metadata hash.
                for module in broken["modules"]:
                    name = module["reflection"]
                    reflection_path = path.parent / name
                    reflection = yaml.safe_load(reflection_path.read_bytes())
                    reflection["bindlessLoweringSha256"] = broken["loweringSha256"]
                    data = yaml.safe_dump(reflection, sort_keys=False).encode()
                    reflection_path.write_bytes(data)
                    broken["artifacts"][name] = hashlib.sha256(data).hexdigest()
                publish(broken)
                verify("RHI-BINDLESS-PACKAGE-RECORD-BINARY-TYPE")
                for name, data in originals.items(): (path.parent / name).write_bytes(data)
                publish(payload)
                verify()
                reflection = yaml.safe_load(originals[payload["modules"][1]["reflection"]])
                name = reflection["targets"]["spirv"]
                binary_path = path.parent / name
                original_binary = binary_path.read_bytes()
                facts = SpirvLayoutFacts(binary_path)
                block = facts.buffer_layouts()[(0, 4)]["blockTypeId"]
                _, block_type = facts.definition(block, 30, 2)
                array_id = block_type[1]
                _, array_type = facts.definition(array_id, 29, 2)
                record_id = array_type[1]
                for opcode, operands, position, replacement, diagnostic in (
                    (72, (record_id, 0, 35, 0), 4, 4, "RHI-BINDLESS-PACKAGE-RECORD-BINARY"),
                    (71, (array_id, 6, 48), 3, 64, "RHI-BINDLESS-PACKAGE-RECORD-BINARY-TYPE"),
                ):
                    # Mutate a real target instruction selected by the existing
                    # declaration parser, not by assumed compiler result IDs.
                    selected = [offset for offset, actual_opcode, actual_operands in facts.instructions
                                if actual_opcode == opcode and actual_operands == operands]
                    self.assertEqual(len(selected), 1)
                    words = list(struct.unpack(f"<{len(original_binary)//4}I", original_binary))
                    words[selected[0] + position] = replacement
                    binary_path.write_bytes(struct.pack(f"<{len(words)}I", *words))
                    broken = copy.deepcopy(payload)
                    broken["artifacts"][name] = hashlib.sha256(binary_path.read_bytes()).hexdigest()
                    publish(broken)
                    verify(diagnostic)
                    binary_path.write_bytes(original_binary)
                publish(payload)
                if mode == "Emulated":
                    reflection = yaml.safe_load(originals[payload["modules"][1]["reflection"]])
                    name = reflection["targets"]["wgsl"]
                    artifact = path.parent / name
                    original = artifact.read_bytes()
                    source = original.decode()
                    field = "@align(16) signedValues_0 : vec4<i32>"
                    self.assertEqual(source.count(field), 1)
                    artifact.write_bytes(source.replace(field, "@align(16) signedValues_0 : vec4<u32>").encode())
                    broken = copy.deepcopy(payload)
                    broken["artifacts"][name] = hashlib.sha256(artifact.read_bytes()).hexdigest()
                    publish(broken)
                    verify("RHI-BINDLESS-PACKAGE-RECORD-WGSL-TYPE")
                    artifact.write_bytes(original)
                    # A correct shape still cannot turn a readonly input into
                    # read_write storage by changing and rehashing the target.
                    self.assertIn("var<storage, read> Instances_0_0", source)
                    artifact.write_bytes(source.replace("var<storage, read> Instances_0_0", "var<storage, read_write> Instances_0_0").encode())
                    broken["artifacts"][name] = hashlib.sha256(artifact.read_bytes()).hexdigest()
                    publish(broken)
                    verify("RHI-BINDLESS-PACKAGE-RECORD-WGSL-TYPE")
                    artifact.write_bytes(original)
                    publish(payload)
                    verify()

    def test_deterministic_complete_mapping(self):
        for mode in ("Native", "Emulated"):
            result = generate_bindless(self.schema, mode)
            self.assertEqual(result, generate_bindless(copy.deepcopy(self.schema), mode))
            self.assertEqual(len(result.reflection["bindings"]), 7 if mode == "Native" else 13)
            self.assertNotIn("binding_array", result.source)
            self.assertNotIn("ddx", result.source)
            self.assertNotIn("ddy", result.source)
            self.assertIn("fault = true", result.source)
            self.assertEqual(result.reflection["activePrefixContractVersion"], 2)
            self.assertEqual(result.reflection["faultReportingContractVersion"], 0)
            self.assertEqual(result.reflection["snapshotBytes"], self.schema["snapshotBytes"])
            self.assertIn("index >= activeCount", result.source)
            self.assertIn("samplerIndex >= samplerActiveCount", result.source)
            self.assertIn("activeCount > 2u", result.source)
            layouts = [t["bufferLayout"] for t in result.reflection["tables"] if "bufferLayout" in t]
            self.assertEqual([b["elementStrideBytes"] for b in layouts], [16, 4, 4])
            self.assertEqual(layouts[1]["identity"], layouts[2]["identity"])
            self.assertNotEqual(layouts[0]["identity"], layouts[1]["identity"])

    def test_snapshot_metadata_is_reserved_and_countless(self):
        for mode in ("Native", "Emulated"):
            result = generate_bindless(self.schema, mode)
            self.assertEqual(result.reflection["snapshotMetadata"],
                dict(contractVersion=1, binding=13, wordCount=40, stageMask=6))
            self.assertEqual([t["metadataWordOffset"] for t in result.reflection["tables"]], [4,10,16,22,28,34])
            self.assertIn("uint Inputs_load(uint index, uint location, out bool fault)", result.source)
            self.assertNotIn("uint index, uint activeCount", result.source)
            self.assertIn("uint elementCount = ArpBindlessSnapshotMetadata[32u + index]", result.source)
        self.schema["snapshotMetadataBinding"] = 0
        with self.assertRaisesRegex(BindlessContractError, "COLLISION"):
            generate_bindless(self.schema, "Native")

    def test_fault_latch_is_reserved_and_covers_every_rejection(self):
        self.schema["faultLatchBinding"] = 14
        for mode in ("Native", "Emulated"):
            result = generate_bindless(self.schema, mode)
            self.assertEqual(result.reflection["faultLatch"],
                dict(contractVersion=1, binding=14, stageMask=6))
            self.assertEqual(result.reflection["faultReportingContractVersion"], 1)
            self.assertIn("InterlockedOr(ArpBindlessFaultLatch[0], 1u, previous)", result.source)
            self.assertEqual(result.source.count("fault = true;"),
                result.source.count("arpBindlessReportFault();"))
            self.assertEqual(result.reflection["bindings"][-1]["name"], "ArpBindlessFaultLatch")
        self.schema["snapshotBytes"] = 167
        with self.assertRaisesRegex(BindlessContractError, "SNAPSHOT-BUDGET"):
            generate_bindless(self.schema, "Native")
        self.schema["snapshotBytes"] = 65536
        self.schema["faultLatchBinding"] = 13
        with self.assertRaisesRegex(BindlessContractError, "COLLISION"):
            generate_bindless(self.schema, "Emulated")
        self.schema["faultLatchBinding"] = 16
        with self.assertRaisesRegex(BindlessContractError, "BINDING-BUDGET"):
            generate_bindless(self.schema, "Emulated")
        self.schema["faultLatchBinding"] = 14
        self.schema["tables"][4]["stages"] = ["vertex", "compute"]
        vertex_latch = generate_bindless(self.schema, "Native")
        self.assertEqual(vertex_latch.reflection["faultLatch"]["stageMask"], 7)
        self.schema["tables"][4]["stages"] = ["compute"]
        self.schema["tables"][5]["binding"] = 15
        with self.assertRaisesRegex(BindlessContractError, "BINDING-BUDGET"):
            generate_bindless(self.schema, "Emulated")

    def test_reject_implicit_derivatives(self):
        self.schema["tables"][0]["operations"] = ["sample"]
        with self.assertRaisesRegex(BindlessContractError, "OPERATION-UNSUPPORTED"):
            generate_bindless(self.schema, "Emulated")

    def test_sampled_array_shape_requires_versioned_dimension_and_guards_layers_mips(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_completed_array_root.yaml").read_text())
        for mode in ("Native", "Emulated"):
            result = generate_bindless(schema, mode)
            self.assertEqual(result.reflection["textureShapeContractVersion"], 1)
            images = [b for b in result.reflection["bindings"] if b["type"] == "SampledTexture"]
            self.assertTrue(all(b["textureViewDimension"] == "Texture2DArray" for b in images))
            self.assertIn("Texture2DArray<float4>", result.source)
            self.assertIn("int4 location", result.source)
            self.assertIn("uint layer", result.source)
            self.assertIn("float3(uv, float(layer))", result.source)
            self.assertIn("uint3 Images_query", result.source)
            self.assertIn("uint(location.z) >= layers", result.source)
            self.assertIn("uint(location.w) >= levels", result.source)
        schema.pop("textureShapeContractVersion")
        with self.assertRaisesRegex(BindlessContractError, "TEXTURE-SHAPE-VERSION"):
            generate_bindless(schema, "Emulated")
        schema["textureShapeContractVersion"] = 1
        schema["tables"][0]["dimension"] = "TextureCube"
        with self.assertRaisesRegex(BindlessContractError, "TEXTURE-DIMENSION"):
            generate_bindless(schema, "Native")
        schema["tables"][0]["dimension"] = "Texture2DArray"
        schema["tables"][1]["dimension"] = "Texture2D"
        with self.assertRaisesRegex(BindlessContractError, "TEXTURE-DIMENSION"):
            generate_bindless(schema, "Native")

    def test_array_package_checks_actual_spirv_shape_after_manifest_rehash(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass --slangc and --runtime-verifier for target proof")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_completed_array_root.yaml").read_text())
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-array-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_completed_array_root.slang",
                    ["cleanMain", "faultMain"], mode, directory, SLANGC, reflect)
                def verify(expected="valid"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, "completed-array-root"],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                verify()
                envelope = yaml.safe_load(path.read_text())
                payload_path = path.parent / envelope["payload"]
                original_payload = payload_path.read_bytes()
                manifest = yaml.safe_load(original_payload)
                for binding in manifest["bindings"]:
                    if binding["type"] == "SampledTexture":
                        binding["textureViewDimension"] = "Texture2D"
                changed = yaml.safe_dump(manifest, sort_keys=False).encode()
                payload_path.write_bytes(changed)
                envelope["shaderPackageIdentity"] = hashlib.sha256(changed).hexdigest()
                path.write_text(yaml.safe_dump(envelope, sort_keys=False))
                verify("RHI-BINDLESS-PACKAGE-LOWERING-METADATA-MISMATCH")
                payload_path.write_bytes(original_payload)
                envelope["shaderPackageIdentity"] = hashlib.sha256(original_payload).hexdigest()
                path.write_text(yaml.safe_dump(envelope, sort_keys=False))
                verify()
                manifest = yaml.safe_load(original_payload)
                spirv_name = next(name for name in manifest["artifacts"] if name.endswith("cleanMain.spv"))
                spirv_path = path.parent / spirv_name
                original_spirv = spirv_path.read_bytes()
                words = list(struct.unpack(f"<{len(original_spirv)//4}I", original_spirv))
                offset = 5
                while offset < len(words) and words[offset] & 0xffff != 25:
                    offset += words[offset] >> 16
                self.assertLess(offset, len(words))
                self.assertEqual(words[offset+5], 1)  # OpTypeImage.Arrayed
                words[offset+5] = 0
                changed_spirv = struct.pack(f"<{len(words)}I", *words)
                spirv_path.write_bytes(changed_spirv)
                manifest["artifacts"][spirv_name] = hashlib.sha256(changed_spirv).hexdigest()
                changed = yaml.safe_dump(manifest, sort_keys=False).encode()
                payload_path.write_bytes(changed)
                envelope["shaderPackageIdentity"] = hashlib.sha256(changed).hexdigest()
                path.write_text(yaml.safe_dump(envelope, sort_keys=False))
                verify("RHI-BINDLESS-PACKAGE-IMAGE-SHAPE-MISMATCH")
                spirv_path.write_bytes(original_spirv)
                payload_path.write_bytes(original_payload)
                envelope["shaderPackageIdentity"] = hashlib.sha256(original_payload).hexdigest()
                path.write_text(yaml.safe_dump(envelope, sort_keys=False))
                verify()
                if mode == "Native":
                    # A cyclic descriptor array must reject within the table
                    # ABI's fixed declaration-depth budget, without recursion.
                    words = list(struct.unpack(f"<{len(original_spirv)//4}I", original_spirv))
                    offset = 5
                    while offset < len(words) and words[offset] & 0xffff != 28:
                        offset += words[offset] >> 16
                    self.assertLess(offset, len(words))
                    words[offset+2] = words[offset+1]  # element type = array itself
                    changed_spirv = struct.pack(f"<{len(words)}I", *words)
                    spirv_path.write_bytes(changed_spirv)
                    manifest = yaml.safe_load(original_payload)
                    manifest["artifacts"][spirv_name] = hashlib.sha256(changed_spirv).hexdigest()
                    changed = yaml.safe_dump(manifest, sort_keys=False).encode()
                    payload_path.write_bytes(changed)
                    envelope["shaderPackageIdentity"] = hashlib.sha256(changed).hexdigest()
                    path.write_text(yaml.safe_dump(envelope, sort_keys=False))
                    verify("RHI-BINDLESS-PACKAGE-IMAGE-SHAPE-MISMATCH")
                    spirv_path.write_bytes(original_spirv)
                    payload_path.write_bytes(original_payload)
                    envelope["shaderPackageIdentity"] = hashlib.sha256(original_payload).hexdigest()
                    path.write_text(yaml.safe_dump(envelope, sort_keys=False))
                    verify()

    def test_indirect_instance_base_requires_explicit_version_and_fault_sink(self):
        self.schema["indirectInstanceBaseContractVersion"] = 1
        with self.assertRaisesRegex(BindlessContractError, "INDIRECT-INSTANCE-BASE-FAULT-LATCH-MISSING"):
            generate_bindless(self.schema, "Emulated")
        self.schema["faultLatchBinding"] = 14
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(self.schema, mode)
            self.assertEqual(generated.reflection["indirectInstanceBaseContractVersion"], 1)
            self.assertIn("physical.firstInstance = 0u", generated.source)
            self.assertIn("instanceCount > capacity - firstInstance", generated.source)
            self.assertIn("physicalInstance >= mapping.y", generated.source)
        self.schema["indirectInstanceBaseContractVersion"] = 2
        with self.assertRaisesRegex(BindlessContractError, "INDIRECT-INSTANCE-BASE-VERSION"):
            generate_bindless(self.schema, "Emulated")

    def test_indirect_instance_consumer_is_bounded_vertex_reader(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_webgpu_public_graphics.yaml").read_text())
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated.reflection["indirectInstanceConsumer"],
                dict(contractVersion=1, binding=0, recordIndex=0, instanceCapacity=2))
            reader = generated.source.split("bool arpBindlessLoadIndirectInstance", 1)[1].split("}\n", 1)[0]
            self.assertNotIn("GetDimensions", reader)
            self.assertIn("mapping.z != 2u", reader)
            self.assertNotIn("arpBindlessReportFault", reader)
            mapping = next(b for b in generated.reflection["bindings"]
                if b["name"] == "ArpBindlessIndirectInstanceMappings")
            self.assertEqual(mapping["stageMask"], 1)
        for field, value, reason in (("binding", 2, "COLLISION"),
                ("binding", 14, "BINDING-BUDGET"),
                ("instanceCapacity", 0, "INVALID-CONSUMER-INSTANCE-CAPACITY"),
                ("recordIndex", -1, "INVALID-CONSUMER-RECORD-INDEX")):
            changed = copy.deepcopy(schema)
            changed["indirectInstanceConsumer"][field] = value
            with self.assertRaisesRegex(BindlessContractError, reason):
                generate_bindless(changed, "Emulated")
        del schema["indirectInstanceBaseContractVersion"]
        with self.assertRaisesRegex(BindlessContractError, "CONSUMER-ABI"):
            generate_bindless(schema, "Emulated")

    def test_indirect_instance_consumer_package_and_metadata_tampering(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for target reflection")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_webgpu_public_graphics.yaml").read_text())
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-consumer-") as directory:
            for mode in ("Native", "Emulated"):
                path = compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_webgpu_public_graphics.slang",
                    ["vertexMain", "fragmentMain", "instanceBaseMain"], mode, directory, SLANGC, reflect)
                if not RUNTIME_VERIFIER:
                    continue
                def verify(expected="valid"):
                    result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, "consumer"],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                verify()
                envelope_bytes = path.read_bytes()
                envelope = yaml.safe_load(envelope_bytes)
                payload_path = path.parent / envelope["payload"]
                payload_bytes = payload_path.read_bytes()
                payload = yaml.safe_load(payload_bytes)
                try:
                    for field, value, reason in (("contractVersion", 2, "INDIRECT-INSTANCE-CONSUMER-ABI"),
                            ("binding", 2, "INDIRECT-INSTANCE-CONSUMER-ABI"),
                            ("instanceCapacity", 0, "INDIRECT-INSTANCE-CONSUMER-ABI"),
                            ("instanceCapacity", 3, "LOWERING-METADATA-MISMATCH"),
                            ("recordIndex", 1, "LOWERING-METADATA-MISMATCH")):
                        changed = copy.deepcopy(payload)
                        changed["indirectInstanceConsumer"][field] = value
                        data = yaml.safe_dump(changed, sort_keys=False).encode()
                        payload_path.write_bytes(data)
                        path.write_text(yaml.safe_dump(dict(envelope,
                            shaderPackageIdentity=hashlib.sha256(data).hexdigest())), encoding="utf-8")
                        verify("RHI-BINDLESS-PACKAGE-" + reason)
                    forged = copy.deepcopy(payload)
                    forged["zeroBaseIndirectProducerContractVersion"] = 1
                    data = yaml.safe_dump(forged, sort_keys=False).encode()
                    payload_path.write_bytes(data)
                    path.write_text(yaml.safe_dump(dict(envelope,
                        shaderPackageIdentity=hashlib.sha256(data).hexdigest())), encoding="utf-8")
                    verify("RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-PRODUCER-PROVENANCE")
                finally:
                    payload_path.write_bytes(payload_bytes)
                    path.write_bytes(envelope_bytes)

    def test_pbr_material_demand_and_sampling_refusals(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_graphics_pbr_material.yaml").read_text())
        # Fifteen indexed bindings + two ordinary bindings. These are explicit
        # workload counts, never a substitute for queried device limits.
        for mode in ("Native", "Emulated"):
            generate_bindless(schema, mode)
        for update, reason in (({"maxSwitchCases": 20}, "SWITCH-BUDGET"),
                               ({"maxBindings": 14}, "BINDING-BUDGET")):
            broken = dict(schema, **update)
            with self.assertRaisesRegex(BindlessContractError, reason):
                generate_bindless(broken, "Emulated")
        with tempfile.TemporaryDirectory(prefix="arp-bindless-pbr-budget-") as directory:
            with self.assertRaisesRegex(BindlessContractError, "PIPELINE-BINDING-BUDGET"):
                compile_bindless_package(dict(schema, maxBindings=16),
                    ROOT / "tests/fixtures/bindless_graphics_pbr_material.slang",
                    ["vertexMain", "cleanFragment", "faultFragment"], "Emulated",
                    directory, "must-not-run", "must-not-run",
                    [ROOT / "shaders/core/leaf_surface.slang", ROOT / "shaders/core/pbr_material.slang"])
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))
        broken = copy.deepcopy(schema)
        broken["tables"][0]["operations"] = ["sample"]
        with self.assertRaisesRegex(BindlessContractError, "OPERATION-UNSUPPORTED"):
            generate_bindless(broken, "Emulated")
        broken = copy.deepcopy(schema)
        broken["tables"][0]["stages"] = ["vertex", "fragment"]
        with self.assertRaisesRegex(BindlessContractError, "GRADIENT-STAGE"):
            generate_bindless(broken, "Emulated")
        broken = copy.deepcopy(schema)
        broken["tables"][1]["stages"] = ["vertex"]
        with self.assertRaisesRegex(BindlessContractError, "SAMPLER-STAGE"):
            generate_bindless(broken, "Emulated")

    def test_terrain27_complete_binding_and_gradient_refusals(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_graphics_terrain27.yaml").read_text())
        for mode in ("Native", "Emulated"):
            generate_bindless(schema, mode)
        for update, reason in (({"maxSwitchCases": 27}, "SWITCH-BUDGET"),
                               ({"maxBindings": 30}, "BINDING-BUDGET"),
                               ({"snapshotBytes": 203}, "SNAPSHOT-BUDGET")):
            with self.assertRaisesRegex(BindlessContractError, reason):
                generate_bindless(dict(schema, **update), "Emulated")
        with tempfile.TemporaryDirectory(prefix="arp-bindless-terrain27-budget-") as directory:
            with self.assertRaisesRegex(BindlessContractError, "PIPELINE-BINDING-BUDGET"):
                compile_bindless_package(dict(schema, maxBindings=32),
                    ROOT / "tests/fixtures/bindless_graphics_terrain27.slang",
                    ["vertexMain", "cleanFragment", "faultFragment"], "Emulated",
                    directory, "must-not-run", "must-not-run")
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))
        for index in (0, 1):
            broken = copy.deepcopy(schema)
            broken["tables"][index]["operations"] = ["sample"]
            with self.assertRaisesRegex(BindlessContractError, "OPERATION-UNSUPPORTED"):
                generate_bindless(broken, "Emulated")
            broken["tables"][index]["operations"] = ["sampleGrad"]
            broken["tables"][index]["stages"] = ["vertex", "fragment"]
            with self.assertRaisesRegex(BindlessContractError, "GRADIENT-STAGE"):
                generate_bindless(broken, "Emulated")

    def test_explicit_source_libraries_share_whole_source_refusals(self):
        with tempfile.TemporaryDirectory(prefix="arp-bindless-library-") as directory:
            path = pathlib.Path(directory) / "entry.slang"
            library = pathlib.Path(directory) / "library.slang"
            path.write_text("void main() {}", encoding="utf-8")
            for source, reason in (
                ('#include "unguarded.slang"', "SOURCE-CONTRACT"),
                ("#define BYPASS 1", "SOURCE-CONTRACT"),
                ("uint bad() { return Counters[1][0]; }", "DIRECT-TABLE-ACCESS"),
                ("void bad() { InterlockedAdd(1, 1); }", "SOURCE-CONTRACT"),
            ):
                library.write_text(source, encoding="utf-8")
                for mode in ("Native", "Emulated"):
                    with self.subTest(mode=mode, reason=reason):
                        with self.assertRaisesRegex(BindlessContractError, reason):
                            compile_bindless_package(self.schema, path, ["main"], mode,
                                directory, "must-not-run", "must-not-run", [library])
                        self.assertFalse(list(path.parent.glob("*.bindless.yaml")))
            library.write_text("float identity(float value) { return value; }", encoding="utf-8")
            with self.assertRaisesRegex(BindlessContractError, "SOURCE-LIBRARY-DUPLICATE"):
                compile_bindless_package(self.schema, path, ["main"], "Native", directory,
                    "must-not-run", "must-not-run", [library, library.parent / "." / library.name])
            with self.assertRaisesRegex(BindlessContractError, "SOURCE-LIBRARY-INPUT"):
                compile_bindless_package(self.schema, path, ["main"], "Native", directory,
                    "must-not-run", "must-not-run", [path.parent / "missing.slang"])

    def test_authored_table_bypass_rejected_before_compilation(self):
        with tempfile.TemporaryDirectory(prefix="arp-bindless-bypass-") as directory:
            path = pathlib.Path(directory) / "bypass.slang"
            for source, mode, reason in (
                ("uint bad() { return Inputs_0[1000]; }", "Emulated", "DIRECT-TABLE-ACCESS"),
                ("void bad() { Counters[1][0] = 10; }", "Native", "DIRECT-TABLE-ACCESS"),
                ("#define JOIN(a,b) a ## b\n", "Emulated", "SOURCE-CONTRACT"),
                ('#include "unguarded.slang"\n', "Native", "SOURCE-CONTRACT"),
            ):
                with self.subTest(mode=mode, source=source):
                    path.write_text(source)
                    with self.assertRaisesRegex(BindlessContractError, reason):
                        compile_bindless_package(self.schema, path, ["main"], mode,
                            directory, "must-not-run", "must-not-run")
                    self.assertFalse(list(path.parent.glob("*.bindless.yaml")))

    def test_zero_base_producer_closed_schema_and_source_ownership(self):
        schema = zero_base_indirect_producer_schema()
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated, generate_bindless(copy.deepcopy(schema), mode))
            self.assertEqual(generated.reflection["zeroBaseIndirectProducerContractVersion"], 1)
            self.assertEqual(len(generated.reflection["bindings"]), 6)
            self.assertEqual(generated.reflection["tables"][3]["bufferLayout"]["abi"],
                "Slang.uint4.uniform/1")
            self.assertIn("wordCount - parameters.x < 5u", generated.source)
            self.assertIn("PhysicalArgs_store(0u, 4u, 0u, fault)", generated.source)
            self.assertNotIn("asuint(ProducerParams_load", generated.source)
        for mutate, reason in (
                (lambda s: s.update(zeroBaseIndirectProducerContractVersion=2), "PRODUCER-VERSION"),
                (lambda s: s.update(snapshotBytes=4097), "PRODUCER-SCHEMA"),
                (lambda s: s["tables"][1].update(capacity=2), "PRODUCER-SCHEMA"),
                (lambda s: s["tables"][0].update(operations=["load"]), "PRODUCER-SCHEMA")):
            changed = copy.deepcopy(schema)
            mutate(changed)
            with self.assertRaisesRegex(BindlessContractError, reason):
                generate_bindless(changed, "Emulated")
        with tempfile.TemporaryDirectory(prefix="arp-zero-base-ownership-") as directory:
            source = pathlib.Path(directory) / "authored.slang"
            source.write_text("// Even comments are not compiler-owned.\n")
            for authored, entries in ((source, ["arpZeroBaseIndirectMain"]),
                    (None, ["anotherMain"]), (None, ["arpZeroBaseIndirectMain", "anotherMain"])):
                with self.assertRaisesRegex(BindlessContractError, "ENTRY-OWNERSHIP"):
                    compile_bindless_package(schema, authored, entries, "Emulated", directory,
                        "must-not-run", "must-not-run")
            with self.assertRaisesRegex(BindlessContractError, "SOURCE-MISSING"):
                compile_bindless_package(self.schema, None, ["main"], "Emulated", directory,
                    "must-not-run", "must-not-run")

    def test_zero_base_provenance_without_shader_toolchain_is_closed(self):
        with tempfile.TemporaryDirectory(prefix="arp-zero-base-no-tools-") as directory:
            output = pathlib.Path(directory) / "identity.h"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/bindless_zero_base_identity.py"),
                "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            header = output.read_text(encoding="ascii")
            for mode in ("Native", "Emulated"):
                self.assertIn('zeroBaseIndirect' + mode + 'PackageSha256[] = "";', header)
                self.assertIn(hashlib.sha256((generate_bindless(zero_base_indirect_producer_schema(),
                    mode).source + "\n").encode()).hexdigest(), header)
                self.assertIn('zeroBaseIndirectConsumer' + mode + 'PackageSha256[] = "";', header)
                self.assertIn(hashlib.sha256((generate_bindless(zero_base_indirect_consumer_schema(),
                    mode).source + "\n").encode()).hexdigest(), header)

    def test_zero_base_consumer_closed_schema_and_source_ownership(self):
        schema = zero_base_indirect_consumer_schema()
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            self.assertEqual(generated, generate_bindless(copy.deepcopy(schema), mode))
            self.assertEqual(generated.reflection["zeroBaseIndirectConsumerContractVersion"], 1)
            self.assertEqual(generated.reflection["indirectInstanceConsumer"],
                dict(contractVersion=1, binding=7, recordIndex=0, instanceCapacity=3))
            self.assertEqual(len(generated.reflection["bindings"]), 4 if mode == "Native" else 6)
            for expression in ("float2 position : POSITION", "uint instanceId : SV_InstanceID",
                    "arpBindlessLoadIndirectInstance(instanceId, logicalInstance)",
                    "mapping.z != 3u", "mapping.w != 1u", "mapping.y > mapping.z - mapping.x",
                    "physicalInstance >= mapping.y", "Images_load(input.logicalInstance",
                    "input.instanceFault != 0u"):
                self.assertIn(expression, generated.source)
            self.assertNotIn("SV_VertexID", generated.source)
        for mutate, reason in (
                (lambda s: s.update(zeroBaseIndirectConsumerContractVersion=2), "CONSUMER-VERSION"),
                (lambda s: s["indirectInstanceConsumer"].update(instanceCapacity=2), "CONSUMER-SCHEMA"),
                (lambda s: s["indirectInstanceConsumer"].update(binding=0), "CONSUMER-SCHEMA"),
                (lambda s: s["tables"][0].update(operations=["load", "query"]), "CONSUMER-SCHEMA")):
            changed = copy.deepcopy(schema); mutate(changed)
            with self.assertRaisesRegex(BindlessContractError, reason): generate_bindless(changed, "Emulated")
        with tempfile.TemporaryDirectory(prefix="arp-consumer-ownership-") as directory:
            source = pathlib.Path(directory) / "authored.slang"; source.write_text("// caller source\n")
            for authored, entries in ((source, _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES),
                    (None, list(reversed(_ZERO_BASE_INDIRECT_CONSUMER_ENTRIES))),
                    (None, _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES[:1])):
                with self.assertRaisesRegex(BindlessContractError, "CONSUMER-ENTRY-OWNERSHIP"):
                    compile_bindless_package(schema, authored, entries, "Emulated", directory,
                        "must-not-run", "must-not-run")

    def test_zero_base_closed_packages_reject_rehashed_substitution(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for producer compilation")
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-zero-base-package-") as directory:
            for domain, schema, entries, version_field, diagnostic_domain in (
                    ("producer", zero_base_indirect_producer_schema(), ["arpZeroBaseIndirectMain"],
                        "zeroBaseIndirectProducerContractVersion", "PRODUCER"),
                    ("closed-consumer", zero_base_indirect_consumer_schema(), _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES,
                        "zeroBaseIndirectConsumerContractVersion", "CONSUMER")):
                for mode in ("Native", "Emulated"):
                    path = compile_bindless_package(schema, None, entries, mode, directory, SLANGC, reflect)
                    if not RUNTIME_VERIFIER:
                        continue
                    def verify(expected="valid"):
                        result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, domain],
                            capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    verify()

                    envelope_bytes = path.read_bytes()
                    envelope = yaml.safe_load(envelope_bytes)
                    payload_path = path.parent / envelope["payload"]
                    payload_bytes = payload_path.read_bytes()
                    payload = yaml.safe_load(payload_bytes)
                    def publish(changed):
                        data = yaml.safe_dump(changed, sort_keys=False).encode()
                        payload_path.write_bytes(data)
                        path.write_text(yaml.safe_dump(dict(envelope,
                            shaderPackageIdentity=hashlib.sha256(data).hexdigest())), encoding="utf-8")
                    try:
                        for field, value, reason in (
                                (version_field, 2, "ZERO-BASE-INDIRECT-" + diagnostic_domain + "-ABI"),
                                ("indirectInstanceBaseContractVersion", 0, "INDIRECT-INSTANCE-BASE-ABI"),
                                ("snapshotBytes", schema["snapshotBytes"] + 1, "ZERO-BASE-INDIRECT-" + diagnostic_domain + "-PROVENANCE")):
                            changed = copy.deepcopy(payload)
                            changed[field] = value
                            publish(changed)
                            verify("RHI-BINDLESS-PACKAGE-" + reason)
                        # Recomputing every caller-owned hash cannot certify new executable bytes.
                        for name in payload["artifacts"]:
                            target = path.parent / name
                            original = target.read_bytes()
                            try:
                                replacement = original + (b"\x00\x00\x00\x00" if name.endswith(".spv") else b"\n// altered\n")
                                target.write_bytes(replacement)
                                changed = copy.deepcopy(payload)
                                changed["artifacts"][name] = hashlib.sha256(replacement).hexdigest()
                                publish(changed)
                                verify("RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-" + diagnostic_domain + "-PROVENANCE")
                            finally:
                                target.write_bytes(original)
                    finally:
                        payload_path.write_bytes(payload_bytes)
                        path.write_bytes(envelope_bytes)
                    verify()
    def test_closed_consumer_semantic_substitutions_and_version_stripping(self):
        if not SLANGC or not RUNTIME_VERIFIER:
            self.skipTest("Pass shader tools and runtime verifier for closed semantic provenance")
        import bindless_codegen as generator
        reflect = spirv_reflect_tool()
        original_body = generator._ZERO_BASE_INDIRECT_CONSUMER_BODY
        mutations = (
            ("bool valid = arpBindlessLoadIndirectInstance(instanceId, logicalInstance);",
             "logicalInstance = instanceId; bool valid = true;", "REFLECTION-STAGE-MISMATCH"),
            ("output.instanceFault = valid ? 0u : 1u;", "output.instanceFault = valid ? 1u : 0u;", None),
            ("return Images_load(input.logicalInstance, int3(0, 0, 0), fault);",
             "return Images_load(input.logicalInstance + 1u, int3(0, 0, 0), fault);", None),
        )
        with tempfile.TemporaryDirectory(prefix="arp-closed-consumer-semantic-") as directory:
            for before, after, compiler_failure in mutations:
                # Still-active resources and fully recompiled/rehashed targets:
                # generic reflection cannot establish the semantic difference.
                generator._ZERO_BASE_INDIRECT_CONSUMER_BODY = original_body.replace(before, after)
                try:
                    if compiler_failure:
                        with self.assertRaisesRegex(BindlessContractError, compiler_failure):
                            compile_bindless_package(zero_base_indirect_consumer_schema(), None,
                                _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES, "Emulated", directory, SLANGC, reflect)
                        continue
                    path = compile_bindless_package(zero_base_indirect_consumer_schema(), None,
                        _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES, "Emulated", directory, SLANGC, reflect)
                    result = subprocess.run([RUNTIME_VERIFIER, str(path),
                        "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-CONSUMER-PROVENANCE", "closed-consumer"],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                finally:
                    generator._ZERO_BASE_INDIRECT_CONSUMER_BODY = original_body
            generate = generator.generate_bindless
            for before, after in (
                    ("mapping.w != 1u", "mapping.w != 2u"),
                    ("mapping.z != 3u", "mapping.z != 2u"),
                    ("physicalInstance >= mapping.y", "physicalInstance > mapping.y"),
                    ("mapping.y > mapping.z - mapping.x", "mapping.y < mapping.z - mapping.x")):
                def changed_generator(document, mode):
                    generated = generate(document, mode)
                    self.assertIn(before, generated.source)
                    source = generated.source.replace(before, after)
                    reflection = dict(generated.reflection, sourceSha256=hashlib.sha256(source.encode()).hexdigest())
                    return generator.GeneratedBindless(source, reflection)
                with mock.patch.object(generator, "generate_bindless", changed_generator):
                    path = compile_bindless_package(zero_base_indirect_consumer_schema(), None,
                        _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES, "Emulated", directory, SLANGC, reflect)
                    result = subprocess.run([RUNTIME_VERIFIER, str(path),
                        "RHI-BINDLESS-PACKAGE-ZERO-BASE-INDIRECT-CONSUMER-PROVENANCE", "closed-consumer"],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            path = compile_bindless_package(zero_base_indirect_consumer_schema(), None,
                _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES, "Emulated", directory, SLANGC, reflect)
            envelope = yaml.safe_load(path.read_text())
            payload_path = path.parent / envelope["payload"]
            payload = yaml.safe_load(payload_path.read_text())
            del payload["zeroBaseIndirectConsumerContractVersion"]
            lowering = yaml.safe_load(payload["loweringYaml"])
            del lowering["zeroBaseIndirectConsumerContractVersion"]
            payload["loweringYaml"] = yaml.safe_dump(lowering, sort_keys=False)
            payload["loweringSha256"] = hashlib.sha256(payload["loweringYaml"].encode()).hexdigest()
            for name in payload["artifacts"]:
                if name.endswith(".reflection.yaml"):
                    reflection_path = path.parent / name
                    reflection = yaml.safe_load(reflection_path.read_text())
                    reflection["bindlessLoweringSha256"] = payload["loweringSha256"]
                    data = yaml.safe_dump(reflection, sort_keys=False).encode()
                    reflection_path.write_bytes(data); payload["artifacts"][name] = hashlib.sha256(data).hexdigest()
            data = yaml.safe_dump(payload, sort_keys=False).encode(); payload_path.write_bytes(data)
            path.write_text(yaml.safe_dump(dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest())))
            result = subprocess.run([RUNTIME_VERIFIER, str(path), "valid", "closed-consumer-negative"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_independent_sampler_combinations_are_budgeted(self):
        result = generate_bindless(self.schema, "Emulated")
        self.assertEqual(result.reflection["switchCases"], 32)
        self.schema["maxSwitchCases"] = 31
        with self.assertRaisesRegex(BindlessContractError, "SWITCH-BUDGET"):
            generate_bindless(self.schema, "Emulated")

    def test_indirect_processor_intent_and_graphics_only_consumer(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for processor preparation compilation")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_indirect_consumer.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_indirect_consumer.slang"
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-indirect-processor-") as directory:
            for mode in ("Native", "Emulated"):
                consumer = compile_bindless_package(schema, source, ["vertexMain", "fragmentMain"],
                    mode, pathlib.Path(directory) / "consumer", SLANGC, reflect)
                producer = compile_bindless_package(zero_base_indirect_producer_schema(), None,
                    ["arpZeroBaseIndirectMain"], mode, pathlib.Path(directory) / "producer", SLANGC, reflect)
                generated = generate_bindless(schema, mode)
                self.assertTrue(all(not binding["stageMask"] & 4
                    for binding in generated.reflection["bindings"]))
                if RUNTIME_VERIFIER:
                    result = subprocess.run([RUNTIME_VERIFIER, str(consumer), "valid", "processor", str(producer)],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    mixed_source = pathlib.Path(directory) / "mixed.slang"
                    mixed_source.write_text(source.read_text()
                        + '\n[shader("compute")]\n[numthreads(1, 1, 1)]\nvoid unrelatedMain() {}\n', encoding="ascii")
                    mixed = compile_bindless_package(schema, mixed_source,
                        ["vertexMain", "fragmentMain", "unrelatedMain"], mode,
                        pathlib.Path(directory) / "mixed", SLANGC, reflect)
                    result = subprocess.run([RUNTIME_VERIFIER, str(mixed), "valid", "processor-mixed", str(producer)],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsafe_or_unknown_schema_rejected(self):
        changes = [("capacity", 0), ("capacity", True), ("capacity", 0xFFFFFFFF),
                   ("binding", 0xFFFFFFFF), ("name", "x; injected"),
                   ("operations", ["atomicAdd"]), ("unknown", 1)]
        for field, value in changes:
            with self.subTest(field=field, value=value):
                schema = copy.deepcopy(self.schema)
                schema["tables"][0][field] = value
                with self.assertRaises(BindlessContractError):
                    generate_bindless(schema, "Emulated")

    def test_collisions_and_sampler_stage(self):
        self.schema["tables"][1]["binding"] = 1
        with self.assertRaisesRegex(BindlessContractError, "COLLISION"):
            generate_bindless(self.schema, "Emulated")
        self.schema["tables"][1]["binding"] = 2
        self.schema["tables"][1]["stages"] = ["compute"]
        with self.assertRaisesRegex(BindlessContractError, "SAMPLER-STAGE"):
            generate_bindless(self.schema, "Emulated")

    def test_real_slang_targets(self):
        if not SLANGC:
            self.skipTest("Pass --slangc to validate SPIR-V, MSL and WGSL artifacts")
        with tempfile.TemporaryDirectory(prefix="arp-bindless-") as directory:
            output = pathlib.Path(directory)
            for mode in ("Native", "Emulated"):
                generated = generate_bindless(self.schema, mode)
                # Every helper is referenced by a stage-compatible entry. The
                # fault value is visible in output; it cannot be optimized away.
                source = generated.source + (ROOT / "tests/fixtures/bindless_access.slang").read_text()
                source += (ROOT / "tests/fixtures/bindless_vertex.slang").read_text()
                path = output / f"{mode}.slang"
                path.write_text(source, encoding="utf-8")
                for target in (("spirv", "metal") if mode == "Native" else ("spirv", "metal", "wgsl")):
                    for entry, stage in (("computeMain", "compute"), ("fragmentMain", "fragment"), ("vertexMain", "vertex")):
                        artifact = output / f"{mode}-{entry}.{target}"
                        reflection = output / f"{mode}-{entry}-{target}.json"
                        args = [SLANGC, str(path), "-entry", entry, "-stage", stage,
                                "-target", target, "-profile", "sm_6_0", "-matrix-layout-row-major",
                                "-reflection-json", str(reflection), "-o", str(artifact)]
                        if mode == "Native":
                            args += ["-D", "ARP_BINDLESS_" + target.upper() + "=1"]
                        if target == "wgsl" and stage == "compute":
                            # Closed generated operation schema; reuse the
                            # engine's validated atomic storage ABI lowering.
                            args += ["-ignore-capabilities"]
                        run = subprocess.run(args, capture_output=True, text=True)
                        self.assertEqual(run.returncode, 0, f"{mode}/{target}/{entry}: {run.stdout}\n{run.stderr}")
                        self.assertGreater(artifact.stat().st_size, 0)
                        self.assertTrue(reflection.exists())
                        if target == "spirv":
                            validator = spirv_validator_tool()
                            validation = subprocess.run([str(validator), "--target-env", "vulkan1.2", str(artifact)],
                                                        capture_output=True, text=True)
                            self.assertEqual(validation.returncode, 0, validation.stdout + validation.stderr)
                        if target == "metal" and mode == "Native":
                            active = [b for b in generated.reflection["bindings"]
                                      if b["stageMask"] & {"vertex":1,"fragment":2,"compute":4}[stage]]
                            lower_metal_argument_buffer_artifact(str(artifact), active)
                        if target == "wgsl":
                            if stage == "compute":
                                lower_wgsl_atomic_storage_buffers(str(artifact), generated.source)
                            text = artifact.read_text()
                            self.assertNotIn("binding_array", text)
                            if stage != "vertex": self.assertIn("switch", text)

    def test_metal_compute_buffers_targets(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_metal_compute.yaml").read_text())
        generated = generate_bindless(schema, "Native")
        self.assertEqual(generated.reflection["faultLatch"]["stageMask"], 4)
        self.assertEqual(len(generated.reflection["bindings"]), 5)
        if not SLANGC:
            self.skipTest("Pass --slangc for target compilation")
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-metal-") as directory:
            for mode in ("Native", "Emulated"):
                publication = compile_bindless_package(schema,
                    ROOT / "tests/fixtures/bindless_metal_compute.slang",
                    ["cleanMain", "faultMain"], mode, directory, SLANGC, reflect)
                manifest = read_bindless_publication(publication)
                self.assertEqual(len(manifest["modules"]), 2)
                for module in manifest["modules"]:
                    reflection = yaml.safe_load((pathlib.Path(directory) / module["reflection"]).read_text())
                    self.assertEqual(reflection["entryPoints"][0]["stage"], "Compute")
                    if mode == "Native":
                        self.assertEqual(reflection["metalArgumentBufferContractVersion"], "MetalArgumentBuffer/1")
                    else:
                        wgsl = (pathlib.Path(directory) / reflection["targets"]["wgsl"]).read_text()
                        self.assertIn("array<atomic<u32>>", wgsl)
                        self.assertIn("atomicAdd", wgsl)

    def test_metal_compute_images_targets(self):
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_metal_images.yaml").read_text())
        native = generate_bindless(schema, "Native")
        self.assertEqual(native.reflection["faultLatch"]["stageMask"], 4)
        self.assertEqual(len(native.reflection["bindings"]), 6)
        self.assertEqual(native.reflection["tables"][0]["samplerTableId"], 2)
        if not SLANGC:
            self.skipTest("Pass --slangc for target compilation")
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-metal-images-") as directory:
            for mode in ("Native", "Emulated"):
                publication = compile_bindless_package(schema,
                    ROOT / "tests/fixtures/bindless_metal_images.slang",
                    ["cleanMain", "faultMain"], mode, directory, SLANGC, reflect)
                manifest = read_bindless_publication(publication)
                self.assertEqual(len(manifest["modules"]), 2)
                for module in manifest["modules"]:
                    reflection = yaml.safe_load((pathlib.Path(directory) / module["reflection"]).read_text())
                    self.assertEqual(reflection["entryPoints"][0]["stage"], "Compute")
                    self.assertTrue((pathlib.Path(directory) / reflection["targets"]["spirv"]).is_file())
                    self.assertTrue((pathlib.Path(directory) / reflection["targets"]["metal"]).is_file())
                    if mode == "Emulated":
                        self.assertTrue((pathlib.Path(directory) / reflection["targets"]["wgsl"]).is_file())
                if RUNTIME_VERIFIER:
                    result = subprocess.run([RUNTIME_VERIFIER, str(publication), "valid", "images"], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_fault_latch_targets(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for target compilation")
        self.schema["faultLatchBinding"] = 14
        reflect = spirv_reflect_tool()
        self.schema["indirectInstanceBaseContractVersion"] = 1
        with tempfile.TemporaryDirectory(prefix="arp-bindless-fault-") as directory:
            source = pathlib.Path(directory) / "fault.slang"
            source.write_text((ROOT / "tests/fixtures/bindless_access.slang").read_text(), encoding="utf-8")
            for mode in ("Native", "Emulated"):
                package = compile_bindless_package(self.schema, source,
                    ["computeMain", "fragmentMain"], mode, directory, SLANGC, reflect)
                manifest = read_bindless_publication(package)
                self.assertEqual(manifest["faultLatch"],
                    dict(contractVersion=1, binding=14, stageMask=6))
                self.assertEqual(manifest["faultReportingContractVersion"], 1)
                if RUNTIME_VERIFIER:
                    result = subprocess.run([RUNTIME_VERIFIER, str(package), "valid", "latch"],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    envelope_bytes = package.read_bytes()
                    envelope = yaml.safe_load(envelope_bytes)
                    payload_path = package.parent / envelope["payload"]
                    payload_bytes = payload_path.read_bytes()
                    payload = yaml.safe_load(payload_bytes)
                    try:
                        for value, reason in ((2, "INDIRECT-INSTANCE-BASE-ABI"),
                                              (None, "LOWERING-METADATA-MISMATCH")):
                            changed = copy.deepcopy(payload)
                            if value is None:
                                del changed["indirectInstanceBaseContractVersion"]
                            else:
                                changed["indirectInstanceBaseContractVersion"] = value
                            data = yaml.safe_dump(changed, sort_keys=False).encode()
                            payload_path.write_bytes(data)
                            altered = dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest())
                            package.write_text(yaml.safe_dump(altered, sort_keys=False), encoding="utf-8")
                            result = subprocess.run([RUNTIME_VERIFIER, str(package),
                                "RHI-BINDLESS-PACKAGE-" + reason], capture_output=True, text=True)
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        for key, value in (("binding", 15), ("stageMask", 4)):
                            changed = copy.deepcopy(payload)
                            changed["faultLatch"][key] = value
                            data = yaml.safe_dump(changed, sort_keys=False).encode()
                            payload_path.write_bytes(data)
                            altered = dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest())
                            package.write_text(yaml.safe_dump(altered, sort_keys=False), encoding="utf-8")
                            result = subprocess.run([RUNTIME_VERIFIER, str(package),
                                "RHI-BINDLESS-PACKAGE-FAULT-LATCH-ABI"],
                                capture_output=True, text=True)
                            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        changed = copy.deepcopy(payload)
                        changed["faultReportingContractVersion"] = 0
                        data = yaml.safe_dump(changed, sort_keys=False).encode()
                        payload_path.write_bytes(data)
                        altered = dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest())
                        package.write_text(yaml.safe_dump(altered, sort_keys=False), encoding="utf-8")
                        result = subprocess.run([RUNTIME_VERIFIER, str(package),
                            "RHI-BINDLESS-PACKAGE-FAULT-REPORTING-ABI"],
                            capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    finally:
                        payload_path.write_bytes(payload_bytes)
                        package.write_bytes(envelope_bytes)
                if mode == "Emulated":
                    compute = pathlib.Path(directory) / ("BindlessFixture.emulated.computeMain.wgsl")
                    fragment = pathlib.Path(directory) / ("BindlessFixture.emulated.fragmentMain.wgsl")
                    self.assertIn("array<atomic<u32>>", compute.read_text())
                    self.assertIn("atomicOr", compute.read_text())
                    self.assertIn("atomicOr", fragment.read_text())

    def test_graphics_fault_fixture_has_sealed_stage_and_target_contracts(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for package compilation")
        reflect = spirv_reflect_tool()
        self.assertTrue(reflect.is_file())
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_metal_graphics.yaml").read_text())
        source = ROOT / "tests/fixtures/bindless_metal_graphics.slang"
        with tempfile.TemporaryDirectory(prefix="arp-bindless-graphics-fault-") as directory:
            for mode in ("Native",):
                publication = compile_bindless_package(schema, source,
                    ["vertexMain", "cleanFragment", "faultFragment"], mode, directory, SLANGC, reflect)
                manifest = read_bindless_publication(publication)
                self.assertEqual(manifest["matrixLayout"], "row-major")
                self.assertEqual(manifest["snapshotMetadata"]["stageMask"], 3)
                self.assertEqual(manifest["faultLatch"], dict(contractVersion=1, binding=7, stageMask=3))
                self.assertEqual(manifest["faultReportingContractVersion"], 1)
                self.assertEqual(len(manifest["modules"]), 3)
                for module in manifest["modules"]:
                    reflection = yaml.safe_load((pathlib.Path(directory) / module["reflection"]).read_text())
                    self.assertTrue((pathlib.Path(directory) / reflection["targets"]["spirv"]).is_file())
                    self.assertTrue((pathlib.Path(directory) / reflection["targets"]["metal"]).is_file())
                    stage = 1 if module["entry"] == "vertexMain" else 2
                    self.assertTrue(all(binding["stageMask"] == stage for binding in reflection["bindings"]))
                    if mode == "Emulated":
                        self.assertTrue((pathlib.Path(directory) / reflection["targets"]["wgsl"]).is_file())
                if RUNTIME_VERIFIER:
                    result = subprocess.run([RUNTIME_VERIFIER, str(publication), "valid", "graphics-fault-fixture"],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_graphics_fault_emulated_vertex_refusal(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for actual Vertex WGSL refusal")
        schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_metal_graphics.yaml").read_text())
        with tempfile.TemporaryDirectory(prefix="arp-bindless-vertex-refusal-") as directory:
            with self.assertRaisesRegex(BindlessContractError, "WGSL-VERTEX-WRITABLE-STORAGE"):
                compile_bindless_package(schema, ROOT / "tests/fixtures/bindless_metal_graphics.slang",
                    ["vertexMain", "cleanFragment", "faultFragment"], "Emulated", directory,
                    SLANGC, spirv_reflect_tool())
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))

    def test_completed_graphics_indexed_vertex_emulated_refusal(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for actual Vertex WGSL refusal")
        for family in ("buffers", "records", "vertex"):
            with self.subTest(family=family), tempfile.TemporaryDirectory(prefix="arp-root-vertex-refusal-") as directory:
                if family == "vertex":
                    schema = yaml.safe_load((ROOT / "tests/fixtures/bindless_metal_graphics.yaml").read_text())
                    schema["name"] = "RootVertexBuffer"
                    schema["tables"] = [table for table in schema["tables"] if table["name"] == "Inputs"]
                    schema["tables"][0]["stages"] = ["vertex"]
                    schema["tables"][0]["operations"] = ["load"]
                    source = ROOT / "tests/fixtures/bindless_vertex_buffer.slang"
                    entries = ["vertexMain"]
                else:
                    base = ROOT / ("tests/fixtures/bindless_original_graphics_" + family)
                    schema = yaml.safe_load(base.with_suffix(".yaml").read_text())
                    source = base.with_suffix(".slang")
                    entries = ["vertexMain", "cleanFragment", "faultFragment"]
                with self.assertRaisesRegex(BindlessContractError, "WGSL-VERTEX-WRITABLE-STORAGE"):
                    compile_bindless_package(schema, source, entries, "Emulated", directory,
                        SLANGC, spirv_reflect_tool())
                self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))

    def test_authoritative_package_and_failed_replacement(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for package compilation")
        reflect = spirv_reflect_tool()
        self.assertTrue(reflect.is_file(), "Package tests require authoritative SPIRV-Reflect")
        with tempfile.TemporaryDirectory(prefix="arp-bindless-package-test-") as directory:
            fixture = PreparedBindingFixture()
            source = fixture.write_source(directory)
            self.schema = fixture.schema
            for mode in ("Native", "Emulated"):
                manifest_path = compile_bindless_package(self.schema,
                    source, ["computeMain", "fragmentMain", "vertexMain"],
                    mode, directory, SLANGC, reflect)
                manifest = read_bindless_publication(manifest_path)
                self.assertEqual(manifest["validationStatus"], "TargetReflectionValidated")
                self.assertEqual(manifest["matrixLayout"], "row-major")
                self.assertEqual(len(manifest["shaderPackageIdentity"]), 64)
                self.assertEqual(len(manifest["modules"]), 3)
                compute = manifest["modules"][0]
                runtime = yaml.safe_load((pathlib.Path(directory) / compute["reflection"]).read_text())
                self.assertEqual(runtime["bindlessContractVersion"], "BindlessCross/1")
                self.assertEqual(runtime["bindlessExecution"], mode)
                self.assertEqual(len(runtime["bindlessLoweringSha256"]), 64)
                self.assertTrue(any(b["name"] == "FaultOutput" for b in compute["bindings"]))
                self.assertTrue(all(b["stageMask"] == 4 for b in compute["bindings"]))
                import hashlib
                for name, digest in manifest["artifacts"].items():
                    self.assertEqual(hashlib.sha256((pathlib.Path(directory) / name).read_bytes()).hexdigest(), digest)
                if RUNTIME_VERIFIER:
                    self.verify_runtime_package(manifest_path)
                rebuilt = compile_bindless_package(self.schema, source,
                    ["computeMain", "fragmentMain", "vertexMain"], mode,
                    pathlib.Path(directory) / "relocated", SLANGC, reflect)
                rebuilt_manifest = read_bindless_publication(rebuilt)
                self.assertEqual(manifest["shaderPackageIdentity"], rebuilt_manifest["shaderPackageIdentity"],
                                 "Staging/build paths must not change the sealed package identity")
                if RUNTIME_VERIFIER:
                    result = subprocess.run([RUNTIME_VERIFIER, str(rebuilt), "valid", "retain"],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                broken = copy.deepcopy(self.schema)
                broken["maxSwitchCases"] = 1
                with self.assertRaisesRegex(BindlessContractError, "SWITCH-BUDGET"):
                    compile_bindless_package(broken, source,
                        ["computeMain", "fragmentMain"], mode, directory, SLANGC, reflect)
                self.assertFalse(manifest_path.exists(), "Failed replacement must invalidate old manifest")

    def verify_runtime_package(self, path):
        def run(expected="valid", option=""):
            result = subprocess.run([RUNTIME_VERIFIER, str(path), expected, option], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        run(option="retain")
        prefix = "RHI-BINDLESS-PACKAGE-"
        for option, reason in (("file", "FILE-READ-FAILED"), ("total", "FILE-READ-FAILED"),
                ("files", "FILE-BUDGET"), ("modules", "MODULE-BUDGET"),
                ("bindings", "BINDING-BUDGET"), ("tables", "TABLE-BUDGET"), ("zero", "LOAD-BUDGET")):
            with self.subTest(budget=option): run(prefix + reason, option)
        envelope_bytes = path.read_bytes()
        envelope = yaml.safe_load(envelope_bytes)
        payload_path = path.parent / envelope["payload"]
        payload_bytes = payload_path.read_bytes()
        payload = yaml.safe_load(payload_bytes)

        def publish(value):
            data = yaml.safe_dump(value, sort_keys=False).encode()
            payload_path.write_bytes(data)
            changed = dict(envelope, shaderPackageIdentity=hashlib.sha256(data).hexdigest())
            path.write_text(yaml.safe_dump(changed), encoding="utf-8")

        mutations = [
            (lambda p: p.update(indirectInstanceBaseContractVersion=2), "INDIRECT-INSTANCE-BASE-ABI"),
            (lambda p: p.update(indirectInstanceBaseContractVersion=1), "INDIRECT-INSTANCE-BASE-ABI"),
            (lambda p: p["snapshotMetadata"].update(wordCount=39), "METADATA-ABI"),
            (lambda p: p["snapshotMetadata"].update(binding=12), "METADATA-ABI"),
            (lambda p: p["tables"][0].update(metadataWordOffset=5), "METADATA-ABI"),
            (lambda p: p.update(activePrefixContractVersion=1), "ACTIVE-PREFIX-ABI"),
            (lambda p: p.update(faultReportingContractVersion=1), "FAULT-REPORTING-ABI"),
            (lambda p: p["tables"][0].update(capacity=3), "TABLE-MAPPING"),
            (lambda p: p["tables"][1].update(firstBinding=p["tables"][0]["firstBinding"]), "OPERATION"),
            (lambda p: p["tables"].pop(), "METADATA-ABI"),
            (lambda p: p["tables"][3]["bufferLayout"].update(elementStrideBytes=4), "BUFFER-ABI"),
            (lambda p: p["tables"][0].update(samplerTableId=5), "SAMPLER-MAPPING"),
            (lambda p: p.update(switchCases=1), "SWITCH-BUDGET"),
            (lambda p: p.update(snapshotBytes=0), "SNAPSHOT-BUDGET"),
            (lambda p: p["tables"][0].update(operations=["store"]), "OPERATION"),
            (lambda p: p["tables"][4].update(operations=["load", "load"]), "OPERATION-DUPLICATE"),
            (lambda p: p["modules"][0]["bindings"][0].update(binding=999), "MODULE-METADATA-MISMATCH"),
            (lambda p: p.update(matrixLayout="column-major"), "ARTIFACT-CONTRACT"),
            (lambda p: p["tables"][4].update(tableId=50), "LOWERING-METADATA-MISMATCH"),
            (lambda p: p.update(loweringYaml=p["loweringYaml"] + "\n"), "LOWERING-HASH"),
        ]
        for mutate, reason in mutations:
            with self.subTest(malformed=reason):
                changed = copy.deepcopy(payload)
                mutate(changed)
                publish(changed)
                run(prefix + reason)
        payload_path.write_bytes(payload_bytes)
        path.write_bytes(envelope_bytes)
        def alter_reflection(mutate, reason):
            changed = copy.deepcopy(payload)
            module = changed["modules"][0]
            target = path.parent / module["reflection"]
            original = target.read_bytes()
            reflection = yaml.safe_load(original)
            mutate(reflection)
            data = yaml.safe_dump(reflection, sort_keys=False).encode()
            target.write_bytes(data)
            changed["artifacts"][target.name] = hashlib.sha256(data).hexdigest()
            for field in ("bindings", "webGpuBindingSpace", "metalArgumentBuffer"):
                module[field] = reflection[field]
            publish(changed)
            try:
                run(prefix + reason)
            finally:
                target.write_bytes(original)
                payload_path.write_bytes(payload_bytes)
                path.write_bytes(envelope_bytes)
        for mutate, reason in (
            (lambda r: r.update(sourceSha256="f" * 64), "SOURCE-HASH-MISMATCH"),
            (lambda r: r["entryPoints"][0].update(stage="Vertex"), "BINDING-INVALID"),
            (lambda r: r["bindings"][0].update(stageMask=2), "BINDING-INVALID"),
            (lambda r: r.update(bindlessLoweringSha256="f" * 64), "REFLECTION-MISMATCH"),
        ):
            with self.subTest(reflection=reason): alter_reflection(mutate, reason)
        payload_path.write_bytes(payload_bytes + b"\n")
        run(prefix + "HASH-MISMATCH")
        payload_path.write_bytes(payload_bytes)
        for changes, reason in (({"payload": "../escape.yaml"}, "FILENAME"),
                                ({"contractVersion": 2}, "PUBLICATION-VERSION")):
            path.write_text(yaml.safe_dump(dict(envelope, **changes)), encoding="utf-8")
            run(prefix + reason)
        path.write_bytes(envelope_bytes)
        # Exercise missing and byte-corrupted targets, source and reflection.
        for name in payload["artifacts"]:
            target = path.parent / name
            original = target.read_bytes()
            target.write_bytes(original + b"changed")
            run(prefix + "HASH-MISMATCH")
            target.unlink()
            run(prefix + "FILE-READ-FAILED")
            target.write_bytes(original)
        run()

    def test_reflected_stage_mismatch_is_not_a_package(self):
        if not SLANGC:
            self.skipTest("Pass --slangc for target reflection")
        reflect = spirv_reflect_tool()
        with tempfile.TemporaryDirectory(prefix="arp-bindless-stage-") as directory:
            self.schema["tables"][4]["stages"] = ["vertex"]
            with self.assertRaisesRegex(BindlessContractError, "REFLECTION-STAGE-MISMATCH"):
                compile_bindless_package(self.schema, ROOT / "tests/fixtures/bindless_access.slang",
                    ["computeMain", "fragmentMain"], "Emulated", directory, SLANGC, reflect)
            self.assertFalse(list(pathlib.Path(directory).glob("*.bindless.yaml")))


SLANGC = None
RUNTIME_VERIFIER = None
SPIRV_REFLECT = None
SPIRV_VAL = None
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc")
    parser.add_argument("--spirv-reflect")
    parser.add_argument("--spirv-val")
    parser.add_argument("--runtime-verifier")
    args, remaining = parser.parse_known_args()
    SLANGC = args.slangc
    SPIRV_REFLECT = args.spirv_reflect
    SPIRV_VAL = args.spirv_val
    RUNTIME_VERIFIER = args.runtime_verifier
    unittest.main(argv=[sys.argv[0]] + remaining)
