"""Exercise the actual CMake product selector, without claiming GPU execution."""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import argparse
from contextlib import redirect_stdout
from io import StringIO
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from cmake_gen import (BuildBackendSelection, BuildBackendSelectionError, CmakeWriter,
                       _generate_single_cmake_presets, generate_cmake_presets)
from cmake_presets_document import CMakePresetDocument


class RootBuildSelectionTests(unittest.TestCase):
    root = Path(__file__).resolve().parents[2]

    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('arp_web_build_selection', cls.root / 'build.py')
        cls.build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.build)

    def test_configure_auto_reaches_the_matching_preset(self):
        for platform, expected in (('win', 'vulkan'), ('linux', 'vulkan'),
                ('android', 'vulkan'), ('mac', 'metal'), ('ios', 'metal'), ('web', 'webgpu')):
            with self.subTest(platform=platform), TemporaryDirectory() as directory:
                args = SimpleNamespace(platform=platform, backend='auto', config='debug', regenerate_cmake=False)
                tools = ('gcc', 'g++', 'ninja', '', '')
                with patch.object(self.build, 'ROOT', Path(directory)), \
                        patch.object(self.build, '_find_mingw_compilers', return_value=tools), \
                        patch.object(self.build, '_find_mac_compilers', return_value=tools), \
                        patch.object(self.build, '_find_emscripten_compilers', return_value=tools), \
                        patch.object(self.build, '_auto_discover_modules', return_value={}), \
                        patch.object(self.build, 'CmakeWriter') as writer, \
                        patch.object(self.build, 'generate_cmake_presets') as presets, \
                        patch.object(self.build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                        redirect_stdout(StringIO()):
                    self.build.cmd_configure(args)
                self.assertEqual(writer.call_args.args[4], expected)
                self.assertEqual(presets.call_args.args[3], expected)
                stem = 'android-arm64-vulkan' if platform == 'android' else f'{platform}-{expected}'
                self.assertEqual(run.call_args.args[0][2], stem + '-debug')
                self.assertEqual(args.backend, 'auto')
                self.assertFalse(writer.return_value.generate.called)

    def test_invalid_selection_refuses_before_discovery_or_file_writes(self):
        for platform, backend, diagnostic in (
                ('web', 'webgl1', 'RHI-WEBGL1-BUILD-UNAVAILABLE'),
                ('win', 'webgl1', 'RHI-WEB-BUILD-PLATFORM'),
                ('web', 'vulkan', 'RHI-BUILD-BACKEND-PLATFORM'),
                ('win', 'webgpu', 'RHI-WEB-BUILD-PLATFORM'),
                ('mac', 'vulkan', 'RHI-BUILD-BACKEND-PLATFORM'),
                ('web', 'WebGL1', 'RHI-BUILD-BACKEND-UNKNOWN'),
                ('web', 'webgl2', 'RHI-BUILD-BACKEND-UNKNOWN'),
                ('web', '../webgpu', 'RHI-BUILD-BACKEND-UNKNOWN'),
                ('web', 'webgpu;webgl1', 'RHI-BUILD-BACKEND-UNKNOWN'),
                ('unknown', 'auto', 'RHI-BUILD-PLATFORM-UNKNOWN')):
            with self.subTest(platform=platform, backend=backend), TemporaryDirectory() as directory:
                args = SimpleNamespace(platform=platform, backend=backend, config='debug', regenerate_cmake=True)
                with patch.object(self.build, 'ROOT', Path(directory)), \
                        patch.object(self.build, '_find_mingw_compilers') as mingw, \
                        patch.object(self.build, '_find_mac_compilers') as mac, \
                        patch.object(self.build, '_find_emscripten_compilers') as emscripten, \
                        patch.object(self.build, '_auto_discover_modules') as discovery, \
                        patch.object(self.build, 'CmakeWriter') as writer, \
                        patch.object(self.build, 'generate_cmake_presets') as presets, \
                        patch.object(self.build.subprocess, 'run') as run, \
                        redirect_stdout(StringIO()):
                    # A dedicated selection error must precede all of these effects.
                    with self.assertRaisesRegex(ValueError, diagnostic):
                        self.build.cmd_configure(args)
                for operation in (mingw, mac, emscripten, discovery, writer, presets, run):
                    operation.assert_not_called()
                self.assertEqual(list(Path(directory).iterdir()), [])

    def test_codegen_apis_refuse_without_overwriting_existing_file(self):
        def generate_root(output, config, platform, backend, *tools):
            CmakeWriter({}, 'unused-build', config, platform, backend, *tools).generate(output)

        for generator in (generate_cmake_presets, _generate_single_cmake_presets, generate_root):
            for platform, backend in (('web', 'webgl1'), ('win', 'webgpu'),
                    ('mac', 'vulkan'), ('web', 'webgl2')):
                with self.subTest(generator=generator.__name__, platform=platform, backend=backend), \
                        TemporaryDirectory() as directory:
                    output = Path(directory) / 'CMakePresets.json'
                    original = b'preserve existing unparsed bytes\r\n'
                    output.write_bytes(original)
                    with self.assertRaises(BuildBackendSelectionError):
                        generator(str(output), 'debug', platform, backend, 'gcc', 'g++', 'ninja', '', '')
                    self.assertEqual(output.read_bytes(), original)

    def test_rejected_cli_runs_the_current_script_in_an_isolated_root(self):
        # Exact script bytes, real argument parsing and process exit; only the
        # copied script's ROOT differs so a regression cannot overwrite this repo.
        with TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / 'build.py'
            script.write_bytes((self.root / 'build.py').read_bytes())
            sentinels = {name: b'unchanged input\r\n' for name in ('CMakeLists.txt', 'CMakePresets.json')}
            for name, data in sentinels.items():
                (root / name).write_bytes(data)
            environment = dict(os.environ)
            environment['PYTHONPATH'] = str(self.root / 'scripts')
            for platform, backend, diagnostic in (
                    ('web', 'webgl1', 'RHI-WEBGL1-BUILD-UNAVAILABLE'),
                    ('win', 'webgl1', 'RHI-WEB-BUILD-PLATFORM'),
                    ('web', 'webgl2', 'RHI-BUILD-BACKEND-UNKNOWN'),
                    ('win', 'metal', 'RHI-BUILD-BACKEND-PLATFORM'),
                    ('unknown', 'auto', 'RHI-BUILD-PLATFORM-UNKNOWN')):
                with self.subTest(platform=platform, backend=backend):
                    result = subprocess.run([sys.executable, str(script), 'configure',
                        '--platform', platform, '--backend', backend, '--regenerate-cmake'],
                        cwd=root, env=environment, capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn(diagnostic, result.stderr)
                    self.assertNotIn('Traceback', result.stderr)
                    self.assertEqual(result.stdout, '')
                    self.assertFalse((root / 'build').exists())
                    for name, data in sentinels.items():
                        self.assertEqual((root / name).read_bytes(), data)

    def test_explicit_products_match_auto_without_granting_gpu_capabilities(self):
        expected = dict(win='vulkan', linux='vulkan', android='vulkan',
                        mac='metal', ios='metal', web='webgpu')
        for platform, backend in expected.items():
            self.assertEqual(BuildBackendSelection.resolve(platform, 'auto'), backend)
            self.assertEqual(BuildBackendSelection.resolve(platform, backend), backend)
            writer = CmakeWriter({}, 'unused-build', 'debug', platform, 'auto',
                                 'gcc', 'g++', 'ninja', '', '')
            self.assertEqual(writer.backend, backend)
        self.assertEqual(BuildBackendSelection.VERSION, 1)

    def test_selected_configure_presets_exist_for_all_products_and_configs(self):
        stems = {'win': 'win-vulkan', 'web': 'web-webgpu', 'mac': 'mac-metal',
                 'ios': 'ios-metal', 'linux': 'linux-vulkan', 'android': 'android-arm64-vulkan'}
        for platform, stem in stems.items():
            for config in ('debug', 'release'):
                with self.subTest(platform=platform, config=config), TemporaryDirectory() as directory:
                    output = Path(directory) / 'CMakePresets.json'
                    with redirect_stdout(StringIO()):
                        generate_cmake_presets(str(output), config, platform, 'auto',
                            'gcc', 'g++', 'ninja', '', '')
                    document = json.loads(output.read_text(encoding='utf-8'))
                    name = stem + '-' + config
                    configure = {p['name']: p for p in document['configurePresets']}
                    self.assertIn(name, configure)
                    self.assertEqual(configure[name]['binaryDir'], '${sourceDir}/build/' + name)
                    self.assertEqual(configure[name]['cacheVariables']['CMAKE_BUILD_TYPE'], config.title())
                    for category in ('buildPresets', 'testPresets'):
                        entry = next(p for p in document[category] if p['name'] == name)
                        self.assertEqual(entry['configurePreset'], name)
                        if platform == 'ios':
                            self.assertEqual(entry['configuration'], config.title())
                    for legacy in ('macos-metal', 'ios-metal', 'linux-vulkan', 'android-arm64-vulkan'):
                        self.assertIn(legacy, configure)
                    (Path(directory) / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\n')
                    parsed = subprocess.run(['cmake', '--list-presets=all'], cwd=directory,
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(parsed.returncode, 0, parsed.stdout + parsed.stderr)
                    self.assertIn('"' + name + '"', parsed.stdout)

    def test_invalid_config_is_rejected_by_codegen_without_touching_files(self):
        def writer(output, config, platform, backend, *tools):
            CmakeWriter({}, 'unused-build', config, platform, backend, *tools).generate(output)
        for api in (writer, generate_cmake_presets, _generate_single_cmake_presets):
            for config, diagnostic in (('package', 'RHI-BUILD-CONFIG-UNAVAILABLE'),
                    ('Release', 'RHI-BUILD-CONFIG-UNKNOWN'), ('unknown', 'RHI-BUILD-CONFIG-UNKNOWN')):
                with self.subTest(api=api.__name__, config=config), TemporaryDirectory() as directory:
                    output = Path(directory) / 'CMakePresets.json'
                    original = b'preserve exact bytes\r\n'
                    output.write_bytes(original)
                    with self.assertRaisesRegex(BuildBackendSelectionError, diagnostic):
                        api(str(output), config, 'win', 'auto', 'gcc', 'g++', 'ninja', '', '')
                    self.assertEqual(output.read_bytes(), original)

    def test_single_generator_preserves_the_selected_product_configuration(self):
        for platform, stem in (('web', 'web-webgpu'), ('ios', 'ios-metal'),
                               ('android', 'android-arm64-vulkan')):
            for config in ('debug', 'release'):
                with self.subTest(platform=platform, config=config), TemporaryDirectory() as directory:
                    output = Path(directory) / 'CMakePresets.json'
                    with redirect_stdout(StringIO()):
                        _generate_single_cmake_presets(str(output), config, platform, 'auto',
                            'gcc', 'g++', 'ninja', '', '')
                    document = json.loads(output.read_text(encoding='utf-8'))
                    entry = document['configurePresets'][0]
                    self.assertEqual(entry['name'], stem + '-' + config)
                    self.assertEqual(entry['cacheVariables']['CMAKE_BUILD_TYPE'], config.title())
                    self.assertEqual(entry['binaryDir'], '${sourceDir}/build/' + entry['name'])
                    if platform == 'ios':
                        self.assertEqual(document['buildPresets'][0]['configuration'], config.title())
                        self.assertEqual(document['testPresets'][0]['configuration'], config.title())

    def test_package_cli_refuses_in_each_command_without_file_changes(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / 'build.py'
            script.write_bytes((self.root / 'build.py').read_bytes())
            for name in ('CMakeLists.txt', 'CMakePresets.json'):
                (root / name).write_bytes(b'preserve bytes\r\n')
            environment = dict(os.environ, PYTHONPATH=str(self.root / 'scripts'))
            for command in ('configure', 'build', 'test'):
                for platform in ('win', 'ios', 'linux', 'android', 'web', 'mac'):
                    args = [sys.executable, str(script), command, '--platform', platform,
                            '--backend', 'auto', '--config', 'package']
                    if command == 'configure':
                        args.append('--regenerate-cmake')
                    with self.subTest(command=command, platform=platform):
                        result = subprocess.run(args, cwd=root, env=environment,
                            capture_output=True, text=True, timeout=30)
                        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                        self.assertIn('RHI-BUILD-CONFIG-UNAVAILABLE', result.stderr)
                        self.assertEqual(result.stdout, '')
                        self.assertFalse((root / 'build').exists())
                        for name in ('CMakeLists.txt', 'CMakePresets.json'):
                            self.assertEqual((root / name).read_bytes(), b'preserve bytes\r\n')

    def test_build_and_test_route_explicit_products_and_refuse_package(self):
        for handler in (self.build.cmd_build, self.build.cmd_test):
            for platform, stem in (('ios', 'ios-metal'), ('linux', 'linux-vulkan'),
                                   ('android', 'android-arm64-vulkan')):
                args = SimpleNamespace(platform=platform, backend='auto', config='release',
                    target=None, parallel=None, filter=None)
                with self.subTest(handler=handler.__name__, platform=platform), \
                        patch.object(self.build, '_configured_build_intent',
                            return_value=SimpleNamespace(configuration='Release')), \
                        patch.object(self.build.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run, \
                        redirect_stdout(StringIO()):
                    handler(args)
                command = run.call_args.args[0]
                self.assertEqual(command[command.index('--preset') + 1], stem + '-release')
                args.config = 'package'
                with patch.object(self.build.subprocess, 'run') as run, redirect_stdout(StringIO()):
                    with self.assertRaisesRegex(BuildBackendSelectionError, 'RHI-BUILD-CONFIG-UNAVAILABLE'):
                        handler(args)
                run.assert_not_called()


class CMakePresetDocumentTests(unittest.TestCase):
    """Document preservation is tooling evidence, never native admission."""

    generators = (generate_cmake_presets, _generate_single_cmake_presets)

    def test_selected_preset_copy_cannot_mutate_document(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / 'CMakePresets.json'
            self.generate(generate_cmake_presets, output)
            document = CMakePresetDocument.load(output)
            before = output.read_bytes()
            entry = document.preset('configurePresets', 'win-vulkan-debug')
            expected = json.loads(json.dumps(entry))
            entry['cacheVariables']['CMAKE_BUILD_TYPE'] = 'Release'
            self.assertEqual(document.preset('configurePresets', 'win-vulkan-debug'), expected)
            self.assertIsNone(document.preset('configurePresets', 'missing'))
            with self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-SHAPE'):
                document.preset('unknown', 'win-vulkan-debug')
            self.assertEqual(output.read_bytes(), before)

    @classmethod
    def setUpClass(cls):
        RootBuildSelectionTests.setUpClass()

    def generate(self, generator, output):
        with redirect_stdout(StringIO()):
            generator(str(output), 'debug', 'win', 'auto', 'gcc', 'g++', 'ninja', '', '')

    def invalid_documents(self):
        return (
            (b'{', 'JSON'), (b'\xff', 'ENCODING'),
            (b'[]', 'SHAPE'), (b'null', 'SHAPE'),
            (b'{"version":6,"version":6}', 'DUPLICATE-KEY'),
            (b'{"version":6,"vendor":{"x":{"k":1,"k":2}}}', 'DUPLICATE-KEY'),
            (b'{"version":6,"vendor":{"x":NaN}}', 'JSON'),
            (b'{"version":6,"vendor":{"x":Infinity}}', 'JSON'),
            (b'{"version":6,"vendor":{"x":1e999}}', 'JSON'),
            *tuple((json.dumps({'version': version}).encode(), 'VERSION')
                   for version in (True, 6.0, '6', 0, 5, 7, None)),
            (b'{}', 'VERSION'),
            *tuple((json.dumps({'version': 6, category: value}).encode(), 'SHAPE')
                   for category in ('configurePresets', 'buildPresets', 'testPresets',
                                    'packagePresets', 'workflowPresets')
                   for value in ({}, None, [None], [{}], [{'name': ''}], [{'name': 1}],
                                 [{'name': 'same'}, {'name': 'same'}])),
            *tuple((json.dumps({'version': 6, key: value}).encode(), 'SHAPE')
                   for key, value in (('include', 'other.json'), ('include', [None]),
                       ('include', ['']), ('vendor', []), ('cmakeMinimumRequired', []),
                       ('cmakeMinimumRequired', {'major': True}),
                       ('cmakeMinimumRequired', {'major': -1}),
                       ('cmakeMinimumRequired', {'unknown': 1}))),
        )

    def test_invalid_existing_documents_never_get_rebuilt(self):
        for generator in self.generators:
            for original, diagnostic in self.invalid_documents():
                with self.subTest(generator=generator.__name__, document=original), \
                        TemporaryDirectory() as directory:
                    output = Path(directory) / 'CMakePresets.json'
                    output.write_bytes(original)
                    with self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-' + diagnostic):
                        self.generate(generator, output)
                    self.assertEqual(output.read_bytes(), original)
                    self.assertEqual(sorted(p.name for p in Path(directory).iterdir()), ['CMakePresets.json'])

    def test_root_preflight_precedes_discovery_and_regeneration(self):
        build = RootBuildSelectionTests.build
        for original, diagnostic in self.invalid_documents():
            with self.subTest(document=original), TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'CMakePresets.json').write_bytes(original)
                (root / 'CMakeLists.txt').write_bytes(b'preserve root cmake\r\n')
                args = SimpleNamespace(platform='win', backend='auto', config='debug', regenerate_cmake=True)
                with patch.object(build, 'ROOT', root), \
                        patch.object(build, '_find_mingw_compilers') as compilers, \
                        patch.object(build, '_auto_discover_modules') as discovery, \
                        patch.object(build, 'CmakeWriter') as writer, \
                        patch.object(build.subprocess, 'run') as run, redirect_stdout(StringIO()) as stdout:
                    with self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-' + diagnostic):
                        build.cmd_configure(args)
                for operation in (compilers, discovery, writer, run):
                    operation.assert_not_called()
                self.assertEqual(stdout.getvalue(), '')
                self.assertEqual((root / 'CMakeLists.txt').read_bytes(), b'preserve root cmake\r\n')
                self.assertEqual((root / 'CMakePresets.json').read_bytes(), original)
                self.assertFalse((root / 'build').exists())

    def test_preserves_root_fields_unmanaged_entries_and_real_cmake_parse(self):
        cmake = shutil.which('cmake')
        self.assertIsNotNone(cmake)
        for generator in self.generators:
            with self.subTest(generator=generator.__name__), TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / 'CMakePresets.json'
                included = {'version': 6, 'configurePresets': [{'name': 'included', 'hidden': True}]}
                (root / 'included.json').write_text(json.dumps(included), encoding='utf-8')
                original = {'version': 6, 'include': ['included.json'],
                    'vendor': {'arp.example/presets/1': {'description': '保留字段', 'flags': [1, True, None]}},
                    'cmakeMinimumRequired': {'major': 3, 'minor': 25, 'patch': 0},
                    'configurePresets': [{'name': 'custom', 'inherits': 'included', 'generator': 'Ninja',
                        'binaryDir': '${sourceDir}/custom-build', 'vendor': {'custom.example/1': {'x': 1}}}],
                    'buildPresets': [{'name': 'custom', 'configurePreset': 'custom'}],
                    'testPresets': [{'name': 'custom', 'configurePreset': 'custom'}],
                    'packagePresets': [{'name': 'custom-package', 'configurePreset': 'custom', 'generators': ['ZIP']}],
                    'workflowPresets': [{'name': 'custom-workflow', 'steps': [
                        {'type': 'configure', 'name': 'custom'}, {'type': 'build', 'name': 'custom'},
                        {'type': 'test', 'name': 'custom'}, {'type': 'package', 'name': 'custom-package'}]}]}
                output.write_text(json.dumps(original, ensure_ascii=False), encoding='utf-8')
                self.generate(generator, output)
                document = json.loads(output.read_text(encoding='utf-8'))
                for key in ('version', 'include', 'vendor', 'cmakeMinimumRequired', 'packagePresets', 'workflowPresets'):
                    self.assertEqual(document[key], original[key])
                for category in ('configurePresets', 'buildPresets', 'testPresets'):
                    self.assertEqual(next(p for p in document[category] if p['name'] == 'custom'), original[category][0])
                parsed = subprocess.run([cmake, '--list-presets=all'], cwd=root,
                    capture_output=True, text=True, encoding='utf-8', timeout=30)
                self.assertEqual(parsed.returncode, 0, parsed.stdout + parsed.stderr)
                for name in ('custom', 'custom-package', 'custom-workflow', 'win-vulkan-debug'):
                    self.assertIn(name, parsed.stdout)

    def test_single_refresh_keeps_existing_matrix_and_unknown_root_metadata(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / 'CMakePresets.json'
            self.generate(generate_cmake_presets, output)
            before = json.loads(output.read_text(encoding='utf-8'))
            # Envelope custody preserves fields it does not interpret; this is
            # deliberately not a CMake-valid root extension or native proof.
            before['futureMetadata'] = {'opaque': ['unchanged']}
            output.write_text(json.dumps(before), encoding='utf-8')
            self.generate(_generate_single_cmake_presets, output)
            after = json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual(after['futureMetadata'], before['futureMetadata'])
            for category in ('configurePresets', 'buildPresets', 'testPresets'):
                self.assertEqual({p['name'] for p in after[category]}, {p['name'] for p in before[category]})
                for preset in before[category]:
                    if preset['name'] != 'win-vulkan-debug':
                        self.assertIn(preset, after[category])

    def test_unreadable_document_is_not_missing(self):
        for generator in self.generators:
            with self.subTest(generator=generator.__name__), TemporaryDirectory() as directory:
                output = Path(directory) / 'CMakePresets.json'
                output.mkdir()
                with self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-IO'):
                    self.generate(generator, output)
                self.assertTrue(output.is_dir())

    def test_atomic_publication_failures_preserve_original_and_clean_temporary(self):
        import cmake_presets_document as documents
        for generator in self.generators:
            for failure in ('write', 'replace'):
                with self.subTest(generator=generator.__name__, failure=failure), TemporaryDirectory() as directory:
                    output = Path(directory) / 'CMakePresets.json'
                    original = b'{"version":6,"vendor":{"arp.example/1":{"keep":true}}}\r\n'
                    output.write_bytes(original)
                    named_file = documents.tempfile.NamedTemporaryFile

                    def failing_file(*args, **kwargs):
                        stream = named_file(*args, **kwargs)
                        real_write = stream.write

                        def partial_write_then_fail(data):
                            real_write(data[:7])
                            raise OSError('injected partial temporary write')

                        stream.write = partial_write_then_fail
                        return stream

                    operation = patch.object(documents.tempfile, 'NamedTemporaryFile', side_effect=failing_file) \
                        if failure == 'write' else patch.object(documents.os, 'replace', side_effect=OSError('injected replace'))
                    with operation, self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-WRITE'):
                        self.generate(generator, output)
                    self.assertEqual(output.read_bytes(), original)
                    self.assertEqual(sorted(p.name for p in Path(directory).iterdir()), ['CMakePresets.json'])

    def test_changed_snapshot_is_never_overwritten(self):
        for initial, replacement in ((None, b'{"version":6}'),
                (b'{"version":6}', b'{"version":6,"vendor":{}}'), (b'{"version":6}', None)):
            with self.subTest(initial=initial, replacement=replacement), TemporaryDirectory() as directory:
                output = Path(directory) / 'CMakePresets.json'
                if initial is not None:
                    output.write_bytes(initial)
                document = CMakePresetDocument.load(output)
                if replacement is None:
                    output.unlink()
                else:
                    output.write_bytes(replacement)
                with self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-CHANGED'):
                    document.write_managed({'version': 6, 'configurePresets': [{'name': 'new'}]})
                self.assertEqual(output.read_bytes() if output.exists() else None, replacement)
                self.assertFalse(list(Path(directory).glob('*.tmp')))

    def test_io_failures_never_become_new_documents(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / 'CMakePresets.json'
            with patch.object(Path, 'read_bytes', side_effect=PermissionError('injected read refusal')):
                with self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-IO'):
                    CMakePresetDocument.load(output)
            missing_parent = Path(directory) / 'absent' / 'CMakePresets.json'
            with self.assertRaisesRegex(ValueError, 'RHI-BUILD-PRESETS-WRITE'):
                self.generate(generate_cmake_presets, missing_parent)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_repeated_refresh_is_byte_stable(self):
        for generator in self.generators:
            with self.subTest(generator=generator.__name__), TemporaryDirectory() as directory:
                output = Path(directory) / 'CMakePresets.json'
                self.generate(generator, output)
                first = output.read_bytes()
                self.generate(generator, output)
                self.assertEqual(output.read_bytes(), first)

    def test_cli_document_rejection_is_diagnostic_and_preserves_bytes(self):
        root_source = Path(__file__).resolve().parents[2]
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'build.py').write_bytes((root_source / 'build.py').read_bytes())
            (root / 'CMakeLists.txt').write_bytes(b'keep root\r\n')
            (root / 'CMakePresets.json').write_bytes(b'{"version":6,"include":false}')
            environment = dict(os.environ, PYTHONPATH=str(root_source / 'scripts'))
            result = subprocess.run([sys.executable, str(root / 'build.py'), 'configure',
                '--regenerate-cmake'], cwd=root, env=environment, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn('RHI-BUILD-PRESETS-SHAPE', result.stderr)
            self.assertNotIn('Traceback', result.stderr)
            self.assertEqual(result.stdout, '')
            self.assertEqual((root / 'CMakeLists.txt').read_bytes(), b'keep root\r\n')
            self.assertEqual((root / 'CMakePresets.json').read_bytes(), b'{"version":6,"include":false}')
            self.assertFalse((root / 'build').exists())


class WebRhiBuildSelectionTests(unittest.TestCase):
    cmake = shutil.which("cmake")
    root = Path(__file__).resolve().parents[2]

    def writer(self):
        return CmakeWriter({}, "unused-build", "debug", "win", "vulkan",
                           "unused-gcc", "unused-gxx", "unused-ninja", "", "")

    def test_regenerated_cmake_keeps_selection_before_dependency_discovery(self):
        # Exercise the full generator, without writing the shared root CMake.
        # Script-mode stubs stop at dependency discovery; no compiler/native
        # execution is claimed by this build-product ordering test.
        with TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / "cmake").mkdir()
            shutil.copyfile(self.root / "cmake/ArpWebRhiSelection.cmake",
                            directory / "cmake/ArpWebRhiSelection.cmake")
            generated = directory / "generated.cmake"
            self.writer().generate(str(generated))
            driver = directory / "driver.cmake"
            driver.write_text(
                'function(project)\nendfunction()\n'
                'function(add_compile_options)\nendfunction()\n'
                'function(add_link_options)\nendfunction()\n'
                'function(find_package)\nmessage(FATAL_ERROR "TEST-DEPENDENCY-DISCOVERY")\nendfunction()\n'
                'set(CMAKE_SYSTEM_NAME "${TEST_SYSTEM}")\n'
                'include("${CMAKE_CURRENT_LIST_DIR}/generated.cmake")\n', encoding="utf-8")
            for system, requested, expected in (
                ("Emscripten", "webgl1", "RHI-WEBGL1-BUILD-UNAVAILABLE"),
                ("Emscripten", "none", "RHI-WEB-BUILD-SELECTION-REQUIRED"),
                ("Windows", "webgl1", "RHI-WEB-BUILD-PLATFORM"),
                ("Emscripten", "webgl2", "RHI-WEB-BUILD-SELECTION-UNKNOWN"),
                ("Windows", "none", "TEST-DEPENDENCY-DISCOVERY"),
                ("Emscripten", "webgpu", "TEST-DEPENDENCY-DISCOVERY"),
            ):
                with self.subTest(system=system, requested=requested):
                    result = subprocess.run(
                        [self.cmake, f"-DTEST_SYSTEM={system}", f"-DARP_WEB_RHI={requested}",
                         "-P", str(driver)], cwd=directory, capture_output=True,
                        text=True, encoding="utf-8", timeout=30)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, result.stderr)
                    if expected != "TEST-DEPENDENCY-DISCOVERY":
                        self.assertNotIn("TEST-DEPENDENCY-DISCOVERY", result.stderr)

    def test_generated_webgpu_edges_consume_selected_product(self):
        writer = self.writer()
        attributes = dict(srcs=["src/platform/render_system.cpp"], include_dirs=[],
                          deps=[], compile_defs={}, link_flags=[], platform_link_flags={},
                          platform_deps={}, platform_srcs={}, apple_frameworks=[])
        writer._emit_library(SimpleNamespace(name="arp_rhi_webgpu", **attributes))
        attributes["platform_deps"] = {"web": ["arp_rhi_webgpu"]}
        attributes["platform_srcs"] = {"web": ["src/platform/render_system_webgpu.cpp"]}
        writer._emit_library(SimpleNamespace(name="arp_platform", **attributes))
        writer._emit_rhi_linkage()
        # Record emitted CMake edges using CMake's own conditional evaluator.
        # These are codegen traces, not real libraries or native payloads.
        functions = ("add_library", "target_include_directories", "target_link_libraries",
                     "target_compile_definitions", "target_compile_options", "target_link_options",
                     "target_sources")
        prefix = "".join(
            f'function({name})\nmessage(STATUS "EDGE={name}|${{ARGV}}")\nendfunction()\n'
            for name in functions)
        prefix += 'set(CMAKE_SYSTEM_NAME "${TEST_SYSTEM}")\n'
        prefix += 'set(APPLE "${TEST_APPLE}")\n'
        with TemporaryDirectory() as directory:
            driver = Path(directory) / "edges.cmake"
            driver.write_text(prefix + "\n".join(writer.lines), encoding="utf-8")
            for system, selected, apple, expected in (
                ("Emscripten", "webgpu", "FALSE", "arp_rhi_webgpu"),
                ("Emscripten", "none", "FALSE", None),
                ("Windows", "none", "FALSE", "arp_rhi_vulkan"),
                ("Darwin", "none", "TRUE", "arp_rhi_metal"),
            ):
                with self.subTest(system=system, selected=selected):
                    result = subprocess.run(
                        [self.cmake, f"-DTEST_SYSTEM={system}", f"-DTEST_APPLE={apple}",
                         f"-DARP_SELECTED_WEB_RHI={selected}", "-P", str(driver)],
                        capture_output=True, text=True, encoding="utf-8", timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    output = result.stdout
                    webgpu_selected = selected == "webgpu"
                    self.assertEqual("--use-port=emdawnwebgpu" in output, webgpu_selected)
                    self.assertEqual("-lembind" in output, webgpu_selected)
                    self.assertEqual("render_system_webgpu.cpp" in output, webgpu_selected)
                    web_edge = "EDGE=target_link_libraries|arp_platform;PUBLIC;arp_rhi_webgpu"
                    self.assertEqual(web_edge in output, webgpu_selected)
                    if expected:
                        self.assertIn("EDGE=target_link_libraries|arp_platform;PUBLIC;" + expected, output)
                    else:
                        self.assertNotIn("EDGE=target_link_libraries|arp_platform;PUBLIC;arp_rhi_", output)

    def select(self, system, requested):
        self.assertIsNotNone(self.cmake, "CMake is required; missing tools are not passing evidence")
        with TemporaryDirectory() as directory:
            driver = Path(directory) / "selection.cmake"
            driver.write_text(
                f'include("{(self.root / "cmake/ArpWebRhiSelection.cmake").as_posix()}")\n'
                'arp_select_web_rhi("${TEST_SYSTEM}" "${TEST_REQUEST}" selected)\n'
                'message(STATUS "SELECTED=${selected}")\n', encoding="utf-8")
            return subprocess.run(
                [self.cmake, f"-DTEST_SYSTEM={system}", f"-DTEST_REQUEST={requested}", "-P", str(driver)],
                capture_output=True, text=True, encoding="utf-8", timeout=30)

    def test_legacy_platform_routes_are_preserved(self):
        for system in ("Windows", "Darwin", "Linux", "Android", "iOS", "Emscripten"):
            for requested in ("", "webgpu" if system == "Emscripten" else "none"):
                with self.subTest(system=system, requested=requested):
                    result = self.select(system, requested)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    expected = "webgpu" if system == "Emscripten" else "none"
                    self.assertIn("SELECTED=" + expected, result.stdout)

    def test_webgl1_production_selection_refuses_before_returning_route(self):
        result = self.select("Emscripten", "webgl1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RHI-WEBGL1-BUILD-UNAVAILABLE", result.stderr)
        self.assertNotIn("SELECTED=", result.stdout)

    def test_invalid_or_inapplicable_selection_never_falls_back(self):
        for system in ("Emscripten", "Windows", "Darwin", "Linux"):
            for requested in ("WebGL1", "webgl2", "vulkan", "webgpu;webgl1"):
                with self.subTest(system=system, requested=requested):
                    result = self.select(system, requested)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("RHI-WEB-BUILD-SELECTION-UNKNOWN", result.stderr)
                    self.assertNotIn("SELECTED=", result.stdout)
        for system in ("Windows", "Darwin", "Linux"):
            for requested in ("webgpu", "webgl1"):
                result = self.select(system, requested)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("RHI-WEB-BUILD-PLATFORM", result.stderr)
                self.assertNotIn("SELECTED=", result.stdout)
        result = self.select("Emscripten", "none")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RHI-WEB-BUILD-SELECTION-REQUIRED", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=False, default=WebRhiBuildSelectionTests.cmake)
    args, rest = parser.parse_known_args()
    WebRhiBuildSelectionTests.cmake = args.cmake
    unittest.main(argv=[sys.argv[0]] + rest)
