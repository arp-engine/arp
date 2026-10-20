"""WGSL IO interpolation must survive normalization of internal struct copies."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.compile_shaders import normalize_wgsl_struct_member_delimiters


class WgslStructSyntaxTests(unittest.TestCase):
    def normalize(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "target.wgsl"
            path.write_text(source, encoding="utf-8")
            normalize_wgsl_struct_member_delimiters(path)
            result = path.read_text(encoding="utf-8")
            normalize_wgsl_struct_member_delimiters(path)
            self.assertEqual(path.read_text(encoding="utf-8"), result)
            return result

    def test_internal_copy_preserves_types_and_entry_interface(self):
        source = """struct Internal { position: vec4<f32>, @interpolate(flat) sum: u32, };
struct Input { @interpolate(flat) @location(0) sum: u32, };
@fragment fn main(input: Input) -> @location(0) vec4<f32> {
 let local = Internal(vec4<f32>(0), input.sum); return vec4<f32>(f32(local.sum)); }
"""
        self.assertEqual(self.normalize(source), source.replace("@interpolate(flat) sum", " sum", 1))

    def test_location_attribute_order_and_multiline_sampling_unchanged(self):
        source = """struct Output {
 @location(7) @interpolate(perspective, centroid) uv: vec2<f32>,
 @interpolate(flat)
 @location(3) material: u32,
 @location (9) @interpolate (flat) other: u32,
};"""
        self.assertEqual(self.normalize(source), source)

    def test_comments_cannot_authorize_a_location_or_modify_commented_code(self):
        source = """// struct Fake { @interpolate(flat) a: u32, }
struct Internal { /* @location(0) */ @interpolate(flat) a: u32, };
/* struct Fake2 { @interpolate(flat) b: u32, } */"""
        self.assertEqual(self.normalize(source), source.replace("*/ @interpolate(flat) a:", "*/  a:"))

    def test_missing_entry_location_is_not_manufactured(self):
        result = self.normalize("struct Input { @interpolate(flat) value: u32, }; @fragment fn main(i: Input) {}")
        self.assertNotIn("@location", result)
        self.assertIn("value: u32", result)

    def test_non_interpolation_attributes_and_non_struct_text_unchanged(self):
        source = "struct Data { @align(16) @size(16) values: array<vec2<f32>, 2>, };\n// @interpolate(flat)"
        self.assertEqual(self.normalize(source), source)


if __name__ == "__main__":
    unittest.main()
