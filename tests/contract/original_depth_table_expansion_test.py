"""Closed original-program lowering; no runtime actor/native authority."""
import copy
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest import mock

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from original_depth_table_expansion import (OriginalDepthTableExpansion,
    OriginalTableExpansionError, compile_expansion, read_expansion)
from spirv_layout import SpirvLayoutFacts
from compile_shaders import get_shader_bindings


class OriginalDepthTableExpansionTest(unittest.TestCase):
    def schema(self, profile="Compute"):
        return yaml.safe_load((ROOT / "tests/fixtures" /
            ("original_depth_table_" + profile.lower() + "_expansion.yaml")).read_text())

    def source(self, profile="Compute"):
        return (ROOT / "tests/fixtures" / ("descriptor_depth_compute.slang"
            if profile == "Compute" else "original_depth_table_geometry.slang"
            if profile == "GraphicsGeometry" else "descriptor_depth.slang")).read_bytes()

    def test_complete_program_and_exact_map(self):
        for profile in ("Compute", "Graphics", "GraphicsGeometry"):
            generated = OriginalDepthTableExpansion.generate(self.schema(profile), self.source(profile))
            self.assertEqual(generated.source.count("ARP_SAMPLED_DEPTH(Inputs_"), 3)
            self.assertNotIn("Inputs[", generated.source)
            self.assertEqual(generated.source.count(".Load(location)"), 3)
            self.assertEqual([m["arrayElement"] for m in generated.metadata["elementMap"]], [0, 1, 2])
            self.assertEqual([m["binding"] for m in generated.metadata["elementMap"]], [0, 1, 2])
            self.assertEqual(generated.metadata["logicalTable"]["capacity"], 3)
            original = self.source(profile).decode()
            # All code outside the two approved transformation sites is retained.
            self.assertIn(original[original.index('[shader("'):].replace(
                "Inputs[thread.x % 3].Load(int3(0, 0, 0))" if profile == "Compute"
                else "Inputs[element].Load(int3(0, 0, 0))",
                "arpOriginalInputsLoad(thread.x % 3, int3(0, 0, 0))" if profile == "Compute"
                else "arpOriginalInputsLoad(element, int3(0, 0, 0))"), generated.source)
            self.assertEqual(generated.metadata["executionAvailable"], False)

    def test_v1_backend_publication_is_frozen_and_not_webgl_authority(self):
        for profile in ('Compute', 'Graphics', 'GraphicsGeometry'):
            with self.subTest(profile=profile):
                generated = OriginalDepthTableExpansion.generate(self.schema(profile), self.source(profile))
                self.assertEqual(generated.metadata['contractVersion'], 1)
                self.assertIs(generated.metadata['executionAvailable'], False)
                self.assertEqual(set(generated.metadata['backendDecisions']), {'Null', 'Vulkan', 'Metal', 'WebGPU'})
                for decision in generated.metadata['backendDecisions'].values():
                    self.assertEqual(decision, dict(mode='DiagnosticFailure', executionAvailable=False,
                        diagnostic='RHI-ORIGINAL-TYPED-INPUT-TABLE-PROFILE-UNAVAILABLE'))

    def test_unknown_missing_and_hostile_fields(self):
        schema = self.schema()
        for key in schema:
            bad = copy.deepcopy(schema); del bad[key]
            with self.assertRaises(OriginalTableExpansionError):
                OriginalDepthTableExpansion.generate(bad, self.source())
        for field, value in (("extra", 1), ("contractVersion", 2), ("contractVersion", True),
                ("type", "BindlessShaderVariant"), ("profile", "Fragment"),
                ("sourceSha256", "0" * 64), ("capacity", 1), ("capacity", True),
                ("maxPhysicalBindings", 3), ("maxSwitchCases", 2),
                ("maxArtifactBytes", 0), ("maxArtifactBytes", True)):
            bad = copy.deepcopy(schema); bad[field] = value
            with self.assertRaises(OriginalTableExpansionError):
                OriginalDepthTableExpansion.generate(bad, self.source())

    def test_program_semantics_cannot_be_reasserted_by_author(self):
        for before, after in ((b"% 3", b"% 2"), (b"float>", b"float4>"),
                (b"5, 1, 1", b"6, 1, 1"), (b"* 30", b"* 31"),
                (b"int3(0, 0, 0)", b"int3(1, 0, 0)"), (b"[3]", b"[4]")):
            import hashlib
            changed = self.source().replace(before, after)
            bad = self.schema(); bad["sourceSha256"] = hashlib.sha256(changed).hexdigest()
            with self.assertRaisesRegex(OriginalTableExpansionError, "SOURCE-IDENTITY"):
                OriginalDepthTableExpansion.generate(bad, changed)

    def test_geometry_program_cannot_reassert_procedural_or_instance_semantics(self):
        import hashlib
        original=self.source('GraphicsGeometry')
        for before,after in ((b'vertex.position + instance.offset',b'float2(0, 0)'),
                (b'instance.color',b'float4(0, 0, 1, 1)'),
                (b'uint(input.color.x)',b'0'),(b'% 3',b'% 2')):
            changed=original.replace(before,after);self.assertNotEqual(changed,original)
            schema=self.schema('GraphicsGeometry');schema['sourceSha256']=hashlib.sha256(changed).hexdigest()
            with self.assertRaisesRegex(OriginalTableExpansionError,'SOURCE-IDENTITY'):
                OriginalDepthTableExpansion.generate(schema,changed)

    def test_geometry_reflection_rejects_missing_retyped_or_duplicate_attributes(self):
        generated=OriginalDepthTableExpansion.generate(self.schema('GraphicsGeometry'),self.source('GraphicsGeometry'))
        reflection=dict(bindings=[],sourceSha256=generated.metadata['generatedSourceSha256'],
            entryPoints=[dict(name='vertexMain',stage='Vertex')],webGpuBindingSpace=[],webGpuBindingSpaceStatus='Available',
            vertexAttributes=[dict(location=0,format='Float2'),dict(location=1,format='Float2'),dict(location=2,format='Float4')])
        OriginalDepthTableExpansion.verify_entry(generated,'vertexMain','Vertex',reflection)
        for mutate in (lambda n:n['vertexAttributes'].pop(),
                lambda n:n['vertexAttributes'][2].update(format='Float3'),
                lambda n:n['vertexAttributes'][2].update(location=1)):
            bad=copy.deepcopy(reflection);mutate(bad)
            with self.assertRaisesRegex(OriginalTableExpansionError,'VERTEX-ABI'):
                OriginalDepthTableExpansion.verify_entry(generated,'vertexMain','Vertex',bad)

    def test_independent_budget_exact_and_less(self):
        for profile, bindings in (("Compute", 4), ("Graphics", 3), ("GraphicsGeometry", 3)):
            schema = self.schema(profile)
            schema.update(maxPhysicalBindings=bindings, maxSwitchCases=3)
            OriginalDepthTableExpansion.generate(schema, self.source(profile))
            for field in ("maxPhysicalBindings", "maxSwitchCases"):
                bad = copy.deepcopy(schema); bad[field] -= 1
                with self.assertRaisesRegex(OriginalTableExpansionError, "BUDGET"):
                    OriginalDepthTableExpansion.generate(bad, self.source(profile))

    def test_explicit_depth_macros_never_enter_legacy_name_inference(self):
        original = get_shader_bindings(self.source('Graphics').decode())
        self.assertEqual(len(original), 1)
        self.assertEqual((original[0]['name'],original[0]['arrayCount'],original[0]['set']), ('Inputs',3,0))
        expanded = OriginalDepthTableExpansion.generate(self.schema('Graphics'), self.source('Graphics'))
        physical = get_shader_bindings(expanded.source)
        self.assertEqual(len(physical),3)
        self.assertEqual([b['name'] for b in physical], ['Inputs_0','Inputs_1','Inputs_2'])
        self.assertTrue(all(b['type']=='SampledTexture' and b['depthTexture'] and b['arrayCount']==1 for b in physical))

    def test_real_targets_reflection_and_hostile_publication(self):
        if not SLANGC:
            self.skipTest("real target test requires --slangc and --spirv-reflect")
        for profile in ("Compute", "Graphics", "GraphicsGeometry"):
            with tempfile.TemporaryDirectory(prefix="arp-original-table-") as directory:
                path = compile_expansion(self.schema(profile), self.source(profile), directory, SLANGC, REFLECT)
                publication = read_expansion(path)
                expected = ["computeMain"] if profile == "Compute" else ["vertexMain", "fragmentMain"]
                self.assertEqual([e["entry"] for e in publication["entries"]], expected)
                for entry in publication["entries"]:
                    reflection_path = path.parent / entry["files"]["reflection"]["path"]
                    reflection = yaml.safe_load(reflection_path.read_text())
                    active = reflection["bindings"]
                    self.assertEqual(len(active), 0 if entry["stage"] == "Vertex" else (4 if profile == "Compute" else 3))
                    self.assertTrue(all(b["arrayCount"] == 1 for b in active))
                    for b in active[:3]:
                        self.assertTrue(b["depthTexture"])
                    wgsl = (path.parent / entry["files"]["wgsl"]["path"]).read_text()
                    self.assertNotIn("binding_array", wgsl)
                original = path.read_bytes()
                for mutate in (
                        lambda n: n["elementMap"].reverse(),
                        lambda n: n["elementMap"][1].update(binding=0),
                        lambda n: n.update(executionAvailable=True),
                        lambda n: n.update(executionAvailable=0),
                        lambda n: n.update(contractVersion=True),
                        lambda n: n.update(contractVersion=2),
                        lambda n: n['backendDecisions'].update(WebGL=dict(n['backendDecisions']['WebGPU'])),
                        lambda n: n['backendDecisions'].update(WebGL=dict(mode='Native', executionAvailable=True,
                            diagnostic='unauthorized')),
                        lambda n: n['backendDecisions'].pop('Metal'),
                        lambda n: n['backendDecisions'].update(WebGL=n['backendDecisions'].pop('WebGPU')),
                        lambda n: n["elementMap"][0].update(arrayElement=False),
                        lambda n: n["entries"][0].update(stage="Fragment"),
                        lambda n: n.update(extra="unauthorized")):
                    changed = copy.deepcopy(publication); mutate(changed)
                    path.write_text(yaml.safe_dump(changed, sort_keys=False))
                    with self.assertRaises(OriginalTableExpansionError): read_expansion(path)
                path.write_bytes(original)
                # A rehashed reflection must still satisfy the exact generated
                # physical interface; hashes alone are not semantic proof.
                for entry in publication["entries"]:
                    role = entry["files"]["reflection"]
                    artifact = path.parent / role["path"]
                    data = artifact.read_bytes()
                    reflection = yaml.safe_load(data)
                    if not reflection["bindings"]:
                        continue
                    for field, value in (("group", 3), ("webGpuBinding", 7),
                            ("capacity", 3), ("arrayElement", 1), ("depthTexture", False),
                            ("physicalType", "Sampler")):
                        import hashlib
                        changed_reflection = copy.deepcopy(reflection)
                        changed_reflection["webGpuBindingSpace"][0][field] = value
                        changed_data = yaml.safe_dump(changed_reflection, sort_keys=False).encode()
                        artifact.write_bytes(changed_data)
                        changed = copy.deepcopy(publication)
                        changed_role = next(e for e in changed["entries"] if e["entry"] == entry["entry"])["files"]["reflection"]
                        changed_role["sha256"] = hashlib.sha256(changed_data).hexdigest()
                        changed["artifactBytes"] += len(changed_data) - len(data)
                        path.write_text(yaml.safe_dump(changed, sort_keys=False))
                        with self.assertRaisesRegex(OriginalTableExpansionError, "PHYSICAL-MAP"):
                            read_expansion(path)
                    artifact.write_bytes(data)
                path.write_bytes(original)
                consuming = next(e for e in publication["entries"] if e["stage"] != "Vertex")
                for role in ("spirv", "wgsl", "metal"):
                    import hashlib
                    artifact = path.parent / consuming["files"][role]["path"]
                    data = artifact.read_bytes()
                    if role == "spirv":
                        changed_data = bytearray(data)
                        image_offset = next(offset for offset, opcode, operands in
                            SpirvLayoutFacts(artifact).instructions if opcode == 25)
                        struct.pack_into('<I', changed_data, (image_offset + 4) * 4, 0)
                        changed_data = bytes(changed_data)
                    elif role == "wgsl":
                        changed_data = data.replace(b'texture_depth_2d', b'texture_2d<f32>')
                    else:
                        changed_data = data.replace(b'[[texture(0)]]', b'[[texture(7)]]')
                    self.assertNotEqual(data, changed_data)
                    artifact.write_bytes(changed_data)
                    changed = copy.deepcopy(publication)
                    changed_role = next(e for e in changed["entries"] if e["entry"] == consuming["entry"])["files"][role]
                    changed_role["sha256"] = hashlib.sha256(changed_data).hexdigest()
                    changed["artifactBytes"] += len(changed_data) - len(data)
                    path.write_text(yaml.safe_dump(changed, sort_keys=False))
                    with self.assertRaises(OriginalTableExpansionError): read_expansion(path)
                    artifact.write_bytes(data)
                path.write_bytes(original)
                for entry in publication["entries"]:
                    for role in entry["files"]:
                        artifact = path.parent / entry["files"][role]["path"]
                        data = artifact.read_bytes(); artifact.write_bytes(data + b"tampered")
                        with self.assertRaises(OriginalTableExpansionError): read_expansion(path)
                        artifact.write_bytes(data)
                read_expansion(path)
                bad = self.schema(profile); bad["maxArtifactBytes"] = 1
                with self.assertRaisesRegex(OriginalTableExpansionError, "ARTIFACT-BUDGET"):
                    compile_expansion(bad, self.source(profile), directory, SLANGC, REFLECT)
                self.assertFalse(path.exists(), "failed replacement retained final success manifest")
                compile_expansion(self.schema(profile), self.source(profile), directory, SLANGC, REFLECT)
                with mock.patch('original_depth_table_expansion.read_expansion', side_effect=OriginalTableExpansionError('staged validation fault')):
                    with self.assertRaisesRegex(OriginalTableExpansionError, 'staged validation fault'):
                        compile_expansion(self.schema(profile), self.source(profile), directory, SLANGC, REFLECT)
                self.assertFalse(path.exists(), 'failed staged validation retained a final manifest')
                self.assertFalse(list(path.parent.glob('*.pending.yaml')), 'failed staging left a publishable manifest')


SLANGC = REFLECT = None
if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc")
    parser.add_argument("--spirv-reflect")
    args, remaining = parser.parse_known_args()
    SLANGC, REFLECT = args.slangc, args.spirv_reflect
    unittest.main(argv=[sys.argv[0]] + remaining)
