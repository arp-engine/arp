"""Buffer declaration facts: adversarial binary/YAML and real Slang artifacts."""
import argparse
import copy
import pathlib
import os
import struct
import subprocess
import sys
import tempfile
import unittest

import yaml

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "fixtures"))
from spirv_layout import SpirvLayoutFacts
from shader_dependencies import parse_depfile, freeze_dependencies
from shader_bindings_codegen import BufferDeclarationLayoutReflector, _run_spirv_reflect
from buffer_declaration_type_layout_crosscheck import BufferTypeLayoutCrossCheck


class BinaryLayoutFixture:
    native_verifier = os.environ.get("ARP_BUFFER_LAYOUT_VERIFIER")
    native_type_verifier = os.environ.get("ARP_BUFFER_TYPE_LAYOUT_VERIFIER")
    def __init__(self, folder):
        self.path = pathlib.Path(folder) / "layout.spv"

    def write(self, instructions):
        words = [SpirvLayoutFacts.MAGIC, 0x00010600, 0, 4096, 0]
        for opcode, operands in instructions:
            words += [((len(operands) + 1) << 16) | opcode, *operands]
        self.path.write_bytes(struct.pack(f"<{len(words)}I", *words))
        return self.verify()

    def verify(self):
        error = None
        try:
            layouts = SpirvLayoutFacts(self.path).buffer_layouts()
        except RuntimeError as rejected:
            error = rejected
        if self.native_verifier:
            result = subprocess.run([self.native_verifier, str(self.path), str(self.path.stat().st_size),
                                     "--buffer-declarations", "16"], capture_output=True, text=True)
            if error:
                assert result.returncode == 1 and not result.stdout and result.stderr.startswith("SHADER-"), (error, result.stdout, result.stderr)
            else:
                assert result.returncode == 0, result.stderr
                native = yaml.safe_load(result.stdout)
                actual = {(r["set"], r["binding"]): {k: v for k, v in r.items() if k not in ("set", "binding")}
                          for r in native["bindings"]}
                expected = {key: {k: v for k, v in values.items() if k != "contractVersion"} for key, values in layouts.items()}
                assert actual == expected, (actual, expected)
        if error:
            if self.native_type_verifier:
                BufferTypeLayoutCrossCheck(self.native_type_verifier).invoke(self.path, '--spirv', expect_failure=True)
            raise error
        if self.native_type_verifier:
            BufferTypeLayoutCrossCheck(self.native_type_verifier).binary(self.path, SpirvLayoutFacts(self.path), layouts)
        return layouts

    @staticmethod
    def block(children=(1,), offsets=(0,), storage=12):
        return [(21, (1, 32, 0)), (30, (10, *children)), (32, (11, storage, 10)),
                (59, (11, 12, storage)), (71, (10, 2)), (71, (12, 34, 0)),
                (71, (12, 33, 3)), *[(72, (10, i, 35, v)) for i, v in enumerate(offsets)]]


class BufferLayoutBinaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="arp-buffer-layout-")
        self.addCleanup(self.temporary.cleanup)
        self.fixture = BinaryLayoutFixture(self.temporary.name)

    def reject(self, instructions, diagnostic):
        with self.assertRaisesRegex(RuntimeError, diagnostic):
            self.fixture.write(instructions)

    def image_facts(self, image=(2, 1, 1, 0, 0, 0, 1, 0), extra=(), pointer=2):
        instructions = [(22, (1, 32)), (25, image), (32, (3, 0, pointer)),
                        (59, (3, 4, 0)), (71, (4, 34, 1)), (71, (4, 33, 7)), *extra]
        self.fixture.write(instructions)
        return SpirvLayoutFacts(self.fixture.path).descriptor_image_types()[(1, 7)]

    def test_image_dimensions_depth_and_array_are_independent(self):
        for dimension, depth, arrayed in [(1, 0, 0), (1, 0, 1), (3, 0, 0), (1, 1, 0), (1, 2, 0)]:
            facts = self.image_facts((2, 1, dimension, depth, arrayed, 0, 1, 0))
            self.assertEqual((facts["dimension"], facts["depth"], facts["arrayed"]),
                             (dimension, depth, bool(arrayed)))
            self.assertEqual(facts["scalarKind"], "Float")
            self.assertEqual(facts["imageFormat"], 0)  # Not an invented runtime view format.

    def test_storage_image_and_descriptor_array(self):
        facts = self.image_facts((2, 1, 1, 0, 0, 0, 2, 3),
            [(21, (5, 32, 0)), (43, (5, 6, 4)), (28, (7, 2, 6))], pointer=7)
        self.assertEqual((facts["sampled"], facts["imageFormat"], facts["descriptorCount"]), (2, 3, 4))
        facts = self.image_facts((2, 1, 1, 0, 0, 1, 1, 0))
        self.assertTrue(facts["multisampled"])

    def test_independent_sampler_shape(self):
        instructions = [(26, (2,)), (32, (3, 0, 2)), (59, (3, 4, 0)),
                        (71, (4, 34, 1)), (71, (4, 33, 7))]
        self.fixture.write(instructions)
        facts = SpirvLayoutFacts(self.fixture.path).descriptor_image_types()[(1, 7)]
        self.assertEqual(facts["kind"], "Sampler")
        self.assertNotIn("dimension", facts)

    def test_image_malformed_shape_and_combined_sampler_reject(self):
        for image in [(2, 1, 1, 0, 0, 0, 0, 0), (2, 1, 7, 0, 0, 0, 1, 0),
                      (2, 1, 1, 3, 0, 0, 1, 0), (2, 1, 1, 0, 2, 0, 1, 0)]:
            with self.subTest(image=image), self.assertRaisesRegex(RuntimeError, "IMAGE-TYPE-SHAPE"):
                self.image_facts(image)
        with self.assertRaisesRegex(RuntimeError, "IMAGE-TYPE-UNSUPPORTED"):
            self.image_facts(extra=[(27, (5, 2))], pointer=5)

    def test_image_cycle_and_duplicate_binding_reject(self):
        with self.assertRaisesRegex(RuntimeError, "IMAGE-TYPE-CYCLIC"):
            self.image_facts(extra=[(21, (5, 32, 0)), (43, (5, 6, 2)), (28, (7, 7, 6))], pointer=7)
        with self.assertRaisesRegex(RuntimeError, "IMAGE-TYPE-DUPLICATE"):
            self.image_facts(extra=[(59, (3, 5, 0)), (71, (5, 34, 1)), (71, (5, 33, 7))])

    def test_compiler_depfile_escaping(self):
        targets, inputs = parse_depfile("out\\ file.spv: src\\ file.slang \\\ninclude.slang\n")
        self.assertEqual(targets, ["out file.spv"])
        self.assertEqual(inputs, ["src file.slang", "include.slang"])
        targets, inputs = parse_depfile(r"D\:\\out.spv: D\:\\shaders\\source.slang")
        self.assertEqual(targets, [r"D:\out.spv"])
        self.assertEqual(inputs, [r"D:\shaders\source.slang"])

    def test_compiler_depfile_invalid_rules_reject(self):
        for value in ("output source", "a b: source", "a: source\nb: other", "a:", "a: source\\"):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "SHADER-DEPENDENCY-"):
                parse_depfile(value)

    def test_compiler_dependency_identity_includes_headers(self):
        folder = pathlib.Path(self.temporary.name)
        source, include, depfile = folder / "source.slang", folder / "included.slang", folder / "inputs.d"
        source.write_text("#include included.slang", encoding="utf-8")
        include.write_text("original", encoding="utf-8")
        depfile.write_text("out.spv: source.slang included.slang included.slang", encoding="utf-8")
        original_cwd = pathlib.Path.cwd()
        try:
            os.chdir(folder)
            before = freeze_dependencies(depfile, folder / "out.spv", source, {"Engine": folder})
            self.assertEqual(len(before), 2)
            include.write_text("changed", encoding="utf-8")
            self.assertNotEqual(before, freeze_dependencies(depfile, folder / "out.spv", source, {"Engine": folder}))
            for text, diagnostic in [("wrong.spv: source.slang", "TARGET"),
                                     ("out.spv: included.slang", "SOURCE-MISSING"),
                                     ("out.spv: ../foreign.slang source.slang", "OWNER")]:
                depfile.write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "SHADER-DEPENDENCY-" + diagnostic):
                    freeze_dependencies(depfile, folder / "out.spv", source, {"Engine": folder})
        finally:
            os.chdir(original_cwd)

    def test_scalar_and_explicit_prefix(self):
        facts = self.fixture.write(self.fixture.block((1, 1), (0, 12)))[(0, 3)]
        self.assertEqual(facts["staticOccupiedExtentBytes"], 16)
        self.assertFalse(facts["hasRuntimeArray"])

    def test_binary_header_and_instruction_framing(self):
        self.fixture.write(self.fixture.block())
        original = self.fixture.path.read_bytes()
        for index, value, diagnostic in [(0, 0, "INVALID-BINARY"), (1, 0x00010700, "INVALID-HEADER"),
                                          (3, 0, "INVALID-HEADER"), (4, 1, "INVALID-HEADER"),
                                          (5, 21, "INVALID-INSTRUCTION")]:
            changed = bytearray(original)
            struct.pack_into('<I', changed, index * 4, value)
            self.fixture.path.write_bytes(changed)
            with self.subTest(index=index), self.assertRaisesRegex(RuntimeError, diagnostic):
                self.fixture.verify()

    def test_runtime_tail_with_prefix(self):
        instructions = self.fixture.block((1, 2), (0, 16)) + [(29, (2, 1)), (71, (2, 6, 8))]
        facts = self.fixture.write(instructions)[(0, 3)]
        self.assertEqual((facts["staticOccupiedExtentBytes"], facts["runtimeArrayOffsetBytes"],
                          facts["runtimeArrayStrideBytes"], facts["runtimeElementOccupiedExtentBytes"]), (4, 16, 8, 4))

    def test_fixed_array_uses_last_occupied_byte(self):
        instructions = self.fixture.block((2,), (0,)) + [(43, (1, 3, 3)), (28, (2, 1, 3)), (71, (2, 6, 16))]
        self.assertEqual(self.fixture.write(instructions)[(0, 3)]["staticOccupiedExtentBytes"], 36)

    def test_matrix_array_exact_stride(self):
        instructions = self.fixture.block((5,), (0,)) + [
            (22, (2, 32)), (23, (3, 2, 3)), (24, (4, 3, 4)),
            (43, (1, 6, 2)), (28, (5, 4, 6)), (71, (5, 6, 64)),
            (72, (10, 0, 5)), (72, (10, 0, 7, 16))]
        self.assertEqual(self.fixture.write(instructions)[(0, 3)]["staticOccupiedExtentBytes"], 124)

    def test_descriptor_arrays_are_separate_from_byte_arrays(self):
        instructions = self.fixture.block()
        instructions[2] = (32, (11, 12, 2))
        instructions += [(43, (1, 3, 2)), (28, (2, 10, 3))]
        facts = self.fixture.write(instructions)[(0, 3)]
        self.assertEqual(facts["descriptorCount"], 2)
        self.assertEqual(facts["staticOccupiedExtentBytes"], 4)

    def test_type_graph_same_width_signedness_and_member_matrix_stride(self):
        signed = self.fixture.block((1, 2), (0, 4)) + [(21, (2, 32, 1))]
        self.assertEqual(self.fixture.write(signed)[(0, 3)]["staticOccupiedExtentBytes"], 8)
        matrices = self.fixture.block((4, 4), (0, 128)) + [
            (22, (2, 32)), (23, (3, 2, 3)), (24, (4, 3, 4)),
            (72, (10, 0, 5)), (72, (10, 0, 7, 16)),
            (72, (10, 1, 5)), (72, (10, 1, 7, 32))]
        self.assertEqual(self.fixture.write(matrices)[(0, 3)]["staticOccupiedExtentBytes"], 236)

    def test_type_graph_preserves_nested_descriptor_dimensions(self):
        instructions = self.fixture.block()
        instructions[2] = (32, (11, 12, 5))
        instructions += [(43, (1, 3, 3)), (43, (1, 4, 2)), (28, (2, 10, 3)), (28, (5, 2, 4))]
        facts = self.fixture.write(instructions)[(0, 3)]
        self.assertEqual(facts["descriptorCount"], 6)
        self.assertEqual(facts["staticOccupiedExtentBytes"], 4)

    def test_missing_and_duplicate_decorations(self):
        original = self.fixture.block()
        for index in (4, 5, 6, 7):
            with self.subTest(index=index):
                self.reject(original[:index] + original[index + 1:], "DECORATION")
                self.reject(original + [original[index]], "DECORATION")

    def test_duplicate_definition_and_binding(self):
        self.reject(self.fixture.block() + [(21, (1, 32, 0))], "DUPLICATE-DEFINITION")
        self.reject(self.fixture.block() + [(59, (11, 13, 12)), (71, (13, 34, 0)), (71, (13, 33, 3))],
                    "BINDING-DUPLICATE")

    def test_overlap_and_missing_layout(self):
        self.reject(self.fixture.block((1, 1), (0, 2)), "OVERLAP")
        self.reject(self.fixture.block((2,), (0,)) + [(29, (2, 1))], "DECORATION")
        self.reject(self.fixture.block((2,), (0,)) + [(29, (2, 1)), (71, (2, 6, 2))], "STRIDE")

    def test_runtime_placement_and_uniform_rejected(self):
        runtime = [(29, (2, 1)), (71, (2, 6, 4))]
        self.reject(self.fixture.block((2, 1), (0, 4)) + runtime, "RUNTIME-PLACEMENT")
        self.reject(self.fixture.block((2,), (0,), 2) + runtime, "RUNTIME-UNIFORM")
        self.reject(self.fixture.block((3,), (0,)) + runtime + [(30, (3, 2)), (72, (3, 0, 35, 0))],
                    "RUNTIME-PLACEMENT")

    def test_unknown_bool_pointer_and_legacy_block_rejected(self):
        self.reject(self.fixture.block((2,), (0,)) + [(20, (2,))], "TYPE-UNSUPPORTED")
        instructions = self.fixture.block()
        instructions[2] = (32, (11, 2, 10))
        self.reject(instructions, "STORAGE-CLASS")
        instructions = self.fixture.block()
        instructions[4] = (71, (10, 3))
        self.reject(instructions, "DECORATION")

    def test_cycles_and_iterative_deep_types(self):
        self.reject(self.fixture.block((10,), (0,)), "CYCLIC-TYPE")
        instructions = self.fixture.block((2,), (0,)) + [(43, (1, 4, 2)), (28, (2, 3, 4)),
                                                        (28, (3, 2, 4)), (71, (2, 6, 8)), (71, (3, 6, 8))]
        self.reject(instructions, "CYCLIC-TYPE")
        # 1500 nested structs exceeds Python recursive parsing depth. Input
        # relationships define this test, and iterative parsing handles it.
        instructions = self.fixture.block((100,), (0,))
        for identifier in range(100, 1600):
            instructions += [(30, (identifier, identifier + 1 if identifier < 1599 else 1)),
                             (72, (identifier, 0, 35, 0))]
        self.assertEqual(self.fixture.write(instructions)[(0, 3)]["staticOccupiedExtentBytes"], 4)

    def test_zero_specialization_and_overflow_lengths_rejected(self):
        for opcode, constant in [(43, (1, 3, 0)), (50, (1, 3, 2))]:
            self.reject(self.fixture.block((2,), (0,)) + [(opcode, constant), (28, (2, 1, 3)),
                                                         (71, (2, 6, 4))], "ARRAY-LENGTH|TYPE")
        self.reject(self.fixture.block((2,), (0,)) + [(21, (4, 64, 0)), (43, (4, 3, 0xffffffff, 0xffffffff)),
                                                     (28, (2, 1, 3)), (71, (2, 6, 8))], "OVERFLOW")

    def test_matrix_major_missing_and_small_stride_rejected(self):
        base = self.fixture.block((4,), (0,)) + [(22, (2, 32)), (23, (3, 2, 4)), (24, (4, 3, 4))]
        self.reject(base, "MATRIX-MISSING")
        self.reject(base + [(72, (10, 0, 4)), (72, (10, 0, 7, 16))], "MEMBER-DECORATION")
        self.reject(base + [(72, (10, 0, 5)), (72, (10, 0, 7, 8))], "STRIDE")

    def test_shared_nested_yaml_members_are_reusable(self):
        self.fixture.write(self.fixture.block((2, 2), (0, 16)) + [(30, (2, 1)), (72, (2, 0, 35, 0))])
        def description(identifier, opcode, members):
            return dict(id=identifier, op=opcode, member_count=len(members), members=members,
                        traits={"array": {"dims_count": 0, "dims": [], "stride": 0}})
        scalar = description(1, 21, [])
        inner = description(2, 30, [scalar])
        outer = description(10, 30, [inner, inner])
        leaf = dict(offset=0, member_count=0, members=[], type_description=scalar)
        first = dict(offset=0, member_count=1, members=[leaf], type_description=inner)
        second = dict(offset=16, member_count=1, members=[leaf], type_description=inner)
        block = dict(offset=0, member_count=2, members=[first, second], type_description=outer, size=20, padded_size=32)
        descriptor = dict(set=0, binding=3, spirv_id=12, descriptor_type=7,
                          array={"dims_count": 0, "dims": []}, block=block)
        root = {"all_descriptor_bindings": [descriptor], "all_type_descriptions": [scalar, inner, outer],
                "all_block_variables": [leaf, first, second, block],
                "module": {"descriptor_binding_count": 1, "descriptor_bindings": [descriptor]}}
        layouts = BufferDeclarationLayoutReflector.parse(yaml.safe_dump(root), self.fixture.path)
        self.assertEqual(layouts[(0, 3)]["staticOccupiedExtentBytes"], 20)


class RealBufferLayoutFixture:
    def __init__(self, executable, folder):
        self.executable, self.folder, self.hostile_cases = executable, pathlib.Path(folder), 0

    def parse(self, node, path):
        return BufferDeclarationLayoutReflector.parse(yaml.safe_dump(node, sort_keys=False), path)

    def reject(self, original, path, mutation, diagnostic):
        changed = copy.deepcopy(original)
        mutation(changed)
        try:
            self.parse(changed, path)
        except RuntimeError as error:
            if diagnostic not in str(error):
                raise AssertionError(f"expected {diagnostic}, got {error}") from error
        else:
            raise AssertionError(f"accepted hostile reflection: {diagnostic}")
        self.hostile_cases += 1

    def verify(self):
        reports = []
        for path in sorted(self.folder.rglob("*.entry.yaml")):
            publication = yaml.safe_load(path.read_text())
            spirv = path.parent / publication["files"]["spirv"]["path"]
            root = yaml.safe_load(_run_spirv_reflect(self.executable, spirv))
            facts = self.parse(root, spirv)
            if BinaryLayoutFixture.native_verifier:
                native = BinaryLayoutFixture(spirv.parent)
                native.path = spirv
                binary = native.verify()
                assert {k: {f: v for f, v in r.items() if not f.startswith("reflectedBlock")} for k, r in facts.items()} == binary
            self.reject(root, spirv, lambda n: n["module"].update(descriptor_binding_count=True), "FIELD")
            if facts:
                first = next(d for d in root["module"]["descriptor_bindings"] if d["descriptor_type"] in (6, 7))
                index = root["module"]["descriptor_bindings"].index(first)
                binding = lambda n: n["module"]["descriptor_bindings"][index]
                for field, value, diagnostic in [("set", 99, "BINARY-MISMATCH"), ("spirv_id", 0, "BINARY-MISMATCH"),
                                                  ("descriptor_type", 0, "TYPE")]:
                    self.reject(root, spirv, lambda n, f=field, v=value: binding(n).update({f: v}), diagnostic)
                self.reject(root, spirv, lambda n: binding(n)["array"].update(dims_count=1, dims=[99]), "ARRAY")
                self.reject(root, spirv, lambda n: binding(n)["block"].update(member_count=99), "MEMBERS")
                self.reject(root, spirv, lambda n: binding(n)["block"]["type_description"].update(id=0), "TYPE")
                self.reject(root, spirv, lambda n: binding(n)["block"]["members"][0].update(offset=99), "MEMBER-MISMATCH")
                self.reject(root, spirv, lambda n: binding(n)["block"].update(padded_size=-1), "FIELD")
                self.reject(root, spirv, lambda n: n["module"]["descriptor_bindings"].append(binding(n)), "DESCRIPTORS")
                self.reject(root, spirv, lambda n: n["module"].update(descriptor_bindings=[]), "DESCRIPTORS")
                self.reject(root, spirv, lambda n: n["module"]["descriptor_bindings"].__setitem__(
                    index, copy.deepcopy(binding(n))), "ALIASES")
                if first["block"]["members"][0]["type_description"]["op"] == 29:
                    self.reject(root, spirv, lambda n: binding(n)["block"]["members"][0]["type_description"]["traits"]["array"].update(stride=999), "STRIDE")
            reports.append({"publication": str(path.resolve()), "entry": publication["entry"],
                            "stage": publication["stage"], "bindings": [dict(set=k[0], binding=k[1], **v) for k, v in sorted(facts.items())]})
        assert reports, "real Slang publications missing"
        selected = {pathlib.Path(r["publication"]).parent.name: r for r in reports}
        uniform = selected["uniformReadFragment"]["bindings"][0]
        assert uniform["staticOccupiedExtentBytes"] == 4 and uniform["reflectedBlockPaddedSizeBytes"] == 16
        runtime = selected["indirectProducer"]["bindings"][0]
        assert runtime["hasRuntimeArray"] and runtime["runtimeArrayStrideBytes"] == 4
        assert runtime["staticOccupiedExtentBytes"] == 0 and runtime["reflectedBlockSizeBytes"] == 0
        assert selected["storageArrayFragment"]["bindings"][0]["descriptorCount"] == 2
        assert selected["uniformArrayFragment"]["bindings"][0]["descriptorCount"] == 2
        return reports


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spirv-reflect")
    parser.add_argument("--fixtures")
    parser.add_argument("--report")
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(BufferLayoutBinaryTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if bool(args.spirv_reflect) != bool(args.fixtures):
        parser.error("real verification requires both --spirv-reflect and --fixtures")
    if args.fixtures:
        fixture = RealBufferLayoutFixture(args.spirv_reflect, args.fixtures)
        reports = fixture.verify()
        report = {"type": "ShaderBufferDeclarationLayoutValidation", "contractVersion": 1,
                  "binaryTests": result.testsRun, "realPublications": len(reports), "hostileYamlCases": fixture.hostile_cases,
                  "nativeBindingMinimumProven": False, "executionAccessProven": False, "publications": reports}
        if args.report:
            pathlib.Path(args.report).write_text(yaml.safe_dump(report, sort_keys=False))
        print(f"SPIRV_BUFFER_LAYOUT_FACTS_OK binary{result.testsRun} real{len(reports)} hostile{fixture.hostile_cases} padding-distinct runtime-stride descriptor-count native-closed")


if __name__ == "__main__":
    main()
