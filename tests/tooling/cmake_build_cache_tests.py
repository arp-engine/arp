"""Build-cache metadata admission; no compiler/graphics authority is claimed."""

from contextlib import redirect_stdout
from io import StringIO
import importlib.util
import json
import copy
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from cmake_gen import generate_cmake_presets
from cmake_build_cache import CMakeBuildCacheError, CMakeBuildIntent, CMakeCacheDocument


class CacheProjectFixture:
    def project(self, root):
        tools = root / 'tools'
        tools.mkdir()
        for name in ('gcc.exe', 'g++.exe', 'ninja.exe'):
            (tools / name).write_text('metadata fixture only', encoding='utf-8')
            (tools / name).chmod(0o755)
        self.compilers = tuple(str(tools / name) for name in ('gcc.exe', 'g++.exe', 'ninja.exe')) + ('', '')
        with redirect_stdout(StringIO()):
            generate_cmake_presets(str(root / 'CMakePresets.json'), 'debug', 'win', 'auto', *self.compilers)
        (root / 'CMakeLists.txt').write_bytes(b'preserve root cmake\r\n')
        directory = root / 'build/win-vulkan-debug'
        directory.mkdir(parents=True)
        self.entries = dict(CMAKE_HOME_DIRECTORY=('INTERNAL', str(root)),
            CMAKE_CACHEFILE_DIR=('INTERNAL', str(directory)), CMAKE_GENERATOR=('INTERNAL', 'Ninja'),
            CMAKE_BUILD_TYPE=('STRING', 'Debug'), CMAKE_C_COMPILER=('FILEPATH', self.compilers[0]),
            CMAKE_CXX_COMPILER=('FILEPATH', self.compilers[1]), ARP_WEB_RHI=('STRING', 'none'))
        self.cache = directory / 'CMakeCache.txt'
        self.write_cache()
        return self.cache

    def write_cache(self):
        self.cache.write_text(''.join(f'{key}:{kind}={value}\n'
            for key, (kind, value) in self.entries.items()), encoding='utf-8')

    def args(self):
        return SimpleNamespace(platform='win', backend='auto', config='debug', regenerate_cmake=True,
            target=None, parallel=None, filter=None)

    def preset(self, root):
        document = json.loads((root / 'CMakePresets.json').read_text(encoding='utf-8'))
        return next(p for p in document['configurePresets'] if p['name'] == 'win-vulkan-debug')


class RootCacheAdmissionTests(CacheProjectFixture, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('arp_cache_root_build', ROOT / 'build.py')
        cls.build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.build)

    def test_configure_rejects_mismatch_before_engineering_side_effects(self):
        for key, value, diagnostic in (
                ('CMAKE_HOME_DIRECTORY', 'other-source', 'SOURCE'),
                ('CMAKE_CACHEFILE_DIR', 'moved-build', 'DIRECTORY'),
                ('CMAKE_GENERATOR', 'Unix Makefiles', 'GENERATOR'),
                ('CMAKE_BUILD_TYPE', 'Release', 'CONFIG'),
                ('CMAKE_CXX_COMPILER', 'other-compiler', 'COMPILER'),
                ('CMAKE_TOOLCHAIN_FILE', 'other-toolchain.cmake', 'TOOLCHAIN'),
                ('ARP_WEB_RHI', 'webgpu', 'BACKEND')):
            with self.subTest(key=key), TemporaryDirectory() as directory:
                root = Path(directory)
                self.project(root)
                if key in ('CMAKE_HOME_DIRECTORY', 'CMAKE_CACHEFILE_DIR', 'CMAKE_CXX_COMPILER', 'CMAKE_TOOLCHAIN_FILE'):
                    value = str(root / value)
                self.entries[key] = (self.entries.get(key, ('FILEPATH', ''))[0], value)
                self.write_cache()
                before = {p: p.read_bytes() for p in (self.cache, root / 'CMakeLists.txt', root / 'CMakePresets.json')}
                with patch.object(self.build, 'ROOT', root), \
                        patch.object(self.build, '_find_mingw_compilers', return_value=self.compilers) as compilers, \
                        patch.object(self.build, '_auto_discover_modules') as discover, \
                        patch.object(self.build, 'CmakeWriter') as writer, \
                        patch.object(self.build, 'generate_cmake_presets') as presets, \
                        patch.object(self.build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                        redirect_stdout(StringIO()):
                    with self.assertRaisesRegex(ValueError, 'RHI-BUILD-CACHE-' + diagnostic):
                        self.build.cmd_configure(self.args())
                if key in ('CMAKE_HOME_DIRECTORY', 'CMAKE_CACHEFILE_DIR'):
                    compilers.assert_not_called()
                for operation in (discover, writer, presets, run):
                    operation.assert_not_called()
                for path, contents in before.items():
                    self.assertEqual(path.read_bytes(), contents)

    def test_build_and_test_refuse_wrong_cache_before_subprocess(self):
        for handler in (self.build.cmd_build, self.build.cmd_test):
            with self.subTest(handler=handler.__name__), TemporaryDirectory() as directory:
                root = Path(directory)
                self.project(root)
                self.entries['CMAKE_BUILD_TYPE'] = ('STRING', 'Release')
                self.write_cache()
                before = self.cache.read_bytes()
                with patch.object(self.build, 'ROOT', root), \
                        patch.object(self.build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                        redirect_stdout(StringIO()) as stdout:
                    with self.assertRaisesRegex(ValueError, 'RHI-BUILD-CACHE-CONFIG'):
                        handler(self.args())
                run.assert_not_called()
                self.assertEqual(stdout.getvalue(), '')
                self.assertEqual(self.cache.read_bytes(), before)

    def test_build_and_test_require_configured_cache(self):
        for handler in (self.build.cmd_build, self.build.cmd_test):
            with self.subTest(handler=handler.__name__), TemporaryDirectory() as directory:
                root = Path(directory)
                self.project(root)
                self.cache.unlink()
                with patch.object(self.build, 'ROOT', root), \
                        patch.object(self.build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                        redirect_stdout(StringIO()):
                    with self.assertRaisesRegex(ValueError, 'RHI-BUILD-CACHE-MISSING'):
                        handler(self.args())
                run.assert_not_called()
                self.assertFalse(self.cache.exists())

    def test_matching_cache_reaches_build_and_test_with_exact_configuration(self):
        for handler, executable in ((self.build.cmd_build, 'cmake'), (self.build.cmd_test, 'ctest')):
            with self.subTest(handler=handler.__name__), TemporaryDirectory() as directory:
                root = Path(directory)
                self.project(root)
                with patch.object(self.build, 'ROOT', root), \
                        patch.object(self.build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                        redirect_stdout(StringIO()):
                    handler(self.args())
                command = run.call_args.args[0]
                self.assertEqual(command[0], executable)
                self.assertEqual(command[command.index('--preset') + 1], 'win-vulkan-debug')
                if executable == 'ctest':
                    self.assertIn('-C', command)
                    self.assertEqual(command[command.index('-C') + 1], 'Debug')

    def test_operation_cannot_redirect_the_selected_configure_preset(self):
        for handler, category in ((self.build.cmd_build, 'buildPresets'), (self.build.cmd_test, 'testPresets')):
            with self.subTest(category=category), TemporaryDirectory() as directory:
                root = Path(directory)
                self.project(root)
                path = root / 'CMakePresets.json'
                document = json.loads(path.read_text(encoding='utf-8'))
                next(p for p in document[category] if p['name'] == 'win-vulkan-debug')['configurePreset'] = 'win-vulkan-release'
                path.write_text(json.dumps(document), encoding='utf-8')
                with patch.object(self.build, 'ROOT', root), patch.object(self.build.subprocess, 'run') as run:
                    with self.assertRaisesRegex(CMakeBuildCacheError, 'RHI-BUILD-CACHE-INTENT'):
                        handler(self.args())
                run.assert_not_called()

    def test_real_cli_rejects_wrong_source_without_writes_or_traceback(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            (root / 'build.py').write_bytes((ROOT / 'build.py').read_bytes())
            self.entries['CMAKE_HOME_DIRECTORY'] = ('INTERNAL', str(root / 'other-source'))
            self.write_cache()
            paths = (self.cache, root / 'CMakeLists.txt', root / 'CMakePresets.json')
            before = {path: path.read_bytes() for path in paths}
            environment = dict(os.environ, PYTHONPATH=str(ROOT / 'scripts'))
            for operation in ('configure', 'build', 'test'):
                command = [sys.executable, str(root / 'build.py'), operation]
                if operation == 'configure':
                    command.append('--regenerate-cmake')
                result = subprocess.run(command, cwd=root, env=environment,
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn('RHI-BUILD-CACHE-SOURCE', result.stderr)
                self.assertNotIn('Traceback', result.stderr)
                self.assertEqual(result.stdout, '')
                for path in paths:
                    self.assertEqual(path.read_bytes(), before[path])


class CacheDocumentTests(CacheProjectFixture, unittest.TestCase):
    def test_cache_parser_preserves_values_and_rejects_ambiguous_records(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / 'CMakeCache.txt'
            path.write_text('# comment\n // comment\n\n"colon:key":STRING= a=b;NaN \n'
                'EMPTY:INTERNAL=\n', encoding='utf-8')
            cache = CMakeCacheDocument.load(path)
            self.assertEqual(cache.value('colon:key'), ' a=b;NaN ')
            self.assertEqual(cache.value('EMPTY'), '')
            for raw, diagnostic in ((b'\xff', 'ENCODING'), (b'KEY:STRING=x\0', 'SHAPE'),
                    (b'not a record', 'SHAPE'), (b'KEY=x', 'SHAPE'), (b'KEY:UNKNOWN=x', 'SHAPE'),
                    (b'KEY:STRING=x\nKEY:INTERNAL=y\n', 'DUPLICATE'),
                    (b'KEY:STRING=x\n"KEY":STRING=y\n', 'DUPLICATE')):
                with self.subTest(raw=raw):
                    path.write_bytes(raw)
                    with self.assertRaisesRegex(CMakeBuildCacheError, 'RHI-BUILD-CACHE-' + diagnostic):
                        CMakeCacheDocument.load(path)
                    self.assertEqual(path.read_bytes(), raw)

    def test_missing_cache_is_distinct_from_io_and_missing_fields(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / 'CMakeCache.txt'
            self.assertIsNone(CMakeCacheDocument.load(path, allow_missing=True))
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-MISSING'):
                CMakeCacheDocument.load(path)
            path.mkdir()
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-IO'):
                CMakeCacheDocument.load(path, allow_missing=True)
            with patch.object(Path, 'read_bytes', side_effect=PermissionError('read refusal')):
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-IO'):
                    CMakeCacheDocument.load(path, allow_missing=True)
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.cache.write_text('# incomplete\n', encoding='utf-8')
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-MISSING'):
                CMakeBuildIntent(root, self.preset(root), 'vulkan', 'Debug').validate_existing(allow_missing=True)

    def test_locations_types_and_configuration_must_match(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            preset = self.preset(root)
            intent = CMakeBuildIntent(root, preset, 'vulkan', 'Debug')
            self.assertIsNotNone(intent.validate_existing())
            original = dict(self.entries)
            for key, value, diagnostic in (
                    ('CMAKE_HOME_DIRECTORY', str(root / 'elsewhere'), 'SOURCE'),
                    ('CMAKE_CACHEFILE_DIR', str(root), 'DIRECTORY'),
                    ('CMAKE_GENERATOR', 'Xcode', 'GENERATOR'),
                    ('CMAKE_BUILD_TYPE', 'Release', 'CONFIG'),
                    ('CMAKE_C_COMPILER', str(root / 'other-c'), 'COMPILER'),
                    ('CMAKE_CXX_COMPILER', str(root / 'other-cxx'), 'COMPILER'),
                    ('ARP_WEB_RHI', 'webgpu', 'BACKEND')):
                self.entries = dict(original)
                self.entries[key] = (self.entries[key][0], value)
                self.write_cache()
                with self.subTest(key=key), self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-' + diagnostic):
                    intent.validate_existing()
            self.entries = dict(original)
            self.entries['CMAKE_CACHEFILE_DIR'] = ('INTERNAL', str(root / 'build/../build/win-vulkan-debug'))
            self.entries['ARP_WEB_RHI'] = ('STRING', '')
            self.write_cache()
            intent.validate_existing()
            self.entries.pop('ARP_WEB_RHI')
            self.write_cache()
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-MISSING'):
                intent.validate_existing()

    def test_multiconfig_uses_actual_configuration_types(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            preset = self.preset(root)
            preset['generator'] = 'Xcode'
            self.entries['CMAKE_GENERATOR'] = ('INTERNAL', 'Xcode')
            self.entries['CMAKE_CONFIGURATION_TYPES'] = ('STRING', 'Debug;Release')
            self.write_cache()
            CMakeBuildIntent(root, preset, 'metal', 'Release').validate_existing()
            self.entries['CMAKE_CONFIGURATION_TYPES'] = ('STRING', 'Debug')
            self.write_cache()
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-CONFIG'):
                CMakeBuildIntent(root, preset, 'metal', 'Release').validate_existing()

    def test_toolchain_and_web_product_are_explicit(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            preset = self.preset(root)
            self.entries['CMAKE_TOOLCHAIN_FILE'] = ('FILEPATH', str(root / 'emscripten.cmake'))
            self.write_cache()
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-TOOLCHAIN'):
                CMakeBuildIntent(root, preset, 'vulkan', 'Debug').validate_existing()
            preset['toolchainFile'] = '${sourceDir}/emscripten.cmake'
            self.entries['ARP_WEB_RHI'] = ('STRING', 'webgpu')
            self.write_cache()
            CMakeBuildIntent(root, preset, 'webgpu', 'Debug').validate_existing()
            for toolchain in ('${sourceDir}/different.cmake', 'relative.cmake'):
                preset['toolchainFile'] = toolchain
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-TOOLCHAIN'):
                    CMakeBuildIntent(root, preset, 'webgpu', 'Debug').validate_existing()

    def test_typed_variables_env_macros_and_bare_compiler_lookup(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            preset = self.preset(root)
            variables = preset['cacheVariables']
            variables['CMAKE_CXX_COMPILER'] = {'type': 'FILEPATH', 'value': 'g++.exe'}
            variables['CMAKE_C_COMPILER'] = '${sourceDir}/tools/gcc.exe'
            preset['environment'] = {'PATH': '${sourceDir}/tools'}
            CMakeBuildIntent(root, preset, 'vulkan', 'Debug').validate_existing()
            preset['environment'] = {'PATH': '$penv{ARP_CACHE_TEST_PATH}'}
            with patch.dict(os.environ, ARP_CACHE_TEST_PATH=str(root / 'tools')):
                CMakeBuildIntent(root, preset, 'vulkan', 'Debug').validate_existing()
            variables['CMAKE_CXX_COMPILER']['value'] = 'absent-compiler.exe'
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-UNRESOLVED'):
                CMakeBuildIntent(root, preset, 'vulkan', 'Debug').validate_existing()

    def test_incomplete_inherited_and_unresolved_intents_refuse(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            before = self.cache.read_bytes()
            for update, diagnostic in (({'inherits': 'base'}, 'INTENT'),
                    ({'binaryDir': '${unresolved}/build'}, 'UNRESOLVED'),
                    ({'binaryDir': '$env{ARP_CACHE_ABSENT_VALUE}/build'}, 'UNRESOLVED'),
                    ({'binaryDir': ''}, 'INTENT'), ({'cacheVariables': []}, 'INTENT')):
                preset = self.preset(root)
                preset.update(update)
                with patch.dict(os.environ, {}, clear=True), self.subTest(update=update), \
                        self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-' + diagnostic):
                    CMakeBuildIntent(root, preset, 'vulkan', 'Debug').validate_existing()
                self.assertEqual(self.cache.read_bytes(), before)
            preset = self.preset(root)
            preset['binaryDir'] = '$env{CYCLIC}'
            preset['environment'] = {'CYCLIC': '$env{CYCLIC}'}
            with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-UNRESOLVED'):
                CMakeBuildIntent(root, preset, 'vulkan', 'Debug')

    def test_intent_owns_selected_preset_snapshot(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            preset = self.preset(root)
            intent = CMakeBuildIntent(root, preset, 'vulkan', 'Debug')
            preset['generator'] = 'Xcode'
            preset['cacheVariables']['CMAKE_CXX_COMPILER'] = str(root / 'other')
            intent.validate_existing()

    def test_invalid_typed_identity_has_stable_diagnostic(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            for kind in ([], {}, None, 1):
                with self.subTest(kind=kind):
                    preset = self.preset(root)
                    preset['cacheVariables']['CMAKE_CXX_COMPILER'] = {'type': kind, 'value': 'g++.exe'}
                    with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-INTENT'):
                        CMakeBuildIntent(root, preset, 'vulkan', 'Debug').validate_existing()

    def test_real_cmake_single_and_multi_config_cache(self):
        cmake, ninja = shutil.which('cmake'), shutil.which('ninja')
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ninja)
        for generator in ('Ninja', 'Ninja Multi-Config'):
            with self.subTest(generator=generator), TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\n'
                    'project(CacheOwnership NONE)\nset(ARP_WEB_RHI none CACHE STRING "product")\n', encoding='utf-8')
                binary = root / 'actual-cache'
                result = subprocess.run([cmake, '-S', str(root), '-B', str(binary), '-G', generator,
                    '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_MAKE_PROGRAM=' + ninja],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                preset = dict(name='actual', generator=generator, binaryDir=str(binary), cacheVariables={})
                configuration = 'Release' if generator == 'Ninja Multi-Config' else 'Debug'
                CMakeBuildIntent(root, preset, 'vulkan', configuration).validate_existing()
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-SOURCE'):
                    CMakeBuildIntent(root / 'wrong-source', preset, 'vulkan', configuration).validate_existing()
                if generator == 'Ninja':
                    with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-CONFIG'):
                        CMakeBuildIntent(root, preset, 'vulkan', 'Release').validate_existing()


if __name__ == '__main__':
    unittest.main()
