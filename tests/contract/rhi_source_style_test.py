"""Guard dependency organization and formatting-independent source evidence."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tooling"))
from cpp_source_evidence import CppSource, embedded_javascript_regions, tokens


class RhiSourceStyleTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def test_dependencies_precede_code_and_inline_definitions_follow_it(self):
        for path in (self.ROOT / "src/rhi").rglob("*"):
            if path.suffix not in {".h", ".cpp", ".mm", ".inl"}:
                continue
            source = path.read_text(encoding="utf-8-sig")
            # Mask comments/literals without removing line boundaries. Includes
            # are inspected in the original line; literals cannot fake code.
            masked = list(source)
            for token, start, end in tokens(source):
                if token.startswith(('//', '/*', '"', "'", 'R"')):
                    masked[start:end] = ['\n' if c == '\n' else ' ' for c in token]
            code_seen = False
            definitions_seen = False
            continuation = False
            for original, line in zip(source.splitlines(), ''.join(masked).splitlines()):
                if continuation:
                    continuation = original.rstrip().endswith('\\')
                    continue
                if line.lstrip().startswith('#'):
                    continuation = original.rstrip().endswith('\\')
                    include = re.match(r'\s*#\s*(?:include|import)\s+[<"]([^>"]+)', original)
                    if include:
                        if include[1].endswith('.inl'):
                            self.assertTrue(code_seen, str(path))
                            definitions_seen = True
                        else:
                            self.assertFalse(code_seen, f"Late import in {path}: {original}")
                    continue
                if line.strip():
                    self.assertFalse(definitions_seen, f"Declaration after inline definitions: {path}")
                    code_seen = True

    def test_migration_provenance_does_not_replace_file_structure(self):
        for path in (self.ROOT / "src/rhi").rglob("*"):
            if path.suffix in {".h", ".cpp", ".mm", ".inl"}:
                source = path.read_text(encoding="utf-8-sig")
                self.assertNotIn("// package-source", source, str(path))
                self.assertLessEqual(source.count('#pragma once'), 1, str(path))

    def test_evidence_ignores_layout_but_preserves_operators_and_literals(self):
        source = CppSource('if (owner != nullptr)\n    return "RHI-EXACT-ID";')
        self.assertIn('if(owner!=nullptr)return "RHI-EXACT-ID";', source)
        self.assertNotIn('owner == nullptr', source)
        self.assertNotIn('"RHI-EXACT- ID"', source)
        self.assertNotIn('ow ner != nullptr', source)
        self.assertEqual([t[0] for t in tokens("1'000u")], ["1'000u"])
        self.assertNotIn('value == = expected', CppSource('value === expected'))
        self.assertNotIn('value = > result', CppSource('value => result'))
        self.assertIn('call(value)', CppSource('call(/* formatting boundary */ value)'))

    def test_embedded_javascript_keeps_its_language_and_formatter_boundary(self):
        script = []
        for path in (self.ROOT / "src/rhi").rglob("*"):
            if path.suffix not in {".h", ".cpp", ".mm", ".inl"}:
                continue
            source = path.read_text(encoding="utf-8-sig")
            for name, _, _, opening, closing in embedded_javascript_regions(source):
                body = source[opening:closing]
                self.assertEqual(body.count('// clang-format off'), 1, str(path))
                self.assertEqual(body.count('// clang-format on'), 1, str(path))
                script.append(f'async function embedded{len(script)}() {{\n{body}\n}}\n')
        self.assertTrue(script)
        node = shutil.which('node')
        if node:
            result = subprocess.run([node, '--check'], input=''.join(script), text=True,
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_async_javascript_regions_preserve_and_check_native_macro_bodies(self):
        source = '''// EM_ASYNC_JS(fake, ignored, (), {})
EM_ASYNC_JS(int, observe, (int value), {
// clang-format off
const result = await Promise.resolve(value);
return [result].some(v => v === 7);
// clang-format on
});'''
        regions = embedded_javascript_regions(source)
        self.assertEqual(len(regions), 1)
        name, start, end, opening, closing = regions[0]
        self.assertEqual(name, 'EM_ASYNC_JS')
        self.assertTrue(source[start:end].startswith('EM_ASYNC_JS(int, observe'))
        body = source[opening:closing]
        self.assertIn('v => v === 7', body)
        node = shutil.which('node')
        if not node:
            self.skipTest('Node unavailable; embedded JS syntax probe not validated')
        for candidate, accepted in ((body, True),
                                    (body.replace('=>', '= >'), False),
                                    (body.replace('===', '== ='), False)):
            result = subprocess.run([node, '--check'],
                                    input=f'async function probe(value) {{\n{candidate}\n}}',
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode == 0, accepted, result.stderr)

    def test_async_javascript_bodies_are_checked_with_original_offsets(self):
        source = '''EM_ASYNC_JS(int, waitForGpu, (int handle), {
            // clang-format off
            const result = await Promise.all([handle].map(x => x));
            return result[0] === handle;
            // clang-format on
        });'''
        regions = embedded_javascript_regions(source)
        self.assertEqual(len(regions), 1)
        name, start, end, opening, closing = regions[0]
        self.assertEqual(name, 'EM_ASYNC_JS')
        self.assertEqual(source[start:end], source[:-1])
        self.assertIn('x => x', source[opening:closing])
        self.assertIn('=== handle', source[opening:closing])


    def test_embedded_javascript_is_guarded_repository_wide(self):
        source_roots = ("src", "editor", "examples", "modules", "tests", "tools")
        suffixes = {".h", ".hpp", ".hh", ".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".inl"}
        excluded = {"3rd", "third_party", "generated", "build"}
        script = []
        for source_root in source_roots:
            for path in (self.ROOT / source_root).rglob("*"):
                if path.suffix.lower() not in suffixes or excluded.intersection(path.parts):
                    continue
                source = path.read_text(encoding="utf-8-sig")
                for name, _, _, opening, closing in embedded_javascript_regions(source):
                    body = source[opening:closing]
                    self.assertEqual(body.count("// clang-format off"), 1, f"{path}: {name}")
                    self.assertEqual(body.count("// clang-format on"), 1, f"{path}: {name}")
                    script.append(f"async function embedded{len(script)}() {{\n{body}\n}}\n")

        self.assertTrue(script)
        node = shutil.which("node")
        if node:
            result = subprocess.run([node, "--check"], input="".join(script), text=True,
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_scoped_slices_choose_the_first_matching_code_tokens(self):
        source = CppSource('first(\n arg); unrelated(); first(arg);')
        self.assertEqual(source.index('first(arg)'), 0)
        self.assertEqual(source.count('first(arg)'), 2)
        selected = source.split('first(arg)', 1)[1].split('first(arg)', 1)[0]
        self.assertIn('unrelated()', selected)
        self.assertNotIn('first(arg)', selected)
        self.assertIn('"diagnostic suffix"', CppSource('return "diagnostic suffix";'))


if __name__ == "__main__":
    unittest.main()
