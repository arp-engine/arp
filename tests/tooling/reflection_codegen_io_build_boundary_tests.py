"""Use the generated dependency graph to verify the host-tool/core direction."""
import argparse
import subprocess
import unittest


parser = argparse.ArgumentParser()
parser.add_argument('--ninja', required=True)
parser.add_argument('--build-dir', required=True)
options = parser.parse_args()


class ReflectionBuildBoundaryTests(unittest.TestCase):
    def inputs(self, target):
        return subprocess.check_output([options.ninja, '-C', options.build_dir, '-t', 'inputs', target],
                                       text=True, timeout=30).replace('\\', '/')

    def test_core_bootstrap_does_not_depend_on_reflection_codegen(self):
        inputs = self.inputs('arp_core')
        self.assertNotIn('reflect_codegen', inputs)
        self.assertNotIn('reflection_codegen_io', inputs)
        self.assertNotIn('/src/reflection/', inputs)
        self.assertNotIn('/tools/', inputs)

    def test_real_generator_consumes_shared_file_service(self):
        inputs = self.inputs('arp_reflect_codegen')
        for source in ('/tools/reflect_codegen/reflection_tool_io.cpp',
                       '/tools/io/native_tool_outputs.cpp', '/tools/io/cold_tool_files.cpp',
                       '/src/core/io/file_service.cpp', '/src/network/http/byte_read_transport.cpp'):
            self.assertIn(source, inputs)
        for forbidden in ('/src/scene/', '/src/rhi/', '/src/renderer/'):
            self.assertNotIn(forbidden, inputs)


if __name__ == '__main__':
    unittest.main(argv=[__file__])
