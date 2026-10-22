"""Reflection metadata survives formatting and carries typed reference targets."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CODEGEN = sys.argv.pop(1)


class CodegenTests(unittest.TestCase):
    def test_multiline_property_reference(self):
        with tempfile.TemporaryDirectory(prefix='arp-reflection-metadata-') as folder:
            root = Path(folder)
            header = root / 'schema.h'
            source = '''namespace fixture {
ASTRUCT(Construct = Default)
struct State {
    APROP(Serialize,
        Editor,
        ResourceReference,
        OptionalReference,
        ReferenceTarget = "SpriteResource",
        Doc = "display information only")
    core::pmr::String sprite;
};
}
'''
            header.write_text(source, encoding='utf-8')
            command = [CODEGEN, '--module', 'serialization_fixture', '--out-json', str(root/'schema.json'),
                       '--out-cpp', str(root/'schema.cpp'), '--header', str(header)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            metadata = json.loads((root/'schema.json').read_text())['classes'][0]['properties'][0]
            self.assertEqual(metadata['referenceTarget'], 'SpriteResource')
            self.assertIn('Serialize', metadata['flags'])
            generated = (root/'schema.cpp').read_text()
            self.assertIn('ReferenceKind::Resource, "SpriteResource", false', generated)
            header.write_text(source.replace('        ReferenceTarget = "SpriteResource",\n', ''), encoding='utf-8')
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('ReferenceTarget', result.stderr)


if __name__ == '__main__': unittest.main()
