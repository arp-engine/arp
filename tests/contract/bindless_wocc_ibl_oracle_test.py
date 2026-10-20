"""Hostile checks for CPU anchors and independently sampled GPU IBL parity."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from bindless_graphics_readback_oracles import OrdinaryWoccIblOracle


class WoccIblOracleTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='arp-ibl-oracle-')
        self.addCleanup(self.directory.cleanup)
        directory = Path(self.directory.name)
        cube = b''.join(struct.pack('<4e', .5, .25, 0, 1) * ((64 >> mip)**2 * 6)
                        for mip in range(7))
        for name in ('horizon.rgba16f', 'zenith.rgba16f'):
            (directory / name).write_bytes(cube)
        (directory / 'brdf.rgba16f').write_bytes(struct.pack('<4e', .75, .125, 0, 1) * 128**2)
        self.oracle = OrdinaryWoccIblOracle(directory)
        self.reference = struct.pack('<424f', *self.oracle.expected)

    def test_exact_inputs_and_unfiltered_cpu_anchors(self):
        self.assertEqual(sum(len(mip) for row in self.oracle.texture_chains
                             for layer in row[4] for mip in layer), 655328)
        checks = self.oracle.verify_direct_reference(self.reference)
        self.assertEqual(checks['cpuExactLanes'], 80)
        self.assertFalse(checks['cpuFilteredAnalyticalParityClaimed'])
        values, words = self.oracle.verify_direct_parity(self.reference, self.reference)
        self.assertEqual(len(values), 424)
        self.assertEqual(len(words), 424)

    def test_every_cpu_anchor_and_nonfinite_reference_refuses(self):
        for index in range(424):
            lane = index % 106
            bad_values = [float('nan'), float('inf')]
            if (lane < 96 and lane % 8 == 6) or 96 <= lane < 104:
                bad_values.append(self.oracle.expected[index] + .001)
            for bad in bad_values:
                values = list(self.oracle.expected)
                values[index] = bad
                with self.assertRaises(RuntimeError):
                    self.oracle.verify_direct_reference(struct.pack('<424f', *values))

    def test_filtered_cpu_delta_does_not_relax_gpu_parity(self):
        values = list(self.oracle.expected)
        values[0] += 1 / 4096
        independent = struct.pack('<424f', *values)
        checks = self.oracle.verify_direct_reference(independent)
        self.assertEqual(checks['maxCpuAnalyticalDelta'], 1 / 4096)
        self.oracle.verify_direct_parity(independent, independent)
        for index in range(424):
            damaged = bytearray(independent)
            damaged[index*4] ^= 1  # Even one output bit must refuse.
            with self.assertRaises(RuntimeError):
                self.oracle.verify_direct_parity(bytes(damaged), independent)

    def test_truncated_or_extended_readback_refuses(self):
        for data in (self.reference[:-4], self.reference + b'\0'*4):
            with self.assertRaises(RuntimeError):
                self.oracle.verify_direct_reference(data)
            with self.assertRaises(RuntimeError):
                self.oracle.verify_direct_parity(data, self.reference)


if __name__ == '__main__':
    unittest.main()
