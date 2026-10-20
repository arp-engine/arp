"""Address attribution must not silently confuse ASLR or another module."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('native_profiler', Path(__file__).resolve().parents[2] / 'scripts/profile_native_windows.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SymbolAttributionTests(unittest.TestCase):
    def test_module_loader_retry_boundary(self):
        sampler = module.WindowsProcessSampler
        self.assertTrue(sampler.retry_module_snapshot(299, image_known=False))
        self.assertFalse(sampler.retry_module_snapshot(299, image_known=True))
        for image_known in (False, True):
            self.assertTrue(sampler.retry_module_snapshot(24, image_known))
            self.assertFalse(sampler.retry_module_snapshot(5, image_known))
            self.assertFalse(sampler.retry_module_snapshot(87, image_known))

    def test_aslr_and_interior_address(self):
        symbols = module.PeSymbolTable(0x140000000, 0x10000,
                                      [(0x140002000, 'second'), (0x140001000, 'first')])
        self.assertEqual(symbols.resolve(0x7FF000001100, 0x7FF000000000), 'first')
        self.assertEqual(symbols.resolve(0x7FF000002000, 0x7FF000000000), 'second')

    def test_foreign_module_and_boundaries(self):
        symbols = module.PeSymbolTable(0x140000000, 0x10000, [(0x140001000, 'first')])
        self.assertIsNone(symbols.resolve(0x7FEFFFFFFFFF, 0x7FF000000000))
        self.assertIsNone(symbols.resolve(0x7FF000010000, 0x7FF000000000))
        self.assertEqual(symbols.resolve(0x7FF000000000, 0x7FF000000000), '<before first text symbol>')


if __name__ == '__main__':
    unittest.main()
