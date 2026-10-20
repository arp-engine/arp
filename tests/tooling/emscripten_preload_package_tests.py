"""Frozen Debug/Release metadata custody and executable-token rejection."""
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from emscripten_preload_package import EmscriptenPreloadPackage
from javascript_data_literal import JavaScriptDataLiteral


class PreloadPackageTests(unittest.TestCase):
    def test_original_debug_and_minified_release_read_identical_ranges(self):
        manifests = [
            '\n loadPackage({"files":[{"filename":"/f","start":0,"end":4}],"remote_package_size":4});\n',
            '(()=>{loadPackage({files:[{filename:"/f",start:0,end:4}],remote_package_size:4})})();']
        with TemporaryDirectory() as directory:
            path = Path(directory) / 'runtime.js'
            path.with_suffix('.data').write_bytes(b'GPU!')
            for manifest in manifests:
                path.write_text(manifest)
                self.assertEqual(EmscriptenPreloadPackage(path).read('/f'), b'GPU!')

    def test_exponent_offsets_preserve_exact_ranges(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / 'runtime.js'
            path.with_suffix('.data').write_bytes(b'A' * 28000 + b'GPU!')
            path.write_text('loadPackage({files:[{filename:"/a",start:0,end:28e3},'
                '{filename:"/f",start:28e3,end:28004}],remote_package_size:28004})')
            self.assertEqual(EmscriptenPreloadPackage(path).read('/f'), b'GPU!')
        for value in (True, -1, 0.5, 2**53, float('inf'), float('nan'), '4'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                EmscriptenPreloadPackage._byte_offset(value)

    def test_code_ambiguity_duplicate_ranges_and_schema_fail_closed(self):
        valid = 'loadPackage({files:[{filename:"/f",start:0,end:4}],remote_package_size:4})'
        invalid = [valid + valid, valid.replace('start:0', 'start:1'),
            valid.replace('end:4', 'end:true'), valid.replace('end:4', 'end:4,end:4'),
            valid.replace('size:4', 'size:4,unknown:0'), valid.replace('end:4', 'end:danger()'),
            valid[:-1] + ',danger())', valid.replace('size:4', 'size:5'),
            valid.replace('"/f"', '"/../f"'), valid.replace('"/f"', '"/./f"')]
        with TemporaryDirectory() as directory:
            path = Path(directory) / 'runtime.js'
            path.with_suffix('.data').write_bytes(b'GPU!')
            for manifest in invalid:
                path.write_text(manifest)
                with self.subTest(manifest=manifest), self.assertRaises(ValueError):
                    EmscriptenPreloadPackage(path)

    def test_escaped_strings_and_explicit_nesting_budget(self):
        value, end = JavaScriptDataLiteral.parse('{key:"x\\\"})loadPackage({",value:[true,null,-2]}suffix', max_nesting=2)
        self.assertEqual(value, {'key': 'x"})loadPackage({', 'value': [True, None, -2]})
        self.assertGreater(end, 0)
        for text in ('{key:call()}', '{key:undefined}', '{key:{nested:[]}}', '{key:1,key:2}', '{key:1e999}'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                JavaScriptDataLiteral.parse(text, max_nesting=2)


if __name__ == '__main__':
    unittest.main()
