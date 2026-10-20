"""Reject source/recipe drift without launching a compiler or a GPU."""
import pathlib
import tempfile
import unittest

from rhi_null_fixture_repair_evidence import (
    CLOSURE_FIXTURE, CLOSURE_NEW, CLOSURE_OLD, FIXTURE, MAIN, NEW, OBJECT, OLD, PAYLOAD_FIXTURE, PAYLOAD_NEW, PAYLOAD_OLD,
    ROOT, TARGET, NullFixtureRepairEvidence as Evidence)
from bindless_finite_lifecycle_acceptance import fingerprint


class NullFixtureRepairTests(unittest.TestCase):
    def compile_command(self):
        return ('C:/mingw64/bin/g++.exe -DARP_STATIC_LINK=1 -I' + ROOT.as_posix() + '/src '
                '-O3 -DNDEBUG -std=c++17 -UNDEBUG -Wa,-mbig-obj -MD -MT ' + OBJECT +
                ' -MF ' + OBJECT + '.d -o ' + OBJECT + ' -c ' + (ROOT / MAIN).as_posix())

    def test_repair_preserves_surrounding_bytes_and_both_negative_cases(self):
        for newline in (b'\n', b'\r\n'):
            data = b'prefix' + newline + OLD + newline + b'suffix' + newline
            expected = b'prefix' + newline + NEW.replace(b'\n', newline) + newline + b'suffix' + newline
            self.assertEqual(Evidence.repaired_bytes(data), expected)

    def test_repair_rejects_missing_duplicate_and_preexisting_version9(self):
        for data in (b'', OLD + OLD, OLD + b'bad.contractVersion=9u;'):
            with self.assertRaisesRegex(ValueError, 'EXACT-NEGATIVE-GUARD'):
                Evidence.repaired_bytes(data)

    def test_payload_repair_adds_four_backend_execution_closure_and_version9(self):
        data = b'prefix\r\n' + PAYLOAD_OLD + b'\r\nsuffix\r\n'
        self.assertEqual(Evidence.repaired_bytes(data, PAYLOAD_FIXTURE),
                         b'prefix\r\n' + PAYLOAD_NEW.replace(b'\n', b'\r\n') + b'\r\nsuffix\r\n')
        with self.assertRaisesRegex(ValueError, 'FIXTURE-DOMAIN'):
            Evidence.repaired_bytes(data, 'src/rhi/semantics/command_lowering.h')

    def test_closure_repair_preserves_legacy_assertions_and_wrong_domain_rejections(self):
        for newline in (b'\n', b'\r\n'):
            data = b'prefix' + newline + CLOSURE_OLD.replace(b'\n', newline) + newline + b'suffix'
            self.assertEqual(Evidence.repaired_bytes(data, CLOSURE_FIXTURE),
                             b'prefix' + newline + CLOSURE_NEW.replace(b'\n', newline) + newline + b'suffix')

    def test_compile_relocates_only_source_and_object_metadata(self):
        output = ROOT / 'build/repair-test-only'
        args = Evidence.compile_recipe(self.compile_command(), ROOT, output / 'overlay', output)
        self.assertEqual(args[args.index('-c') + 1], str(output / 'overlay' / MAIN))
        self.assertEqual(args[args.index('-o') + 1], str(output / 'smoke.obj'))
        self.assertIn('-I' + ROOT.as_posix() + '/src', args)
        self.assertEqual([arg for arg in args if arg in ('-DNDEBUG', '-UNDEBUG')], ['-DNDEBUG', '-UNDEBUG'])
        self.assertIn('-O3', args)
        self.assertIn('-Wa,-mbig-obj', args)

    def test_compile_rejects_disabled_assertions_or_other_main(self):
        for command in (self.compile_command().replace('-UNDEBUG', ''),
                        self.compile_command().replace('-UNDEBUG', '-UNDEBUG -DNDEBUG'),
                        self.compile_command().replace(MAIN, 'tests/another.cpp')):
            with self.assertRaisesRegex(ValueError, 'SOURCE-ASSERTIONS|COMPILE-RECIPE'):
                Evidence.compile_recipe(command, ROOT, ROOT / 'build/overlay', ROOT / 'build/output')

    def test_compile_rejects_shell_control_tokens(self):
        with self.assertRaisesRegex(ValueError, 'COMPILER'):
            Evidence.compile_recipe(self.compile_command() + ' && echo fake', ROOT, ROOT / 'build/overlay', ROOT / 'build/output')

    def test_link_strips_only_fixed_wrapper_and_preserves_libraries(self):
        compiler = 'C:/mingw64/bin/g++.exe'
        command = ('C:/Windows/system32/cmd.exe /C "cd . && ' + compiler +
                   ' -O3 -DNDEBUG -static-libstdc++ -static-libgcc ' + OBJECT + ' -o ' + TARGET +
                   '.exe -Wl,--out-implib,lib' + TARGET + '.dll.a libarp_rhi.a libarp_core.a -luser32 && cd ."')
        output = ROOT / 'build/repair-test-only'
        args = Evidence.link_recipe(command, compiler, output)
        self.assertEqual(args[-3:], ['libarp_rhi.a', 'libarp_core.a', '-luser32'])
        self.assertEqual(args[args.index('-o') + 1], str(output / 'smoke.exe'))
        self.assertNotIn('cmd.exe', args)
        for altered in (command.replace(' && cd .', ' && echo injected && cd .'),
                        command.replace(OBJECT, 'unrelated.obj'), command.replace('cd . && ', 'cd elsewhere && ')):
            with self.assertRaises(ValueError):
                Evidence.link_recipe(altered, compiler, output)

    def test_unchanged_inputs_rejects_same_size_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'input.h'
            source.write_bytes(b'original')
            inputs = {source: fingerprint(source)}
            Evidence.verify_unchanged(inputs)
            source.write_bytes(b'changed!')
            with self.assertRaisesRegex(ValueError, 'INPUT-DRIFT'):
                Evidence.verify_unchanged(inputs)

    def test_output_cannot_reuse_build_or_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            for output in (build, build / 'child'):
                with self.assertRaisesRegex(ValueError, 'OUTPUT-OWNER'):
                    Evidence(build, build / FIXTURE, build / 'sealed', output, build / 'ninja')


if __name__ == '__main__':
    unittest.main()
