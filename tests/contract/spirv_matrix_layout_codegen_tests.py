"""Matrix array layouts require transparent reflection and actual binary facts."""
import argparse
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from shader_bindings_codegen import _normalize_transparent_push_members, reflect_push_constant_block
from spirv_layout import SpirvLayoutFacts
from compile_shaders import push_constant_lowering_contract


class MatrixLayoutContract(unittest.TestCase):
    def test_authored_push_cannot_silently_publish_zero_size(self):
        body = "struct ProbePushConstants { float4 a; float4 b; };\n"
        valid = body + "[[vk::push_constant]] ProbePushConstants Push;"
        self.assertIsNotNone(push_constant_lowering_contract(valid, "probe.slang"))
        self.assertIsNone(push_constant_lowering_contract(body, "probe.slang"))
        for declaration in ("ConstantBuffer<ProbePushConstants> Push;", "MissingPushConstants Push;",
                            "EmptyPushConstants Push;"):
            with self.subTest(declaration=declaration):
                with self.assertRaisesRegex(RuntimeError, "SHADER-PUSH-SOURCE-DECLARATION-UNSUPPORTED"):
                    push_constant_lowering_contract(body + "[[vk::push_constant]] " + declaration, "probe.slang")

    def facts(self, decorations):
        words = [SpirvLayoutFacts.MAGIC, 0x00010000, 0, 32, 0]
        for decoration, values in decorations:
            operands = [25, 0, decoration, *values]
            words.extend([((len(operands) + 1) << 16) | 72, *operands])
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "layout.spv"
        path.write_bytes(struct.pack(f"<{len(words)}I", *words))
        return SpirvLayoutFacts(path)

    def wrapper(self):
        return [dict(name="lightViewProjection", offset="32", size="192",
                     members=[1], type_description="*td0"),
                dict(name="data", offset="0", size="192", _matrix_row_count=4,
                     _matrix_column_count=4, _matrix_stride=0, _array_dims=[3])]

    def normalize(self, entries, facts):
        _normalize_transparent_push_members(entries, {0: {"id": "25"}}, [0], facts)

    def test_full_range_wrapper_preserves_authored_identity(self):
        entries = self.wrapper()
        self.normalize(entries, self.facts([(5, []), (7, [16])]))
        self.assertEqual(entries[0]["name"], "lightViewProjection")
        self.assertEqual(entries[0]["offset"], "32")
        self.assertEqual(entries[0]["_matrix_stride"], 16)
        self.assertEqual(entries[0]["_array_dims"], [3])

    def test_no_guessed_matrix_stride_or_major(self):
        for decorations in ([], [(5, [])], [(7, [16])], [(4, []), (7, [16])],
                            [(4, []), (5, []), (7, [16])], [(5, []), (7, [0])],
                            [(5, [1]), (7, [16])], [(5, []), (7, [16, 32])]):
            with self.subTest(decorations=decorations):
                with self.assertRaisesRegex(RuntimeError, "MATRIX-FACTS"):
                    self.normalize(self.wrapper(), self.facts(decorations))

    def test_conflicting_binary_and_reflected_stride(self):
        with self.assertRaisesRegex(RuntimeError, "CONFLICTING-DECORATION"):
            self.facts([(7, [16]), (7, [32])])
        entries = self.wrapper()
        entries[1]["_matrix_stride"] = 32
        with self.assertRaisesRegex(RuntimeError, "stride conflicts"):
            self.normalize(entries, self.facts([(5, []), (7, [16])]))

    def test_nontransparent_and_ambiguous_wrappers_reject(self):
        edits = [lambda e: e[1].update(offset="4"), lambda e: e[1].update(size="128"),
                 lambda e: e[0].update(_array_dims=[2]), lambda e: e[0].update(members=[1, 1]),
                 lambda e: e[0].update(members=[0]), lambda e: e[0].pop("type_description")]
        for edit in edits:
            entries = self.wrapper()
            edit(entries)
            with self.subTest(entries=entries):
                with self.assertRaisesRegex(RuntimeError, "push member|wrapper type"):
                    self.normalize(entries, self.facts([(5, []), (7, [16])]))

    def test_real_slang_cascaded_shadow_array(self):
        block = reflect_push_constant_block(ARGS.spirv_reflect, ARGS.spv)
        self.assertEqual(block.size, 224)
        member = block.members[0]
        self.assertEqual((member.name, member.size, member.offset), ("lightViewProjection", 192, 0))
        self.assertEqual((member.scalar_width, member.matrix_row_count, member.matrix_column_count,
                          member.matrix_stride, member.array_stride), (32, 4, 4, 16, 64))
        self.assertEqual(member.array_dims, [3])
        self.assertTrue(member.decorations & 8)
        self.assertFalse(member.decorations & 4)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--spirv-reflect", required=True)
    parser.add_argument("--spv", required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
