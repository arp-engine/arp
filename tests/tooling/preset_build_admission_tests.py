"""Production CLI metadata admission; synthetic caches grant no GPU authority."""
from contextlib import redirect_stdout
from io import StringIO
import importlib.util
import json
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
from cmake_build_cache import CMakeBuildCacheError
from cmake_build_cache_tests import CacheProjectFixture

spec = importlib.util.spec_from_file_location('arp_preset_build', ROOT / 'scripts/build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class PresetBuildAdmissionTests(CacheProjectFixture, unittest.TestCase):
    def update_document(self, root, change):
        path = root / 'CMakePresets.json'
        document = json.loads(path.read_text(encoding='utf-8'))
        change(document)
        path.write_text(json.dumps(document), encoding='utf-8')

    def test_unknown_preset_does_not_guess_debug(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            with patch.object(build, 'ROOT', str(root)):
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-INTENT'):
                    build._config_for_preset('unknown')

    def test_default_configuration_consumes_metadata(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            # A name is not the source of configuration truth.
            self.update_document(root, lambda d: d['configurePresets'][0]['cacheVariables'].update(
                CMAKE_BUILD_TYPE='Release'))
            with patch.object(build, 'ROOT', str(root)):
                self.assertEqual(build._config_for_preset('win-vulkan-debug'), 'Release')

    def test_every_subprocess_entry_refuses_wrong_cache(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.entries['CMAKE_HOME_DIRECTORY'] = ('INTERNAL', str(root / 'other'))
            self.write_cache()
            before = {p: p.read_bytes() for p in (self.cache, root / 'CMakePresets.json', root / 'CMakeLists.txt')}
            calls = (
                lambda: build._cmake(['--preset', 'win-vulkan-debug'], cwd=str(root)),
                lambda: build._cmake(['--build', '--preset', 'win-vulkan-debug', '--config', 'Debug'], cwd=str(root)),
                lambda: build._ctest('win-vulkan-debug', cwd=str(root)),
                lambda: build._ctest_regex_for_target('win-vulkan-debug', 'sample', cwd=str(root)),
                lambda: build._cmake_target_exists('win-vulkan-debug', 'sample', 'Debug', cwd=str(root)),
            )
            for index, call in enumerate(calls):
                with self.subTest(index=index), patch.object(build.subprocess, 'run') as run, \
                        redirect_stdout(StringIO()) as stdout:
                    with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-SOURCE'):
                        call()
                run.assert_not_called()
                self.assertEqual(stdout.getvalue(), '')
                for path, content in before.items():
                    self.assertEqual(path.read_bytes(), content)

    def test_only_configure_accepts_missing_cache(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.cache.unlink()
            with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                    redirect_stdout(StringIO()):
                build._cmake(['--preset', 'win-vulkan-debug'], cwd=str(root))
                self.assertEqual(run.call_count, 1)
                run.reset_mock()
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-MISSING'):
                    build._cmake(['--build', '--preset', 'win-vulkan-debug', '--config', 'Debug'], cwd=str(root))
                run.assert_not_called()

    def test_new_configure_cannot_claim_another_configuration(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.cache.unlink()
            with patch.object(build.subprocess, 'run') as run:
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-CONFIG'):
                    build._cmake(['--preset', 'win-vulkan-debug'], cwd=str(root), configuration='Release')
            run.assert_not_called()

    def test_missing_cache_still_rejects_unresolved_identity(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.cache.unlink()
            self.update_document(root, lambda d: d['configurePresets'][0]['cacheVariables'].update(
                CMAKE_CXX_COMPILER='${unsupported}/compiler'))
            with patch.object(build.subprocess, 'run') as run:
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-UNRESOLVED'):
                    build._cmake(['--preset', 'win-vulkan-debug'], cwd=str(root))
            run.assert_not_called()

    def test_new_single_configure_requires_declared_build_type(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.update_document(root, lambda d: d['configurePresets'][0]['cacheVariables'].pop('CMAKE_BUILD_TYPE'))
            # A real existing cache and explicit caller configuration suffice
            # for build. New configure cannot rely on CMake's empty default.
            with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                    redirect_stdout(StringIO()):
                build._cmake(['--build', '--preset', 'win-vulkan-debug', '--config', 'Debug'], cwd=str(root))
                run.assert_called_once()
                run.reset_mock()
                self.cache.unlink()
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-CONFIG'):
                    build._cmake(['--preset', 'win-vulkan-debug'], cwd=str(root), configuration='Debug')
                run.assert_not_called()

    def test_query_and_execution_pass_the_same_configuration(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            response = SimpleNamespace(returncode=0, stdout=json.dumps(dict(tests=[dict(
                name='sample', command=['sample.exe'])])))
            with patch.object(build.subprocess, 'run', return_value=response) as run, redirect_stdout(StringIO()):
                build._ctest_regex_for_target('win-vulkan-debug', 'sample', cwd=str(root), config='Debug')
                build._ctest('win-vulkan-debug', cwd=str(root), config='Debug')
            for call in run.call_args_list:
                command = call.args[0]
                self.assertEqual(command[command.index('-C') + 1], 'Debug')

    def test_explicit_configuration_cannot_bypass_single_config_cache(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            with patch.object(build.subprocess, 'run') as run:
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-CONFIG'):
                    build._cmake(['--build', '--preset', 'win-vulkan-debug', '--config', 'Release'], cwd=str(root))
            run.assert_not_called()

    def test_multiconfig_requires_explicit_or_operation_configuration(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.update_document(root, lambda d: d['configurePresets'][0].update(generator='Ninja Multi-Config'))
            self.entries.update(CMAKE_GENERATOR=('INTERNAL', 'Ninja Multi-Config'),
                CMAKE_CONFIGURATION_TYPES=('STRING', 'Debug;Release'))
            self.write_cache()
            with patch.object(build, 'ROOT', str(root)):
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-CONFIG'):
                    build._config_for_preset('win-vulkan-debug')
            self.update_document(root, lambda d: d['testPresets'][0].update(configuration='Release'))
            with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                    redirect_stdout(StringIO()):
                build._ctest('win-vulkan-debug', cwd=str(root))
            self.assertIn('-C', run.call_args.args[0])
            self.assertEqual(run.call_args.args[0][run.call_args.args[0].index('-C') + 1], 'Release')

    def test_test_and_build_cannot_use_different_configure_presets(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.update_document(root, lambda d: d['testPresets'][0].update(configurePreset='win-vulkan-release'))
            with patch.object(build.subprocess, 'run') as run:
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-INTENT'):
                    build._ctest_regex_for_target('win-vulkan-debug', 'sample', cwd=str(root))
            run.assert_not_called()

    def test_registered_operation_cannot_redirect_its_product(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.update_document(root, lambda d: d['buildPresets'][0].update(configurePreset='web-webgpu-debug'))
            with patch.object(build.subprocess, 'run') as run:
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-INTENT'):
                    build._cmake(['--build', '--preset', 'win-vulkan-debug', '--config', 'Debug'], cwd=str(root))
            run.assert_not_called()

    def test_custom_operation_can_reference_registered_configure(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.update_document(root, lambda d: d['buildPresets'].append(dict(
                name='explicit-alias', configurePreset='win-vulkan-debug')))
            with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                    redirect_stdout(StringIO()):
                build._cmake(['--build', '--preset', 'explicit-alias', '--config', 'Debug'], cwd=str(root))
            run.assert_called_once()

    def test_registered_and_custom_product_declarations(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            for value, diagnostic in (('webgpu', 'CACHE-BACKEND'), ('webgl1', 'WEBGL1-BUILD-UNAVAILABLE'),
                    ('invalid', 'CACHE-BACKEND')):
                self.update_document(root, lambda d: d['configurePresets'][0]['cacheVariables'].update(ARP_WEB_RHI=value))
                with self.subTest(value=value), patch.object(build.subprocess, 'run') as run:
                    with self.assertRaisesRegex(ValueError, diagnostic):
                        build._cmake(['--preset', 'win-vulkan-debug'], cwd=str(root))
                run.assert_not_called()
            def customize(d):
                for category in ('configurePresets', 'buildPresets', 'testPresets'):
                    d[category][0]['name'] = 'custom'
                    if category != 'configurePresets':
                        d[category][0]['configurePreset'] = 'custom'
                d['configurePresets'][0]['cacheVariables'].pop('ARP_WEB_RHI')
            self.update_document(root, customize)
            with patch.object(build.subprocess, 'run') as run:
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-BACKEND'):
                    build._cmake(['--preset', 'custom'], cwd=str(root))
            run.assert_not_called()
            self.update_document(root, lambda d: d['configurePresets'][0]['cacheVariables'].update(ARP_WEB_RHI='none'))
            with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                    redirect_stdout(StringIO()):
                build._cmake(['--preset', 'custom'], cwd=str(root))
            run.assert_called_once()

    def test_editor_and_smoke_targets_consume_declared_product(self):
        for name, product, expect_cli in (('web-custom', 'none', True), ('native-custom', 'webgpu', False)):
            with self.subTest(name=name), TemporaryDirectory() as directory:
                root = Path(directory)
                self.project(root)
                def customize(d):
                    for category in ('configurePresets', 'buildPresets', 'testPresets'):
                        d[category][0]['name'] = name
                        if category != 'configurePresets':
                            d[category][0]['configurePreset'] = name
                    d['configurePresets'][0]['cacheVariables']['ARP_WEB_RHI'] = product
                self.update_document(root, customize)
                with patch.object(build, 'ROOT', str(root)), patch.object(build, '_cmake') as cmake, \
                        patch.object(build, '_ctest'), redirect_stdout(StringIO()):
                    args = SimpleNamespace(preset=name, config='Debug', target=None)
                    targets = build._editor_targets(name)
                    self.assertEqual(build.EDITOR_CLI_TARGET in targets, expect_cli)
                    build.cmd_test(args)
                    submitted = cmake.call_args_list[1].args[0]
                    self.assertEqual(build.SMOKE_TESTS[0] in submitted, expect_cli)

    def test_explicit_product_cannot_reuse_an_ambiguous_default_cache(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            self.update_document(root, lambda d: d['configurePresets'][0]['cacheVariables'].update(ARP_WEB_RHI='none'))
            self.entries['ARP_WEB_RHI'] = ('STRING', '')
            self.write_cache()
            with patch.object(build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run:
                with self.assertRaisesRegex(CMakeBuildCacheError, 'CACHE-BACKEND'):
                    build._cmake(['--build', '--preset', 'win-vulkan-debug', '--config', 'Debug'], cwd=str(root))
            run.assert_not_called()

    def test_cli_rejections_preserve_files_without_traceback(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.project(root)
            # Execute current CLI bytes; imports use the real scripts directory.
            cli = root / 'scripts/build.py'
            cli.parent.mkdir()
            cli.write_bytes((ROOT / 'scripts/build.py').read_bytes())
            self.entries['CMAKE_HOME_DIRECTORY'] = ('INTERNAL', str(root / 'wrong'))
            self.write_cache()
            before = {p: p.read_bytes() for p in (self.cache, root / 'CMakeLists.txt', root / 'CMakePresets.json')}
            env = dict(os.environ, PYTHONPATH=str(ROOT / 'scripts'))
            for args, diagnostic in ((['engine', '--no-editor'], 'CACHE-SOURCE'),
                    (['editor'], 'CACHE-SOURCE'), (['shaders'], 'CACHE-SOURCE'),
                    (['game', 'pika'], 'CACHE-SOURCE'), (['all'], 'CACHE-SOURCE'),
                    (['test', '--target', 'sample'], 'CACHE-SOURCE'),
                    (['configure'], 'CACHE-SOURCE'),
                    (['--preset', 'unknown', 'engine'], 'CACHE-INTENT')):
                with self.subTest(args=args):
                    result = subprocess.run([sys.executable, '-X', 'utf8', str(cli), *args],
                        cwd=root, env=env, capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn(diagnostic, result.stderr)
                    self.assertNotIn('Traceback', result.stderr)
                    self.assertEqual(result.stdout, '')
                    for path, content in before.items():
                        self.assertEqual(path.read_bytes(), content)

    def test_real_cmake_build_and_ctest_single_and_multi_config(self):
        cmake, ninja = shutil.which('cmake'), shutil.which('ninja')
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ninja)
        for generator, config in (('Ninja', 'Debug'), ('Ninja Multi-Config', 'Release')):
            with self.subTest(generator=generator), TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\n'
                    'project(Admission NONE)\nset(ARP_WEB_RHI none CACHE STRING "product")\n'
                    'enable_testing()\nadd_custom_target(cache_probe COMMAND ${CMAKE_COMMAND} -E echo built)\n'
                    f'add_test(NAME cache_probe COMMAND "${{CMAKE_COMMAND}}" -DACTUAL=$<CONFIG> -DEXPECTED={config} '
                    '-P "${CMAKE_CURRENT_SOURCE_DIR}/check.cmake")\n', encoding='utf-8')
                (root / 'check.cmake').write_text('if(NOT ACTUAL STREQUAL EXPECTED)\n'
                    'message(FATAL_ERROR "configuration mismatch")\nendif()\n', encoding='utf-8')
                document = dict(version=6,
                    configurePresets=[dict(name='custom', generator=generator, binaryDir='${sourceDir}/build',
                        cacheVariables=dict(ARP_WEB_RHI='none', CMAKE_BUILD_TYPE=config, CMAKE_MAKE_PROGRAM=ninja))],
                    buildPresets=[dict(name='custom', configurePreset='custom', configuration=config)],
                    testPresets=[dict(name='custom', configurePreset='custom', configuration=config)])
                (root / 'CMakePresets.json').write_text(json.dumps(document), encoding='utf-8')
                with patch.object(build, 'ROOT', str(root)):
                    args = SimpleNamespace(preset='custom', config=config, target='cache_probe')
                    build.cmd_configure(args)
                    build.cmd_test(args)


if __name__ == '__main__':
    unittest.main()
