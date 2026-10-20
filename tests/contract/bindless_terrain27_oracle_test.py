"""Independent capture oracle and hostile readback checks for Terrain27."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from bindless_graphics_readback_oracles import Terrain27Oracle, verify_numeric_readback


class Terrain27OracleTests(unittest.TestCase):
    def test_layout_layers_and_weight_semantics(self):
        oracle = Terrain27Oracle()
        self.assertEqual(len(oracle.expected), 128)
        self.assertEqual(sorted(oracle.ordinary_buffers), [16, 96])
        self.assertEqual(len(oracle.texture_layers), 27)
        self.assertEqual(sum(len(layers) == 2 for layers in oracle.texture_layers), 2)
        for pixel in range(4):
            lanes = oracle.expected[pixel*32:(pixel+1)*32]
            self.assertAlmostEqual(sum(lanes[8:14]), 1)
            self.assertEqual(lanes[28:], [1+pixel % 2, pixel//2, 1, 1])
        self.assertEqual(oracle.expected[:4], [c/255 for c in (30, 40, 50, 60)])
        self.assertNotEqual(oracle.expected[16], oracle.expected[48])

    def test_all_lanes_reject_corruption_and_nonfinite(self):
        oracle = Terrain27Oracle()
        verify_numeric_readback(oracle, struct.pack('<128f', *oracle.expected))
        for lane in range(128):
            for bad in (oracle.expected[lane]+.01, float('nan'), float('inf')):
                values = list(oracle.expected)
                values[lane] = bad
                with self.assertRaises(RuntimeError):
                    verify_numeric_readback(oracle, struct.pack('<128f', *values))


if __name__ == '__main__':
    unittest.main()
