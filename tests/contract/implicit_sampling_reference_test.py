"""Real sealed compiler roles and hostile evidence for derivative parity."""
import argparse
import copy
import hashlib
import pathlib
import shutil
import sys
import tempfile
import unittest

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from implicit_sampling_reference import load_reference_contract, validate_reference_result


class ImplicitReferenceTests(unittest.TestCase):
    def test_real_cross_target_roles_and_both_fragment_children_preserve_derivatives(self):
        for array in (False, True):
            with self.subTest(array=array):
                self.assertEqual(load_reference_contract(RUNNER, array)["contractVersion"], 1)

    def test_resealed_fixed_lod_reference_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory)
            shutil.copytree(RUNNER / "bindless-implicit-reference", destination / "bindless-implicit-reference")
            folder = destination / "bindless-implicit-reference/ordinary_implicit_array"
            publication_path = folder / "ordinary_implicit_array.graphics.module.yaml"
            publication = yaml.safe_load(publication_path.read_text())
            wgsl = folder / publication["files"]["wgsl"]["path"]
            # Reuse the actual legacy lowering: its bytes remain correctly sealed
            # but must not qualify as a quality-preserving implicit reference.
            from compile_shaders import lower_wgsl_implicit_texture_sampling
            lower_wgsl_implicit_texture_sampling(wgsl)
            publication["files"]["wgsl"]["sha256"] = hashlib.sha256(wgsl.read_bytes()).hexdigest()
            publication_path.write_text(yaml.safe_dump(publication))
            with self.assertRaisesRegex(ValueError, "IMPLICIT-REFERENCE-DERIVATIVES"):
                load_reference_contract(destination, True)

    def evidence(self):
        contract = load_reference_contract(RUNNER, True)
        pixels = [33,45,65,255,0,0,0,255,49,61,69,255,17,23,37,255]
        reference = dict(contract, success=True, realImplicitDerivatives=True, mipZeroSubstitutionRejected=True,
                         publicExecutionGranted=False, nativeErrors=[], cleanRootReceipts=2, pixels=pixels)
        message = "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_ARRAY_PIXELS fault0 rgba " + " ".join(map(str, pixels))
        return contract, dict(implicitSamplingReference=reference, progress=[dict(message=message), dict(message=message)])

    def test_matching_pixels_require_original_root_receipts(self):
        contract, result = self.evidence()
        self.assertTrue(validate_reference_result(result, contract))
        for mutation in (lambda r: r["progress"].pop(), lambda r: r["progress"].append(r["progress"][0]),
                         lambda r: r["implicitSamplingReference"]["pixels"].__setitem__(2, 45)):
            altered = copy.deepcopy(result)
            mutation(altered)
            self.assertFalse(validate_reference_result(altered, contract))

    def test_hash_dimension_and_no_native_errors_are_mandatory(self):
        contract, result = self.evidence()
        for field, value in (("shaderSha256", "0" * 64), ("dimension", "Texture2D"), ("nativeErrors", ["validation"]),
                             ("success", False), ("publicExecutionGranted", True), ("realImplicitDerivatives", False)):
            with self.subTest(field=field):
                altered = copy.deepcopy(result)
                altered["implicitSamplingReference"][field] = value
                self.assertFalse(validate_reference_result(altered, contract))

    def test_matching_base_level_receipts_cannot_pass_the_mip_counterexample(self):
        contract, result = self.evidence()
        result["implicitSamplingReference"]["pixels"][2] = 45
        message = "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_ARRAY_PIXELS fault0 rgba " + " ".join(map(str, result["implicitSamplingReference"]["pixels"]))
        result["progress"] = [dict(message=message), dict(message=message)]
        self.assertFalse(validate_reference_result(result, contract))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", required=True, type=pathlib.Path)
    args, rest = parser.parse_known_args()
    RUNNER = args.runner
    unittest.main(argv=[sys.argv[0]] + rest)
