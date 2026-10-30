"""Enforce foundation ownership independently of C++ build configuration."""
from pathlib import Path
import importlib.util
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = {
    'algorithm', 'concurrency', 'container', 'diagnostics', 'encoding', 'hash', 'image',
    'io', 'math', 'memory', 'meta', 'ownership', 'runtime', 'system', 'text', 'time',
}
PUBLIC_ROOTS = {'api.h', 'cpu_allocator.h', 'math.h', 'log.h', 'curve.h'}
SOURCE_SUFFIXES = {'.h', '.hpp', '.hh', '.c', '.cc', '.cpp', '.cxx', '.m', '.mm', '.inl'}
INCLUDES = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)


def upward_dependencies(text):
    return [name for name in INCLUDES.findall(text)
            if (ROOT / 'src' / name.split('/')[0]).is_dir()
            and not name.startswith('core/')]


class CorePackageBoundaryTests(unittest.TestCase):
    def test_core_has_only_declared_packages_and_public_roots(self):
        for path in (ROOT / 'src/core').iterdir():
            self.assertIn(path.name, PACKAGES if path.is_dir() else PUBLIC_ROOTS, str(path))
        self.assertFalse((ROOT / 'src/image').exists())
        self.assertFalse((ROOT / 'src/scene/time').exists())
        for name in ('game_clock.h', 'timer_service.h', 'tween.h'):
            self.assertTrue((ROOT / 'src/core/time' / name).is_file(), name)
        for name in PUBLIC_ROOTS - {'api.h'}:
            text = (ROOT / 'src/core' / name).read_text(encoding='utf-8-sig')
            self.assertEqual(len(INCLUDES.findall(text)), 1, name)
            self.assertNotRegex(text, r'\b(class|struct|namespace)\b', name)

    def test_core_cannot_depend_on_engine_domains(self):
        for path in (ROOT / 'src/core').rglob('*'):
            if path.suffix in SOURCE_SUFFIXES:
                dependencies = upward_dependencies(path.read_text(encoding='utf-8-sig'))
                allowed = {'network/http/byte_read_transport.h', 'network/http/fetch_byte_transfer.h'}
                if path == ROOT / 'src/core/io/http_file_backend.cpp':
                    self.assertEqual(set(dependencies), allowed)
                else:
                    self.assertEqual(dependencies, [], str(path))

    def test_scanner_rejects_domain_dependencies(self):
        for domain in ('rhi', 'renderer', 'reflection', 'scriptvm', 'assets', 'scene'):
            self.assertEqual(upward_dependencies(f'#include "{domain}/test.h"'), [f'{domain}/test.h'])
        self.assertEqual(upward_dependencies('#include "core/math/math.h"\n#include <vector>'), [])

    def test_core_target_compiles_only_foundation_sources(self):
        text = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8-sig')
        sources = re.search(r'add_library\(arp_core\s+(.*?)\n\)', text, re.DOTALL).group(1).split()
        self.assertTrue(sources)
        for name in sources:
            self.assertTrue(name.startswith('src/core/'), name)
            self.assertTrue((ROOT / name).is_file(), name)
        for links in re.findall(r'target_link_libraries\(arp_core\s+([^)]*)\)', text):
            self.assertEqual(links.split(), ['PUBLIC', 'arp_core_foundation', 'arp_network_http_transport'])

    def test_fileservice_and_network_dependency_direction(self):
        self.assertFalse((ROOT / 'src/core/fileservice').exists())
        self.assertTrue((ROOT / 'src/core/io/file_service.h').is_file())
        self.assertTrue((ROOT / 'src/core/io/file_backend.h').is_file())
        for path in (ROOT / 'src/network').rglob('*'):
            if path.suffix in SOURCE_SUFFIXES:
                self.assertNotIn('core/io/file_service.h', path.read_text(encoding='utf-8-sig'), str(path))
                self.assertNotIn('core/io/file_backend.h', path.read_text(encoding='utf-8-sig'), str(path))
        spec = importlib.util.spec_from_file_location('arp_build_graph', ROOT / 'build.py')
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        modules = build._auto_discover_modules()
        self.assertEqual(modules['arp_core_foundation'].deps, ['arp_mimalloc'])
        self.assertEqual(modules['arp_network_http_transport'].deps, ['arp_core_foundation'])
        self.assertEqual(modules['arp_core'].deps, ['arp_core_foundation', 'arp_network_http_transport'])
        self.assertNotIn('arp_core', modules['arp_network'].deps)
        self.assertTrue(all((ROOT / name).is_file() for name in modules['arp_network_http_transport'].srcs))
        self.assertTrue(all(not name.startswith('src/core/io/') and '/runtime/async_runtime.cpp' not in name
                            for name in modules['arp_core_foundation'].srcs))

    def test_build_discovery_preserves_foundation_ownership(self):
        spec = importlib.util.spec_from_file_location('arp_build_discovery', ROOT / 'build.py')
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        modules = build._auto_discover_modules()
        self.assertFalse((ROOT / 'src/tooling').exists())
        self.assertTrue(modules['arp_core'].srcs)
        self.assertTrue(all(name.startswith('src/core/') for name in modules['arp_core'].srcs))
        self.assertTrue(all(name.startswith('src/scriptvm/plugin/')
                            for name in modules['arp_scriptvm_plugin_build'].srcs))
        self.assertEqual(modules['arp_scriptvm_plugin_build'].deps, ['arp_core'])
        self.assertEqual(set(modules['arp_ui_font'].srcs), {
            'src/ui/ui_font.cpp', 'src/ui/ui_font_face_cache.cpp',
            'src/ui/ui_font_source.cpp', 'src/ui/detail/ui_freetype_memory.cpp',
        })
        self.assertTrue(set(modules['arp_ui_font'].srcs).isdisjoint(modules['arp_ui_runtime'].srcs))
        self.assertIn('arp_scriptvm_plugin_build', modules['arp_scriptvm_runtime'].deps)
        self.assertTrue(all(name.startswith('src/pcg/') for name in modules['arp_pcg'].srcs))

    def test_domain_policy_definitions_cannot_return_to_core(self):
        forbidden = r'\b(?:Camera|FrameTelemetry|MobilePerformanceBudget|FrameRetiredPageSliceAllocator|ScreenDiameterLod|RadianceCube)\s*\{'
        for path in (ROOT / 'src/core').rglob('*.h'):
            self.assertNotRegex(path.read_text(encoding='utf-8-sig'), forbidden, str(path))

    def test_rhi_uses_numeric_conversion_without_lighting_dependency(self):
        for path in (ROOT / 'src/rhi').rglob('*'):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding='utf-8-sig')
            self.assertNotIn('renderer/gi/ibl_bake.h', text, str(path))
            self.assertNotIn('core::ibl::', text, str(path))

    def test_all_first_party_core_includes_resolve(self):
        for directory in ('src', 'modules', 'editor', 'tools', 'tests', 'games', 'examples'):
            for path in (ROOT / directory).rglob('*'):
                if path.suffix not in SOURCE_SUFFIXES:
                    continue
                for name in INCLUDES.findall(path.read_text(encoding='utf-8-sig')):
                    if name.startswith(('core/', 'tooling/', 'image/')):
                        self.assertTrue((ROOT / 'src' / name).is_file(), f'{path}: {name}')


if __name__ == '__main__':
    unittest.main()
